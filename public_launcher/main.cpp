#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../common/app_version.h"
#include "../common/windows_arguments.h"
#include "../common/windows_activation.h"
#include "../common/network_status.h"

namespace {

constexpr WORD kTunnelResource = 201;
constexpr WORD kClientResource = 202;
constexpr WORD kUpdaterResource = 203;
constexpr DWORD kUpdateInstallExitCode = 73;
HANDLE gOwnedJob = nullptr;
struct OwnedJob {
  OwnedJob() {
    gOwnedJob = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (gOwnedJob && !SetInformationJobObject(gOwnedJob,
        JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
      CloseHandle(gOwnedJob);
      gOwnedJob = nullptr;
    }
  }
  ~OwnedJob() { if (gOwnedJob) CloseHandle(gOwnedJob); }
};
constexpr wchar_t kManifestUrl[] =
    L"https://egg-ota-test.oss-cn-beijing.aliyuncs.com/plane-pet/windows/release/latest.json";

std::wstring Quote(const std::wstring &value) { return plane_pet_windows::Quote(value); }

std::wstring WideUtf8(const std::string &text) {
  if (text.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<size_t>(length), L'\0');
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), result.data(),
                             length) == length
      ? result
      : std::wstring{};
}

std::wstring Option(const wchar_t *name) {
  return plane_pet_windows::Option(name);
}

uint16_t LocalPort() {
  wchar_t value[16]{};
  const DWORD length = GetEnvironmentVariableW(
      L"PLANE_PET_LOCAL_PORT", value, static_cast<DWORD>(std::size(value)));
  if (length == 0 || length >= std::size(value)) return 32110;
  wchar_t *end = nullptr;
  const unsigned long parsed = std::wcstoul(value, &end, 10);
  return end != value && *end == L'\0' && parsed >= 1024 && parsed <= 65535
             ? static_cast<uint16_t>(parsed)
             : 32110;
}

std::filesystem::path DataDirectory() {
  wchar_t localAppData[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
  std::filesystem::path root = length > 0 && length < std::size(localAppData)
                                   ? std::filesystem::path(localAppData)
                                   : std::filesystem::current_path();
  return root / L"PlanePet";
}

bool ExtractExecutable(WORD resourceId, const std::filesystem::path &target) {
  HMODULE module = GetModuleHandleW(nullptr);
  HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId),
                                 MAKEINTRESOURCEW(10));
  if (resource == nullptr) return false;
  HGLOBAL loaded = LoadResource(module, resource);
  const void *data = loaded == nullptr ? nullptr : LockResource(loaded);
  const DWORD size = SizeofResource(module, resource);
  if (data == nullptr || size == 0) return false;
  const std::filesystem::path temporary = target.wstring() + L".tmp";
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, data, size, &written, nullptr) != FALSE &&
                  written == size && FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  if (!ok || !MoveFileExW(temporary.c_str(), target.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

bool StartChild(const std::filesystem::path &executable,
                const std::wstring &arguments, DWORD flags,
                PROCESS_INFORMATION &process, bool owned = false) {
  std::wstring command = Quote(executable.wstring()) + L" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  process = PROCESS_INFORMATION{};
  if (owned && !gOwnedJob) return false;
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        flags | (owned ? CREATE_SUSPENDED : 0),
                        nullptr, executable.parent_path().c_str(), &startup,
                        &process)) return false;
  if (owned) {
    if (!AssignProcessToJobObject(gOwnedJob, process.hProcess) ||
        ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
      TerminateProcess(process.hProcess, 1);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      process = PROCESS_INFORMATION{};
      return false;
    }
  }
  return true;
}

void CloseProcessHandles(PROCESS_INFORMATION &process) {
  if (process.hThread != nullptr) CloseHandle(process.hThread);
  if (process.hProcess != nullptr) CloseHandle(process.hProcess);
  process = PROCESS_INFORMATION{};
}

void StopOwnedProcess(PROCESS_INFORMATION &process) {
  if (process.hProcess != nullptr &&
      WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, 0);
    WaitForSingleObject(process.hProcess, 1000);
  }
  CloseProcessHandles(process);
}

bool ReadUpdateRequest(const std::filesystem::path &path,
                       std::wstring &version, std::wstring &sha256,
                       std::filesystem::path &package) {
  std::ifstream input(path, std::ios::binary);
  std::string header;
  std::string versionText;
  std::string sha256Text;
  std::string packageText;
  if (!std::getline(input, header) || header != "PLANE_PET_UPDATE 1" ||
      !std::getline(input, versionText) ||
      !std::getline(input, sha256Text) ||
      !std::getline(input, packageText) || versionText.empty() ||
      sha256Text.size() != 64U || packageText.empty()) return false;
  version = WideUtf8(versionText);
  sha256 = WideUtf8(sha256Text);
  const std::wstring packageWide = WideUtf8(packageText);
  if (version.empty() || sha256.empty() || packageWide.empty()) return false;
  package = packageWide;
  std::error_code error;
  return std::filesystem::exists(package, error) && !error;
}

bool WriteHealthMarker(const std::filesystem::path &data,
                       const std::wstring &token) {
  if (token.empty()) return true;
  const std::filesystem::path target = data / L"update.health";
  const std::filesystem::path temporary = target.wstring() + L".tmp";
  std::wofstream output(temporary, std::ios::trunc);
  output << token << L'\n';
  output.close();
  return output && MoveFileExW(temporary.c_str(), target.c_str(),
                               MOVEFILE_REPLACE_EXISTING |
                                   MOVEFILE_WRITE_THROUGH) != FALSE;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  OwnedJob ownedJob;
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"PlanePetPublicLauncher");
  if (mutex == nullptr) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    bool activated = false;
    for (int attempt = 0; attempt < 20 && !activated; ++attempt) {
      activated = plane_pet_windows::ActivatePublicInstance();
      if (!activated) Sleep(100);
    }
    if (!activated) MessageBoxW(nullptr,
        L"Plane Pet 已在运行或正在启动。\n请查看托盘；旧版客户端需先退出再运行新版。",
        L"Plane Pet", MB_OK | MB_ICONINFORMATION);
    CloseHandle(mutex);
    return 0;
  }
  const std::filesystem::path data = DataDirectory();
  const std::filesystem::path runtime =
      data / (std::wstring(L"runtime-release-") +
              plane_pet_version::kWideString);
  std::error_code error;
  std::filesystem::create_directories(runtime, error);
  const std::filesystem::path tunnel = runtime / L"PlanePetTunnel.exe";
  const std::filesystem::path client = runtime / L"PlanePetClient.exe";
  const std::filesystem::path updater = runtime / L"PlanePetUpdater.exe";
  const uint16_t localPort = LocalPort();
  if (error || !ExtractExecutable(kTunnelResource, tunnel) ||
      !ExtractExecutable(kClientResource, client) ||
      !ExtractExecutable(kUpdaterResource, updater) ||
      !ExtractExecutable(204, runtime / L"THIRD-PARTY-NOTICES.txt") ||
      !ExtractExecutable(205, runtime / L"PRIVACY.txt")) {
    MessageBoxW(nullptr, L"无法释放联网组件，请检查存档权限或安全软件拦截。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 2;
  }

  PROCESS_INFORMATION tunnelProcess{};
  PROCESS_INFORMATION clientProcess{};
  if (Option(L"complete-update").empty() &&
      std::filesystem::is_regular_file(data / L"update.pending", error)) {
    wchar_t modulePath[32768]{};
    PROCESS_INFORMATION recovery{};
    const bool started = GetModuleFileNameW(nullptr, modulePath,
        static_cast<DWORD>(std::size(modulePath))) &&
        StartChild(updater, L"--recover=1 --parent-pid=" +
            std::to_wstring(GetCurrentProcessId()) + L" --target=" +
            Quote(modulePath) + L" --data=" + Quote(data.wstring()),
            CREATE_NO_WINDOW, recovery);
    if (started) CloseProcessHandles(recovery);
    else MessageBoxW(nullptr, L"无法启动升级恢复。原程序和备份均未改动，请重新解压安装包。",
                     L"Plane Pet", MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return started ? 0 : 5;
  }
  const std::wstring tunnelArguments =
      L"--local-port=" + std::to_wstring(localPort);
  if (!StartChild(tunnel, tunnelArguments, CREATE_NO_WINDOW, tunnelProcess, true)) {
    MessageBoxW(nullptr, L"无法启动加密联网隧道。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }
  CloseHandle(tunnelProcess.hThread);
  tunnelProcess.hThread = nullptr;
  Sleep(700);
  if (WaitForSingleObject(tunnelProcess.hProcess, 0) != WAIT_TIMEOUT) {
    CloseProcessHandles(tunnelProcess);
    const auto network = plane_pet_network::Read(data / L"tunnel.status");
    const auto reason = L"加密联网隧道启动失败。\n\n" +
        plane_pet_network::Describe(network) + L"\n错误码：" + std::to_wstring(network.error);
    MessageBoxW(nullptr, reason.c_str(), L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }

  const std::wstring state = (data / L"public.binding").wstring();
  const std::wstring updateRequest = (data / L"update.request").wstring();
  const bool completedUpdate = !Option(L"complete-update").empty();
  const bool rolledBackUpdate = Option(L"update-rollback") == L"1";
  const std::wstring readyName = completedUpdate
      ? L"Local\\PlanePet.Ready." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetTickCount64()) : L"";
  HANDLE readyEvent = completedUpdate
      ? CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()) : nullptr;
  struct ReadyHandle { HANDLE value; ~ReadyHandle() { if (value) CloseHandle(value); } } readyHandle{readyEvent};
  if (completedUpdate && !readyEvent) {
    StopOwnedProcess(tunnelProcess);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 5;
  }
  const std::wstring arguments =
      L"--server=127.0.0.1:" + std::to_wstring(localPort) +
      L" --code=0 --slot=1 --name=我 --peer=好友 "
      L"--pet-x=120 --pet-y=180 --telemetry-upload=1 --state=" + Quote(state) +
      L" --update-enabled=1 --update-manifest=" + Quote(kManifestUrl) +
      L" --update-request=" + Quote(updateRequest) +
      L" --public-instance=1" +
      L" --network-status=" + Quote((data / L"tunnel.status").wstring()) +
      (completedUpdate ? L" --ready-event=" + Quote(readyName) : L"") +
      (completedUpdate ? L" --update-installed=1 --update-token=" +
          Quote(Option(L"complete-update")) : L"") +
      (rolledBackUpdate ? L" --update-rollback=1" : L"") +
      (Option(L"update-failed") == L"1" ? L" --update-failed=1" : L"");
  if (!StartChild(client, arguments, 0, clientProcess, true)) {
    StopOwnedProcess(tunnelProcess);
    MessageBoxW(nullptr, L"无法启动桌宠客户端。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 4;
  }
  CloseHandle(clientProcess.hThread);
  clientProcess.hThread = nullptr;
  Sleep(800);
  if (WaitForSingleObject(clientProcess.hProcess, 0) != WAIT_TIMEOUT) {
    CloseProcessHandles(clientProcess);
    StopOwnedProcess(tunnelProcess);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 4;
  }
  // Only commit after the initialized client has pumped its UI for 2 seconds.
  // No dependency on a working Internet connection or a currently online peer.
  HANDLE readiness[] = {readyEvent, clientProcess.hProcess, tunnelProcess.hProcess};
  if (completedUpdate &&
      (WaitForMultipleObjects(3, readiness, FALSE, 30000) != WAIT_OBJECT_0 ||
       WaitForSingleObject(clientProcess.hProcess, 0) != WAIT_TIMEOUT ||
       !WriteHealthMarker(data, Option(L"complete-update")))) {
    StopOwnedProcess(clientProcess);
    StopOwnedProcess(tunnelProcess);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 5;
  }
  HANDLE children[] = {clientProcess.hProcess, tunnelProcess.hProcess};
  const DWORD stopped = WaitForMultipleObjects(2, children, FALSE, INFINITE);
  bool updateHandoffFailed = false;
  std::filesystem::path launcherForRetry;
  if (stopped == WAIT_OBJECT_0) {
    DWORD clientExitCode = 0;
    GetExitCodeProcess(clientProcess.hProcess, &clientExitCode);
    CloseProcessHandles(clientProcess);
    // Allow the tunnel to forward the client's final app_exited event.
    Sleep(200);
    StopOwnedProcess(tunnelProcess);
    if (clientExitCode == kUpdateInstallExitCode) {
      wchar_t modulePath[32768]{};
      if (GetModuleFileNameW(nullptr, modulePath,
                             static_cast<DWORD>(std::size(modulePath))) != 0) {
        launcherForRetry = modulePath;
      }
      std::wstring version;
      std::wstring sha256;
      std::filesystem::path package;
      if (!launcherForRetry.empty() &&
          ReadUpdateRequest(data / L"update.request", version, sha256,
                            package)) {
        PROCESS_INFORMATION updaterProcess{};
        const std::wstring updaterArguments =
            L"--parent-pid=" + std::to_wstring(GetCurrentProcessId()) +
            L" --target=" + Quote(launcherForRetry.wstring()) + L" --package=" +
            Quote(package.wstring()) + L" --sha256=" + sha256 +
            L" --version=" + version + L" --data=" +
            Quote(data.wstring());
        if (StartChild(updater, updaterArguments, CREATE_NO_WINDOW,
                       updaterProcess)) {
          CloseProcessHandles(updaterProcess);
        } else {
          updateHandoffFailed = true;
        }
      } else {
        updateHandoffFailed = true;
      }
    }
  } else {
    CloseProcessHandles(tunnelProcess);
    MessageBoxW(nullptr,
                L"加密联网组件异常退出，桌宠将关闭。\n请重新打开 Plane Pet；若反复出现，请检查网络或安全软件。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    StopOwnedProcess(clientProcess);
  }
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  if (updateHandoffFailed) {
    if (!launcherForRetry.empty()) {
      PROCESS_INFORMATION retry{};
      if (StartChild(launcherForRetry, L"--update-failed=1", 0, retry))
        CloseProcessHandles(retry);
    }
    return 5;
  }
  return 0;
}
