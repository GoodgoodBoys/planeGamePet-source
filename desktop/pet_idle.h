#pragma once
#include <cmath>
#include <cstdint>

namespace plane_pet_ui {
inline constexpr double kIdleShotMaxTurn = 3.14159265358979323846 / 12.0;
inline constexpr uint32_t kIdleShotIntervalMs = 620;
inline constexpr uint32_t kIdleShotLifetimeMs = 1120;
struct IdleShot { double x, y, dx, dy; };

// Per-launch seed + shot identity: random between shots, stable on repaint.
inline double IdleAutoTurn(uint32_t seed, uint32_t index) {
  uint32_t value = seed + (index + 1U) * 0x9e3779b9U;
  value = (value ^ (value >> 16U)) * 0x85ebca6bU;
  value = (value ^ (value >> 13U)) * 0xc2b2ae35U;
  value ^= value >> 16U;
  return (static_cast<double>(value) / 4294967295.0 * 2.0 - 1.0) * kIdleShotMaxTurn;
}

template <typename PoseAt, typename Draw>
void ForEachIdleAutoShot(uint32_t elapsed, uint32_t seed, PoseAt poseAt, Draw draw) {
  const uint32_t newest = elapsed / kIdleShotIntervalMs;
  // Bounded reconstruction even after sleep: no queued catch-up burst,
  // per-frame RNG, mouse state, persistent bullet list or networking.
  for (uint32_t back = 0; back < 3 && back <= newest; ++back) {
    const uint32_t index = newest - back;
    const uint32_t fired = index * kIdleShotIntervalMs;
    const uint32_t age = elapsed - fired;
    if (age >= kIdleShotLifetimeMs) continue;
    const auto pose = poseAt(fired);
    const double turn = IdleAutoTurn(seed, index);
    const double dx = pose.directionX * std::cos(turn) - pose.directionY * std::sin(turn);
    const double dy = pose.directionX * std::sin(turn) + pose.directionY * std::cos(turn);
    const double distance = age * 0.105;
    draw(IdleShot{pose.x + pose.directionX * 13.0 + dx * distance,
                 pose.y + pose.directionY * 13.0 + dy * distance, dx, dy});
  }
}
}  // namespace plane_pet_ui
