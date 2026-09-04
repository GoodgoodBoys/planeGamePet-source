#define WIN32_LEAN_AND_MEAN
#include "update_manager.h"

#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

#include "../common/app_version.h"

namespace plane_pet_update {
namespace {

constexpr wchar_t kAllowedHost[] =
    L"egg-ota-test.oss-cn-beijing.aliyuncs.com";
constexpr size_t kMaximumManifestBytes = 64U * 1024U;
constexpr uint64_t kMaximumArtifactBytes = 64ULL * 1024ULL * 1024ULL;
constexpr char kPublicModulusBase64[] =
    "tRSXy4RGnHkle5TcsSSee2UIrNcW6PCsuRGgXE8Mhe5kgq2Ek+lazQPEfGq4bz2A"
    "zCSxOkECubyeFGYFQXUrNqkVSfNJVBvWwevAMgB3MXkFaPyfxoFNeVdlULXYAkDc"
    "bUsLeDaXSa/68W7NpZe6KiJXpWq3z/pVE4IOEdIG+g6zVXeJvIC+TcNJXA/xE2qY"
    "zYBDZQjCmLAptmzITGixZdjVmJHaG/fDzhQlRFlsc5/AnmUUitSsmeRAHXfkmrcKP"
    "ar6zF4N6OzOTwrWEr+tiYdVkBSe450rQ3XSQTfgQUhxbt7YMnlGP2+Oc5ADYEwkDr"
    "y+BQ+TEu5zmnZVbGFwYQ==";

uint64_t UnixTimeMillis() {
  FILETIME time{};
  GetSystemTimeAsFileTime(&time);
  ULARGE_INTEGER ticks{};
  ticks.LowPart = time.dwLowDateTime;
  ticks.HighPart = time.dwHighDateTime;
  constexpr uint64_t kEpochDifference = 116444736000000000ULL;
  return ticks.QuadPart > kEpochDifference
             ? (ticks.QuadPart - kEpochDifference) / 10000ULL
             : 0;
}

std::wstring Wide(const std::string &text) {
  if (text.empty()) return L"";
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
  if (length <= 0) return L"";
  std::wstring result(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), length);
  return result;
}

std::string Utf8(const std::wstring &text) {
  if (text.empty()) return {};
  const int length = WideCharToMultiByte(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
      nullptr, 0, nullptr, nullptr);
  if (length <= 0) return {};
  std::string result(static_cast<size_t>(length), '\0');
  return WideCharToMultiByte(
      CP_UTF8, 0, text.data(),
             static_cast<int>(text.size()), result.data(), length, nullptr,
             nullptr) == length
      ? result
      : std::string{};
}

bool DecodeBase64(const std::string &input, std::vector<uint8_t> &output) {
  DWORD size = 0;
  if (!CryptStringToBinaryA(input.c_str(), static_cast<DWORD>(input.size()),
                            CRYPT_STRING_BASE64_ANY, nullptr, &size, nullptr,
                            nullptr)) {
    return false;
  }
  output.resize(size);
  return CryptStringToBinaryA(input.c_str(), static_cast<DWORD>(input.size()),
                              CRYPT_STRING_BASE64_ANY, output.data(), &size,
                              nullptr, nullptr) != FALSE &&
         (output.resize(size), true);
}

bool Sha256(const uint8_t *bytes, size_t length,
            std::array<uint8_t, 32> &digest) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0;
  DWORD copied = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0 ||
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&objectLength),
                        sizeof(objectLength), &copied, 0) < 0) {
    if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
    return false;
  }
  std::vector<uint8_t> object(objectLength);
  const bool ok =
      BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr,
                       0, 0) >= 0 &&
      (length == 0 ||
       BCryptHashData(hash, const_cast<PUCHAR>(bytes),
                      static_cast<ULONG>(length), 0) >= 0) &&
      BCryptFinishHash(hash, digest.data(),
                       static_cast<ULONG>(digest.size()), 0) >= 0;
  if (hash != nullptr) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return ok;
}

std::string Hex(const std::array<uint8_t, 32> &bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2U);
  for (uint8_t value : bytes) {
    result.push_back(kDigits[value >> 4U]);
    result.push_back(kDigits[value & 0x0FU]);
  }
  return result;
}

bool VerifyManifestSignature(const std::vector<uint8_t> &manifest,
                             const std::vector<uint8_t> &signature) {
  std::vector<uint8_t> modulus;
  if (!DecodeBase64(kPublicModulusBase64, modulus) || modulus.size() != 256U ||
      signature.size() != 256U) {
    return false;
  }
  std::array<uint8_t, 32> digest{};
  if (!Sha256(manifest.data(), manifest.size(), digest)) return false;

  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr,
                                  0) < 0) {
    return false;
  }
  const std::array<uint8_t, 3> exponent{{1, 0, 1}};
  std::vector<uint8_t> blob(sizeof(BCRYPT_RSAKEY_BLOB) + exponent.size() +
                            modulus.size());
  auto *header = reinterpret_cast<BCRYPT_RSAKEY_BLOB *>(blob.data());
  header->Magic = BCRYPT_RSAPUBLIC_MAGIC;
  header->BitLength = static_cast<ULONG>(modulus.size() * 8U);
  header->cbPublicExp = static_cast<ULONG>(exponent.size());
  header->cbModulus = static_cast<ULONG>(modulus.size());
  header->cbPrime1 = 0;
  header->cbPrime2 = 0;
  memcpy(blob.data() + sizeof(*header), exponent.data(), exponent.size());
  memcpy(blob.data() + sizeof(*header) + exponent.size(), modulus.data(),
         modulus.size());
  const bool imported =
      BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key,
                          blob.data(), static_cast<ULONG>(blob.size()), 0) >= 0;
  BCRYPT_PKCS1_PADDING_INFO padding{};
  padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
  const bool verified = imported &&
      BCryptVerifySignature(key, &padding, digest.data(), digest.size(),
                            const_cast<PUCHAR>(signature.data()),
                            static_cast<ULONG>(signature.size()),
                            BCRYPT_PAD_PKCS1) >= 0;
  if (key != nullptr) BCryptDestroyKey(key);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return verified;
}

struct ParsedUrl {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = 0;
};

bool ParseAllowedUrl(const std::string &url, ParsedUrl &parsed) {
  const std::wstring wide = Wide(url);
  if (wide.empty() || wide.size() > 2048U) return false;
  URL_COMPONENTSW components{};
  components.dwStructSize = sizeof(components);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0,
                       &components) ||
      components.nScheme != INTERNET_SCHEME_HTTPS ||
      components.nPort != INTERNET_DEFAULT_HTTPS_PORT) {
    return false;
  }
  parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
  if (_wcsicmp(parsed.host.c_str(), kAllowedHost) != 0) return false;
  parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
  if (components.dwExtraInfoLength != 0)
    parsed.path.append(components.lpszExtraInfo,
                       components.dwExtraInfoLength);
  parsed.port = components.nPort;
  return !parsed.path.empty();
}

bool HttpGet(const std::string &url, size_t maximum,
             std::vector<uint8_t> &result,
             const std::atomic<bool> *cancel = nullptr,
             std::atomic<unsigned> *progress = nullptr) {
  ParsedUrl parsed;
  if (!ParseAllowedUrl(url, parsed)) return false;
  HINTERNET session = WinHttpOpen(
      L"PlanePetUpdater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session == nullptr) return false;
  WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);
  HINTERNET connection = WinHttpConnect(session, parsed.host.c_str(),
                                        parsed.port, 0);
  HINTERNET request = connection == nullptr
      ? nullptr
      : WinHttpOpenRequest(connection, L"GET", parsed.path.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                           WINHTTP_FLAG_SECURE);
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (request != nullptr)
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                     sizeof(redirectPolicy));
  bool ok = request != nullptr &&
      WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(request, nullptr);
  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  if (ok)
    ok = WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status,
                             &statusSize, WINHTTP_NO_HEADER_INDEX) &&
         status == 200;
  uint64_t contentLength = 0;
  wchar_t contentLengthText[32]{};
  DWORD contentLengthSize = sizeof(contentLengthText);
  if (ok && WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                contentLengthText, &contentLengthSize,
                                WINHTTP_NO_HEADER_INDEX)) {
    contentLength = _wcstoui64(contentLengthText, nullptr, 10);
    if (contentLength > maximum) ok = false;
  }
  result.clear();
  std::array<uint8_t, 16384> buffer{};
  while (ok) {
    if (cancel != nullptr && cancel->load()) {
      ok = false;
      break;
    }
    DWORD received = 0;
    if (!WinHttpReadData(request, buffer.data(), buffer.size(), &received)) {
      ok = false;
      break;
    }
    if (received == 0) break;
    if (result.size() > maximum - received) {
      ok = false;
      break;
    }
    result.insert(result.end(), buffer.begin(), buffer.begin() + received);
    if (progress != nullptr && contentLength != 0)
      progress->store(static_cast<unsigned>(
          std::min<uint64_t>(99, result.size() * 100ULL / contentLength)));
  }
  if (request != nullptr) WinHttpCloseHandle(request);
  if (connection != nullptr) WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return ok;
}

bool JsonString(const std::string &json, const char *key, std::string &value) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t at = json.find(needle);
  if (at == std::string::npos) return false;
  at = json.find(':', at + needle.size());
  if (at == std::string::npos) return false;
  at = json.find('"', at + 1);
  if (at == std::string::npos) return false;
  value.clear();
  for (++at; at < json.size(); ++at) {
    const char ch = json[at];
    if (ch == '"') return true;
    if (ch == '\\') {
      if (++at >= json.size()) return false;
      const char escaped = json[at];
      if (escaped == 'n') value.push_back('\n');
      else if (escaped == 'r') value.push_back('\r');
      else if (escaped == 't') value.push_back('\t');
      else if (escaped == '"' || escaped == '\\' || escaped == '/')
        value.push_back(escaped);
      else
        return false;
    } else {
      value.push_back(ch);
    }
  }
  return false;
}

bool JsonUnsigned(const std::string &json, const char *key, uint64_t &value) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t at = json.find(needle);
  if (at == std::string::npos) return false;
  at = json.find(':', at + needle.size());
  if (at == std::string::npos) return false;
  do { ++at; } while (at < json.size() && std::isspace(
      static_cast<unsigned char>(json[at])));
  if (at >= json.size() || !std::isdigit(static_cast<unsigned char>(json[at])))
    return false;
  uint64_t parsed = 0;
  while (at < json.size() &&
         std::isdigit(static_cast<unsigned char>(json[at]))) {
    const unsigned digit = static_cast<unsigned>(json[at++] - '0');
    if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10ULL)
      return false;
    parsed = parsed * 10ULL + digit;
  }
  value = parsed;
  return true;
}

bool ParseVersion(const std::string &text, std::array<unsigned, 3> &version) {
  version = {};
  size_t begin = 0;
  for (size_t part = 0; part < 3; ++part) {
    const size_t end = part == 2 ? text.size() : text.find('.', begin);
    if (end == std::string::npos || end == begin || end - begin > 3U)
      return false;
    unsigned value = 0;
    for (size_t at = begin; at < end; ++at) {
      if (!std::isdigit(static_cast<unsigned char>(text[at]))) return false;
      value = value * 10U + static_cast<unsigned>(text[at] - '0');
    }
    if (value > 255U) return false;
    version[part] = value;
    begin = end + 1;
  }
  return begin == text.size() + 1U;
}

int CompareVersion(const std::string &left, const std::string &right) {
  std::array<unsigned, 3> l{};
  std::array<unsigned, 3> r{};
  if (!ParseVersion(left, l) || !ParseVersion(right, r)) return 0;
  return l < r ? -1 : (r < l ? 1 : 0);
}

bool ParseManifest(const std::vector<uint8_t> &bytes,
                   Manager::Manifest &manifest) {
  const std::string json(bytes.begin(), bytes.end());
  uint64_t schema = 0;
  std::string product;
  std::string summary;
  if (!JsonUnsigned(json, "schema", schema) || schema != 1 ||
      !JsonString(json, "product_id", product) ||
      product != plane_pet_version::kProductId ||
      !JsonString(json, "version", manifest.version) ||
      !JsonString(json, "minimum_supported_version",
                  manifest.minimumVersion) ||
      !JsonString(json, "url", manifest.artifactUrl) ||
      !JsonString(json, "sha256", manifest.sha256) ||
      !JsonUnsigned(json, "size", manifest.size) ||
      manifest.size == 0 || manifest.size > kMaximumArtifactBytes ||
      manifest.sha256.size() != 64U) {
    return false;
  }
  std::array<unsigned, 3> parsed{};
  if (!ParseVersion(manifest.version, parsed) ||
      !ParseVersion(manifest.minimumVersion, parsed)) return false;
  for (char ch : manifest.sha256)
    if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
  std::transform(manifest.sha256.begin(), manifest.sha256.end(),
                 manifest.sha256.begin(), [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  ParsedUrl artifact;
  if (!ParseAllowedUrl(manifest.artifactUrl, artifact)) return false;
  for (size_t index = 0; index < 3; ++index) {
    const std::string key = "summary_" + std::to_string(index + 1U);
    if (JsonString(json, key.c_str(), summary))
      manifest.summary[index] = Wide(summary);
  }
  return true;
}

bool WriteBytesAtomically(const std::filesystem::path &target,
                          const std::vector<uint8_t> &bytes) {
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) return false;
  const std::filesystem::path temporary = target.wstring() + L".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  output.close();
  return output && MoveFileExW(temporary.c_str(), target.c_str(),
                               MOVEFILE_REPLACE_EXISTING |
                                   MOVEFILE_WRITE_THROUGH) != FALSE;
}

}  // namespace

Manager::~Manager() {
  cancel_.store(true);
  if (worker_.joinable()) worker_.join();
}

void Manager::Configure(HWND owner, std::string manifestUrl,
                        std::filesystem::path requestPath, bool enabled,
                        uint64_t optionalSnoozeUntilMs) {
  owner_ = owner;
  manifestUrl_ = std::move(manifestUrl);
  requestPath_ = std::move(requestPath);
  optionalSnoozeUntilMs_ = optionalSnoozeUntilMs;
  nextAutomaticCheckMs_ = UnixTimeMillis() + 15000ULL;
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.state = enabled && !manifestUrl_.empty() ? State::Idle
                                                     : State::Disabled;
  snapshot_.currentVersion = plane_pet_version::kWideString;
  ++snapshot_.generation;
}

bool Manager::Enabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_.state != State::Disabled;
}

void Manager::JoinFinishedWorker() {
  if (!workerRunning_.load() && worker_.joinable()) worker_.join();
}

void Manager::Tick(bool idleUiAvailable, bool peerVersionDiffers,
                   bool localUpdateAvailable, bool peerMajorMismatch) {
  JoinFinishedWorker();
  if (!Enabled() || workerRunning_.load()) return;
  const uint64_t now = UnixTimeMillis();
  if (peerVersionDiffers && localUpdateAvailable &&
      !peerRequirementHandled_) {
    if (!idleUiAvailable) return;
    if (!peerMajorMismatch && now < optionalSnoozeUntilMs_) return;
    peerRequirementHandled_ = true;
    StartCheck(false, peerMajorMismatch);
    return;
  }
  if (!peerVersionDiffers) peerRequirementHandled_ = false;
  if (idleUiAvailable && now >= nextAutomaticCheckMs_) {
    nextAutomaticCheckMs_ = now + 24ULL * 60ULL * 60ULL * 1000ULL;
    if (now >= optionalSnoozeUntilMs_) StartCheck(false, false);
  }
}

void Manager::StartCheck(bool manual, bool requiredByPeer) {
  JoinFinishedWorker();
  if (workerRunning_.exchange(true)) return;
  cancel_.store(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.state = State::Checking;
    snapshot_.manual = manual;
    snapshot_.required = requiredByPeer;
    snapshot_.message = L"正在安全检查新版本…";
    snapshot_.progressPercent = 0;
    downloadProgress_.store(0);
    ++snapshot_.generation;
  }
  PostMessageW(owner_, kChangedMessage, 0, 0);
  worker_ = std::thread(&Manager::CheckWorker, this, manual, requiredByPeer);
}

void Manager::CheckNow() { StartCheck(true, false); }

void Manager::CheckWorker(bool manual, bool requiredByPeer) {
  std::vector<uint8_t> manifestBytes;
  std::vector<uint8_t> signatureText;
  std::vector<uint8_t> signature;
  Manifest parsed;
  bool ok = HttpGet(manifestUrl_, kMaximumManifestBytes, manifestBytes,
                    &cancel_) &&
            HttpGet(manifestUrl_ + ".sig", 4096U, signatureText, &cancel_) &&
            DecodeBase64(std::string(signatureText.begin(), signatureText.end()),
                         signature) &&
            VerifyManifestSignature(manifestBytes, signature) &&
            ParseManifest(manifestBytes, parsed);
  if (cancel_.load()) {
    workerRunning_.store(false);
    return;
  }
  if (!ok) {
    SetError(L"无法验证更新信息，请稍后重试。", manual);
    workerRunning_.store(false);
    return;
  }
  const std::string current = plane_pet_version::kString;
  const bool newer = CompareVersion(current, parsed.version) < 0;
  const bool belowMinimum = CompareVersion(current, parsed.minimumVersion) < 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    manifest_ = parsed;
    snapshot_.latestVersion = Wide(parsed.version);
    for (size_t index = 0; index < 3; ++index)
      snapshot_.summary[index] = parsed.summary[index];
    snapshot_.manual = manual;
    snapshot_.required = requiredByPeer || belowMinimum;
    if (newer) {
      snapshot_.state = snapshot_.required ? State::Required
                                           : State::Available;
      snapshot_.message = snapshot_.required
          ? L"需升级后才能继续与好友联机"
          : L"发现新版本";
    } else if (requiredByPeer) {
      snapshot_.state = State::Required;
      snapshot_.message = L"好友使用了不同的大版本，但更新暂未发布";
    } else {
      snapshot_.state = State::Current;
      snapshot_.message = L"当前已是最新版本";
    }
    ++snapshot_.generation;
  }
  PostMessageW(owner_, kChangedMessage, 0, 0);
  workerRunning_.store(false);
}

void Manager::AcceptAndDownload() {
  JoinFinishedWorker();
  if (workerRunning_.exchange(true)) return;
  Manifest selected;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.state != State::Available &&
        snapshot_.state != State::Required) {
      workerRunning_.store(false);
      return;
    }
    selected = manifest_;
    snapshot_.state = State::Downloading;
    snapshot_.message = L"正在下载更新…";
    snapshot_.progressPercent = 0;
    ++snapshot_.generation;
  }
  cancel_.store(false);
  PostMessageW(owner_, kChangedMessage, 0, 0);
  worker_ = std::thread(&Manager::DownloadWorker, this, selected);
}

void Manager::DownloadWorker(Manifest manifest) {
  std::vector<uint8_t> bytes;
  const bool downloaded =
      HttpGet(manifest.artifactUrl,
              static_cast<size_t>(std::min<uint64_t>(
                  kMaximumArtifactBytes, manifest.size + 1ULL)),
              bytes, &cancel_, &downloadProgress_) &&
      bytes.size() == manifest.size;
  std::array<uint8_t, 32> digest{};
  if (cancel_.load()) {
    workerRunning_.store(false);
    return;
  }
  if (!downloaded || !Sha256(bytes.data(), bytes.size(), digest) ||
      Hex(digest) != manifest.sha256) {
    SetError(L"更新包校验失败，未安装任何文件。", true);
    workerRunning_.store(false);
    return;
  }
  const std::filesystem::path package =
      requestPath_.parent_path() / L"updates" / Wide(manifest.version) /
      L"PlanePet.exe.download";
  if (!WriteBytesAtomically(package, bytes)) {
    SetError(L"无法保存更新包，请检查磁盘空间和安全软件。", true);
    workerRunning_.store(false);
    return;
  }
  const std::wstring requestText =
      L"PLANE_PET_UPDATE 1\n" + Wide(manifest.version) + L"\n" +
      Wide(manifest.sha256) + L"\n" + package.wstring() + L"\n";
  const std::string requestUtf8 = Utf8(requestText);
  const std::vector<uint8_t> requestBytes(requestUtf8.begin(),
                                          requestUtf8.end());
  if (requestUtf8.empty() ||
      !WriteBytesAtomically(requestPath_, requestBytes)) {
    SetError(L"无法创建安装请求，当前版本保持不变。", true);
    workerRunning_.store(false);
    return;
  }
  installRequestReady_.store(true);
  downloadProgress_.store(100);
  workerRunning_.store(false);
  PostMessageW(owner_, kInstallMessage, 0, 0);
}

void Manager::SetError(const std::wstring &message, bool manual) {
  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_.state = State::Error;
  snapshot_.message = message;
  snapshot_.manual = manual;
  ++snapshot_.generation;
  PostMessageW(owner_, kChangedMessage, 0, 0);
}

void Manager::Dismiss() {
  cancel_.store(true);
  std::lock_guard<std::mutex> lock(mutex_);
  if (snapshot_.state != State::Disabled) snapshot_.state = State::Idle;
  snapshot_.message.clear();
  ++snapshot_.generation;
}

void Manager::SetOptionalSnoozeUntil(uint64_t unixTimeMs) {
  optionalSnoozeUntilMs_ = unixTimeMs;
  Dismiss();
}

Snapshot Manager::GetSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Snapshot copy = snapshot_;
  if (copy.state == State::Downloading)
    copy.progressPercent = downloadProgress_.load();
  return copy;
}

bool Manager::HasInstallRequest() const {
  return installRequestReady_.load();
}

}  // namespace plane_pet_update
