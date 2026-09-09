#pragma once
#include "dnd_protocol.h"
#include "pc_motion.h"

namespace pcmotion {
constexpr uint8_t kSupported = 1U << 7U;
constexpr auto kInputType = static_cast<plink::PacketType>(14);
constexpr auto kSnapshotType = static_cast<plink::PacketType>(15);
constexpr auto kAbortType = static_cast<plink::PacketType>(16);
struct Batch {
  uint64_t round = 0;
  uint32_t first = 0;
  uint8_t count = 0;
  std::array<Command, kBatchLimit> commands{};
};
inline bool WriteBatch(plink::PacketWriter &w, const Batch &b) {
  if (!b.round || !b.first || !b.count || b.count > kBatchLimit || b.first > UINT32_MAX - b.count) return false;
  if (!w.U32(static_cast<uint32_t>(b.round)) || !w.U32(static_cast<uint32_t>(b.round >> 32)) ||
      !w.U32(b.first) || !w.U8(b.count)) return false;
  for (unsigned i = 0; i < b.count; ++i) {
    const auto &c = b.commands[i];
    if (!Valid(c) || !w.U8(c.x) || !w.U16(static_cast<uint16_t>(c.y | (c.keys << 9) | (c.mouse ? 0x2000 : 0)))) return false;
  }
  return true;
}
inline bool ReadBatch(plink::PayloadReader &r, Batch &b) {
  uint32_t lo = 0, hi = 0;
  if (!r.U32(lo) || !r.U32(hi) || !r.U32(b.first) || !r.U8(b.count) ||
      !b.first || !b.count || b.count > kBatchLimit || b.first > UINT32_MAX - b.count) return false;
  b.round = uint64_t(lo) | (uint64_t(hi) << 32);
  if (!b.round) return false;
  for (unsigned i = 0; i < b.count; ++i) {
    uint16_t bits = 0;
    auto &c = b.commands[i];
    if (!r.U8(c.x) || !r.U16(bits) || bits & 0xC000) return false;
    c.y = bits & 511; c.keys = (bits >> 9) & 15; c.mouse = (bits & 0x2000) != 0;
    if (!Valid(c)) return false;
  }
  return r.Done();
}
struct Snapshot {
  uint32_t tick = 0;
  uint8_t fractions[4]{};
  bool syncFailed = false;
  pcpair::PresenceSnapshot presence;
  Point Position(unsigned i) const {
    return {presence.game.players[i].x * kUnit + fractions[2 * i],
            presence.game.players[i].y * kUnit + fractions[2 * i + 1]};
  }
};
inline bool WriteSnapshot(plink::PacketWriter &w, const Snapshot &s) {
  return w.U32(s.tick) && w.U8(s.fractions[0]) && w.U8(s.fractions[1]) &&
      w.U8(s.fractions[2]) && w.U8(s.fractions[3]) && w.U8(s.syncFailed ? 1 : 0) &&
      pcpair::WritePresenceSnapshot(w, s.presence);
}
inline bool ReadSnapshot(plink::PayloadReader &r, Snapshot &s) {
  uint8_t failure = 0;
  if (!r.U32(s.tick) || !r.U8(s.fractions[0]) || !r.U8(s.fractions[1]) ||
      !r.U8(s.fractions[2]) || !r.U8(s.fractions[3]) || !r.U8(failure) || failure > 1 ||
      !pcpair::ReadPresenceSnapshot(r, s.presence)) return false;
  s.syncFailed = failure != 0;
  for (unsigned i = 0; i < 2; ++i)
    if (s.Position(i) != Bound(s.Position(i), i) || s.presence.game.players[i].health > 3) return false;
  return true;
}
static_assert(plink::kHeaderSize + 13 + 3 * kBatchLimit + plink::kCrcSize <= plink::kMaxPacketSize);
static_assert(plink::kHeaderSize + 9 + 29 + 38 + 7 * plink::kMaxBullets + plink::kCrcSize <= plink::kMaxPacketSize);
} // namespace pcmotion
