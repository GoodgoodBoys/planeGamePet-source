#pragma once

#include <stdint.h>

#include "plane_protocol.h"

namespace plink {

constexpr int16_t kWorldWidth = 240;
constexpr int16_t kWorldHeight = 320;
constexpr int16_t kHudHeight = 38;
constexpr int16_t kPlaneHalfWidth = 10;
constexpr int16_t kPlaneHalfHeight = 13;
constexpr int16_t kPlayerSpeed = 3;
constexpr int16_t kBulletSpeed = 6;
constexpr uint16_t kFirePeriodTicks = 24;
constexpr uint8_t kInitialHealth = 3;
constexpr int16_t kPlayerOneSpawnX = kWorldWidth / 2;
constexpr int16_t kPlayerOneSpawnY =
    kWorldHeight - 1 - (kHudHeight + kPlaneHalfHeight + 4);
constexpr int16_t kPlayerTwoSpawnX = kWorldWidth - 1 - kPlayerOneSpawnX;
constexpr int16_t kPlayerTwoSpawnY = kWorldHeight - 1 - kPlayerOneSpawnY;
constexpr int16_t kPlayerOneMinX = kPlaneHalfWidth + 3;
constexpr int16_t kPlayerOneMaxX = kWorldWidth - kPlaneHalfWidth - 3;
constexpr int16_t kPlayerTwoMinX = kWorldWidth - 1 - kPlayerOneMaxX;
constexpr int16_t kPlayerTwoMaxX = kWorldWidth - 1 - kPlayerOneMinX;
constexpr int16_t kPlayerOneMinY =
    kWorldHeight / 2 + kPlaneHalfHeight + 3;
constexpr int16_t kPlayerOneMaxY = kWorldHeight - kPlaneHalfHeight - 3;
constexpr int16_t kPlayerTwoMinY = kWorldHeight - 1 - kPlayerOneMaxY;
constexpr int16_t kPlayerTwoMaxY = kWorldHeight - 1 - kPlayerOneMinY;

static_assert(kPlayerTwoMinX == 12 && kPlayerTwoMaxX == 226,
              "Player two X bounds must mirror player one's local view.");
static_assert(kPlayerTwoMinY == 15 && kPlayerTwoMaxY == 143,
              "Player two Y bounds must mirror player one's local view.");

struct SimBullet {
  bool active = false;
  uint16_t id = 0;
  uint8_t owner = 0;
  int16_t x = 0;
  int16_t y = 0;
};

struct WorldState {
  uint32_t tick = 0;
  PlayerState players[2]{};
  SimBullet bullets[kMaxBullets]{};
  uint16_t nextBulletId = 1;
  uint16_t hitCount[2]{};
};

inline int16_t ClampI16(int16_t value, int16_t minimum, int16_t maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

inline void ResetPlayer(WorldState &world, uint8_t index) {
  PlayerState &player = world.players[index];
  player.x = index == 0 ? kPlayerOneSpawnX : kPlayerTwoSpawnX;
  player.y = index == 0 ? kPlayerOneSpawnY : kPlayerTwoSpawnY;
  player.health = kInitialHealth;
  player.flags = 1;
}

inline void RespawnPlayer(WorldState &world, uint8_t index) {
  PlayerState &player = world.players[index];
  player.x = index == 0 ? kPlayerOneSpawnX : kPlayerTwoSpawnX;
  player.y = index == 0 ? kPlayerOneSpawnY : kPlayerTwoSpawnY;
  player.flags = player.health > 0 ? 1 : 0;
}

inline void InitializeWorld(WorldState &world) {
  world = WorldState{};
  ResetPlayer(world, 0);
  ResetPlayer(world, 1);
}

inline void MovePlayer(PlayerState &player, uint8_t input, uint8_t index) {
  if (player.health == 0 || (player.flags & 1U) == 0U) return;
  int16_t dx = 0;
  int16_t dy = 0;
  if ((input & InputUp) != 0U) dy -= kPlayerSpeed;
  if ((input & InputDown) != 0U) dy += kPlayerSpeed;
  if ((input & InputLeft) != 0U) dx -= kPlayerSpeed;
  if ((input & InputRight) != 0U) dx += kPlayerSpeed;
  // Inputs are always expressed in the local player's view. Player 2 sees a
  // 180-degree-rotated world, so reverse both axes before applying its input
  // to the canonical authoritative world.
  if (index == 1) {
    dx = static_cast<int16_t>(-dx);
    dy = static_cast<int16_t>(-dy);
  }
  player.x = ClampI16(static_cast<int16_t>(player.x + dx),
                      index == 0 ? kPlayerOneMinX : kPlayerTwoMinX,
                      index == 0 ? kPlayerOneMaxX : kPlayerTwoMaxX);
  player.y = ClampI16(static_cast<int16_t>(player.y + dy),
                      index == 0 ? kPlayerOneMinY : kPlayerTwoMinY,
                      index == 0 ? kPlayerOneMaxY : kPlayerTwoMaxY);
}

inline void SpawnBullet(WorldState &world, uint8_t owner) {
  if (world.players[owner].health == 0 ||
      (world.players[owner].flags & 1U) == 0U) return;
  for (SimBullet &bullet : world.bullets) {
    if (bullet.active) continue;
    bullet.active = true;
    bullet.id = world.nextBulletId++;
    if (world.nextBulletId == 0) world.nextBulletId = 1;
    bullet.owner = static_cast<uint8_t>(owner + 1);
    bullet.x = world.players[owner].x;
    bullet.y = static_cast<int16_t>(
        world.players[owner].y +
        (owner == 0 ? -kPlaneHalfHeight : kPlaneHalfHeight));
    return;
  }
}

inline bool SweptHit(int16_t x, int16_t oldY, int16_t newY,
                     const PlayerState &target) {
  const int16_t top = static_cast<int16_t>(target.y - kPlaneHalfHeight);
  const int16_t bottom = static_cast<int16_t>(target.y + kPlaneHalfHeight);
  const int16_t segmentTop = oldY < newY ? oldY : newY;
  const int16_t segmentBottom = oldY > newY ? oldY : newY;
  return segmentBottom >= top && segmentTop <= bottom &&
         x >= target.x - kPlaneHalfWidth &&
         x <= target.x + kPlaneHalfWidth;
}

inline void DamagePlayer(WorldState &world, uint8_t target,
                         bool respawnAfterSurvivingHit = true) {
  PlayerState &player = world.players[target];
  if (player.health == 0 || (player.flags & 1U) == 0U) return;
  ++world.hitCount[target == 0 ? 1 : 0];
  --player.health;
  if (player.health == 0) {
    player.flags = 0;
  } else if (respawnAfterSurvivingHit) {
    RespawnPlayer(world, target);
  }
}

inline void StepWorld(WorldState &world, uint8_t inputPlayer1,
                      uint8_t inputPlayer2,
                      bool respawnAfterSurvivingHit = true) {
  ++world.tick;
  MovePlayer(world.players[0], inputPlayer1, 0);
  MovePlayer(world.players[1], inputPlayer2, 1);

  if ((world.tick % kFirePeriodTicks) == 0U) {
    SpawnBullet(world, 0);
    SpawnBullet(world, 1);
  }

  for (SimBullet &bullet : world.bullets) {
    if (!bullet.active) continue;
    const int16_t oldY = bullet.y;
    const int16_t direction = bullet.owner == 1 ? -1 : 1;
    bullet.y = static_cast<int16_t>(bullet.y + direction * kBulletSpeed);
    const uint8_t target = bullet.owner == 1 ? 1 : 0;
    if (world.players[target].health > 0 &&
        SweptHit(bullet.x, oldY, bullet.y, world.players[target])) {
      DamagePlayer(world, target, respawnAfterSurvivingHit);
      bullet.active = false;
      continue;
    }
    if (bullet.y < 0 || bullet.y >= kWorldHeight) {
      bullet.active = false;
    }
  }
}

inline int16_t ViewX(int16_t worldX, uint8_t viewerIndex) {
  return viewerIndex == 0
             ? worldX
             : static_cast<int16_t>(kWorldWidth - 1 - worldX);
}

inline int16_t ViewY(int16_t worldY, uint8_t viewerIndex) {
  return viewerIndex == 0
             ? worldY
             : static_cast<int16_t>(kWorldHeight - 1 - worldY);
}

inline PlayerState ToLocalView(const PlayerState &worldPlayer,
                               uint8_t viewerIndex) {
  PlayerState view = worldPlayer;
  view.x = ViewX(worldPlayer.x, viewerIndex);
  view.y = ViewY(worldPlayer.y, viewerIndex);
  return view;
}

inline BulletState ToLocalView(const BulletState &worldBullet,
                               uint8_t viewerIndex) {
  BulletState view = worldBullet;
  view.x = ViewX(worldBullet.x, viewerIndex);
  view.y = ViewY(worldBullet.y, viewerIndex);
  return view;
}

inline void MakeSnapshot(const WorldState &world, uint32_t serverTimeMs,
                         uint32_t lastInputSequence,
                         SnapshotPayload &snapshot) {
  snapshot = SnapshotPayload{};
  snapshot.serverTimeMs = serverTimeMs;
  snapshot.lastProcessedInput = lastInputSequence;
  snapshot.players[0] = world.players[0];
  snapshot.players[1] = world.players[1];
  snapshot.hitCount[0] = world.hitCount[0];
  snapshot.hitCount[1] = world.hitCount[1];
  for (const SimBullet &bullet : world.bullets) {
    if (!bullet.active || snapshot.bulletCount >= kMaxBullets) continue;
    BulletState &wire = snapshot.bullets[snapshot.bulletCount++];
    wire.id = bullet.id;
    wire.owner = bullet.owner;
    wire.x = bullet.x;
    wire.y = bullet.y;
  }
}

inline uint32_t WorldDigest(const WorldState &world) {
  uint32_t hash = 2166136261U;
  auto mix = [&hash](uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) {
      hash ^= static_cast<uint8_t>((value >> (i * 8U)) & 0xFFU);
      hash *= 16777619U;
    }
  };
  mix(world.tick);
  for (const PlayerState &player : world.players) {
    mix(static_cast<uint16_t>(player.x));
    mix(static_cast<uint16_t>(player.y));
    mix(player.health);
  }
  for (const SimBullet &bullet : world.bullets) {
    mix(bullet.active ? 1U : 0U);
    mix(bullet.id);
    mix(bullet.owner);
    mix(static_cast<uint16_t>(bullet.x));
    mix(static_cast<uint16_t>(bullet.y));
  }
  mix(world.hitCount[0]);
  mix(world.hitCount[1]);
  return hash;
}

}  // namespace plink
