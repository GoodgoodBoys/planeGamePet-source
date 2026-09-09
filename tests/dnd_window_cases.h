template <typename Check>
void RunDndWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_ui;
  HDC metrics = GetDC(window);
  HFONT font = plane_pet_text::CreatePetFont(14, FW_BOLD);
  const auto old = SelectObject(metrics, font);
  SIZE title{}, countdown{};
  GetTextExtentPoint32W(metrics, L"对方已开启勿扰...", 10, &title);
  GetTextExtentPoint32W(metrics, L"3s", 2, &countdown);
  SelectObject(metrics, old); DeleteObject(font); ReleaseDC(window, metrics);
  check("dnd_notice_title_fits", title.cx <= kDndNoticeLayout.message.right - kDndNoticeLayout.message.left);
  check("dnd_notice_countdown_fits", countdown.cx <= kDndNoticeLayout.countdown.right - kDndNoticeLayout.countdown.left);
  check("dnd_tooltip_created", IsWindow(gStatusTooltip) != FALSE);
  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    for (int state = 0; state < 6; ++state) {
      const bool online = state < 4, self = state == 2 || state == 3 || state == 5;
      const bool peer = state == 1 || state == 3;
      for (bool bubbles : {false, true}) {
        gClient.SeedDndRenderCase(self, peer, online, bubbles, false);
        gClient.ChangeDpi(dpi);
        RECT rect{}; GetClientRect(window, &rect);
        const auto label = "dnd_state_" + std::to_string(state) + "_" +
            std::to_string(bubbles) + "_" + std::to_string(dpi);
        const auto badges = gClient.StatusBadges();
        check((label + "_state").c_str(), badges[0].visible == (self || !online) &&
            badges[0].moon == self && badges[0].offline == !online &&
            badges[1].visible == online && (!online || badges[1].moon == peer));
        check((label + "_image").c_str(), rect.right == kPetWidth && rect.bottom == kPetHeight &&
            SaveReleaseFixture(directory / (Wide(label) + L".png"), rect.right, rect.bottom,
                [&](HDC dc) { gClient.Draw(dc, rect); }));
      }
    }
    gClient.SeedDndRenderCase(false, true, true, false, true);
    gClient.SetToolbarTestState(true, true);
    RECT before{}, rect{}; GetWindowRect(window, &before); GetClientRect(window, &rect);
    const auto label = "dnd_notice_" + std::to_string(dpi);
    check((label + "_image").c_str(), SaveReleaseFixture(directory / (Wide(label) + L".png"),
        rect.right, rect.bottom, [&](HDC dc) { gClient.Draw(dc, rect); }));
    check((label + "_emotes").c_str(), gClient.IsQuickEmoteBarVisible() && gClient.CanSendQuickEmote());
    gClient.OnLeftButtonDown(220, 82);
    RECT after{}; GetWindowRect(window, &after);
    check((label + "_cancel").c_str(), !gClient.HasDndNotice() && EqualRect(&before, &after));
    // Cover the self-status branch too (not just the cancellable peer notice).
    // Inspect real rendered ink so stale text coordinates cannot pass via an
    // otherwise correct layout constant or successful image-file creation.
    int statusIndex = 0;
    for (const wchar_t *status : {L"已开启勿扰", L"已恢复接收邀请", L"状态同步中...", L"设置仅本次生效，保存失败"}) {
      gClient.ShowDndNotice(status, false);
      const auto statusLabel = "dnd_self_status_" + std::to_string(statusIndex++) + "_" + std::to_string(dpi);
      bool fits = false, centered = false;
      check((statusLabel + "_image").c_str(), SaveReleaseFixture(
          directory / (Wide(statusLabel) + L".png"), rect.right, rect.bottom, [&](HDC dc) {
        HFONT statusFont = plane_pet_text::CreatePetFont(kDndStatusFontEmHeight, FW_BOLD);
        const auto previousFont = SelectObject(dc, statusFont);
        SIZE extent{};
        fits = GetTextExtentPoint32W(dc, status, static_cast<int>(wcslen(status)), &extent) &&
            extent.cx <= kDndStatusTextRect.right - kDndStatusTextRect.left &&
            extent.cy <= kDndStatusTextRect.bottom - kDndStatusTextRect.top;
        SelectObject(dc, previousFont); DeleteObject(statusFont);
        gClient.Draw(dc, rect); GdiFlush();
        int left = rect.right, right = -1, top = rect.bottom, bottom = -1;
        for (int y = kDndStatusTextRect.top; y < kDndStatusTextRect.bottom; ++y) {
          for (int x = kDndStatusTextRect.left; x < kDndStatusTextRect.right; ++x) {
            const auto color = GetPixel(dc, x, y);
            if (color != CLR_INVALID && GetRValue(color) > 210 && GetGValue(color) > 210 && GetBValue(color) > 210) {
              left = std::min(left, x); right = std::max(right, x);
              top = std::min(top, y); bottom = std::max(bottom, y);
            }
          }
        }
        centered = right >= left && bottom - top >= 13 &&
            std::abs((top + bottom) - (kDndNoticeLayout.panel.top + kDndNoticeLayout.panel.bottom)) <= 6 &&
            std::abs((left + right) - (kDndNoticeLayout.panel.left + kDndNoticeLayout.panel.right)) <= 6;
      }));
      check((statusLabel + "_fits").c_str(), fits);
      check((statusLabel + "_ink_centered").c_str(), centered);
      GetWindowRect(window, &after);
      check((statusLabel + "_window_stable").c_str(), EqualRect(&before, &after));
    }
    gClient.ClearDndNotice();
  }
  gClient.SeedDpiRenderCase(false);
  check("dnd_help_image", SaveReleaseFixture(directory / L"help-full.png", 640, kHelpContentHeight,
      [&](HDC dc) { gClient.DrawHelpWindow(dc, RECT{0, 0, 640, kHelpContentHeight}); }));
}
