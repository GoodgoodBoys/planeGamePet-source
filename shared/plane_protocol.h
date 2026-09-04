#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace plink {

constexpr uint8_t kProtocolVersion = 5;
constexpr size_t kHeaderSize = 22;
constexpr size_t kCrcSize = 2;
constexpr size_t kMaxPacketSize = 192;
constexpr uint16_t kDefaultPort = 32100;
constexpr uint32_t kDefaultRoom = 1;
constexpr uint8_t kMaxBullets = 12;
constexpr uint32_t kInviteTimeoutMs = 60000;
constexpr uint32_t kPoorNetworkAfterMs = 1500;
constexpr uint32_t kDisconnectAfterMs = 5000;

enum class PacketType : uint8_t {
  Hello = 1,
  Welcome = 2,
  Input = 3,
  Snapshot = 4,
  Ping = 5,
  Pong = 6,
  Reject = 7,
  Action = 8,
};

enum class GamePhase : uint8_t {
  Menu = 0,
  Waiting = 1,
  Countdown = 2,
  Playing = 3,
  Finished = 4,
};

enum class MatchEndReason : uint8_t {
  None = 0,
  Destroyed = 1,
  TimeLimitDraw = 2,
  PlayerDisconnected = 3,
  ServerUnavailable = 4,
  InviteRejected = 5,
  InviteTimedOut = 6,
};

enum class PlayerAction : uint8_t {
  Invite = 1,
  Accept = 2,
  ReturnToMenu = 3,
  EmoteLaugh = 4,
  EmoteCry = 5,
  EmoteAngry = 6,
  EmoteBeckon = 7,
};

inline bool IsQuickEmote(PlayerAction action) {
  return action >= PlayerAction::EmoteLaugh &&
         action <= PlayerAction::EmoteBeckon;
}

inline uint8_t QuickEmoteValue(PlayerAction action) {
  return IsQuickEmote(action)
      ? static_cast<uint8_t>(action) -
            static_cast<uint8_t>(PlayerAction::EmoteLaugh) + 1U
      : 0U;
}

enum InputBits : uint8_t {
  InputUp = 1U << 0,
  InputDown = 1U << 1,
  InputLeft = 1U << 2,
  InputRight = 1U << 3,
};

struct PacketHeader {
  PacketType type = PacketType::Reject;
  uint32_t session = 0;
  uint32_t sequence = 0;
  uint32_t acknowledgment = 0;
  uint32_t tick = 0;
  uint16_t payloadLength = 0;
};

struct HelloPayload {
  uint32_t clientId = 0;
  uint32_t roomId = kDefaultRoom;
  uint8_t requestedSlot = 0;
  uint8_t capabilities = 0;
};

struct WelcomePayload {
  uint8_t assignedSlot = 0;
  uint8_t tickRate = 30;
  uint16_t firePeriodTicks = 24;
  uint32_t serverTimeMs = 0;
};

struct InputPayload {
  uint32_t clientTimeMs = 0;
  uint8_t current = 0;
  uint8_t previous1 = 0;
  uint8_t previous2 = 0;
};

struct ActionPayload {
  PlayerAction action = PlayerAction::Invite;
};

struct PlayerState {
  int16_t x = 0;
  int16_t y = 0;
  uint8_t health = 3;
  uint8_t flags = 1;
};

struct BulletState {
  uint16_t id = 0;
  uint8_t owner = 0;
  int16_t x = 0;
  int16_t y = 0;
};

struct SnapshotPayload {
  uint32_t serverTimeMs = 0;
  uint32_t lastProcessedInput = 0;
  GamePhase phase = GamePhase::Menu;
  uint8_t inviterSlot = 0;
  uint8_t winnerSlot = 0;
  MatchEndReason endReason = MatchEndReason::None;
  uint8_t onlineMask = 0;
  uint32_t phaseRemainingMs = 0;
  uint32_t matchElapsedMs = 0;
  PlayerState players[2]{};
  uint8_t bulletCount = 0;
  BulletState bullets[kMaxBullets]{};
  uint16_t hitCount[2]{};
};

inline void PutU16(uint8_t *dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

inline void PutU32(uint8_t *dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  dst[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  dst[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

inline uint16_t GetU16(const uint8_t *src) {
  return static_cast<uint16_t>(src[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(src[1]) << 8U);
}

inline uint32_t GetU32(const uint8_t *src) {
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8U) |
         (static_cast<uint32_t>(src[2]) << 16U) |
         (static_cast<uint32_t>(src[3]) << 24U);
}

inline uint16_t Crc16Ccitt(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0U
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

class PacketWriter {
 public:
  PacketWriter(uint8_t *buffer, size_t capacity, PacketType type,
               uint32_t session, uint32_t sequence, uint32_t acknowledgment,
               uint32_t tick)
      : buffer_(buffer), capacity_(capacity), position_(kHeaderSize), valid_(true) {
    if (buffer_ == nullptr || capacity_ < kHeaderSize + kCrcSize) {
      valid_ = false;
      return;
    }
    buffer_[0] = 'P';
    buffer_[1] = 'L';
    buffer_[2] = kProtocolVersion;
    buffer_[3] = static_cast<uint8_t>(type);
    PutU32(buffer_ + 4, session);
    PutU32(buffer_ + 8, sequence);
    PutU32(buffer_ + 12, acknowledgment);
    PutU32(buffer_ + 16, tick);
    PutU16(buffer_ + 20, 0);
  }

  bool U8(uint8_t value) { return Bytes(&value, 1); }

  bool U16(uint16_t value) {
    uint8_t data[2];
    PutU16(data, value);
    return Bytes(data, sizeof(data));
  }

  bool I16(int16_t value) { return U16(static_cast<uint16_t>(value)); }

  bool U32(uint32_t value) {
    uint8_t data[4];
    PutU32(data, value);
    return Bytes(data, sizeof(data));
  }

  bool Bytes(const uint8_t *data, size_t length) {
    if (!valid_ || data == nullptr || position_ + length + kCrcSize > capacity_) {
      valid_ = false;
      return false;
    }
    memcpy(buffer_ + position_, data, length);
    position_ += length;
    return true;
  }

  size_t Finish() {
    if (!valid_ || position_ < kHeaderSize ||
        position_ - kHeaderSize > UINT16_MAX) {
      return 0;
    }
    PutU16(buffer_ + 20,
           static_cast<uint16_t>(position_ - kHeaderSize));
    const uint16_t crc = Crc16Ccitt(buffer_, position_);
    PutU16(buffer_ + position_, crc);
    position_ += kCrcSize;
    return position_;
  }

 private:
  uint8_t *buffer_;
  size_t capacity_;
  size_t position_;
  bool valid_;
};

class PayloadReader {
 public:
  PayloadReader(const uint8_t *data, size_t length)
      : data_(data), length_(length), position_(0), valid_(data != nullptr) {}

  bool U8(uint8_t &value) {
    if (!Require(1)) return false;
    value = data_[position_++];
    return true;
  }

  bool U16(uint16_t &value) {
    if (!Require(2)) return false;
    value = GetU16(data_ + position_);
    position_ += 2;
    return true;
  }

  bool I16(int16_t &value) {
    uint16_t raw = 0;
    if (!U16(raw)) return false;
    value = static_cast<int16_t>(raw);
    return true;
  }

  bool U32(uint32_t &value) {
    if (!Require(4)) return false;
    value = GetU32(data_ + position_);
    position_ += 4;
    return true;
  }

  bool Done() const { return valid_ && position_ == length_; }
  bool Valid() const { return valid_; }
  size_t Remaining() const { return position_ <= length_ ? length_ - position_ : 0; }

 private:
  bool Require(size_t count) {
    if (!valid_ || position_ + count > length_) {
      valid_ = false;
      return false;
    }
    return true;
  }

  const uint8_t *data_;
  size_t length_;
  size_t position_;
  bool valid_;
};

inline bool ParsePacket(const uint8_t *data, size_t length,
                        PacketHeader &header, const uint8_t *&payload) {
  if (data == nullptr || length < kHeaderSize + kCrcSize ||
      length > kMaxPacketSize || data[0] != 'P' || data[1] != 'L' ||
      data[2] != kProtocolVersion) {
    return false;
  }
  const uint16_t payloadLength = GetU16(data + 20);
  if (length != kHeaderSize + payloadLength + kCrcSize) {
    return false;
  }
  const uint16_t expected = GetU16(data + length - kCrcSize);
  if (Crc16Ccitt(data, length - kCrcSize) != expected) {
    return false;
  }
  header.type = static_cast<PacketType>(data[3]);
  header.session = GetU32(data + 4);
  header.sequence = GetU32(data + 8);
  header.acknowledgment = GetU32(data + 12);
  header.tick = GetU32(data + 16);
  header.payloadLength = payloadLength;
  payload = data + kHeaderSize;
  return true;
}

inline bool WriteHello(PacketWriter &writer, const HelloPayload &value) {
  return writer.U32(value.clientId) && writer.U32(value.roomId) &&
         writer.U8(value.requestedSlot) && writer.U8(value.capabilities);
}

inline bool ReadHello(PayloadReader &reader, HelloPayload &value) {
  return reader.U32(value.clientId) && reader.U32(value.roomId) &&
         reader.U8(value.requestedSlot) && reader.U8(value.capabilities) &&
         reader.Done();
}

inline bool WriteWelcome(PacketWriter &writer, const WelcomePayload &value) {
  return writer.U8(value.assignedSlot) && writer.U8(value.tickRate) &&
         writer.U16(value.firePeriodTicks) && writer.U32(value.serverTimeMs);
}

inline bool ReadWelcome(PayloadReader &reader, WelcomePayload &value) {
  return reader.U8(value.assignedSlot) && reader.U8(value.tickRate) &&
         reader.U16(value.firePeriodTicks) && reader.U32(value.serverTimeMs) &&
         reader.Done();
}

inline bool WriteInput(PacketWriter &writer, const InputPayload &value) {
  return writer.U32(value.clientTimeMs) && writer.U8(value.current) &&
         writer.U8(value.previous1) && writer.U8(value.previous2);
}

inline bool ReadInput(PayloadReader &reader, InputPayload &value) {
  return reader.U32(value.clientTimeMs) && reader.U8(value.current) &&
         reader.U8(value.previous1) && reader.U8(value.previous2) &&
         reader.Done();
}

inline bool WriteAction(PacketWriter &writer, const ActionPayload &value) {
  return writer.U8(static_cast<uint8_t>(value.action));
}

inline bool ReadAction(PayloadReader &reader, ActionPayload &value) {
  uint8_t action = 0;
  if (!reader.U8(action) || !reader.Done() ||
      action < static_cast<uint8_t>(PlayerAction::Invite) ||
      action > static_cast<uint8_t>(PlayerAction::EmoteBeckon)) {
    return false;
  }
  value.action = static_cast<PlayerAction>(action);
  return true;
}

inline bool WriteSnapshot(PacketWriter &writer, const SnapshotPayload &value) {
  if (!writer.U32(value.serverTimeMs) ||
      !writer.U32(value.lastProcessedInput) ||
      !writer.U8(static_cast<uint8_t>(value.phase)) ||
      !writer.U8(value.inviterSlot) || !writer.U8(value.winnerSlot) ||
      !writer.U8(static_cast<uint8_t>(value.endReason)) ||
      !writer.U8(value.onlineMask) ||
      !writer.U32(value.phaseRemainingMs) ||
      !writer.U32(value.matchElapsedMs)) {
    return false;
  }
  for (const PlayerState &player : value.players) {
    if (!writer.I16(player.x) || !writer.I16(player.y) ||
        !writer.U8(player.health) || !writer.U8(player.flags)) {
      return false;
    }
  }
  const uint8_t count = value.bulletCount > kMaxBullets
                            ? kMaxBullets
                            : value.bulletCount;
  if (!writer.U8(count)) return false;
  for (uint8_t i = 0; i < count; ++i) {
    const BulletState &bullet = value.bullets[i];
    if (!writer.U16(bullet.id) || !writer.U8(bullet.owner) ||
        !writer.I16(bullet.x) || !writer.I16(bullet.y)) {
      return false;
    }
  }
  return writer.U16(value.hitCount[0]) && writer.U16(value.hitCount[1]);
}

inline bool ReadSnapshot(PayloadReader &reader, SnapshotPayload &value) {
  uint8_t phase = 0;
  uint8_t endReason = 0;
  if (!reader.U32(value.serverTimeMs) ||
      !reader.U32(value.lastProcessedInput) || !reader.U8(phase) ||
      !reader.U8(value.inviterSlot) || !reader.U8(value.winnerSlot) ||
      !reader.U8(endReason) ||
      !reader.U8(value.onlineMask) ||
      !reader.U32(value.phaseRemainingMs) ||
      !reader.U32(value.matchElapsedMs) ||
      phase > static_cast<uint8_t>(GamePhase::Finished) ||
      endReason > static_cast<uint8_t>(MatchEndReason::InviteTimedOut)) {
    return false;
  }
  value.phase = static_cast<GamePhase>(phase);
  value.endReason = static_cast<MatchEndReason>(endReason);
  for (PlayerState &player : value.players) {
    if (!reader.I16(player.x) || !reader.I16(player.y) ||
        !reader.U8(player.health) || !reader.U8(player.flags)) {
      return false;
    }
  }
  if (!reader.U8(value.bulletCount) || value.bulletCount > kMaxBullets) {
    return false;
  }
  for (uint8_t i = 0; i < value.bulletCount; ++i) {
    BulletState &bullet = value.bullets[i];
    if (!reader.U16(bullet.id) || !reader.U8(bullet.owner) ||
        !reader.I16(bullet.x) || !reader.I16(bullet.y)) {
      return false;
    }
  }
  return reader.U16(value.hitCount[0]) && reader.U16(value.hitCount[1]) &&
         reader.Done();
}

// Hash only authoritative game state. Transport-specific time and per-client
// acknowledgments are intentionally excluded, so two clients can compare the
// same snapshot tick directly.
inline uint32_t SnapshotStateDigest(const SnapshotPayload &snapshot) {
  uint32_t hash = 2166136261U;
  const auto mix = [&hash](uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) {
      hash ^= static_cast<uint8_t>((value >> (i * 8U)) & 0xFFU);
      hash *= 16777619U;
    }
  };
  mix(static_cast<uint8_t>(snapshot.phase));
  mix(snapshot.inviterSlot);
  mix(snapshot.winnerSlot);
  mix(static_cast<uint8_t>(snapshot.endReason));
  mix(snapshot.onlineMask);
  mix(snapshot.phaseRemainingMs);
  mix(snapshot.matchElapsedMs);
  for (const PlayerState &player : snapshot.players) {
    mix(static_cast<uint16_t>(player.x));
    mix(static_cast<uint16_t>(player.y));
    mix(player.health);
  }
  mix(snapshot.bulletCount);
  for (uint8_t i = 0; i < snapshot.bulletCount; ++i) {
    mix(snapshot.bullets[i].id);
    mix(snapshot.bullets[i].owner);
    mix(static_cast<uint16_t>(snapshot.bullets[i].x));
    mix(static_cast<uint16_t>(snapshot.bullets[i].y));
  }
  mix(snapshot.hitCount[0]);
  mix(snapshot.hitCount[1]);
  return hash;
}

inline bool WriteTimestamp(PacketWriter &writer, uint32_t timestampMs) {
  return writer.U32(timestampMs);
}

inline bool ReadTimestamp(PayloadReader &reader, uint32_t &timestampMs) {
  return reader.U32(timestampMs) && reader.Done();
}

}  // namespace plink
