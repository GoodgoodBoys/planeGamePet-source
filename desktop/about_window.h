#pragma once

#include <windows.h>
#include <algorithm>
#include <new>
#include <string>
#include "about_content.h"
#include "../common/app_version.h"
#include "../common/windows_dpi.h"

// An isolated, modeless reader: no networking, timers, consent changes, or
// dependency on files next to the EXE. Native controls handle focus and scrolling.
namespace plane_pet_about {
inline constexpr wchar_t kClassName[] = L"PlanePetAboutWindow";
inline constexpr int kPrivacyLink = 6201, kLicensesLink = 6202, kBackLink = 6203;
inline constexpr int kReader = 6204;
inline constexpr int kHomeWidth = 380, kDocumentWidth = 440;
inline constexpr int kHomeHeight = 264, kDocumentHeight = 560;
inline constexpr int kBodyFontHeight = 13, kTitleFontHeight = 20, kLinkFontHeight = 13;
inline constexpr int kMargin = 24, kFooterHeight = 44, kLinkHeight = 28;
inline constexpr int kIconSize = 40, kDetailsValueX = 88;
inline constexpr COLORREF kBackground = RGB(11, 16, 26);
inline constexpr COLORREF kForeground = RGB(241, 246, 255);
inline constexpr COLORREF kSecondary = RGB(184, 198, 218);
inline constexpr COLORREF kLinkColor = RGB(120, 232, 255);
inline HWND gWindow = nullptr;
enum class Page { Home, Privacy, Licenses };

struct State {
  HWND window = nullptr, privacy = nullptr, licenses = nullptr, back = nullptr, reader = nullptr;
  UINT dpi = 96;
  Page page = Page::Home;
  HFONT bodyFont = nullptr, titleFont = nullptr, linkFont = nullptr;
  HICON icon = nullptr;
  HBRUSH background = nullptr;
  RECT homeFrame{};
  ~State() {
    if (bodyFont) DeleteObject(bodyFont);
    if (titleFont) DeleteObject(titleFont);
    if (linkFont) DeleteObject(linkFont);
    if (background) DeleteObject(background);
    if (icon) DestroyIcon(icon);
  }
};

inline State *GetState(HWND window = gWindow) {
  return window ? reinterpret_cast<State *>(GetWindowLongPtrW(window, GWLP_USERDATA)) : nullptr;
}

inline std::wstring EmailLabel() {
  return *kFeedbackEmail ? kFeedbackEmail : kMissingEmail;
}

inline void ReplaceAll(std::wstring &text, const std::wstring &from, const std::wstring &to) {
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::wstring::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

inline std::wstring ReadDocument(int id) {
  const HMODULE module = GetModuleHandleW(nullptr);
  const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
  const DWORD size = resource ? SizeofResource(module, resource) : 0;
  const auto *bytes = resource ? static_cast<const char *>(LockResource(LoadResource(module, resource))) : nullptr;
  if (!bytes || !size || size > 2 * 1024 * 1024)
    return L"文档暂时无法读取，请使用完整的程序版本。";
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes,
                                       static_cast<int>(size), nullptr, 0);
  if (!length) return L"文档编码无效，请使用完整的程序版本。";
  std::wstring source(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(size), source.data(), length);
  if (!source.empty() && source.front() == 0xfeff) source.erase(0, 1);
  ReplaceAll(source, L"{{FEEDBACK_EMAIL}}", EmailLabel());
  ReplaceAll(source, L"反馈邮箱：暂未设置", L"反馈邮箱：" + EmailLabel());
  if (*kFeedbackEmail)
    ReplaceAll(source, L"专用联系邮箱尚未设置。", L"专用联系邮箱：" + EmailLabel() + L"。");
  ReplaceAll(source, L"{{APP_VERSION}}", plane_pet_version::kWideString);
  // The documents use simple Markdown headings; display headings without their
  // markup. Leave copyright/license paragraphs verbatim (apart from CRLF).
  std::wstring result;
  for (size_t start = 0; start < source.size();) {
    const size_t end = source.find(L'\n', start);
    std::wstring line = source.substr(start, end == std::wstring::npos ? end : end - start);
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    size_t marker = 0;
    while (marker < line.size() && line[marker] == L'#') ++marker;
    if (marker > 0 && marker <= 6 && marker < line.size() && line[marker] == L' ')
      line.erase(0, marker + 1);
    result += line + L"\r\n";
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  return result;
}

inline void RefreshFonts(State &state) {
  const auto font = [&](int height, int weight, bool underline = false) {
    return CreateFontW(-plane_pet_dpi::Scale(height, state.dpi), 0, 0, 0, weight,
        FALSE, underline, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
  };
  HFONT body = font(kBodyFontHeight, FW_NORMAL), title = font(kTitleFontHeight, FW_BOLD),
        link = font(kLinkFontHeight, FW_NORMAL, true);
  if (!body || !title || !link) {
    if (body) DeleteObject(body);
    if (title) DeleteObject(title);
    if (link) DeleteObject(link);
    return;
  }
  SendMessageW(state.reader, WM_SETFONT, reinterpret_cast<WPARAM>(body), FALSE);
  for (HWND control : {state.privacy, state.licenses, state.back})
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(link), FALSE);
  if (state.bodyFont) DeleteObject(state.bodyFont);
  if (state.titleFont) DeleteObject(state.titleFont);
  if (state.linkFont) DeleteObject(state.linkFont);
  state.bodyFont = body; state.titleFont = title; state.linkFont = link;
  const int size = plane_pet_dpi::Scale(kIconSize, state.dpi);
  const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                                               IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
  if (state.icon) DestroyIcon(state.icon);
  state.icon = icon;
}

inline void Layout(State &state) {
  RECT client{}; GetClientRect(state.window, &client);
  const auto px = [&](int value) { return plane_pet_dpi::Scale(value, state.dpi); };
  // Four pixels of focus padding put link text on the same 24-DIP grid as
  // the headings and contact labels. Keep the full button hit area usable.
  const int margin = std::min<int>(px(kMargin - 4), client.right / 8);
  const int footer = px(kFooterHeight), linkHeight = px(kLinkHeight);
  const int linkTop = std::max(0L, client.bottom - footer);
  const auto place = [](HWND control, int x, int y, int width, int height) {
    SetWindowPos(control, nullptr, x, y, std::max(1, width), std::max(1, height),
                 SWP_NOZORDER | SWP_NOACTIVATE);
  };
  place(state.privacy, margin, linkTop, px(100), linkHeight);
  place(state.licenses, margin + px(112), linkTop, px(120), linkHeight);
  place(state.back, margin, linkTop, client.right - margin * 2, linkHeight);
  // Native EDIT padding brings the document text back onto the same grid.
  const int readerMargin = std::min<int>(px(kMargin - 8), client.right / 8);
  place(state.reader, readerMargin, px(80), client.right - readerMargin * 2,
        std::max(1L, client.bottom - px(80) - footer - px(12)));
  RECT textRect{}; GetClientRect(state.reader, &textRect);
  InflateRect(&textRect, -px(8), -px(6));
  SendMessageW(state.reader, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&textRect));
}

inline RECT SizedFrame(HWND window, UINT dpi, Page page, const RECT &anchor) {
  const DWORD style = WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  const bool home = page == Page::Home;
  RECT frame{0, 0, plane_pet_dpi::Scale(home ? kHomeWidth : kDocumentWidth, dpi),
             plane_pet_dpi::Scale(home ? kHomeHeight : kDocumentHeight, dpi)};
  plane_pet_dpi::Adjust(frame, style, WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, dpi);
  MONITORINFO monitor{sizeof(MONITORINFO), {}, {}, 0};
  if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
    monitor.rcWork = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  const LONG width = std::min(frame.right - frame.left, std::max(1L, monitor.rcWork.right - monitor.rcWork.left - 16));
  const LONG tall = std::min(frame.bottom - frame.top, std::max(1L, monitor.rcWork.bottom - monitor.rcWork.top - 16));
  return plane_pet_dpi::FitWorkArea(RECT{anchor.left, anchor.top, anchor.left + width, anchor.top + tall}, monitor.rcWork);
}

inline void SetPage(State &state, Page page) {
  RECT anchor{}; GetWindowRect(state.window, &anchor);
  if (state.page == Page::Home) state.homeFrame = anchor;
  state.page = page;
  const bool home = page == Page::Home;
  // Hide first so the old reader does not flash its contents while navigating.
  for (HWND control : {state.privacy, state.licenses, state.back, state.reader})
    ShowWindow(control, SW_HIDE);
  if (!home) {
    const auto text = ReadDocument(page == Page::Privacy ? kPrivacyResource : kLicensesResource);
    SetWindowTextW(state.reader, text.c_str());
    SendMessageW(state.reader, EM_SETSEL, 0, 0);
    SendMessageW(state.reader, EM_SCROLLCARET, 0, 0);
  }
  SetWindowTextW(state.window, home ? L"PlanePet - 关于" :
      page == Page::Privacy ? L"PlanePet - 隐私与数据说明" : L"PlanePet - 第三方组件与许可");
  const RECT frame = SizedFrame(state.window, state.dpi, page,
                               home ? state.homeFrame : anchor);
  SetWindowPos(state.window, nullptr, frame.left, frame.top, frame.right - frame.left,
               frame.bottom - frame.top, SWP_NOZORDER | SWP_NOACTIVATE);
  Layout(state);
  ShowWindow(state.privacy, home ? SW_SHOW : SW_HIDE);
  ShowWindow(state.licenses, home ? SW_SHOW : SW_HIDE);
  ShowWindow(state.back, home ? SW_HIDE : SW_SHOW);
  ShowWindow(state.reader, home ? SW_HIDE : SW_SHOW);
  SetFocus(home ? state.privacy : state.back);
  RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

inline void Paint(State &state, HDC dc) {
  RECT client{}; GetClientRect(state.window, &client);
  FillRect(dc, &client, state.background);
  SetBkMode(dc, TRANSPARENT);
  const auto text = [&](const std::wstring &value, int x, int y, int height,
                        HFONT font, COLORREF color, UINT flags = DT_LEFT | DT_WORDBREAK) {
    const auto old = SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT rect{plane_pet_dpi::Scale(x, state.dpi), plane_pet_dpi::Scale(y, state.dpi),
              client.right - plane_pet_dpi::Scale(kMargin, state.dpi), plane_pet_dpi::Scale(y + height, state.dpi)};
    DrawTextW(dc, value.c_str(), -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, old);
  };
  const auto divider = [&](int y) {
    const auto pen = CreatePen(PS_SOLID, 1, RGB(40, 53, 72));
    if (!pen) return;
    const auto old = SelectObject(dc, pen);
    MoveToEx(dc, plane_pet_dpi::Scale(kMargin, state.dpi), y, nullptr);
    LineTo(dc, client.right - plane_pet_dpi::Scale(kMargin, state.dpi), y);
    SelectObject(dc, old); DeleteObject(pen);
  };
  divider(client.bottom - plane_pet_dpi::Scale(kFooterHeight + 8, state.dpi));
  if (state.page == Page::Home) {
    const int size = plane_pet_dpi::Scale(kIconSize, state.dpi);
    if (state.icon) DrawIconEx(dc, plane_pet_dpi::Scale(kMargin, state.dpi), plane_pet_dpi::Scale(24, state.dpi),
                        state.icon, size, size, 0, nullptr, DI_NORMAL);
    text(L"PlanePet", 80, 20, 28, state.titleFont, kLinkColor);
    text(std::wstring(L"版本  ") + plane_pet_version::kWideString, 80, 50, 20, state.bodyFont, kSecondary);
    text(kDescription, kMargin, 90, 42, state.bodyFont, kForeground);
    divider(plane_pet_dpi::Scale(140, state.dpi));
    text(L"开发者", kMargin, 154, 22, state.bodyFont, kSecondary);
    text(kDeveloper, kDetailsValueX, 154, 22, state.bodyFont, kForeground);
    text(L"反馈邮箱", kMargin, 184, 22, state.bodyFont, kSecondary);
    text(EmailLabel(), kDetailsValueX, 184, 22, state.bodyFont, kForeground, DT_LEFT | DT_SINGLELINE);
  } else {
    text(state.page == Page::Privacy ? L"隐私与数据说明" : L"第三方组件与许可",
         kMargin, 18, 30, state.titleFont, kLinkColor);
    text(L"可滚动阅读、选择和复制文字。", kMargin, 50, 22, state.bodyFont, kSecondary);
  }
}

inline LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  State *state = GetState(window);
  if (message == WM_NCCREATE) {
    state = new (std::nothrow) State;
    if (!state) return FALSE;
    state->window = window;
    state->dpi = plane_pet_dpi::ForWindow(window);
    state->background = CreateSolidBrush(kBackground);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (!state) return DefWindowProcW(window, message, wParam, lParam);
  switch (message) {
    case WM_CREATE: {
      const auto link = [&](int id, const wchar_t *label) {
        return CreateWindowW(L"BUTTON", label, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
      };
      state->privacy = link(kPrivacyLink, L"隐私说明 →");
      state->licenses = link(kLicensesLink, L"第三方许可 →");
      state->back = link(kBackLink, L"← 返回关于");
      state->reader = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL |
          ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL, 0, 0, 1, 1,
          window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReader)), GetModuleHandleW(nullptr), nullptr);
      if (!state->background || !state->privacy || !state->licenses || !state->back || !state->reader) return -1;
      SendMessageW(state->reader, EM_SETLIMITTEXT, 2 * 1024 * 1024, 0);
      RefreshFonts(*state);
      if (!state->bodyFont) return -1;
      SetPage(*state, Page::Home);
      return 0;
    }
    case WM_SIZE: Layout(*state); InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_DPICHANGED: {
      state->dpi = plane_pet_dpi::Normalize(HIWORD(wParam));
      RefreshFonts(*state);
      const auto *suggested = reinterpret_cast<const RECT *>(lParam);
      if (suggested) {
        const RECT frame = SizedFrame(window, state->dpi, state->page, *suggested);
        SetWindowPos(window, nullptr, frame.left, frame.top, frame.right - frame.left,
                     frame.bottom - frame.top, SWP_NOZORDER | SWP_NOACTIVATE);
        state->homeFrame = frame;
      }
      Layout(*state); RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{}; HDC target = BeginPaint(window, &ps);
      RECT rect{}; GetClientRect(window, &rect);
      HDC memory = CreateCompatibleDC(target);
      HBITMAP bitmap = memory ? CreateCompatibleBitmap(target, std::max(1L, rect.right), std::max(1L, rect.bottom)) : nullptr;
      if (bitmap) {
        const auto old = SelectObject(memory, bitmap);
        Paint(*state, memory);
        BitBlt(target, 0, 0, rect.right, rect.bottom, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old); DeleteObject(bitmap);
      } else Paint(*state, target);
      if (memory) DeleteDC(memory);
      EndPaint(window, &ps); return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      const HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, kForeground); SetBkColor(dc, kBackground);
      return reinterpret_cast<LRESULT>(state->background);
    }
    case WM_DRAWITEM: {
      const auto *item = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
      if (!item || (item->hwndItem != state->privacy && item->hwndItem != state->licenses && item->hwndItem != state->back)) break;
      FillRect(item->hDC, &item->rcItem, state->background);
      const auto old = SelectObject(item->hDC, state->linkFont);
      SetBkMode(item->hDC, TRANSPARENT);
      SetTextColor(item->hDC, (item->itemState & ODS_SELECTED) ? kForeground : kLinkColor);
      wchar_t label[64]{}; GetWindowTextW(item->hwndItem, label, 64);
      RECT rect = item->rcItem;
      InflateRect(&rect, -plane_pet_dpi::Scale(4, state->dpi), 0);
      DrawTextW(item->hDC, label, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
        RECT measured{};
        DrawTextW(item->hDC, label, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        const int height = measured.bottom - measured.top;
        RECT focus{rect.left - 2, rect.top + (rect.bottom - rect.top - height) / 2 - 2,
                   std::min(rect.right, rect.left + measured.right + 2),
                   rect.top + (rect.bottom - rect.top + height) / 2 + 2};
        DrawFocusRect(item->hDC, &focus);
      }
      SelectObject(item->hDC, old); return TRUE;
    }
    case WM_SETCURSOR:
      if (reinterpret_cast<HWND>(wParam) == state->privacy || reinterpret_cast<HWND>(wParam) == state->licenses ||
          reinterpret_cast<HWND>(wParam) == state->back) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); return TRUE;
      }
      break;
    case WM_COMMAND:
      if (HIWORD(wParam) == BN_CLICKED) {
        if (LOWORD(wParam) == kPrivacyLink) { SetPage(*state, Page::Privacy); return 0; }
        if (LOWORD(wParam) == kLicensesLink) { SetPage(*state, Page::Licenses); return 0; }
        if (LOWORD(wParam) == kBackLink) { SetPage(*state, Page::Home); return 0; }
      }
      break;
    case WM_MOUSEWHEEL:
      if (state->page != Page::Home) return SendMessageW(state->reader, message, wParam, lParam);
      break;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_NCDESTROY:
      if (gWindow == window) gWindow = nullptr;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      delete state;
      return DefWindowProcW(window, message, wParam, lParam);
    default: break;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

inline void Close() { if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow); }

inline bool Show(HWND owner) {
  if (!IsWindow(owner)) return false;
  if (!gWindow || !IsWindow(gWindow)) {
    WNDCLASSW type{}; type.lpfnWndProc = Procedure; type.hInstance = GetModuleHandleW(nullptr);
    type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    type.hIcon = static_cast<HICON>(LoadImageW(type.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_SHARED));
    type.lpszClassName = kClassName;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    RECT anchor{}; GetWindowRect(owner, &anchor);
    anchor.left = anchor.right + 8;
    const RECT frame = SizedFrame(owner, plane_pet_dpi::ForWindow(owner), Page::Home, anchor);
    gWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kClassName, L"PlanePet - 关于",
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, frame.left, frame.top,
        frame.right - frame.left, frame.bottom - frame.top, owner, nullptr, type.hInstance, nullptr);
    if (!gWindow) return false;
  } else SetPage(*GetState(), Page::Home);
  ShowWindow(gWindow, SW_SHOWNORMAL);
  SendMessageW(gWindow, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
  UpdateWindow(gWindow); SetForegroundWindow(gWindow);
  SetFocus(GetState()->privacy);
  return true;
}

inline bool HandleMessage(MSG *message) {
  State *state = GetState();
  if (!message || !state || (message->hwnd != gWindow && !IsChild(gWindow, message->hwnd))) return false;
  if (message->message == WM_KEYDOWN) {
    if (message->wParam == VK_ESCAPE) {
      if (state->page != Page::Home) SetPage(*state, Page::Home); else Close();
      return true;
    }
    if (message->wParam == VK_RETURN) {
      const HWND focus = GetFocus();
      if (focus == state->privacy || focus == state->licenses || focus == state->back)
        SendMessageW(focus, BM_CLICK, 0, 0);
      return true;
    }
    if (message->wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && GetFocus() == state->reader) {
      SendMessageW(state->reader, EM_SETSEL, 0, -1); return true;
    }
  }
  return IsDialogMessageW(gWindow, message) != FALSE;
}
}  // namespace plane_pet_about
