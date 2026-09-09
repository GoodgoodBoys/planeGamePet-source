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
  ok = ok && !input.bad() && BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
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
  const auto temporary = path.wstring() + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                  written == bytes.size() && FlushFileBuffers(file);
  CloseHandle(file);
  return ok && MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
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

bool PreserveLastGood(const std::filesystem::path &target) {
  const auto backup = target.wstring() + L".old";
  const auto retained = target.wstring() + L".last-good";
  std::error_code error;
  if (!std::filesystem::exists(backup, error)) return !error;
  return MoveFileExW(backup.c_str(), retained.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

void Trace(const std::filesystem::path &data, bool enabled,
           const char *message) {
  if (!enabled) return;
  std::ofstream output(data / L"update.trace", std::ios::app);
  output << message << '\n';
}

void RestartOld(const std::filesystem::path &target, bool rollback = false) {
  std::error_code error;
  if (_wcsicmp(target.filename().c_str(), L"PlanePet.exe") != 0 ||
      !std::filesystem::is_regular_file(target, error)) return;
  PROCESS_INFORMATION process{};
  if (Start(target, rollback ? L"--update-rollback=1 --update-failed=1" : L"--update-failed=1", process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
}

int FailAndRestart(const std::filesystem::path &target,
                   const std::filesystem::path &data, int code,
                   const std::wstring &reason, bool restart = true) {
  RecordFailure(data, reason);
  if (restart) RestartOld(target);
  return code;
}

int RecoverInterrupted(const std::filesystem::path &target,
                       const std::filesystem::path &data) {
  std::error_code validationError;
  if (!target.is_absolute() || !data.is_absolute() ||
      _wcsicmp(target.filename().c_str(), L"PlanePet.exe") != 0 ||
      !std::filesystem::is_directory(data, validationError)) return 2;
  const auto pending = data / L"update.pending";
  const std::filesystem::path backup = target.wstring() + L".old";
  std::ifstream input(pending);
  std::string token, backupText;
  if (!std::getline(input, token) || !std::getline(input, backupText) ||
      token.empty() || token.size() > 64 ||
      token.find_first_not_of("0123456789-") != std::string::npos ||
      NormalizedPath(std::filesystem::u8path(backupText)) != NormalizedPath(backup))
    return FailAndRestart(target, data, 7, L"未完成升级的记录无效；文件未改动，请重新解压安装包。", false);
  input.close();
  const std::wstring wideToken(token.begin(), token.end());
  const std::filesystem::path staged = target.wstring() + L".update-" + wideToken + L".tmp";
  std::error_code error;
  const bool healthy = ReadToken(data / L"update.health", wideToken);
  bool restored = false;
  if (!healthy && std::filesystem::is_regular_file(backup, error)) {
    if (!MoveFileExW(backup.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return FailAndRestart(target, data, 7, L"旧版本恢复失败，备份已保留。请重新解压安装包。", false);
    restored = true;
  } else if (healthy) {
    if (!PreserveLastGood(target))
      return FailAndRestart(target, data, 7,
          L"新版已启动，但旧版备份归档失败。备份和恢复记录已保留，请解除文件占用后重试。", false);
  }
  DeleteFileW(staged.c_str());
  DeleteFileW(pending.c_str());
  if (!healthy) {
    RecordFailure(data, L"上次升级被中断，已保留或恢复原版本，请重新检查更新。");
    RestartOld(target, restored);
  } else {
    PROCESS_INFORMATION process{};
    if (Start(target, L"", process)) {
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
  }
  return 0;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  HANDLE updateLock = CreateMutexW(nullptr, TRUE, L"PlanePetUpdateTransaction");
  if (!updateLock) return 9;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(updateLock);
    return 9;
  }
  struct TransactionLock {
    HANDLE handle;
    ~TransactionLock() { ReleaseMutex(handle); CloseHandle(handle); }
  } transactionLock{updateLock};
  const std::filesystem::path target = Option(L"target");
  const std::filesystem::path package = Option(L"package");
  const std::filesystem::path data = Option(L"data");
  std::wstring expectedWide = Option(L"sha256");
  std::transform(expectedWide.begin(), expectedWide.end(), expectedWide.begin(),
                 [](wchar_t value) { return std::towlower(value); });
  const std::string expected(expectedWide.begin(), expectedWide.end());
  const DWORD parentId = wcstoul(Option(L"parent-pid").c_str(), nullptr, 10);
  const bool trace = Option(L"trace") == L"1";
  const bool recover = Option(L"recover") == L"1";
  if (target.empty() || data.empty() || parentId == 0 ||
      (!recover && (package.empty() || expected.size() != 64U))) return 2;

  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentId);
  if (parent != nullptr) {
    const DWORD waited = WaitForSingleObject(parent, 30000);
    CloseHandle(parent);
    if (waited != WAIT_OBJECT_0)
      return FailAndRestart(target, data, 9, L"旧程序尚未退出，本次更新已停止，文件未改动。", false);
  } else {
    Sleep(1500);
  }
  Trace(data, trace, "parent_finished");
  if (recover) return RecoverInterrupted(target, data);
  if (!ValidatePaths(target, package, data))
    return FailAndRestart(target, data, 2, L"更新路径安全检查失败；旧版本未改动。");
  Trace(data, trace, "paths_validated");
  std::string actual;
  if (!Sha256File(package, actual) || actual != expected) {
    return FailAndRestart(target, data, 3, L"更新包二次校验失败；旧版本未改动。");
  }
  Trace(data, trace, "hash_verified");

  const std::filesystem::path backup = target.wstring() + L".old";
  const std::filesystem::path pending = data / L"update.pending";
  const std::filesystem::path health = data / L"update.health";
  const std::wstring token = std::to_wstring(GetTickCount64()) + L"-" +
      std::to_wstring(GetCurrentProcessId());
  const std::filesystem::path staged = target.wstring() + L".update-" + token + L".tmp";
  // Copy across volumes first, then hash and flush the destination copy. The
  // eventual replacement and rollback are both same-volume operations.
  if (!CopyFileW(package.c_str(), staged.c_str(), TRUE))
    return FailAndRestart(target, data, 5, L"无法在程序目录暂存更新；旧版本未改动，请检查目录权限。");
  HANDLE stageFile = CreateFileW(staged.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
  const bool flushed = stageFile != INVALID_HANDLE_VALUE && FlushFileBuffers(stageFile);
  if (stageFile != INVALID_HANDLE_VALUE) CloseHandle(stageFile);
  if (!flushed || !Sha256File(staged, actual) || actual != expected) {
    DeleteFileW(staged.c_str());
    return FailAndRestart(target, data, 3, L"暂存文件校验失败；旧版本未改动。");
  }
  Trace(data, trace, "same_volume_stage_verified");
  std::error_code error;
  if (std::filesystem::exists(backup, error) || std::filesystem::exists(pending, error)) {
    DeleteFileW(staged.c_str());
    return FailAndRestart(target, data, 4, L"检测到未完成的升级，请重新启动程序完成恢复。");
  }
  DeleteFileW(health.c_str());
  if (!WriteText(pending, token + L"\n" + backup.wstring() + L"\n")) {
    DeleteFileW(staged.c_str());
    return FailAndRestart(target, data, 6, L"无法记录升级状态；旧版本未改动。");
  }
  if (!ReplaceFileW(target.c_str(), staged.c_str(), backup.c_str(),
                     REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
    bool restored = true;
    if (std::filesystem::is_regular_file(backup, error))
      restored = MoveFileExW(backup.c_str(), target.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    DeleteFileW(staged.c_str());
    if (restored) DeleteFileW(pending.c_str());
    return FailAndRestart(target, data, 4, restored
        ? L"无法替换程序文件；旧版本已保留，请解除文件占用后重试。"
        : L"文件替换失败，自动恢复未完成，备份已保留。请重新启动或重新解压安装包。", restored);
  }
  Trace(data, trace, "files_replaced");

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
      if (!PreserveLastGood(target)) {
        // Keep the healthy transaction journal so the next start can retry
        // archiving. Never discard the only rollback copy on an I/O failure.
        RecordFailure(data, L"升级成功，但旧版备份归档未完成；恢复记录已保留。");
        return 0;
      }
      DeleteFileW(pending.c_str());
      DeleteFileW((data / L"update.request").c_str());
      DeleteFileW((data / L"update.failure").c_str());
      DeleteFileW(package.c_str());
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
  if (!MoveFileExW(backup.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    RecordFailure(data, L"新版启动失败，且旧版本自动还原失败。请重新解压安装包。");
    return 7;
  }
  DeleteFileW(pending.c_str());
  RecordFailure(data, L"新版未通过启动检查；已自动还原旧版本。启动错误=" +
                          std::to_wstring(launchError) + L"，退出码=" +
                          std::to_wstring(childExitCode) + L"。");
  RestartOld(target, true);
  Trace(data, trace, "rollback_finished");
  return 8;
}
