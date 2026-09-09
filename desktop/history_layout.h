#pragma once

namespace plane_pet_history_ui {

inline constexpr int kFirstRowTop = 178;
inline constexpr int kRowPitch = 38;
inline constexpr int kRowHeight = 34;
inline constexpr int kOwnPlaneX = 126;
inline constexpr int kPeerPlaneX = 222;
inline constexpr int kPlaneSize = 28;
// Sprites point upwards at zero. Screen-space negative rotation is CCW.
inline constexpr double kPlaneAngle = -3.14159265358979323846 / 4.0;
inline constexpr int kOwnHeartX = 150;
inline constexpr int kPeerHeartX = 248;
inline constexpr int kHeartPitch = 21;
inline constexpr int kHeartScale = 2;
inline constexpr int kHeartWidth = 20;
inline constexpr int kHeartHeight = 18;
inline constexpr int kHeartY = kRowHeight / 2;
inline constexpr int kTimeLeft = 310;

static_assert(kHeartPitch > kHeartWidth);
static_assert(kHeartHeight < kRowHeight);
static_assert(kPeerHeartX + 2 * kHeartPitch + kHeartWidth / 2 < kTimeLeft);
static_assert(kFirstRowTop + 9 * kRowPitch + kRowHeight < 600);

}  // namespace plane_pet_history_ui
