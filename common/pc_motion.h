#pragma once

// PC-only deterministic movement. Never change the ESP32/legacy simulation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "../shared/plane_sim.h"

namespace pcmotion {
constexpr int kUnit = 256;
constexpr int kHz = 60;
constexpr int kSpeed = 90; // world pixels/second, for BOTH input methods
constexpr int kStep = kSpeed * kUnit / kHz;
constexpr size_t kPendingLimit = 60;
constexpr size_t kBatchLimit = 48;
struct Point { int32_t x = 0, y = 0; };
inline bool operator==(Point a, Point b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Point a, Point b) { return !(a == b); }
struct Command {
  uint8_t keys = 0;
  bool mouse = false;
  uint8_t x = 0;
  uint16_t y = 0;
};
inline bool Valid(const Command &c) {
  return c.keys <= 15 && c.x < plink::kWorldWidth && c.y < plink::kWorldHeight &&
      (!c.mouse || c.keys == 0);
}
inline Point Spawn(unsigned i) {
  return {kUnit * (i ? plink::kPlayerTwoSpawnX : plink::kPlayerOneSpawnX),
          kUnit * (i ? plink::kPlayerTwoSpawnY : plink::kPlayerOneSpawnY)};
}
inline Point Bound(Point p, unsigned i) {
  p.x = std::clamp(p.x, kUnit * (i ? plink::kPlayerTwoMinX : plink::kPlayerOneMinX),
      kUnit * (i ? plink::kPlayerTwoMaxX : plink::kPlayerOneMaxX));
  p.y = std::clamp(p.y, kUnit * (i ? plink::kPlayerTwoMinY : plink::kPlayerOneMinY),
      kUnit * (i ? plink::kPlayerTwoMaxY : plink::kPlayerOneMaxY));
  return p;
}
// Integer ceil(sqrt(n)): stable across Windows/Linux; never rounds speed up.
inline int64_t RootCeil(int64_t n) {
  int64_t lo = 0, hi = 131072;
  while (lo < hi) {
    const int64_t mid = (lo + hi) / 2;
    if (mid * mid < n) lo = mid + 1; else hi = mid;
  }
  return lo;
}
inline Point Move(Point p, Command c, unsigned i) {
  if (!Valid(c) || i > 1) return p;
  int64_t dx = 0, dy = 0;
  if (c.mouse) {
    Point target{kUnit * c.x, kUnit * c.y};
    if (i) target = {kUnit * (plink::kWorldWidth - 1) - target.x,
                     kUnit * (plink::kWorldHeight - 1) - target.y};
    target = Bound(target, i);
    dx = target.x - p.x; dy = target.y - p.y;
  } else {
    dx = ((c.keys & plink::InputRight) != 0) - ((c.keys & plink::InputLeft) != 0);
    dy = ((c.keys & plink::InputDown) != 0) - ((c.keys & plink::InputUp) != 0);
    dx *= kUnit; dy *= kUnit;
    if (i) { dx = -dx; dy = -dy; }
  }
  const int64_t length = RootCeil(dx * dx + dy * dy);
  if (!length) return p;
  const int64_t distance = c.mouse ? std::min<int64_t>(length, kStep) : kStep;
  p.x += static_cast<int32_t>(dx * distance / length);
  p.y += static_cast<int32_t>(dy * distance / length);
  return Bound(p, i);
}

// Input ordinal is independent of packet sequence. Retransmitted commands are
// immutable. The server consumes at most ONE command per simulation tick;
// missing input means hold position, never extrapolate an unacknowledged key.
struct InputQueue {
  std::array<Command, kPendingLimit> commands{};
  std::array<uint32_t, kPendingLimit> tags{};
  uint32_t acknowledged = 0;
  bool started = false;
  bool Put(uint32_t sequence, Command c) {
    if (!Valid(c) || sequence == 0 || sequence <= acknowledged ||
        sequence - acknowledged > kPendingLimit) return false;
    const size_t i = sequence % kPendingLimit;
    if (tags[i] == sequence) return true;
    commands[i] = c; tags[i] = sequence; return true;
  }
  bool Pop(Command &c) {
    // A short, fixed initial buffer absorbs packet batching/jitter. A hole is
    // NOT skipped; later packets retransmit it. No time-based speed bonus.
    if (!started) {
      for (uint32_t n = 1; n <= 4; ++n)
        if (tags[(acknowledged + n) % kPendingLimit] != acknowledged + n) return false;
      started = true;
    }
    const uint32_t next = acknowledged + 1;
    if (tags[next % kPendingLimit] != next) return false;
    c = commands[next % kPendingLimit]; tags[next % kPendingLimit] = 0;
    acknowledged = next; return true;
  }
};

struct Predictor {
  Point position{}, previous{};
  std::array<Command, kPendingLimit> pending{};
  uint32_t acknowledged = 0, sequence = 0;
  unsigned slot = 0;
  bool ready = false;
  void Reset(Point p, unsigned i) { *this = Predictor{}; position = previous = p; slot = i; ready = true; }
  bool Push(Command c) {
    if (!ready || !Valid(c) || sequence - acknowledged >= kPendingLimit) return false;
    pending[++sequence % kPendingLimit] = c;
    previous = position; position = Move(position, c, slot); return true;
  }
  // Rewind to the position at the exact processed ordinal, replay ONLY the
  // remaining inputs. In this game movement has no physical impulses: any
  // non-zero position discrepancy is a protocol fault, not a teleport request.
  bool Reconcile(uint32_t ack, Point authoritative) {
    if (!ready || ack < acknowledged || ack > sequence) return false;
    Point replay = authoritative;
    for (uint32_t n = ack + 1; n <= sequence; ++n) replay = Move(replay, pending[n % kPendingLimit], slot);
    if (replay != position) return false;
    acknowledged = ack;
    return true;
  }
};

struct Bullet { bool active = false; uint16_t id = 0; uint8_t owner = 0; Point p{}; uint32_t born = 0; };
// Relative-motion swept AABB: bullet and target are evaluated over the SAME
// tick interval. Glow is decorative; the projectile centre is the hit point.
inline bool SweptHit(Point b0, Point b1, Point p0, Point p1) {
  const double origin[2] = {double(b0.x - p0.x), double(b0.y - p0.y)};
  const double delta[2] = {double(b1.x - p1.x) - origin[0], double(b1.y - p1.y) - origin[1]};
  const double half[2] = {plink::kPlaneHalfWidth * kUnit, plink::kPlaneHalfHeight * kUnit};
  double enter = 0, leave = 1;
  for (unsigned axis = 0; axis < 2; ++axis) {
    if (delta[axis] == 0) { if (std::abs(origin[axis]) > half[axis]) return false; }
    else {
      double a = (-half[axis] - origin[axis]) / delta[axis];
      double b = (half[axis] - origin[axis]) / delta[axis];
      if (a > b) std::swap(a, b);
      enter = std::max(enter, a); leave = std::min(leave, b);
      if (enter > leave) return false;
    }
  }
  return true;
}
struct World {
  uint32_t tick = 0;
  Point players[2]{Spawn(0), Spawn(1)};
  std::array<Bullet, plink::kMaxBullets> bullets{};
  uint16_t nextBullet = 1;
  void Step(plink::WorldState &legacy, const Command commands[2]) {
    ++tick;
    const Point old[2]{players[0], players[1]};
    // Fire at the beginning of this tick, then move both the projectile and
    // targets over the same interval. Spawning after target movement and also
    // advancing the new bullet here would mix two different time intervals.
    if (tick % 48 == 0) { // unchanged: 0.8 seconds/shot, 180 pixels/second
      for (unsigned i = 0; i < 2; ++i) {
        if (!legacy.players[i].health) continue;
        for (auto &b : bullets) if (!b.active) {
          b = {true, nextBullet++, static_cast<uint8_t>(i + 1), players[i], tick};
          if (!nextBullet) nextBullet = 1;
          b.p.y += (i ? 1 : -1) * plink::kPlaneHalfHeight * kUnit;
          break;
        }
      }
    }
    for (unsigned i = 0; i < 2; ++i)
      if (legacy.players[i].health) players[i] = Move(players[i], commands[i], i);
    // Accumulate simultaneous hits before applying health/death. Order in the
    // bullet array cannot award an artificial win to one of two lethal hits.
    unsigned damage[2]{};
    for (auto &b : bullets) if (b.active) {
      const Point start = b.p;
      b.p.y += (b.owner == 1 ? -1 : 1) * (180 * kUnit / kHz);
      const unsigned target = b.owner == 1 ? 1 : 0;
      if (legacy.players[target].health && SweptHit(start, b.p, old[target], players[target])) {
        ++damage[target]; b.active = false;
      } else if (b.p.y < 0 || b.p.y >= plink::kWorldHeight * kUnit) b.active = false;
    }
    for (unsigned i = 0; i < 2; ++i) {
      const unsigned amount = std::min<unsigned>(damage[i], legacy.players[i].health);
      legacy.players[i].health -= static_cast<uint8_t>(amount);
      legacy.hitCount[1 - i] += static_cast<uint16_t>(amount);
      legacy.players[i].flags = legacy.players[i].health ? 1 : 0;
      legacy.players[i].x = static_cast<int16_t>(players[i].x / kUnit);
      legacy.players[i].y = static_cast<int16_t>(players[i].y / kUnit);
    }
    legacy.tick = tick;
    for (size_t i = 0; i < bullets.size(); ++i) {
      const auto &b = bullets[i];
      legacy.bullets[i] = {b.active, b.id, b.owner,
          static_cast<int16_t>(b.p.x / kUnit), static_cast<int16_t>(b.p.y / kUnit)};
    }
  }
};
} // namespace pcmotion
