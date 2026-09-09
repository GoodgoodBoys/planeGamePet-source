#pragma once
#include <windows.h>

namespace plane_pet_windows {
constexpr wchar_t kPublicWindowProperty[] = L"PlanePet.PublicClient.v1";
inline UINT ActivateMessage() {
  static const UINT message = RegisterWindowMessageW(L"PlanePet.ActivatePublic.v1");
  return message;
}
struct ActivationTarget { HWND window = nullptr; DWORD process = 0; };
inline BOOL CALLBACK FindPublicWindow(HWND window, LPARAM parameter) {
  auto &target = *reinterpret_cast<ActivationTarget *>(parameter);
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if ((!target.process || target.process == process) &&
      GetPropW(window, kPublicWindowProperty)) {
    target.window = window;
    return FALSE;
  }
  return TRUE;
}
inline bool ActivatePublicInstance(DWORD expectedProcess = 0) {
  ActivationTarget target{nullptr, expectedProcess};
  EnumWindows(FindPublicWindow, reinterpret_cast<LPARAM>(&target));
  HWND window = target.window;
  if (!window || !ActivateMessage()) return false;
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  AllowSetForegroundWindow(process);
  return PostMessageW(window, ActivateMessage(), 0, 0) != FALSE;
}
}  // namespace plane_pet_windows
