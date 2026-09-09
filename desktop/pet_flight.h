#pragma once
namespace plane_pet_ui {
struct PetAnimationPose {
  double x = 0, y = 0, angle = 0, directionX = 0, directionY = -1;
};
struct PetFormation { PetAnimationPose blue{}, red{}; };
}  // namespace plane_pet_ui
