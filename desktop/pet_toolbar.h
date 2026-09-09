#pragma once
#include <algorithm>
#include <cstdint>
#include <windows.h>

namespace plane_pet_ui {
// One hover row, inside the unchanged 280 x 150 pet. Keep emote/button sizes.
inline constexpr RECT kToolbarPlay{17, 112, 107, 145};
inline constexpr RECT kToolbarFist{23, 116, 46, 140};
inline constexpr RECT kToolbarPlayText{49, 112, 101, 145};
inline constexpr int kToolbarFistResource = 162;
inline constexpr uint64_t kToolbarHideDelayMs = 400;
inline constexpr int kToolbarClickSlop = 6;

constexpr RECT ToolbarEmoteRect(int index) {
  const int left = 113 + std::clamp(index, 0, 3) * 38;
  return RECT{left, 112, left + 35, 145};
}
constexpr RECT ToolbarEmoteIcon(const RECT &button) {
  return RECT{button.left + 6, button.top + 4, button.right - 6, button.bottom - 5};
}
static_assert(ToolbarEmoteRect(3).right == 262);
static_assert(kToolbarPlay.bottom - kToolbarPlay.top == 33);
static_assert(kToolbarPlay.right + 6 == ToolbarEmoteRect(0).left);
static_assert(ToolbarEmoteIcon(ToolbarEmoteRect(0)).right -
              ToolbarEmoteIcon(ToolbarEmoteRect(0)).left == 23);

// Deterministic hover state. Screen-position polling happens in the UI
// thread, so color-key transparent areas need no mouse hook or click surface.
class PetToolbarState {
 public:
  bool visible = false;

  void Reset() {
    visible = false;
    lastInside_ = 0;
  }
  void Hide() {
    visible = false;
  }
  void Observe(bool available, bool inside, bool pointerDown, uint64_t now) {
    if (!available) { Hide(); return; }
    if (inside) {
      // Do not appear underneath a drag that began in another application.
      if (!pointerDown || visible) { visible = true; lastInside_ = now; }
    } else if (visible && now >= lastInside_ && now - lastInside_ >= kToolbarHideDelayMs) {
      Hide();
    }
  }

 private:
  uint64_t lastInside_ = 0;
};
}  // namespace plane_pet_ui
