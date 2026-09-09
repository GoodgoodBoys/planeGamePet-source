#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>

#include "app_version.h"
#include "../shared/plane_protocol.h"

namespace pcpair {

constexpr uint8_t kProtocolVersion = 3;
// V4 keeps the 48-byte authenticated envelope. The high nibble of status
// carries the peer's release epoch; V3 remains byte-for-byte readable.
constexpr uint8_t kReleaseProtocolVersion = 4;
constexpr size_t kMessageSize = 48;
constexpr size_t kAuthTagSize = 8;
constexpr size_t kMaxDatagramSize = plink::kMaxPacketSize + kAuthTagSize;
constexpr uint32_t kWaitingTimeoutMs = 3000;
constexpr plink::PacketType kRoundMetaPacketType =
    static_cast<plink::PacketType>(9);
constexpr plink::PacketType kScopedActionPacketType =
    static_cast<plink::PacketType>(10);
constexpr plink::PacketType kActionAckPacketType =
    static_cast<plink::PacketType>(11);

struct AuthKey {
  uint64_t low = 0;
  uint64_t high = 0;

  bool Enabled() const { return low != 0 || high != 0; }
};

inline uint64_t RotateLeft(uint64_t value, unsigned count) {
  return (value << count) | (value >> (64U - count));
}

inline uint64_t ReadU64(const uint8_t *bytes) {
  uint64_t value = 0;
  for (unsigned index = 0; index < 8; ++index)
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8U);
  return value;
}

inline void WriteU64(uint8_t *bytes, uint64_t value) {
  for (unsigned index = 0; index < 8; ++index)
    bytes[index] = static_cast<uint8_t>(value >> (index * 8U));
}

inline void SipRound(uint64_t &v0, uint64_t &v1, uint64_t &v2,
                     uint64_t &v3) {
  v0 += v1;
  v1 = RotateLeft(v1, 13);
  v1 ^= v0;
  v0 = RotateLeft(v0, 32);
  v2 += v3;
  v3 = RotateLeft(v3, 16);
  v3 ^= v2;
  v0 += v3;
  v3 = RotateLeft(v3, 21);
  v3 ^= v0;
  v2 += v1;
  v1 = RotateLeft(v1, 17);
  v1 ^= v2;
  v2 = RotateLeft(v2, 32);
}

inline uint64_t SipHash24(const uint8_t *bytes, size_t length,
                          const AuthKey &key) {
  uint64_t v0 = 0x736f6d6570736575ULL ^ key.low;
  uint64_t v1 = 0x646f72616e646f6dULL ^ key.high;
  uint64_t v2 = 0x6c7967656e657261ULL ^ key.low;
  uint64_t v3 = 0x7465646279746573ULL ^ key.high;
  size_t position = 0;
  while (position + 8 <= length) {
    const uint64_t block = ReadU64(bytes + position);
    v3 ^= block;
    SipRound(v0, v1, v2, v3);
    SipRound(v0, v1, v2, v3);
    v0 ^= block;
    position += 8;
  }
  uint64_t tail = static_cast<uint64_t>(length) << 56U;
  for (size_t index = 0; position + index < length; ++index)
    tail |= static_cast<uint64_t>(bytes[position + index]) << (index * 8U);
  v3 ^= tail;
  SipRound(v0, v1, v2, v3);
  SipRound(v0, v1, v2, v3);
  v0 ^= tail;
  v2 ^= 0xFFU;
  for (int round = 0; round < 4; ++round) SipRound(v0, v1, v2, v3);
  return v0 ^ v1 ^ v2 ^ v3;
}

inline int HexDigit(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

inline bool ParseAuthKey(const std::string &text, AuthKey &key) {
  key = AuthKey{};
  if (text.empty()) return true;
  if (text.size() != 32) return false;
  uint8_t decoded[16]{};
  for (size_t index = 0; index < 16; ++index) {
    const int high = HexDigit(text[index * 2]);
    const int low = HexDigit(text[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    decoded[index] = static_cast<uint8_t>((high << 4) | low);
  }
  key.low = ReadU64(decoded);
  key.high = ReadU64(decoded + 8);
  return key.Enabled();
}

inline bool AppendPacketAuth(uint8_t *bytes, size_t &length, size_t capacity,
                             const AuthKey &key) {
  if (!key.Enabled()) return true;
  if (bytes == nullptr || length > plink::kMaxPacketSize ||
      capacity - length < kAuthTagSize) {
    return false;
  }
  WriteU64(bytes + length, SipHash24(bytes, length, key));
  length += kAuthTagSize;
  return true;
}

inline bool VerifyAndStripPacketAuth(const uint8_t *bytes, size_t &length,
                                     const AuthKey &key) {
  if (!key.Enabled()) return length <= plink::kMaxPacketSize;
  if (bytes == nullptr || length < kAuthTagSize ||
      length > kMaxDatagramSize) {
    return false;
  }
  const size_t plainLength = length - kAuthTagSize;
  const uint64_t expected = SipHash24(bytes, plainLength, key);
  const uint64_t actual = ReadU64(bytes + plainLength);
  if (expected != actual) return false;
  length = plainLength;
  return true;
}

enum class MessageType : uint8_t {
  Start = 1,
  Cancel = 2,
  Resume = 3,
  Status = 4,
  Goodbye = 5,
  Unbind = 6,
};

enum class Status : uint8_t {
  None = 0,
  Waiting = 1,
  Matched = 2,
  Cancelled = 3,
  Invalid = 4,
  BindingMissing = 5,
  AlreadyBound = 6,
  Unbound = 7,
  StorageError = 8,
};

struct AppVersion {
  uint8_t major = 0;
  uint8_t minor = 0;
  uint8_t patch = 0;
  uint8_t releaseEpoch = 0;

  bool Known() const { return major != 0 || minor != 0 || patch != 0; }
};

inline uint8_t WireVersionFor(const AppVersion &version) {
  return version.releaseEpoch ? kReleaseProtocolVersion : kProtocolVersion;
}

inline uint8_t GameProtocolFor(const AppVersion &version) {
  // Explicit public-1.x -> existing, fully verified battle protocol mapping.
  // An unknown future release major must not silently fall back to old motion.
  if (version.releaseEpoch == 1) return version.major == 1 ? 3 : 0;
  return version.major <= 3 ? version.major : 0;
}

inline bool SameMajorFamily(const AppVersion &a, const AppVersion &b) {
  return !a.Known() || !b.Known() ||
      (a.releaseEpoch == b.releaseEpoch && a.major == b.major);
}

enum CompatibilityFlag : uint8_t {
  PeerVersionKnown = 1U << 0U,
  MajorMismatch = 1U << 1U,
  LocalUpdateRequired = 1U << 2U,
  PeerUpdateRequired = 1U << 3U,
  ServerMinimumRequired = 1U << 4U,
  ScopedActionsSupported = 1U << 5U,
};

inline AppVersion CurrentAppVersion() {
  return {plane_pet_version::kMajor, plane_pet_version::kMinor,
          plane_pet_version::kPatch, plane_pet_version::kReleaseEpoch};
}

struct Message {
  uint8_t wireVersion = 0;  // 0 selects by sender version; responses select recipient format.
  MessageType type = MessageType::Status;
  uint32_t deviceId = 0;
  uint32_t requestId = 0;
  uint32_t pairingCode = 0;
  uint32_t bindingId = 0;
  uint32_t tokenLow = 0;
  uint32_t tokenHigh = 0;
  uint32_t peerDeviceId = 0;
  Status status = Status::None;
  uint8_t assignedSlot = 0;
  AppVersion appVersion{};
  uint8_t compatibilityFlags = 0;
};

struct RoundMeta {
  uint64_t roundId = 0;
  uint64_t inviteId = 0;
  plink::GamePhase phase = plink::GamePhase::Menu;
};

// Additive PC-only messages. Legacy clients keep using the original Action;
// new clients send this only after the server advertises the capability.
struct ScopedAction {
  uint32_t operationId = 0;
  plink::PlayerAction action = plink::PlayerAction::Invite;
  RoundMeta context;
};

inline bool WriteScopedAction(plink::PacketWriter &writer,
                              const ScopedAction &value) {
  return writer.U32(value.operationId) &&
         writer.U8(static_cast<uint8_t>(value.action)) &&
         writer.U32(static_cast<uint32_t>(value.context.roundId)) &&
         writer.U32(static_cast<uint32_t>(value.context.roundId >> 32U)) &&
         writer.U32(static_cast<uint32_t>(value.context.inviteId)) &&
         writer.U32(static_cast<uint32_t>(value.context.inviteId >> 32U)) &&
         writer.U8(static_cast<uint8_t>(value.context.phase));
}

inline bool ReadScopedAction(plink::PayloadReader &reader, ScopedAction &value) {
  uint8_t action = 0, phase = 0;
  uint32_t rl = 0, rh = 0, il = 0, ih = 0;
  if (!reader.U32(value.operationId) || !reader.U8(action) ||
      !reader.U32(rl) || !reader.U32(rh) || !reader.U32(il) ||
      !reader.U32(ih) || !reader.U8(phase) || !reader.Done() ||
      value.operationId == 0 || action < 1 || action > 3 ||
      phase > static_cast<uint8_t>(plink::GamePhase::Finished)) return false;
  value.action = static_cast<plink::PlayerAction>(action);
  value.context = {static_cast<uint64_t>(rl) | (static_cast<uint64_t>(rh) << 32U),
                   static_cast<uint64_t>(il) | (static_cast<uint64_t>(ih) << 32U),
                   static_cast<plink::GamePhase>(phase)};
  return true;
}

inline bool WriteRoundMeta(plink::PacketWriter &writer,
                           const RoundMeta &value) {
  return writer.U32(static_cast<uint32_t>(value.roundId & 0xFFFFFFFFULL)) &&
         writer.U32(static_cast<uint32_t>(value.roundId >> 32U)) &&
         writer.U32(static_cast<uint32_t>(value.inviteId & 0xFFFFFFFFULL)) &&
         writer.U32(static_cast<uint32_t>(value.inviteId >> 32U)) &&
         writer.U8(static_cast<uint8_t>(value.phase));
}

inline bool ReadRoundMeta(plink::PayloadReader &reader, RoundMeta &value) {
  uint32_t low = 0;
  uint32_t high = 0;
  uint32_t inviteLow = 0;
  uint32_t inviteHigh = 0;
  uint8_t phase = 0;
  if (!reader.U32(low) || !reader.U32(high) ||
      !reader.U32(inviteLow) || !reader.U32(inviteHigh) ||
      !reader.U8(phase) ||
      !reader.Done() ||
      phase > static_cast<uint8_t>(plink::GamePhase::Finished)) {
    return false;
  }
  value.roundId = static_cast<uint64_t>(low) |
                  (static_cast<uint64_t>(high) << 32U);
  value.inviteId = static_cast<uint64_t>(inviteLow) |
                   (static_cast<uint64_t>(inviteHigh) << 32U);
  value.phase = static_cast<plink::GamePhase>(phase);
  return true;
}

inline bool Serialize(const Message &message, uint8_t *bytes, size_t capacity,
                      const AuthKey &key = AuthKey{}) {
  if (bytes == nullptr || capacity < kMessageSize) return false;
  memset(bytes, 0, kMessageSize);
  bytes[0] = 'P';
  bytes[1] = 'B';
  const uint8_t wire = message.wireVersion ? message.wireVersion : WireVersionFor(message.appVersion);
  if ((wire != kProtocolVersion && wire != kReleaseProtocolVersion) || message.appVersion.releaseEpoch > 1)
    return false;
  bytes[2] = wire;
  bytes[3] = static_cast<uint8_t>(message.type);
  plink::PutU32(bytes + 4, message.deviceId);
  plink::PutU32(bytes + 8, message.requestId);
  plink::PutU32(bytes + 12, message.pairingCode);
  plink::PutU32(bytes + 16, message.bindingId);
  plink::PutU32(bytes + 20, message.tokenLow);
  plink::PutU32(bytes + 24, message.tokenHigh);
  plink::PutU32(bytes + 28, message.peerDeviceId);
  bytes[32] = static_cast<uint8_t>(message.status) |
      (wire == kReleaseProtocolVersion ? static_cast<uint8_t>(message.appVersion.releaseEpoch << 4U) : 0);
  bytes[33] = message.assignedSlot;
  bytes[34] = message.appVersion.major;
  bytes[35] = message.appVersion.minor;
  bytes[36] = message.appVersion.patch;
  bytes[37] = message.compatibilityFlags;
  plink::PutU16(bytes + 38, plink::Crc16Ccitt(bytes, 38));
  WriteU64(bytes + 40, key.Enabled() ? SipHash24(bytes, 40, key) : 0);
  return true;
}

inline bool Parse(const uint8_t *bytes, size_t length, Message &message,
                  const AuthKey &key = AuthKey{}) {
  if (bytes == nullptr || length != kMessageSize || bytes[0] != 'P' ||
      bytes[1] != 'B' || (bytes[2] != kProtocolVersion && bytes[2] != kReleaseProtocolVersion) ||
      plink::GetU16(bytes + 38) != plink::Crc16Ccitt(bytes, 38) ||
      (key.Enabled() && ReadU64(bytes + 40) != SipHash24(bytes, 40, key)) ||
      (!key.Enabled() && ReadU64(bytes + 40) != 0)) {
    return false;
  }
  const uint8_t type = bytes[3];
  const uint8_t epoch = bytes[2] == kReleaseProtocolVersion ? bytes[32] >> 4U : 0;
  const uint8_t status = bytes[2] == kReleaseProtocolVersion ? bytes[32] & 0x0FU : bytes[32];
  if (epoch > 1) return false;
  if (type < static_cast<uint8_t>(MessageType::Start) ||
      type > static_cast<uint8_t>(MessageType::Unbind) ||
      status > static_cast<uint8_t>(Status::StorageError)) {
    return false;
  }
  message.type = static_cast<MessageType>(type);
  message.wireVersion = bytes[2];
  message.deviceId = plink::GetU32(bytes + 4);
  message.requestId = plink::GetU32(bytes + 8);
  message.pairingCode = plink::GetU32(bytes + 12);
  message.bindingId = plink::GetU32(bytes + 16);
  message.tokenLow = plink::GetU32(bytes + 20);
  message.tokenHigh = plink::GetU32(bytes + 24);
  message.peerDeviceId = plink::GetU32(bytes + 28);
  message.status = static_cast<Status>(status);
  message.assignedSlot = bytes[33];
  message.appVersion = {bytes[34], bytes[35], bytes[36], epoch};
  message.compatibilityFlags = bytes[37];
  return true;
}

}  // namespace pcpair
