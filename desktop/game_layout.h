#pragma once

#include <algorithm>

#include "../../shared/plane_sim.h"

namespace plane_pet_ui {

// The HUD is a separate strip above the complete simulation world.  World
// coordinates must never be shortened by the legacy in-world HUD constant:
// player two's rotated view can legitimately contain peer coordinates below
// plink::kHudHeight.
constexpr int kGameHudHeight = 52;
constexpr int kGamePlayHeight = plink::kWorldHeight;
constexpr int kGameClientWidth = plink::kWorldWidth;
constexpr int kGameClientHeight = kGameHudHeight + kGamePlayHeight;

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
