#pragma once

#include <windows.h>
#include <string>

namespace plane_pet_text {

// Only for text on the pet's opaque cards/buttons. The pet uses physical
// pixels, and is copied 1:1 to its color-keyed HWND. Never apply this renderer
// to transparent sprite edges, or stretch/rotate its rendered text bitmap.
// Negative lfHeight specifies the em height, not the cell including leading.
inline HFONT CreatePetFont(int emPixels, int weight = FW_NORMAL) {
  return CreateFontW(-emPixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
}

inline void PetText(HDC dc, const std::wstring &text, RECT rect, int emPixels,
                 COLORREF color, int weight = FW_NORMAL,
                 UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
  const HFONT font = CreatePetFont(emPixels, weight);
  if (!font) return;
  const HGDIOBJ oldFont = SelectObject(dc, font);
  const int oldMode = SetBkMode(dc, TRANSPARENT);
  const COLORREF oldColor = SetTextColor(dc, color);
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
  SetTextColor(dc, oldColor);
  SetBkMode(dc, oldMode);
  SelectObject(dc, oldFont);
  DeleteObject(font);
}

}  // namespace plane_pet_text
