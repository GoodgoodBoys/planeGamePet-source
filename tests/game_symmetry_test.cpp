#include <iostream>

#include "../shared/plane_sim.h"

namespace {

void MoveUntilClamped(plink::PlayerState &player, uint8_t input,
                      uint8_t index) {
  for (int step = 0; step < 200; ++step) {
    plink::MovePlayer(player, input, index);
  }
}

bool SameLocalPosition(const plink::PlayerState &first,
                       const plink::PlayerState &second) {
  const plink::PlayerState firstLocal = plink::ToLocalView(first, 0);
  const plink::PlayerState secondLocal = plink::ToLocalView(second, 1);
  return firstLocal.x == secondLocal.x && firstLocal.y == secondLocal.y;
}

}  // namespace

int main() {
  plink::WorldState world;
  plink::InitializeWorld(world);
  if (!SameLocalPosition(world.players[0], world.players[1])) {
    std::cerr << "spawn positions are not symmetric\n";
    return 1;
  }

  plink::PlayerState first = world.players[0];
  plink::PlayerState second = world.players[1];
  MoveUntilClamped(first, plink::InputDown, 0);
  MoveUntilClamped(second, plink::InputDown, 1);
  if (!SameLocalPosition(first, second) || first.y != 304 ||
      plink::ViewY(second.y, 1) != 304) {
    std::cerr << "bottom boundary is not symmetric\n";
    return 2;
  }

  MoveUntilClamped(first, plink::InputUp, 0);
  MoveUntilClamped(second, plink::InputUp, 1);
  if (!SameLocalPosition(first, second) || first.y != 176 ||
      plink::ViewY(second.y, 1) != 176) {
    std::cerr << "center boundary is not symmetric\n";
    return 3;
  }

  MoveUntilClamped(first, plink::InputLeft, 0);
  MoveUntilClamped(second, plink::InputLeft, 1);
  if (!SameLocalPosition(first, second) || first.x != 13 ||
      plink::ViewX(second.x, 1) != 13) {
    std::cerr << "left boundary is not symmetric\n";
    return 4;
  }

  MoveUntilClamped(first, plink::InputRight, 0);
  MoveUntilClamped(second, plink::InputRight, 1);
  if (!SameLocalPosition(first, second) || first.x != 227 ||
      plink::ViewX(second.x, 1) != 227) {
    std::cerr << "right boundary is not symmetric\n";
    return 5;
  }

  // A player at the newly reachable top edge must remain hittable.  The old
  // y=38 HUD cutoff destroyed this projectile before it reached the target.
  plink::InitializeWorld(world);
  world.players[1].x = 120;
  world.players[1].y = plink::kPlayerTwoMinY;
  world.bullets[0].active = true;
  world.bullets[0].id = 1;
  world.bullets[0].owner = 1;
  world.bullets[0].x = world.players[1].x;
  world.bullets[0].y = 43;
  const uint8_t healthBefore = world.players[1].health;
  for (int step = 0; step < 8 && world.players[1].health == healthBefore;
       ++step) {
    plink::StepWorld(world, 0, 0, false);
  }
  if (world.players[1].health != healthBefore - 1) {
    std::cerr << "top-edge player became immune to projectiles\n";
    return 6;
  }

  std::cout << "PC_PET_GAME_SYMMETRY_OK bottom=304 top=176 "
               "left=13 right=227 top_edge_hittable=1\n";
  return 0;
}
