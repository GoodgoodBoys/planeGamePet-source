#pragma once

#include <cstdint>

namespace plane_pet_version {

// Public release numbering restarts at 1.0.0. Epoch 0 denotes historical
// test builds (1.x/2.x/3.x); epoch 1 is the public release channel. Never infer
// the wire/game protocol or telemetry schema solely from this display version.
constexpr uint8_t kReleaseEpoch = 1;
constexpr uint8_t kMajor = 1;
constexpr uint8_t kMinor = 0;
constexpr uint8_t kPatch = 1;
constexpr char kString[] = "1.0.1";
constexpr wchar_t kWideString[] = L"1.0.1";
constexpr char kProductId[] = "plane-pet-windows";
constexpr char kReleaseChannel[] = "release";

}  // namespace plane_pet_version
