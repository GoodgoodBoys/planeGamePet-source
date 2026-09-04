#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// A syntactically valid replacement that deliberately never writes the
// updater health marker. It exercises automatic rollback without relying on
// platform-specific handling of a malformed PE file.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  return 12;
}
