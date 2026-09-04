#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::wstring Option(const wchar_t *name) {
  const std::wstring command = GetCommandLineW();
  const std::wstring prefix = std::wstring(L"--") + name + L"=";
  size_t begin = command.find(prefix);
  if (begin == std::wstring::npos) return L"";
  begin += prefix.size();
  const bool quoted = begin < command.size() && command[begin] == L'"';
  if (quoted) ++begin;
  size_t end = quoted ? command.find(L'"', begin) : command.find(L' ', begin);
  if (end == std::wstring::npos) end = command.size();
  return command.substr(begin, end - begin);
}

std::wstring Quote(const std::wstring &value) { return L"\"" + value + L"\""; }

bool Sha256File(const std::filesystem::path &path, std::string &hex) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
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
  bool ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength,
                             nullptr, 0, 0) >= 0;
  std::array<uint8_t, 32768> buffer{};
  while (ok && input) {
    input.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    const std::streamsize count = input.gcount();
    if (count > 0)
      ok = BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) >= 0;
  }
  std::array<uint8_t, 32> digest{};
  ok = ok && BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
  if (hash != nullptr) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) return false;
  static constexpr char digits[] = "0123456789abcdef";
  hex.clear();
  for (uint8_t value : digest) {
    hex.push_back(digits[value >> 4U]);
    hex.push_back(digits[value & 0x0FU]);
  }
  return true;
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

bool WriteText(const std::filesystem::path &path, const std::wstring &text) {
  const std::string bytes = Utf8(text);
  if (!text.empty() && bytes.empty()) return false;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  return !!output;
}

std::wstring NormalizedPath(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path,
                                                                       error);
  if (error) return L"";
  std::wstring text = normalized.wstring();
  std::replace(text.begin(), text.end(), L'/', L'\\');
  std::transform(text.begin(), text.end(), text.begin(), [](wchar_t value) {
    return std::towlower(value);
  });
  return text;
}

bool IsDirectUpdatePackage(const std::filesystem::path &package,
                           const std::filesystem::path &data) {
  const std::wstring packageText = NormalizedPath(package);
  std::wstring updatesText = NormalizedPath(data / L"updates");
  if (packageText.empty() || updatesText.empty()) return false;
  if (updatesText.back() != L'\\') updatesText.push_back(L'\\');
  return packageText.size() > updatesText.size() &&
         packageText.compare(0, updatesText.size(), updatesText) == 0 &&
         _wcsicmp(package.extension().c_str(), L".download") == 0;
}

bool ValidatePaths(const std::filesystem::path &target,
                   const std::filesystem::path &package,
                   const std::filesystem::path &data) {
  std::error_code error;
  const bool targetExists = std::filesystem::is_regular_file(target, error);
  if (error || !targetExists) return false;
  error.clear();
  const bool packageExists = std::filesystem::is_regular_file(package, error);
  if (error || !packageExists) return false;
  error.clear();
  const bool dataExists = std::filesystem::is_directory(data, error);
  return !error && dataExists &&
         _wcsicmp(target.filename().c_str(), L"PlanePet.exe") == 0 &&
         IsDirectUpdatePackage(package, data);
}

bool ReadToken(const std::filesystem::path &path, const std::wstring &expected) {
  std::ifstream input(path);
  std::string value;
  std::getline(input, value);
  if (!value.empty() && value.back() == '\r') value.pop_back();
  return (input.good() || input.eof()) && value == Utf8(expected);
}

bool Start(const std::filesystem::path &executable,
           const std::wstring &arguments, PROCESS_INFORMATION &process) {
  std::wstring command = Quote(executable.wstring()) + L" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  process = PROCESS_INFORMATION{};
  return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, executable.parent_path().c_str(), &startup,
                        &process) != FALSE;
}

void RecordFailure(const std::filesystem::path &data,
                   const std::wstring &reason) {
  WriteText(data / L"update.failure", reason + L"\n");
}

void Trace(const std::filesystem::path &data, bool enabled,
           const char *message) {
  if (!enabled) return;
  std::ofstream output(data / L"update.trace", std::ios::app);
  output << message << '\n';
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  const std::filesystem::path target = Option(L"target");
  const std::filesystem::path package = Option(L"package");
  const std::filesystem::path data = Option(L"data");
  std::wstring expectedWide = Option(L"sha256");
  std::transform(expectedWide.begin(), expectedWide.end(), expectedWide.begin(),
                 [](wchar_t value) { return std::towlower(value); });
  const std::string expected(expectedWide.begin(), expectedWide.end());
  const DWORD parentId = wcstoul(Option(L"parent-pid").c_str(), nullptr, 10);
  const bool trace = Option(L"trace") == L"1";
  if (target.empty() || package.empty() || data.empty() ||
      expected.size() != 64U || parentId == 0) return 2;

  if (!ValidatePaths(target, package, data)) {
    RecordFailure(data, L"更新路径安全检查失败；旧版本未改动。");
    return 2;
  }
  Trace(data, trace, "paths_validated");

  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentId);
  if (parent != nullptr) {
    WaitForSingleObject(parent, 30000);
    CloseHandle(parent);
  } else {
    Sleep(1500);
  }
  Trace(data, trace, "parent_finished");
  std::string actual;
  if (!Sha256File(package, actual) || actual != expected) {
    RecordFailure(data, L"更新包二次校验失败；旧版本未改动。");
    return 3;
  }
  Trace(data, trace, "hash_verified");

  const std::filesystem::path backup = target.wstring() + L".old";
  const std::filesystem::path pending = data / L"update.pending";
  const std::filesystem::path health = data / L"update.health";
  DeleteFileW(health.c_str());
  DeleteFileW(backup.c_str());
  if (!MoveFileExW(target.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH)) {
    RecordFailure(data, L"无法备份当前版本；旧版本未改动。");
    return 4;
  }
  if (!MoveFileExW(package.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
    MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    RecordFailure(data, L"无法写入新版本；已还原旧版本。");
    return 5;
  }
  Trace(data, trace, "files_replaced");

  const std::wstring token = std::to_wstring(GetTickCount64()) + L"-" +
      std::to_wstring(GetCurrentProcessId());
  if (!WriteText(pending, token + L"\n" + backup.wstring() + L"\n")) {
    DeleteFileW(target.c_str());
    MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    RecordFailure(data, L"无法记录升级状态；已还原旧版本。");
    return 6;
  }

  PROCESS_INFORMATION next{};
  DWORD launchError = 0;
  DWORD childExitCode = STILL_ACTIVE;
  if (Start(target, L"--complete-update=" + token +
                        L" --update-health-data=" + Quote(data.wstring()),
            next)) {
    Trace(data, trace, "new_version_started");
    CloseHandle(next.hThread);
    bool healthy = false;
    for (int attempt = 0; attempt < 80; ++attempt) {
      if (ReadToken(health, token)) {
        healthy = true;
        break;
      }
      if (WaitForSingleObject(next.hProcess, 500) != WAIT_TIMEOUT) break;
    }
    if (healthy) {
      Trace(data, trace, "health_confirmed");
      CloseHandle(next.hProcess);
      DeleteFileW(backup.c_str());
      DeleteFileW(pending.c_str());
      DeleteFileW((data / L"update.request").c_str());
      DeleteFileW((data / L"update.failure").c_str());
      return 0;
    }
    if (WaitForSingleObject(next.hProcess, 0) == WAIT_TIMEOUT) {
      TerminateProcess(next.hProcess, 1);
      WaitForSingleObject(next.hProcess, 2000);
    }
    GetExitCodeProcess(next.hProcess, &childExitCode);
    CloseHandle(next.hProcess);
  } else {
    launchError = GetLastError();
    Trace(data, trace, "new_version_start_failed");
  }

  Trace(data, trace, "rollback_started");
  DeleteFileW(target.c_str());
  if (!MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
    RecordFailure(data, L"新版启动失败，且旧版本自动还原失败。请重新解压安装包。");
    return 7;
  }
  DeleteFileW(pending.c_str());
  RecordFailure(data, L"新版未通过启动检查；已自动还原旧版本。启动错误=" +
                          std::to_wstring(launchError) + L"，退出码=" +
                          std::to_wstring(childExitCode) + L"。");
  PROCESS_INFORMATION restored{};
  if (Start(target, L"--update-rollback=1", restored)) {
    CloseHandle(restored.hThread);
    CloseHandle(restored.hProcess);
  }
  Trace(data, trace, "rollback_finished");
  return 8;
}
