#pragma once
#include "pc_motion_protocol.h"

namespace pcmotion {
// Remote planes and bullets share one buffered server timeline. Never smooth
// each bullet toward an unrelated latest coordinate; stable IDs avoid mixing
// removed projectiles with new shots. No speculative damage is rendered.
struct Timeline {
  std::array<Snapshot, 16> states{};
  size_t size = 0;
  int64_t offset = 0;
  void Add(const Snapshot &s, int64_t localMs) {
    if (size && (states[size - 1].presence.roundId != s.presence.roundId ||
        s.presence.game.phase != plink::GamePhase::Playing)) size = 0;
    const int64_t candidate = localMs - s.presence.game.serverTimeMs;
    offset = size ? std::min(offset, candidate) : candidate;
    if (size == states.size()) {
      for (size_t i = 1; i < size; ++i) states[i - 1] = states[i];
      --size;
    }
    states[size++] = s;
  }
  plink::SnapshotPayload Sample(int64_t localMs) const {
    if (!size) return {};
    const int64_t time = localMs - offset - 67; // two 30 Hz snapshots
    size_t a = 0;
    while (a + 1 < size && states[a + 1].presence.game.serverTimeMs <= time) ++a;
    const auto &left = states[a];
    auto result = left.presence.game;
    if (a + 1 == size || time <= result.serverTimeMs) return result; // freeze, don't overshoot
    const auto &right = states[a + 1];
    const int64_t interval = int64_t(right.presence.game.serverTimeMs) - result.serverTimeMs;
    const double t = interval > 0 ? std::clamp(double(time - result.serverTimeMs) / interval, 0.0, 1.0) : 0;
    const auto lerp = [t](int a, int b) { return static_cast<int16_t>(std::lround(a + (b - a) * t)); };
    for (unsigned i = 0; i < 2; ++i) {
      result.players[i].x = static_cast<int16_t>(std::lround((left.Position(i).x + (right.Position(i).x - left.Position(i).x) * t) / kUnit));
      result.players[i].y = static_cast<int16_t>(std::lround((left.Position(i).y + (right.Position(i).y - left.Position(i).y) * t) / kUnit));
    }
    for (unsigned i = 0; i < result.bulletCount; ++i) {
      auto &b = result.bullets[i];
      for (unsigned j = 0; j < right.presence.game.bulletCount; ++j) {
        const auto &n = right.presence.game.bullets[j];
        if (b.id == n.id && b.owner == n.owner) { b.x = lerp(b.x, n.x); b.y = lerp(b.y, n.y); break; }
      }
    }
    return result;
  }
};
} // namespace pcmotion
