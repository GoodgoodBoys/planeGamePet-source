#pragma once
#include "pc_battle.h"

namespace pcbattle {
inline Point Blend(Point a, Point b, double t) {
  return {static_cast<int32_t>(std::lround(a.x + (b.x - a.x) * t)),
          static_cast<int32_t>(std::lround(a.y + (b.y - a.y) * t))};
}
inline World Sample(const Predictor &p, double alpha) {
  alpha = std::clamp(alpha, 0.0, 1.0);
  const auto &a = p.previous, &b = p.current;
  if (alpha >= 1.0) return b;
  if (a.tick + 1 != b.tick) return b;
  World result = a;
  for (unsigned i = 0; i < 2; ++i) result.players[i] = Blend(a.players[i], b.players[i], alpha);
  result.bullets = {};
  unsigned count = 0;
  const auto show = [&](const Bullet &source, Point from) {
    if (count >= result.bullets.size()) return;
    const Impact *hit = nullptr;
    for (unsigned i = 0; i < b.impactCount; ++i)
      if (b.impacts[i].tick == b.tick && b.impacts[i].bullet == source.id) { hit = &b.impacts[i]; break; }
    Point end = source.p;
    if (hit) {
      const double time = hit->fraction / 65535.0;
      if (alpha >= time) return;
      Point terminal = hit->contact;
      terminal.y -= (source.owner == 1 ? -1 : 1) * kProjectileTip;
      end = Blend(from, terminal, time > 0 ? alpha / time : 1);
    } else {
      end = Blend(from, end, alpha);
      if (end.y < 0 || end.y >= 320 * kUnit) return;
    }
    result.bullets[count++] = {true, source.id, source.owner, end};
  };
  for (const auto &old : a.bullets) if (old.active) {
    const Bullet *now = nullptr;
    for (const auto &candidate : b.bullets) if (candidate.id == old.id && candidate.owner == old.owner) { now = &candidate; break; }
    if (now) show(*now, old.p);
    else {
      // A terminal snapshot/slot reuse must not freeze a bullet in empty air.
      auto continuation = old; continuation.p.y += (old.owner == 1 ? -1 : 1) * 3 * kUnit;
      show(continuation, old.p);
    }
  }
  for (const auto &now : b.bullets) if (now.id && ((now.id + 1U) / 2U) * 48U == b.tick) {
    bool existed = false;
    for (const auto &old : a.bullets) if (old.active && old.id == now.id) existed = true;
    if (existed) continue;
    Point start = a.players[now.owner - 1];
    start.y += (now.owner == 1 ? -1 : 1) * plink::kPlaneHalfHeight * kUnit;
    show(now, start);
  }
  for (unsigned i = 0; i < b.impactCount; ++i) {
    const auto &e = b.impacts[i];
    if (e.tick == b.tick && alpha >= e.fraction / 65535.0) result.health[e.target] = e.healthAfter;
  }
  return result;
}
inline plink::SnapshotPayload Presentation(const Predictor &p, double alpha, plink::SnapshotPayload metadata) {
  // A terminal result is authoritative, never a continuation of speculative
  // bullets/HP. Also covers a final packet received after a local sync fault.
  if (metadata.phase == plink::GamePhase::Finished) return metadata;
  const auto sample = Sample(p, alpha);
  plink::WorldState legacy; sample.Export(legacy);
  for (unsigned i = 0; i < 2; ++i) {
    metadata.players[i] = legacy.players[i];
    metadata.players[i].x = static_cast<int16_t>(std::lround(sample.players[i].x / double(kUnit)));
    metadata.players[i].y = static_cast<int16_t>(std::lround(sample.players[i].y / double(kUnit)));
  }
  metadata.bulletCount = 0;
  for (const auto &b : sample.bullets) if (b.active) {
    metadata.bullets[metadata.bulletCount++] = {b.id, b.owner,
        static_cast<int16_t>(std::lround(b.p.x / double(kUnit))),
        static_cast<int16_t>(std::lround(b.p.y / double(kUnit)))};
  }
  return metadata;
}
} // namespace pcbattle
