#include <iostream>

#include "../desktop/game_layout.h"

int main() {
  using namespace plane_pet_ui;
  const GameLayout native = MakeGameLayout(kGameClientWidth,
                                            kGameClientHeight);
  if (native.scale != 1 || native.left != 0 || native.top != 0 ||
      native.playTop != kGameHudHeight ||
      native.PlayBottom() != kGameClientHeight ||
      native.ScreenY(0) != kGameHudHeight ||
      native.ScreenY(plink::kWorldHeight - 1) != kGameClientHeight - 1 ||
      native.WorldYFromScreen(native.playTop) != 0 ||
      native.WorldYFromScreen(native.PlayBottom() - 1) !=
          plink::kWorldHeight - 1) {
    std::cerr << "native layout mapping failed\n";
    return 1;
  }

  const GameLayout doubled = MakeGameLayout(520, 800);
  if (doubled.scale != 2 || doubled.left != 20 || doubled.top != 28 ||
      doubled.playTop != 132 || doubled.PlayBottom() != 772 ||
      doubled.ScreenY(0) != doubled.playTop ||
      doubled.ScreenY(plink::kWorldHeight - 1) != 770) {
    std::cerr << "scaled layout mapping failed\n";
    return 2;
  }

  // This is the coordinate that exposed the bug.  Player one's canonical
  // lower bound becomes y=15 in player two's rotated view.  It must render in
  // the map, below the HUD, with enough room for the visible aircraft body.
  const int rotatedPeerY = plink::ViewY(
      plink::kWorldHeight - plink::kPlaneHalfHeight - 3, 1);
  constexpr int kVisiblePlaneHalfHeight = 11;
  if (rotatedPeerY != 15 ||
      native.ScreenY(rotatedPeerY) - kVisiblePlaneHalfHeight <
          native.playTop) {
    std::cerr << "rotated peer can overlap the HUD\n";
    return 3;
  }

  for (int worldY = 0; worldY < plink::kWorldHeight; ++worldY) {
    const int screenY = native.ScreenY(worldY);
    if (screenY < native.playTop || screenY >= native.PlayBottom() ||
        native.WorldYFromScreen(screenY) != worldY) {
      std::cerr << "world row mapping failed at y=" << worldY << '\n';
      return 4;
    }
  }

  std::cout << "PC_PET_GAME_LAYOUT_OK hud=52 map=240x320 client=240x372 "
               "rotated_peer_y=15 visible=1\n";
  return 0;
}
