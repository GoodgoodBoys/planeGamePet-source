#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef PCPET_PUBLIC_URL
#define PCPET_PUBLIC_URL L"wss://8.166.124.212:32112/v1/tunnel"
#endif

namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t kMaxDatagram = 4096;

std::filesystem::path DataDirectory() {
  wchar_t localAppData[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
  std::filesystem::path root = length > 0 && length < std::size(localAppData)
                                   ? std::filesystem::path(localAppData)
                                   : std::filesystem::current_path();
  return root / L"PlanePet";
}

bool ValidInstance(const std::wstring &instance) {
  if (instance.empty() || instance.size() > 32) return false;
  for (const wchar_t value : instance) {
    if (!((value >= L'a' && value <= L'z') ||
          (value >= L'A' && value <= L'Z') ||
          (value >= L'0' && value <= L'9') || value == L'-' ||
          value == L'_')) return false;
  }
  return true;
}

std::filesystem::path CredentialPath(const std::wstring &instance) {
  const std::wstring name = instance == L"default"
                                ? L"tunnel.credential"
                                : L"tunnel-" + instance + L".credential";
  return DataDirectory() / name;
}

std::filesystem::path LogPath(const std::wstring &instance) {
  const std::wstring name = instance == L"default"
                                ? L"tunnel.log"
                                : L"tunnel-" + instance + L".log";
  return DataDirectory() / name;
}

std::filesystem::path gLogPath;

void LogStatus(const char *stage, DWORD error = 0) {
  static std::mutex logMutex;
  std::lock_guard<std::mutex> lock(logMutex);
  const std::filesystem::path path = gLogPath.empty()
                                         ? DataDirectory() / L"tunnel.log"
                                         : gLogPath;
  std::error_code directoryError;
  std::filesystem::create_directories(path.parent_path(), directoryError);
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  if (GetFileSize(file, nullptr) > 65536) {
    CloseHandle(file);
    file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
  }
  char line[192]{};
  const int length = std::snprintf(
      line, sizeof(line), "%llu %s error=%lu\r\n",
      static_cast<unsigned long long>(GetTickCount64()), stage,
      static_cast<unsigned long>(error));
  DWORD written = 0;
  if (length > 0) WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
  CloseHandle(file);
}

bool ValidToken(const std::wstring &token) {
  if (token.size() != 64) return false;
  for (wchar_t value : token) {
    if (!((value >= L'0' && value <= L'9') ||
          (value >= L'a' && value <= L'f'))) return false;
  }
  return true;
}

bool SaveCredential(const std::filesystem::path &path,
                    const std::wstring &token) {
  const std::string ascii(token.begin(), token.end());
  DATA_BLOB input{};
  input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(ascii.data()));
  input.cbData = static_cast<DWORD>(ascii.size());
  DATA_BLOB protectedData{};
  if (!CryptProtectData(&input, L"Plane Pet TLS credential", nullptr, nullptr,
                        nullptr, CRYPTPROTECT_UI_FORBIDDEN, &protectedData)) {
    return false;
  }
  std::error_code directoryError;
  std::filesystem::create_directories(path.parent_path(), directoryError);
  const std::filesystem::path temporary = path.wstring() + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
  DWORD written = 0;
  const bool ok = file != INVALID_HANDLE_VALUE &&
      WriteFile(file, protectedData.pbData, protectedData.cbData, &written,
                nullptr) != FALSE && written == protectedData.cbData &&
      FlushFileBuffers(file) != FALSE;
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  LocalFree(protectedData.pbData);
  if (!ok || directoryError ||
      !MoveFileExW(temporary.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

bool LoadCredential(const std::filesystem::path &path, std::wstring &token) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const DWORD size = GetFileSize(file, nullptr);
  if (size == INVALID_FILE_SIZE || size == 0 || size > 4096) {
    CloseHandle(file);
    return false;
  }
  std::vector<BYTE> encrypted(size);
  DWORD read = 0;
  const bool readOk = ReadFile(file, encrypted.data(), size, &read, nullptr) != FALSE &&
                      read == size;
  CloseHandle(file);
  if (!readOk) return false;
  DATA_BLOB input{size, encrypted.data()};
  DATA_BLOB plain{};
  if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &plain)) return false;
  const std::string ascii(reinterpret_cast<char *>(plain.pbData), plain.cbData);
  SecureZeroMemory(plain.pbData, plain.cbData);
  LocalFree(plain.pbData);
  token.assign(ascii.begin(), ascii.end());
  return ValidToken(token);
}

bool LoadOrCreateCredential(const std::filesystem::path &path,
                            std::wstring &token) {
  if (LoadCredential(path, token)) return true;
  uint8_t random[32]{};
  if (BCryptGenRandom(nullptr, random, sizeof(random),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
  static constexpr wchar_t kHex[] = L"0123456789abcdef";
  token.clear();
  token.reserve(64);
  for (uint8_t value : random) {
    token.push_back(kHex[value >> 4U]);
    token.push_back(kHex[value & 0x0FU]);
  }
  SecureZeroMemory(random, sizeof(random));
  return SaveCredential(path, token);
}

std::wstring Option(const wchar_t *name, const wchar_t *fallback) {
  const std::wstring command = GetCommandLineW();
  const std::wstring prefix = std::wstring(L"--") + name + L"=";
  const size_t begin = command.find(prefix);
  if (begin == std::wstring::npos) return fallback;
  size_t valueBegin = begin + prefix.size();
  const bool quoted = valueBegin < command.size() && command[valueBegin] == L'"';
  if (quoted) ++valueBegin;
  size_t end = quoted ? command.find(L'"', valueBegin)
                      : command.find(L' ', valueBegin);
  if (end == std::wstring::npos) end = command.size();
  return command.substr(valueBegin, end - valueBegin);
}

bool ParsePort(const std::wstring &text, uint16_t &port) {
  if (text.empty()) return false;
  wchar_t *end = nullptr;
  const unsigned long value = wcstoul(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != L'\0' || value == 0 || value > 65535)
    return false;
  port = static_cast<uint16_t>(value);
  return true;
}

struct ParsedUrl {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  bool secure = true;
  bool valid = false;
};

ParsedUrl ParseSecureWebSocketUrl(const std::wstring &url) {
  std::wstring normalized = url;
  bool secure = true;
  if (normalized.rfind(L"wss://", 0) == 0) {
    normalized.replace(0, 6, L"https://");
  } else if (normalized.rfind(L"ws://", 0) == 0) {
    normalized.replace(0, 5, L"http://");
    secure = false;
  } else {
    return {};
  }
  URL_COMPONENTSW parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(normalized.c_str(), 0, 0, &parts) ||
      (secure && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
      (!secure && parts.nScheme != INTERNET_SCHEME_HTTP) ||
      parts.lpszHostName == nullptr) {
    return {};
  }
  ParsedUrl parsed;
  parsed.host.assign(parts.lpszHostName, parts.dwHostNameLength);
  if (parts.lpszUrlPath != nullptr)
    parsed.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.lpszExtraInfo != nullptr)
    parsed.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  if (parsed.path.empty()) parsed.path = L"/";
  parsed.port = parts.nPort == 0
                    ? (secure ? INTERNET_DEFAULT_HTTPS_PORT
                              : INTERNET_DEFAULT_HTTP_PORT)
                    : parts.nPort;
  parsed.secure = secure;
  parsed.valid = !parsed.host.empty();
  return parsed;
}

class Tunnel {
 public:
  Tunnel(std::wstring url, std::wstring token, uint16_t localPort)
      : url_(std::move(url)), token_(std::move(token)),
        localPort_(localPort) {}

  int Run() {
    if (token_.size() < 32 || !ParseSecureWebSocketUrl(url_).valid) {
      LogStatus("configuration_invalid");
      return 2;
    }
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 3;
    localSocket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (localSocket_ == INVALID_SOCKET) return 4;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(localPort_);
    if (bind(localSocket_, reinterpret_cast<const sockaddr *>(&local),
             sizeof(local)) != 0) {
      LogStatus("local_bind_failed", WSAGetLastError());
      return 5;
    }
    u_long nonBlocking = 1;
    if (ioctlsocket(localSocket_, FIONBIO, &nonBlocking) != 0) return 6;

    auto reconnectAt = Clock::now();
    while (running_) {
      ReceiveLocal();
      if (!connected_ && Clock::now() >= reconnectAt) {
        CleanupConnection();
        if (!Connect()) reconnectAt = Clock::now() + std::chrono::seconds(2);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CleanupConnection(true);
    closesocket(localSocket_);
    WSACleanup();
    return 0;
  }

 private:
  bool Connect() {
    const ParsedUrl parsed = ParseSecureWebSocketUrl(url_);
    if (!parsed.valid) {
      LogStatus("url_invalid");
      return false;
    }
    session_ = WinHttpOpen(L"PlanePetTunnel/1.0.0",
                           WINHTTP_ACCESS_TYPE_NO_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ == nullptr) {
      LogStatus("winhttp_open_failed", GetLastError());
      return false;
    }
    WinHttpSetTimeouts(session_, 5000, 5000, 5000, 10000);
    connection_ = WinHttpConnect(session_, parsed.host.c_str(), parsed.port, 0);
    if (connection_ == nullptr) {
      LogStatus("winhttp_connect_failed", GetLastError());
      return false;
    }
    HINTERNET request = WinHttpOpenRequest(
        connection_, L"GET", parsed.path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parsed.secure ? WINHTTP_FLAG_SECURE : 0);
    if (request == nullptr) {
      LogStatus("request_open_failed", GetLastError());
      return false;
    }
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                          nullptr, 0)) {
      WinHttpCloseHandle(request);
      LogStatus("upgrade_option_failed", GetLastError());
      return false;
    }
    const std::wstring authorization =
        L"Authorization: Bearer " + token_ + L"\r\n";
    const std::wstring enrollment = L"X-Plane-Pet-Enroll: 1\r\n";
    if (!WinHttpAddRequestHeaders(
            request, authorization.c_str(), static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
      const DWORD error = GetLastError();
      WinHttpCloseHandle(request);
      LogStatus("authorization_header_failed", error);
      return false;
    }
    if (!WinHttpAddRequestHeaders(
            request, enrollment.c_str(), static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
      const DWORD error = GetLastError();
      WinHttpCloseHandle(request);
      LogStatus("enrollment_header_failed", error);
      return false;
    }
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
      const DWORD error = GetLastError();
      WinHttpCloseHandle(request);
      LogStatus("handshake_send_failed", error);
      return false;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
      const DWORD error = GetLastError();
      WinHttpCloseHandle(request);
      LogStatus("handshake_receive_failed", error);
      return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX) ||
        status != 101) {
      WinHttpCloseHandle(request);
      LogStatus("handshake_status_failed", status);
      return false;
    }
    webSocket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    if (webSocket_ == nullptr) {
      LogStatus("upgrade_complete_failed", GetLastError());
      return false;
    }
    connected_ = true;
    LogStatus("connected");
    receiver_ = std::thread([this] { ReceiveWebSocket(); });
    return true;
  }

  void ReceiveLocal() {
    for (;;) {
      uint8_t bytes[kMaxDatagram]{};
      sockaddr_in source{};
      int sourceLength = sizeof(source);
      const int received = recvfrom(
          localSocket_, reinterpret_cast<char *>(bytes), sizeof(bytes), 0,
          reinterpret_cast<sockaddr *>(&source), &sourceLength);
      if (received < 0) return;
      if (source.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) continue;
      {
        std::lock_guard<std::mutex> lock(endpointMutex_);
        clientEndpoint_ = source;
        haveClientEndpoint_ = true;
      }
      if (!connected_ || webSocket_ == nullptr) continue;
      const DWORD error = WinHttpWebSocketSend(
          webSocket_, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
          bytes, static_cast<DWORD>(received));
      if (error != NO_ERROR) {
        connected_ = false;
        WinHttpWebSocketShutdown(
            webSocket_, WINHTTP_WEB_SOCKET_ENDPOINT_TERMINATED_CLOSE_STATUS,
            nullptr, 0);
        return;
      }
    }
  }

  void ReceiveWebSocket() {
    std::vector<uint8_t> message;
    while (running_ && connected_) {
      uint8_t bytes[kMaxDatagram]{};
      DWORD received = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type =
          WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
      const DWORD error = WinHttpWebSocketReceive(
          webSocket_, bytes, sizeof(bytes), &received, &type);
      if (error != NO_ERROR ||
          type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
        LogStatus("receive_closed", error);
        break;
      }
      if (type != WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE &&
          type != WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
        break;
      }
      if (message.size() + received > kMaxDatagram) break;
      message.insert(message.end(), bytes, bytes + received);
      if (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) continue;

      sockaddr_in endpoint{};
      bool haveEndpoint = false;
      {
        std::lock_guard<std::mutex> lock(endpointMutex_);
        endpoint = clientEndpoint_;
        haveEndpoint = haveClientEndpoint_;
      }
      if (haveEndpoint && !message.empty()) {
        sendto(localSocket_, reinterpret_cast<const char *>(message.data()),
               static_cast<int>(message.size()), 0,
               reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint));
      }
      message.clear();
    }
    connected_ = false;
  }

  void CleanupConnection(bool shuttingDown = false) {
    if (webSocket_ != nullptr && (connected_ || shuttingDown)) {
      connected_ = false;
      WinHttpWebSocketShutdown(
          webSocket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    }
    if (receiver_.joinable()) receiver_.join();
    if (webSocket_ != nullptr) WinHttpCloseHandle(webSocket_);
    if (connection_ != nullptr) WinHttpCloseHandle(connection_);
    if (session_ != nullptr) WinHttpCloseHandle(session_);
    webSocket_ = connection_ = session_ = nullptr;
  }

  std::wstring url_;
  std::wstring token_;
  uint16_t localPort_ = 32110;
  SOCKET localSocket_ = INVALID_SOCKET;
  HINTERNET session_ = nullptr;
  HINTERNET connection_ = nullptr;
  HINTERNET webSocket_ = nullptr;
  std::thread receiver_;
  std::atomic<bool> running_{true};
  std::atomic<bool> connected_{false};
  std::mutex endpointMutex_;
  sockaddr_in clientEndpoint_{};
  bool haveClientEndpoint_ = false;
};

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  const std::wstring instance = Option(L"instance", L"default");
  uint16_t localPort = 32110;
  if (!ValidInstance(instance) ||
      !ParsePort(Option(L"local-port", L"32110"), localPort)) return 12;
  gLogPath = LogPath(instance);
  const std::wstring mutexName = instance == L"default"
      ? L"PlanePetPublicTlsTunnel"
      : L"PlanePetPublicTlsTunnel-" + instance;
  HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
  if (mutex == nullptr) return 10;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return 0;
  }
  const std::wstring url = Option(L"url", PCPET_PUBLIC_URL);
  std::wstring token = Option(L"token", L"");
  if (token.empty()) {
    if (!LoadOrCreateCredential(CredentialPath(instance), token)) {
      ReleaseMutex(mutex);
      CloseHandle(mutex);
      return 11;
    }
  } else if (!ValidToken(token)) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 13;
  }
  Tunnel tunnel(url, token, localPort);
  const int result = tunnel.Run();
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return result;
}
