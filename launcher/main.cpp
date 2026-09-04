#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <string>

namespace {

constexpr WORD kServerResource = 101;
constexpr WORD kAliceResource = 102;
constexpr WORD kBobResource = 103;

std::wstring Quote(const std::wstring &value) {
  return L"\"" + value + L"\"";
}

std::wstring Option(const wchar_t *name) {
  const std::wstring command = GetCommandLineW();
  const std::wstring prefix = std::wstring(L"--") + name + L"=";
  const size_t begin = command.find(prefix);
  if (begin == std::wstring::npos) return L"";
  size_t valueBegin = begin + prefix.size();
  const bool quoted = valueBegin < command.size() && command[valueBegin] == L'\"';
  if (quoted) ++valueBegin;
  size_t end = quoted ? command.find(L'\"', valueBegin)
                      : command.find(L' ', valueBegin);
  if (end == std::wstring::npos) end = command.size();
  return command.substr(valueBegin, end - valueBegin);
}

std::filesystem::path DefaultDataDirectory() {
  wchar_t localAppData[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
  std::filesystem::path root = length > 0 && length < std::size(localAppData)
                                   ? std::filesystem::path(localAppData)
                                   : std::filesystem::current_path();
  return root / L"PlanePet";
}

bool StartChild(const std::wstring &path, const std::wstring &arguments,
                const std::wstring &directory, DWORD flags,
                PROCESS_INFORMATION &process) {
  std::wstring command = Quote(path) + L" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  process = PROCESS_INFORMATION{};
  return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags,
                        nullptr, directory.c_str(), &startup, &process) != FALSE;
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
  const bool success = WriteFile(file, data, size, &written, nullptr) != FALSE &&
                       written == size && FlushFileBuffers(file) != FALSE;
  CloseHandle(file);
  if (!success) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  if (MoveFileExW(temporary.c_str(), target.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
    DeleteFileW(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"PlanePetFullFlowLocalLauncher");
  if (mutex == nullptr) return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    MessageBoxW(nullptr, L"完整体验已经在运行。", L"Plane Pet",
                MB_OK | MB_ICONINFORMATION);
    CloseHandle(mutex);
    return 0;
  }

  const std::wstring configuredData = Option(L"data-dir");
  const std::filesystem::path dataDirectory =
      configuredData.empty() ? DefaultDataDirectory()
                             : std::filesystem::path(configuredData);
  std::error_code directoryError;
  std::filesystem::create_directories(dataDirectory, directoryError);
  if (directoryError) {
    MessageBoxW(nullptr, L"无法创建桌宠绑定存档目录。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 5;
  }
  const std::filesystem::path runtimeDirectory =
      dataDirectory / L"runtime-0.3.0";
  std::error_code runtimeError;
  std::filesystem::create_directories(runtimeDirectory, runtimeError);
  const std::filesystem::path serverExecutable =
      runtimeDirectory / L"PlanePetServer.exe";
  const std::filesystem::path aliceExecutable =
      runtimeDirectory / L"PlanePetAlice.exe";
  const std::filesystem::path bobExecutable =
      runtimeDirectory / L"PlanePetBob.exe";
  if (runtimeError || !ExtractExecutable(kServerResource, serverExecutable) ||
      !ExtractExecutable(kAliceResource, aliceExecutable) ||
      !ExtractExecutable(kBobResource, bobExecutable)) {
    MessageBoxW(nullptr,
                L"无法释放内置运行组件，请检查本机存档目录权限或安全软件拦截。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 2;
  }
  const std::wstring directory = runtimeDirectory.wstring();
  const std::wstring serverPath = serverExecutable.wstring();
  const std::wstring alicePath = aliceExecutable.wstring();
  const std::wstring bobPath = bobExecutable.wstring();
  const std::wstring serverStore =
      (dataDirectory / L"server_bindings.db").wstring();
  const std::wstring aliceState = (dataDirectory / L"alice.binding").wstring();
  const std::wstring bobState = (dataDirectory / L"bob.binding").wstring();
  const std::wstring telemetry = Option(L"telemetry");
  const std::wstring telemetryArgument = telemetry.empty()
      ? L"" : L" --telemetry=" + telemetry;

  PROCESS_INFORMATION server{};
  PROCESS_INFORMATION alice{};
  PROCESS_INFORMATION bob{};
  if (!StartChild(serverPath,
                  L"--port=32110 --seconds=180 --store=" + Quote(serverStore), directory,
                  CREATE_NO_WINDOW, server)) {
    MessageBoxW(nullptr, L"无法启动本地匹配服务。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }
  CloseHandle(server.hThread);
  server.hThread = nullptr;
  Sleep(350);
  if (WaitForSingleObject(server.hProcess, 0) != WAIT_TIMEOUT) {
    CloseProcessHandles(server);
    MessageBoxW(nullptr,
                L"本地匹配服务未能启动。UDP 32110 端口可能已被占用。",
                L"Plane Pet", MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 3;
  }

  const bool aliceStarted = StartChild(
      alicePath,
      L"--server=127.0.0.1:32110 --code=0 --slot=1 "
      L"--name=Alice --peer=Bob --pet-x=120 --pet-y=180 --state=" +
          Quote(aliceState) + telemetryArgument,
      directory, 0, alice);
  const bool bobStarted = StartChild(
      bobPath,
      L"--server=127.0.0.1:32110 --code=0 --slot=2 "
      L"--name=Bob --peer=Alice --pet-x=430 --pet-y=350 --hidden=0 --state=" +
          Quote(bobState) + telemetryArgument,
      directory, 0, bob);
  if (!aliceStarted || !bobStarted) {
    StopOwnedProcess(alice);
    StopOwnedProcess(bob);
    StopOwnedProcess(server);
    MessageBoxW(nullptr, L"无法启动两个桌宠客户端。", L"Plane Pet",
                MB_OK | MB_ICONERROR);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 4;
  }
  CloseHandle(alice.hThread);
  alice.hThread = nullptr;
  CloseHandle(bob.hThread);
  bob.hThread = nullptr;

  HANDLE clients[2] = {alice.hProcess, bob.hProcess};
  WaitForMultipleObjects(2, clients, TRUE, INFINITE);
  CloseProcessHandles(alice);
  CloseProcessHandles(bob);
  StopOwnedProcess(server);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
