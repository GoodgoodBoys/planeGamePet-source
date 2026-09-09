#pragma once

#include <algorithm>

#include "../shared/plane_sim.h"

namespace plane_pet_ui {

// The HUD is a separate strip above the complete simulation world.  World
// coordinates must never be shortened by the legacy in-world HUD constant:
// player two's rotated view can legitimately contain peer coordinates below
// plink::kHudHeight.
constexpr int kGameHudHeight = 52;
constexpr int kGamePlayHeight = plink::kWorldHeight;
constexpr int kGameClientWidth = plink::kWorldWidth;
constexpr int kGameClientHeight = kGameHudHeight + kGamePlayHeight;
// Same A heart atlas as history, sized for the unchanged 240px compact HUD.
constexpr int kGameHeartWidth = 13;
constexpr int kGameHeartHeight = 12;
constexpr int kGameHeartPitch = 14;
constexpr int kGameOwnHeartX = 28;
constexpr int kGamePeerHeartX = kGameClientWidth - 1 - (kGameOwnHeartX + 2 * kGameHeartPitch);
constexpr int kGameHeartY = 19;
// Odd-sized hearts mirror their pixel centers; even-sized plane sprite boxes
// mirror their edges. This also remains exact when the whole HUD is scaled.
constexpr int kGameOwnPlaneX = 12;
constexpr int kGamePeerPlaneX = kGameClientWidth - kGameOwnPlaneX;
constexpr int kGamePlaneY = 22;
constexpr int kGameOwnHitX = 82;
constexpr int kGamePeerHitX = kGameClientWidth - kGameOwnHitX;
constexpr int kGameHitY = 19;
constexpr int kGameTimerLeft = 100, kGameTimerRight = 140;
static_assert(kGameOwnHeartX + kGamePeerHeartX + 2 * kGameHeartPitch == kGameClientWidth - 1);
static_assert(kGameOwnPlaneX + kGamePeerPlaneX == kGameClientWidth);
static_assert(kGameOwnHitX + kGamePeerHitX == kGameClientWidth);
static_assert(kGameOwnHitX + 18 <= kGameTimerLeft && kGamePeerHitX - 18 >= kGameTimerRight);
static_assert(kGameOwnHeartX + 2 * kGameHeartPitch + kGameHeartWidth / 2 + 1 < kGameOwnHitX - 18);
static_assert(kGameHeartWidth < kGameHeartPitch);
static_assert(kGameOwnHeartX + 2 * kGameHeartPitch + kGameHeartWidth / 2 < 76);
static_assert(kGamePeerHeartX - kGameHeartWidth / 2 > 164);
static_assert(kGameHeartY + kGameHeartHeight / 2 < kGameHudHeight);
static_assert(kGameHeartY + kGameHeartHeight / 2 + 10 < kGameHudHeight);

struct GameLayout {
  int scale = 1;
  int left = 0;
  int top = 0;
  int playTop = kGameHudHeight;

  int PlayBottom() const {
    return playTop + kGamePlayHeight * scale;
  }

  int ScreenY(int worldY) const {
    return playTop + worldY * scale;
  }

  int WorldYFromScreen(int screenY) const {
    return std::clamp((screenY - playTop) / scale, 0,
                      static_cast<int>(plink::kWorldHeight - 1));
  }
};

inline GameLayout MakeGameLayout(int width, int height) {
  GameLayout layout;
  layout.scale = std::max(
      1, std::min(width / plink::kWorldWidth,
                  height / (kGameHudHeight + kGamePlayHeight)));
  layout.left = (width - plink::kWorldWidth * layout.scale) / 2;
  const int contentHeight =
      (kGameHudHeight + kGamePlayHeight) * layout.scale;
  layout.top = (height - contentHeight) / 2;
  layout.playTop = layout.top + kGameHudHeight * layout.scale;
  return layout;
}

static_assert(kGameClientHeight == 372,
              "The HUD must sit above the complete 320-pixel game world.");

}  // namespace plane_pet_ui
