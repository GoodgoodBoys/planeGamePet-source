#pragma once
#include "pairing_protocol.h"

namespace pcpair {
// PC-only, capability-negotiated extension. Existing game/ESP32 packets remain
// byte-for-byte unchanged. Snapshot + presence + outcome travel atomically.
constexpr uint8_t kDndSupported = 1U << 6U;
constexpr plink::PacketType kDndPreferenceType = static_cast<plink::PacketType>(12);
constexpr plink::PacketType kPresenceSnapshotType = static_cast<plink::PacketType>(13);
enum class InviteBlock : uint8_t { None = 0, DoNotDisturb = 1, Synchronizing = 2 };
struct DndPreference { uint32_t revision = 0; bool enabled = false; };
struct PresenceSnapshot {
  uint32_t acknowledgedRevision = 0;
  uint8_t knownMask = 0;
  uint8_t enabledMask = 0;
  uint8_t capableMask = 0;
  bool interruptedByDnd = false;
  uint8_t interruptedInviter = 0;
  uint32_t blockedOperation = 0;
  InviteBlock blockedReason = InviteBlock::None;
  uint64_t roundId = 0;
  uint64_t inviteId = 0;
  plink::SnapshotPayload game;
};
inline bool WriteDndPreference(plink::PacketWriter &writer, const DndPreference &value) {
  return value.revision != 0 && writer.U32(value.revision) && writer.U8(value.enabled ? 1 : 0);
}
inline bool ReadDndPreference(plink::PayloadReader &reader, DndPreference &value) {
  uint8_t enabled = 0;
  if (!reader.U32(value.revision) || !reader.U8(enabled) || !reader.Done() ||
      value.revision == 0 || enabled > 1) return false;
  value.enabled = enabled != 0;
  return true;
}
inline bool WritePresenceSnapshot(plink::PacketWriter &writer, const PresenceSnapshot &value) {
  return writer.U32(value.acknowledgedRevision) && writer.U8(value.knownMask) &&
      writer.U8(value.enabledMask) && writer.U8(value.capableMask) &&
      writer.U8(value.interruptedByDnd ? value.interruptedInviter : 0) && writer.U32(value.blockedOperation) &&
      writer.U8(static_cast<uint8_t>(value.blockedReason)) &&
      writer.U32(static_cast<uint32_t>(value.roundId)) && writer.U32(static_cast<uint32_t>(value.roundId >> 32U)) &&
      writer.U32(static_cast<uint32_t>(value.inviteId)) && writer.U32(static_cast<uint32_t>(value.inviteId >> 32U)) &&
      plink::WriteSnapshot(writer, value.game);
}
inline bool ReadPresenceSnapshot(plink::PayloadReader &reader, PresenceSnapshot &value) {
  uint8_t interrupted = 0, reason = 0;
  uint32_t rl = 0, rh = 0, il = 0, ih = 0;
  if (!reader.U32(value.acknowledgedRevision) || !reader.U8(value.knownMask) ||
      !reader.U8(value.enabledMask) || !reader.U8(value.capableMask) ||
      !reader.U8(interrupted) || !reader.U32(value.blockedOperation) || !reader.U8(reason) ||
      !reader.U32(rl) || !reader.U32(rh) || !reader.U32(il) || !reader.U32(ih) ||
      value.knownMask > 3 || value.enabledMask > 3 || value.capableMask > 3 || interrupted > 2 ||
      (value.enabledMask & ~value.knownMask) || (value.knownMask & ~value.capableMask) ||
      reason > static_cast<uint8_t>(InviteBlock::Synchronizing) || !plink::ReadSnapshot(reader, value.game)) return false;
  value.interruptedByDnd = interrupted != 0;
  value.interruptedInviter = interrupted;
  value.blockedReason = static_cast<InviteBlock>(reason);
  value.roundId = static_cast<uint64_t>(rl) | (static_cast<uint64_t>(rh) << 32U);
  value.inviteId = static_cast<uint64_t>(il) | (static_cast<uint64_t>(ih) << 32U);
  return !value.interruptedByDnd || (value.game.phase == plink::GamePhase::Menu &&
      value.game.endReason == plink::MatchEndReason::InviteRejected && value.inviteId != 0);
}
static_assert(plink::kHeaderSize + 29 + 38 + 7 * plink::kMaxBullets + plink::kCrcSize <= plink::kMaxPacketSize);
} // namespace pcpair
