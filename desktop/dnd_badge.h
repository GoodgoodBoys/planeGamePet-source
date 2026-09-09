#pragma once
#include <windows.h>
#include <gdiplus.h>

namespace plane_pet_ui {
struct StatusBadge {
  POINT center{};
  bool visible = false;
  bool moon = false;
  bool offline = false;
  RECT Bounds() const {
    const int radius = moon ? 9 : 5;
    return {center.x - radius, center.y - radius,
            center.x + radius + (moon && offline ? 3 : 1),
            center.y + radius + (moon && offline ? 3 : 1)};
  }
};
inline void DrawStatusBadge(Gdiplus::Graphics &graphics, const StatusBadge &badge) {
  if (!badge.visible) return;
  const auto state = graphics.Save();
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
  Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
  Gdiplus::SolidBrush red(Gdiplus::Color(255, 255, 59, 77));
  Gdiplus::SolidBrush green(Gdiplus::Color(255, 54, 221, 148));
  Gdiplus::Pen outline(Gdiplus::Color(255, 170, 179, 194), 0.6f);
  Gdiplus::Pen dark(Gdiplus::Color(255, 0, 0, 0), 1.0f);
  const float x = static_cast<float>(badge.center.x), y = static_cast<float>(badge.center.y);
  if (!badge.moon) {
    graphics.FillEllipse(badge.offline ? &red : &green, x - 4, y - 4, 8.0f, 8.0f);
    graphics.DrawEllipse(&dark, x - 4, y - 4, 8.0f, 8.0f);
    graphics.Restore(state);
    return;
  }
  // Geometry from the approved 32-unit SVG. The circle itself is 16 pixels;
  // its crescent stays upright, independent of aircraft/emote rotation.
  graphics.TranslateTransform(x - 14 * 0.64f, y - 14 * 0.64f);
  graphics.ScaleTransform(0.64f, 0.64f);
  graphics.FillEllipse(&black, 1.5f, 1.5f, 25.0f, 25.0f);
  outline.SetWidth(0.9f);
  graphics.DrawEllipse(&outline, 1.5f, 1.5f, 25.0f, 25.0f);
  graphics.FillEllipse(&white, 5.7f, 6.4f, 15.6f, 15.6f);
  graphics.FillEllipse(&black, 9.9f, 3.2f, 14.6f, 14.6f);
  if (badge.offline) {
    dark.SetWidth(1.2f);
    graphics.FillEllipse(&red, 18.6f, 18.6f, 8.8f, 8.8f);
    graphics.DrawEllipse(&dark, 18.6f, 18.6f, 8.8f, 8.8f);
  }
  graphics.Restore(state);
}
inline void DrawStatusBadge(HDC dc, const StatusBadge &badge) {
  Gdiplus::Graphics graphics(dc);
  DrawStatusBadge(graphics, badge);
}
} // namespace plane_pet_ui
