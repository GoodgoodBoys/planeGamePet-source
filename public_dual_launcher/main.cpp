#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <string>

namespace {

constexpr WORD kTunnelResource = 201;
constexpr WORD kClientResource = 202;

std::wstring Quote(const std::wstring &value) { return L"\"" + value + L"\""; }

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

bool StillRunning(const PROCESS_INFORMATION &process) {
  return process.hProcess != nullptr &&
         WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"PlanePetPublicDualLauncher");
  if (mutex == nullptr) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(nullptr, L"单机双端公网测试已经在运行。", L"Plane Pet",
                MB_OK | MB_ICONINFORMATION);
    CloseHandle(mutex);
    return 0;
  }

  const std::filesystem::path data = DataDirectory();
  const std::filesystem::path runtime = data / L"runtime-public-dual-0.6.7";
  std::error_code error;
  std::filesystem::create_directories(runtime, error);
  const std::filesystem::path tunnel = runtime / L"PlanePetTunnel.exe";
  const std::filesystem::path client = runtime / L"PlanePetClient.exe";
  if (error || !ExtractExecutable(kTunnelResource, tunnel) ||
      !ExtractExecutable(kClientResource, client)) {
    MessageBoxW(nullptr, L"无法释放双端联网组件，请检查存档权限或安全软件拦截。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 2;
  }

  PROCESS_INFORMATION tunnelA{};
  PROCESS_INFORMATION tunnelB{};
  PROCESS_INFORMATION clientA{};
  PROCESS_INFORMATION clientB{};
  const bool testMode = Option(L"test", L"0") == L"1";
  std::wstring tunnelArgumentsA = testMode
      ? L"--instance=dual-qa-a --local-port=32110"
      : L"--instance=dual-a --local-port=32110";
  std::wstring tunnelArgumentsB = testMode
      ? L"--instance=dual-qa-b --local-port=32113"
      : L"--instance=dual-b --local-port=32113";
  const std::wstring testTokenA = Option(L"test-token-a", L"");
  const std::wstring testTokenB = Option(L"test-token-b", L"");
  if (!testTokenA.empty()) tunnelArgumentsA += L" --token=" + testTokenA;
  if (!testTokenB.empty()) tunnelArgumentsB += L" --token=" + testTokenB;
  if (!StartChild(tunnel, tunnelArgumentsA,
                  CREATE_NO_WINDOW, tunnelA) ||
      !StartChild(tunnel, tunnelArgumentsB,
                  CREATE_NO_WINDOW, tunnelB)) {
    StopOwnedProcess(tunnelA);
    StopOwnedProcess(tunnelB);
    MessageBoxW(nullptr, L"无法启动两条加密公网隧道。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }
  CloseHandle(tunnelA.hThread);
  tunnelA.hThread = nullptr;
  CloseHandle(tunnelB.hThread);
  tunnelB.hThread = nullptr;
  Sleep(900);
  if (!StillRunning(tunnelA) || !StillRunning(tunnelB)) {
    StopOwnedProcess(tunnelA);
    StopOwnedProcess(tunnelB);
    MessageBoxW(nullptr,
                L"公网隧道启动失败。请先退出其他 Plane Pet 测试程序后重试。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }

  const std::wstring stateA =
      (data / (testMode ? L"public-dual-qa-a.binding"
                        : L"public-dual-a.binding")).wstring();
  const std::wstring stateB =
      (data / (testMode ? L"public-dual-qa-b.binding"
                        : L"public-dual-b.binding")).wstring();
  std::wstring argumentsA =
      L"--server=127.0.0.1:32110 --code=0 --slot=1 --name=A端 --peer=B端 "
      L"--pet-x=120 --pet-y=170 --telemetry-upload=1 --state=" + Quote(stateA);
  std::wstring argumentsB =
      L"--server=127.0.0.1:32113 --code=0 --slot=2 --name=B端 --peer=A端 "
      L"--pet-x=480 --pet-y=350 --telemetry-upload=1 --state=" + Quote(stateB);
  if (testMode) {
    argumentsA += L" --telemetry=0 --hidden=1";
    argumentsB += L" --telemetry=0 --hidden=1";
  }
  if (!StartChild(client, argumentsA, 0, clientA) ||
      !StartChild(client, argumentsB, 0, clientB)) {
    StopOwnedProcess(clientA);
    StopOwnedProcess(clientB);
    StopOwnedProcess(tunnelA);
    StopOwnedProcess(tunnelB);
    MessageBoxW(nullptr, L"无法启动两个公网桌宠客户端。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 4;
  }
  CloseHandle(clientA.hThread);
  clientA.hThread = nullptr;
  CloseHandle(clientB.hThread);
  clientB.hThread = nullptr;

  HANDLE clients[] = {clientA.hProcess, clientB.hProcess};
  WaitForMultipleObjects(2, clients, TRUE, INFINITE);
  CloseProcessHandles(clientA);
  CloseProcessHandles(clientB);
  StopOwnedProcess(tunnelA);
  StopOwnedProcess(tunnelB);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
