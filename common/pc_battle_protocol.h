#pragma once
#include "pc_motion_protocol.h"
#include "pc_battle.h"

namespace pcbattle {
constexpr auto kInputType = static_cast<plink::PacketType>(17);
constexpr auto kStateType = static_cast<plink::PacketType>(18);
constexpr auto kRelayType = static_cast<plink::PacketType>(19);
constexpr auto kImpactType = static_cast<plink::PacketType>(20);
// Existing authenticated batch framing; `first` now IS the world tick.
using Batch = pcmotion::Batch;
using pcmotion::WriteBatch;
using pcmotion::ReadBatch;
inline bool WriteCommand(plink::PacketWriter &w, Command c) {
  return pcmotion::Valid(c) && w.U8(c.x) && w.U16(static_cast<uint16_t>(c.y | (c.keys << 9) | (c.mouse ? 0x2000 : 0)));
}
inline bool ReadCommand(plink::PayloadReader &r, Command &c) {
  uint16_t value = 0;
  if (!r.U8(c.x) || !r.U16(value) || (value & 0xC000)) return false;
  c.y = value & 511; c.keys = (value >> 9) & 15; c.mouse = (value & 0x2000) != 0;
  return pcmotion::Valid(c);
}
inline bool WritePoint(plink::PacketWriter &w, Point p, uint8_t flag = 0) {
  return p.x >= 0 && p.x < 240 * kUnit && p.y >= 0 && p.y < 320 * kUnit && flag <= 1 &&
      w.U16(static_cast<uint16_t>(p.x)) && w.U16(static_cast<uint16_t>(p.y)) &&
      w.U8(static_cast<uint8_t>((p.y >> 16) | (flag << 1)));
}
inline bool ReadPoint(plink::PayloadReader &r, Point &p, uint8_t &flag) {
  uint16_t x = 0, y = 0; uint8_t high = 0;
  if (!r.U16(x) || !r.U16(y) || !r.U8(high) || high > 3) return false;
  p = {x, y | ((high & 1) << 16)}; flag = high >> 1;
  return p.x < 240 * kUnit && p.y < 320 * kUnit;
}
struct State { World world; pcpair::PresenceSnapshot presence; bool failed = false; };
inline bool WriteState(plink::PacketWriter &w, const State &s) {
  if (s.world.tick > kMaxTicks || !w.U32(s.world.tick)) return false;
  for (auto p : s.world.players) if (!w.U8(static_cast<uint8_t>(p.x)) || !w.U8(static_cast<uint8_t>(p.y))) return false;
  if (!w.U8(s.failed ? 1 : 0) || !WriteCommand(w, s.world.last[0]) || !WriteCommand(w, s.world.last[1])) return false;
  uint8_t count = 0;
  for (auto &b : s.world.bullets) if (b.active) ++count;
  if (!w.U8(count)) return false;
  for (auto &b : s.world.bullets) if (b.active)
    if (!b.id || b.owner < 1 || b.owner > 2 || !w.U16(b.id) || !WritePoint(w, b.p, b.owner - 1)) return false;
  auto presence = s.presence;
  plink::WorldState legacy; s.world.Export(legacy);
  for (unsigned i = 0; i < 2; ++i) {
    presence.game.players[i] = legacy.players[i]; presence.game.hitCount[i] = legacy.hitCount[i];
  }
  presence.game.lastProcessedInput = s.world.tick;
  presence.game.bulletCount = 0; // precise bullet data above, not a second lossy copy
  return pcpair::WritePresenceSnapshot(w, presence);
}
inline bool ReadState(plink::PayloadReader &r, State &s) {
  s = State{};
  uint8_t fractions[4]{}, failure = 0, count = 0;
  if (!r.U32(s.world.tick) || s.world.tick > kMaxTicks) return false;
  for (auto &f : fractions) if (!r.U8(f)) return false;
  if (!r.U8(failure) || failure > 1 || !ReadCommand(r, s.world.last[0]) || !ReadCommand(r, s.world.last[1]) ||
      !r.U8(count) || count > plink::kMaxBullets) return false;
  s.failed = failure != 0;
  for (unsigned i = 0; i < count; ++i) {
    auto &b = s.world.bullets[i]; uint8_t owner = 0;
    if (!r.U16(b.id) || !b.id || !ReadPoint(r, b.p, owner)) return false;
    b.owner = owner + 1; b.active = true;
    if (b.id % 2 != (b.owner == 1 ? 1 : 0) || ((b.id + 1U) / 2U) * 48U > s.world.tick) return false;
    for (unsigned j = 0; j < i; ++j) if (s.world.bullets[j].id == b.id) return false;
  }
  if (!pcpair::ReadPresenceSnapshot(r, s.presence) || s.presence.game.bulletCount ||
      s.presence.game.lastProcessedInput != s.world.tick) return false;
  for (unsigned i = 0; i < 2; ++i) {
    const auto &p = s.presence.game.players[i];
    s.world.players[i] = {p.x * kUnit + fractions[2 * i], p.y * kUnit + fractions[2 * i + 1]};
    s.world.health[i] = p.health;
    if (p.health > 3 || s.world.players[i] != pcmotion::Bound(s.world.players[i], i) ||
        s.presence.game.hitCount[1 - i] != 3 - p.health) return false;
  }
  s.presence.game.bulletCount = count;
  for (unsigned i = 0; i < count; ++i) {
    const auto &b = s.world.bullets[i];
    s.presence.game.bullets[i] = {b.id, b.owner, static_cast<int16_t>(b.p.x / kUnit), static_cast<int16_t>(b.p.y / kUnit)};
  }
  return true;
}
struct Events { uint64_t round = 0; std::array<Impact, kMaxImpacts> hits{}; uint8_t count = 0; };
// Snapshots keep below the legacy datagram size limit; confirmed contact data
// travels separately. Join only events at/before this exact authority tick.
inline void AttachEvents(World &world, const Events &events) {
  world.impactCount = 0;
  for (unsigned i = 0; i < events.count; ++i)
    if (events.hits[i].tick <= world.tick) world.impacts[world.impactCount++] = events.hits[i];
}
inline bool Extends(const Events &next, const Events &old) {
  if (next.count < old.count || (old.count && next.round != old.round)) return false;
  for (unsigned i = 0; i < old.count; ++i) {
    const auto &a = next.hits[i], &b = old.hits[i];
    if (a.tick != b.tick || a.bullet != b.bullet || a.fraction != b.fraction ||
        a.target != b.target || a.healthAfter != b.healthAfter || a.contact != b.contact || a.plane != b.plane) return false;
  }
  return true;
}
inline bool WriteEvents(plink::PacketWriter &w, const Events &events) {
  if (!events.round || events.count > kMaxImpacts || !w.U32(static_cast<uint32_t>(events.round)) ||
      !w.U32(static_cast<uint32_t>(events.round >> 32)) || !w.U8(events.count)) return false;
  for (unsigned i = 0; i < events.count; ++i) {
    const auto &e = events.hits[i];
    if (!w.U32(e.tick) || !w.U16(e.bullet) || !w.U16(e.fraction) || !w.U8(e.target) ||
        !w.U8(e.healthAfter) || !WritePoint(w, e.contact) || !WritePoint(w, e.plane)) return false;
  }
  return true;
}
inline bool ReadEvents(plink::PayloadReader &r, Events &events) {
  uint32_t lo = 0, hi = 0;
  if (!r.U32(lo) || !r.U32(hi) || !r.U8(events.count) || events.count > kMaxImpacts) return false;
  events.round = uint64_t(lo) | (uint64_t(hi) << 32);
  if (!events.round) return false;
  uint8_t health[2]{3, 3};
  for (unsigned i = 0; i < events.count; ++i) {
    auto &e = events.hits[i]; uint8_t flag = 0;
    if (!r.U32(e.tick) || !r.U16(e.bullet) || !r.U16(e.fraction) || !r.U8(e.target) ||
        !r.U8(e.healthAfter) || !ReadPoint(r, e.contact, flag) || flag || !ReadPoint(r, e.plane, flag) || flag ||
        !e.tick || e.tick > kMaxTicks || !e.bullet || e.target > 1 || e.healthAfter > 2) return false;
    if (e.bullet % 2 != e.target || ((e.bullet + 1U) / 2U) * 48U > e.tick ||
        !health[e.target] || e.healthAfter != --health[e.target] ||
        e.plane != pcmotion::Bound(e.plane, e.target)) return false;
    for (unsigned j = 0; j < i; ++j) if (e.bullet == events.hits[j].bullet) return false;
    if (i && (e.tick < events.hits[i - 1].tick || (e.tick == events.hits[i - 1].tick &&
        (e.fraction < events.hits[i - 1].fraction || (e.fraction == events.hits[i - 1].fraction && e.bullet < events.hits[i - 1].bullet))))) return false;
  }
  return r.Done();
}
static_assert(plink::kHeaderSize + 16 + 7 * plink::kMaxBullets + 29 + 38 + plink::kCrcSize == 191);
static_assert(plink::kHeaderSize + 9 + 20 * kMaxImpacts + plink::kCrcSize <= plink::kMaxPacketSize);
} // namespace pcbattle
