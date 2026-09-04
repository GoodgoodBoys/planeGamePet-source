#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>
#include <filesystem>
#include <string>

namespace {

constexpr WORD kTunnelResource = 201;
constexpr WORD kClientResource = 202;

std::wstring Quote(const std::wstring &value) { return L"\"" + value + L"\""; }

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
                PROCESS_INFORMATION &process) {
  std::wstring command = Quote(executable.wstring()) + L" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  process = PROCESS_INFORMATION{};
  return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags,
                        nullptr, executable.parent_path().c_str(), &startup,
                        &process) != FALSE;
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

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"PlanePetPublicLauncher");
  if (mutex == nullptr) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(nullptr, L"Plane Pet 已经在运行。", L"Plane Pet",
                MB_OK | MB_ICONINFORMATION);
    CloseHandle(mutex);
    return 0;
  }
  const std::filesystem::path data = DataDirectory();
  const std::filesystem::path runtime = data / L"runtime-public-0.6.7";
  std::error_code error;
  std::filesystem::create_directories(runtime, error);
  const std::filesystem::path tunnel = runtime / L"PlanePetTunnel.exe";
  const std::filesystem::path client = runtime / L"PlanePetClient.exe";
  const uint16_t localPort = LocalPort();
  if (error || !ExtractExecutable(kTunnelResource, tunnel) ||
      !ExtractExecutable(kClientResource, client)) {
    MessageBoxW(nullptr, L"无法释放联网组件，请检查存档权限或安全软件拦截。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 2;
  }

  PROCESS_INFORMATION tunnelProcess{};
  PROCESS_INFORMATION clientProcess{};
  const std::wstring tunnelArguments =
      L"--local-port=" + std::to_wstring(localPort);
  if (!StartChild(tunnel, tunnelArguments, CREATE_NO_WINDOW, tunnelProcess)) {
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
    MessageBoxW(nullptr, L"加密联网隧道启动失败。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }

  const std::wstring state = (data / L"public.binding").wstring();
  const std::wstring arguments =
      L"--server=127.0.0.1:" + std::to_wstring(localPort) +
      L" --code=0 --slot=1 --name=我 --peer=好友 "
      L"--pet-x=120 --pet-y=180 --telemetry-upload=1 --state=" + Quote(state);
  if (!StartChild(client, arguments, 0, clientProcess)) {
    StopOwnedProcess(tunnelProcess);
    MessageBoxW(nullptr, L"无法启动桌宠客户端。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 4;
  }
  CloseHandle(clientProcess.hThread);
  clientProcess.hThread = nullptr;
  HANDLE children[] = {clientProcess.hProcess, tunnelProcess.hProcess};
  const DWORD stopped = WaitForMultipleObjects(2, children, FALSE, INFINITE);
  if (stopped == WAIT_OBJECT_0) {
    CloseProcessHandles(clientProcess);
    // Allow the tunnel to forward the client's final app_exited event.
    Sleep(200);
    StopOwnedProcess(tunnelProcess);
  } else {
    CloseProcessHandles(tunnelProcess);
    MessageBoxW(nullptr,
                L"加密联网组件异常退出，桌宠将关闭。\n请重新打开 Plane Pet；若反复出现，请检查网络或安全软件。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    StopOwnedProcess(clientProcess);
  }
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
