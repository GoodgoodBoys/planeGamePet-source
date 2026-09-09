#pragma once
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <gdiplus.h>
#include <memory>

namespace plane_pet_dpi {
inline UINT Normalize(UINT dpi) { return std::clamp<UINT>(dpi, 96, 768); }
inline int Scale(int logical, UINT dpi) { return MulDiv(logical, Normalize(dpi), 96); }
inline UINT ForWindow(HWND window) {
  using GetDpi = UINT(WINAPI *)(HWND);
  const auto address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
  GetDpi function = nullptr;
  static_assert(sizeof(function) == sizeof(address));
  std::memcpy(&function, &address, sizeof(function));
  return function ? Normalize(function(window)) : 96;
}
inline bool Adjust(RECT &rect, DWORD style, DWORD extendedStyle, UINT dpi) {
  using AdjustDpi = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
  const auto address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi");
  AdjustDpi function = nullptr;
  static_assert(sizeof(function) == sizeof(address));
  std::memcpy(&function, &address, sizeof(function));
  return function ? function(&rect, style, FALSE, extendedStyle, Normalize(dpi)) != FALSE
                  : AdjustWindowRectEx(&rect, style, FALSE, extendedStyle) != FALSE;
}
inline POINT LogicalPoint(POINT point, int physicalWidth, int physicalHeight,
                          int logicalWidth, int logicalHeight) {
  // Floor, including negative positions captured outside the window. Rounding
  // inward at the right/bottom edge must not turn an outside click into a hit.
  const auto convert = [](int value, int logical, int physical) {
    const long long numerator = static_cast<long long>(value) * logical;
    const int denominator = std::max(1, physical);
    return static_cast<LONG>(numerator >= 0 ? numerator / denominator
        : -((-numerator + denominator - 1) / denominator));
  };
  return POINT{convert(point.x, logicalWidth, physicalWidth),
               convert(point.y, logicalHeight, physicalHeight)};
}
inline RECT FitWorkArea(RECT rect, const RECT &work) {
  const LONG width = rect.right - rect.left, height = rect.bottom - rect.top;
  rect.left = std::clamp(rect.left, work.left, std::max(work.left, work.right - width));
  rect.top = std::clamp(rect.top, work.top, std::max(work.top, work.bottom - height));
  rect.right = rect.left + width;
  rect.bottom = rect.top + height;
  return rect;
}
inline RECT FitMonitor(RECT rect) {
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &monitor))
    return FitWorkArea(rect, monitor.rcWork);
  return rect;
}
class LogicalCanvas {
 public:
  LogicalCanvas(HDC dc, int logicalWidth, int logicalHeight, int width, int height)
      : dc_(dc), saved_(SaveDC(dc)) {
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, logicalWidth, logicalHeight, nullptr);
    SetViewportExtEx(dc, std::max(1, width), std::max(1, height), nullptr);
  }
  ~LogicalCanvas() { if (saved_) RestoreDC(dc_, saved_); }
  LogicalCanvas(const LogicalCanvas &) = delete;
  LogicalCanvas &operator=(const LogicalCanvas &) = delete;
 private:
  HDC dc_;
  int saved_;
};

// Give GDI+ an explicit pixel DC and transfer the full logical transform. This
// keeps sprites in the same coordinate space as native high-resolution text,
// including world transforms used by the aircraft fallback and info scrolling.
class ImageCanvas {
 public:
  explicit ImageCanvas(HDC dc) : dc_(dc), saved_(SaveDC(dc)) {
    POINT basis[] = {{0, 0}, {10000, 0}, {0, 10000}};
    LPtoDP(dc, basis, 3);
    SetGraphicsMode(dc, GM_ADVANCED);
    const XFORM identity{1, 0, 0, 1, 0, 0};
    SetWorldTransform(dc, &identity);
    SetMapMode(dc, MM_TEXT);
    SetWindowOrgEx(dc, 0, 0, nullptr);
    SetViewportOrgEx(dc, 0, 0, nullptr);
    graphics_ = std::make_unique<Gdiplus::Graphics>(dc);
    Gdiplus::Matrix matrix(
        (basis[1].x - basis[0].x) / 10000.0f,
        (basis[1].y - basis[0].y) / 10000.0f,
        (basis[2].x - basis[0].x) / 10000.0f,
        (basis[2].y - basis[0].y) / 10000.0f,
        static_cast<Gdiplus::REAL>(basis[0].x), static_cast<Gdiplus::REAL>(basis[0].y));
    graphics_->SetPageUnit(Gdiplus::UnitPixel);
    graphics_->SetTransform(&matrix);
  }
  ~ImageCanvas() { graphics_.reset(); if (saved_) RestoreDC(dc_, saved_); }
  Gdiplus::Graphics &graphics() { return *graphics_; }
  ImageCanvas(const ImageCanvas &) = delete;
  ImageCanvas &operator=(const ImageCanvas &) = delete;
 private:
  HDC dc_;
  int saved_;
  std::unique_ptr<Gdiplus::Graphics> graphics_;
};
}  // namespace plane_pet_dpi
