#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

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

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  const std::wstring token = Option(L"complete-update");
  std::filesystem::path data = Option(L"update-health-data");
  if (data.empty()) {
    wchar_t module[32768]{};
    if (GetModuleFileNameW(nullptr, module,
                           static_cast<DWORD>(std::size(module))) != 0) {
      data = std::filesystem::path(module).parent_path() / L"data";
    }
  }
  if (Option(L"update-failed") == L"1" && !data.empty()) {
    std::ofstream marker(data / L"old-version-restarted");
    marker << "restarted";
  }
  if (token.empty() || data.empty()) return 0;
  std::wofstream output(data / L"update.health", std::ios::trunc);
  output << token << L'\n';
  output.close();
  Sleep(700);
  return output ? 0 : 1;
}
