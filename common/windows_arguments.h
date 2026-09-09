#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

namespace plane_pet_windows {

// Complete Unicode arguments, never substrings of another option or path.
inline std::wstring OptionFrom(const wchar_t *command, const wchar_t *name,
                               const wchar_t *fallback = L"") {
  int count = 0;
  LPWSTR *args = CommandLineToArgvW(command, &count);
  if (!args) return fallback;
  const std::wstring prefix = std::wstring(L"--") + name + L"=";
  std::wstring result = fallback;
  for (int index = 1; index < count; ++index) {
    const std::wstring value = args[index];
    if (value.compare(0, prefix.size(), prefix) == 0) {
      result = value.substr(prefix.size());
      break;
    }
  }
  LocalFree(args);
  return result;
}

inline std::wstring Option(const wchar_t *name, const wchar_t *fallback = L"") {
  return OptionFrom(GetCommandLineW(), name, fallback);
}

inline std::string Utf8(const std::wstring &value) {
  if (value.empty()) return {};
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (length <= 0) return {};
  std::string result(static_cast<size_t>(length), '\0');
  return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), length, nullptr, nullptr)
      == length ? result : std::string{};
}

// CRT/CommandLineToArgvW quoting, including trailing backslashes and quotes.
inline std::wstring Quote(const std::wstring &value) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : value) {
    if (ch == L'\\') { ++slashes; continue; }
    result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (ch == L'"') result.push_back(L'\\');
    result.push_back(ch);
  }
  result.append(slashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

}  // namespace plane_pet_windows
