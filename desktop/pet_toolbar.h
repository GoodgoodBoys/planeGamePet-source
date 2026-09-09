#pragma once
#include <algorithm>
#include <cstdint>
#include <windows.h>

namespace plane_pet_ui {
// Keep the original four button rectangles and icon insets exactly unchanged.
inline constexpr RECT kToolbarPlay{64, 116, 213, 149};
inline constexpr RECT kToolbarToggle{216, 116, 238, 149};
inline constexpr RECT kToolbarFist{99, 120, 122, 144};
inline constexpr RECT kToolbarPlayText{130, 116, 179, 149};
inline constexpr int kToolbarFistResource = 162;
inline constexpr uint64_t kToolbarHideDelayMs = 400;
inline constexpr uint64_t kToolbarSlideMs = 180;
inline constexpr uint64_t kToolbarRememberMs = 10 * 60 * 1000;
inline constexpr int kToolbarClickSlop = 6;

constexpr RECT ToolbarEmoteRect(int index) {
  const int left = 64 + std::clamp(index, 0, 3) * 38;
  return RECT{left, 116, left + 35, 149};
}
constexpr RECT ToolbarEmoteIcon(const RECT &button) {
  return RECT{button.left + 6, button.top + 4, button.right - 6, button.bottom - 5};
}
static_assert(ToolbarEmoteRect(3).right == kToolbarPlay.right);
static_assert(kToolbarToggle.bottom - kToolbarToggle.top == 33);
static_assert(ToolbarEmoteIcon(ToolbarEmoteRect(0)).right -
              ToolbarEmoteIcon(ToolbarEmoteRect(0)).left == 23);

// Deterministic hover/slide state. Screen-position polling happens in the UI
// thread, so color-key transparent areas need no mouse hook or click surface.
class PetToolbarState {
 public:
  bool visible = false;
  bool expanded = false;

  void Reset() {
    visible = expanded = false;
    lastInside_ = started_ = lastEmoteOrOpen_ = 0;
    from_ = 0.0;
  }
  void Hide() {
    visible = false;
    from_ = expanded ? 1.0 : 0.0;
    started_ = 0;
  }
  void Observe(bool available, bool inside, bool pointerDown, uint64_t now) {
    if (expanded && now >= lastEmoteOrOpen_ && now - lastEmoteOrOpen_ >= kToolbarRememberMs) {
      from_ = visible ? Reveal(now) : 0.0;
      expanded = false;
      started_ = visible ? now : 0;
    }
    if (!available) { Hide(); return; }
    if (inside) {
      // Do not appear underneath a drag that began in another application.
      if (!pointerDown || visible) { visible = true; lastInside_ = now; }
    } else if (visible && now >= lastInside_ && now - lastInside_ >= kToolbarHideDelayMs) {
      Hide();
    }
  }
  double Reveal(uint64_t now) const {
    if (!visible) return 0.0;
    const double target = expanded ? 1.0 : 0.0;
    const double elapsed = now >= started_ ? static_cast<double>(now - started_) : 0.0;
    double t = std::min(1.0, elapsed / kToolbarSlideMs);
    t = t * t * (3.0 - 2.0 * t);
    return from_ + (target - from_) * t;
  }
  bool Settled(uint64_t now) const {
    return now >= started_ && now - started_ >= kToolbarSlideMs;
  }
  void Toggle(uint64_t now) {
    if (!visible) return;
    from_ = Reveal(now);
    expanded = !expanded;
    started_ = now;
    if (expanded) lastEmoteOrOpen_ = now;
  }
  void EmoteSent(uint64_t now) {
    if (expanded) lastEmoteOrOpen_ = now;
  }

 private:
  uint64_t lastInside_ = 0;
  uint64_t started_ = 0;
  uint64_t lastEmoteOrOpen_ = 0;
  double from_ = 0.0;
};
}  // namespace plane_pet_ui
