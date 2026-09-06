#pragma once

#include <cstdint>

namespace plane_pet_version {

// 1.0.0 is the first release that contains the built-in update bootstrap.
// Keep these values in one place: the desktop client, server handshake,
// launcher package names and release manifest all derive from them.
constexpr uint8_t kMajor = 1;
constexpr uint8_t kMinor = 0;
constexpr uint8_t kPatch = 2;
constexpr char kString[] = "1.0.2";
constexpr wchar_t kWideString[] = L"1.0.2";
constexpr char kProductId[] = "plane-pet-windows";

}  // namespace plane_pet_version
