#pragma once
#include "pc_motion.h"

// Version 3 PC-only battle model. Legacy/ESP32 and motion-v2 are untouched.
namespace pcbattle {
using pcmotion::Point;
using pcmotion::Command;
using pcmotion::kUnit;
constexpr unsigned kHz = 60;
constexpr unsigned kHistory = 128;
constexpr unsigned kWindow = 60;       // one second, bounded prediction/confirmation
constexpr unsigned kFutureAllowance = 8;
constexpr unsigned kCatchupLimit = 8;  // whole historical ticks, not faster movement
constexpr unsigned kMaxTicks = 3 * 60 * kHz;
constexpr unsigned kMaxImpacts = 6;    // two players, three actual damage units each

inline bool Same(Command a, Command b) {
  return a.keys == b.keys && a.mouse == b.mouse && a.x == b.x && a.y == b.y;
}
struct Inputs {
  std::array<Command, kHistory> commands{};
  std::array<uint32_t, kHistory> tags{};
  // Stale retransmissions are harmless. The first accepted input is immutable.
  bool Put(uint32_t tick, Command command, uint32_t confirmed, uint32_t ceiling) {
    if (!tick || !pcmotion::Valid(command) || tick > kMaxTicks || tick > ceiling) return false;
    if (tick <= confirmed) return true;
    if (tick - confirmed >= kHistory) return false;
    const auto index = tick % kHistory;
    if (tags[index] == tick) return Same(commands[index], command);
    tags[index] = tick; commands[index] = command; return true;
  }
  bool Get(uint32_t tick, Command &command) const {
    if (!tick || tags[tick % kHistory] != tick) return false;
    command = commands[tick % kHistory]; return true;
  }
};

struct Fraction { int64_t n = 0, d = 1; };
inline bool Less(Fraction a, Fraction b) { return a.n * b.d < b.n * a.d; }
inline Fraction Ratio(int64_t n, int64_t d) { return d < 0 ? Fraction{-n, -d} : Fraction{n, d}; }
inline bool RectangleContact(Point b0, Point b1, Point p0, Point p1,
    const int64_t low[2], const int64_t high[2], Fraction &enter) {
  enter = {}; Fraction leave{1, 1};
  const int64_t origin[2]{b0.x - p0.x, b0.y - p0.y};
  const int64_t delta[2]{(b1.x - p1.x) - origin[0], (b1.y - p1.y) - origin[1]};
  for (unsigned axis = 0; axis < 2; ++axis) {
    if (!delta[axis]) { if (origin[axis] < low[axis] || origin[axis] > high[axis]) return false; }
    else {
      auto a = Ratio(low[axis] - origin[axis], delta[axis]);
      auto b = Ratio(high[axis] - origin[axis], delta[axis]);
      if (Less(b, a)) std::swap(a, b);
      if (Less(enter, a)) enter = a;
      if (Less(b, leave)) leave = b;
      if (Less(leave, enter)) return false;
    }
  }
  return true;
}
// D2 at the existing 36 px game size: opaque horizontal hull spans, y=-17..6.
// Exhaust is decorative. This calibrates geometry, NEVER compensates latency.
constexpr int kHull[24][2] = {{-1,0},{-1,0},{-1,0},{-1,0},{-2,1},{-2,1},
    {-2,1},{-2,2},{-3,2},{-3,2},{-3,2},{-3,2},{-4,3},{-6,5},{-7,6},
    {-8,7},{-9,8},{-9,8},{-9,8},{-3,2},{-4,3},{-4,4},{-5,4},{-5,4}};
constexpr int kProjectileTip = 5 * kUnit; // visible colored nose, not the decorative tail
inline bool Contact(Point b0, Point b1, Point p0, Point p1, Fraction &enter, unsigned target = 0) {
  if (target) { b0 = {-b0.x, -b0.y}; b1 = {-b1.x, -b1.y}; p0 = {-p0.x, -p0.y}; p1 = {-p1.x, -p1.y}; }
  const int64_t broadLow[2]{-10 * kUnit, -18 * kUnit}, broadHigh[2]{9 * kUnit, 7 * kUnit};
  if (!RectangleContact(b0, b1, p0, p1, broadLow, broadHigh, enter)) return false;
  bool found = false;
  for (unsigned row = 0; row < 24; ++row) {
    const int64_t low[2]{kHull[row][0] * kUnit - kUnit / 2, (int(row) - 17) * kUnit - kUnit / 2};
    const int64_t high[2]{kHull[row][1] * kUnit + kUnit / 2, (int(row) - 17) * kUnit + kUnit / 2};
    Fraction time;
    if (RectangleContact(b0, b1, p0, p1, low, high, time) && (!found || Less(time, enter))) { enter = time; found = true; }
  }
  return found;
}
inline Point Along(Point a, Point b, Fraction t) {
  return {a.x + static_cast<int32_t>((int64_t(b.x) - a.x) * t.n / t.d),
          a.y + static_cast<int32_t>((int64_t(b.y) - a.y) * t.n / t.d)};
}
struct Bullet { bool active = false; uint16_t id = 0; uint8_t owner = 0; Point p{}; };
struct Impact {
  uint32_t tick = 0;
  uint16_t bullet = 0, fraction = 0;
  uint8_t target = 0, healthAfter = 0;
  Point contact{}, plane{};
};
struct World {
  uint32_t tick = 0;
  Point players[2]{pcmotion::Spawn(0), pcmotion::Spawn(1)};
  uint8_t health[2]{3, 3};
  Command last[2]{};
  std::array<Bullet, plink::kMaxBullets> bullets{};
  std::array<Impact, kMaxImpacts> impacts{};
  unsigned impactCount = 0;
  bool Dead() const { return !health[0] || !health[1]; }
  void Step(const Command command[2]) {
    ++tick;
    const Point old[2]{players[0], players[1]};
    if (tick % 48 == 0) {
      for (unsigned i = 0; i < 2; ++i) if (health[i]) {
        for (auto &b : bullets) if (!b.active) {
          // Birth tick/owner determine identity, independently of array slots.
          b = {true, static_cast<uint16_t>((tick / 48) * 2 - 1 + i),
               static_cast<uint8_t>(i + 1), players[i]};
          b.p.y += (i ? 1 : -1) * plink::kPlaneHalfHeight * kUnit;
          break;
        }
      }
    }
    for (unsigned i = 0; i < 2; ++i) {
      // A speculative death must not freeze local controls and then teleport
      // on correction. Authority stops the round on the first confirmed death.
      players[i] = pcmotion::Move(players[i], command[i], i); last[i] = command[i];
    }
    std::array<Impact, plink::kMaxBullets> contacts{};
    unsigned count = 0;
    for (auto &b : bullets) if (b.active) {
      const auto start = b.p;
      b.p.y += (b.owner == 1 ? -1 : 1) * 3 * kUnit;
      const unsigned target = b.owner == 1 ? 1 : 0;
      Point tipStart = start, tipEnd = b.p;
      tipStart.y += (b.owner == 1 ? -1 : 1) * kProjectileTip;
      tipEnd.y += (b.owner == 1 ? -1 : 1) * kProjectileTip;
      Fraction hit;
      if (health[target] && Contact(tipStart, tipEnd, old[target], players[target], hit, target)) {
        contacts[count++] = {tick, b.id, static_cast<uint16_t>(hit.n * 65535 / hit.d),
            static_cast<uint8_t>(target), 0, Along(tipStart, tipEnd, hit), Along(old[target], players[target], hit)};
        b.active = false;
      } else if (b.p.y < 0 || b.p.y >= plink::kWorldHeight * kUnit) b.active = false;
    }
    // Stable event order. All collisions were collected before any death.
    std::sort(contacts.begin(), contacts.begin() + count, [](const Impact &a, const Impact &b) {
      return a.fraction != b.fraction ? a.fraction < b.fraction : a.bullet < b.bullet;
    });
    for (unsigned i = 0; i < count; ++i) {
      auto event = contacts[i];
      if (!health[event.target]) continue; // overkill never becomes extra damage
      event.healthAfter = --health[event.target];
      if (impactCount < impacts.size()) impacts[impactCount++] = event;
    }
  }
  void Export(plink::WorldState &state) const {
    state.tick = tick;
    for (unsigned i = 0; i < 2; ++i) {
      state.players[i] = {static_cast<int16_t>(players[i].x / kUnit),
          static_cast<int16_t>(players[i].y / kUnit), health[i], static_cast<uint8_t>(health[i] ? 1 : 0)};
      state.hitCount[1 - i] = static_cast<uint16_t>(3 - health[i]);
    }
    for (unsigned i = 0; i < bullets.size(); ++i) {
      const auto &b = bullets[i];
      state.bullets[i] = {b.active, b.id, b.owner, static_cast<int16_t>(b.p.x / kUnit),
          static_cast<int16_t>(b.p.y / kUnit)};
    }
  }
};

struct Authority {
  World world;
  Inputs inputs[2];
  uint32_t budget = 0;
  bool failed = false;
  bool Put(unsigned slot, uint32_t tick, Command command) {
    return slot < 2 && inputs[slot].Put(tick, command, world.tick,
        std::min<uint32_t>(kMaxTicks, budget + kFutureAllowance));
  }
  void Advance(uint32_t elapsedTick, uint32_t finishTick = kMaxTicks) {
    budget = std::min<uint32_t>(elapsedTick, kMaxTicks + kWindow + 1);
    if (failed || world.Dead()) return;
    if (budget > world.tick + kWindow) { failed = true; return; }
    const auto end = std::min({budget, finishTick, world.tick + kCatchupLimit});
    while (world.tick < end) {
      Command command[2];
      if (!inputs[0].Get(world.tick + 1, command[0]) || !inputs[1].Get(world.tick + 1, command[1])) break;
      world.Step(command);
      if (world.Dead()) break;
    }
  }
};

struct Predictor {
  World confirmed, current, previous;
  Inputs inputs[2];
  uint32_t tick = 0, replays = 0;
  unsigned slot = 0;
  bool ready = false, failed = false;
  void Reset(const World &base, unsigned own) {
    *this = Predictor{}; confirmed = current = previous = base;
    slot = own; tick = base.tick; ready = own < 2;
  }
  bool Push(Command command) {
    if (!ready || failed || tick >= kMaxTicks || tick - confirmed.tick >= kWindow) return false;
    if (!inputs[slot].Put(tick + 1, command, confirmed.tick, tick + 1)) return false;
    previous = current;
    Command commands[2]{current.last[0], current.last[1]};
    commands[slot] = command;
    inputs[1 - slot].Get(tick + 1, commands[1 - slot]);
    current.Step(commands); ++tick;
    return true;
  }
  bool Replay() {
    if (!ready || confirmed.tick > tick || tick - confirmed.tick >= kHistory) return false;
    World world = confirmed;
    const auto oldPrevious = previous;
    previous = world.tick == tick && oldPrevious.tick + 1 == tick ? oldPrevious : world;
    while (world.tick < tick) {
      Command commands[2]{world.last[0], world.last[1]};
      if (!inputs[slot].Get(world.tick + 1, commands[slot])) return false;
      inputs[1 - slot].Get(world.tick + 1, commands[1 - slot]);
      previous = world; world.Step(commands);
    }
    // Our true input trajectory cannot be altered by a remote prediction.
    if (world.players[slot] != current.players[slot]) return false;
    current = world; ++replays; return true;
  }
  bool Confirm(const World &base) {
    if (!ready || base.tick < confirmed.tick || base.tick > tick) return false;
    confirmed = base;
    return Replay();
  }
};
} // namespace pcbattle
