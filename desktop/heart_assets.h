#pragma once

// Shared by the history list and compact combat HUD. The approved A PNG is
// kept untouched; only its transparent outer padding is omitted at draw time.
namespace plane_pet_hearts {
inline constexpr int kResourceId = 161;
inline constexpr int kHeartAtlasWidth = 2172;
inline constexpr int kHeartAtlasHeight = 724;
inline constexpr int kHeartSourceX[] = {70, 770, 1470};  // blue, red, depleted
inline constexpr int kHeartSourceY = 58;
inline constexpr int kHeartSourceWidth = 632;
inline constexpr int kHeartSourceHeight = 568;
static_assert(kHeartSourceX[2] + kHeartSourceWidth <= kHeartAtlasWidth);
static_assert(kHeartSourceY + kHeartSourceHeight <= kHeartAtlasHeight);
}  // namespace plane_pet_hearts
