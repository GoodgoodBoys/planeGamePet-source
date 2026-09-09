bool SaveHelpViewportFixture(const std::filesystem::path &path, HWND window) {
  RECT client{}; GetClientRect(window, &client);
  return SaveReleaseFixture(path, client.right, client.bottom, [&](HDC dc) {
    const int body = client.bottom - HelpFooterPixels(window);
    const int page = InfoPageHeight(window);
    {
      plane_pet_dpi::LogicalCanvas canvas(dc, kHelpWidth, page, client.right, body);
      SetWindowOrgEx(dc, 0, gInfoScrollOffset, nullptr);
      IntersectClipRect(dc, 0, gInfoScrollOffset, kHelpWidth, gInfoScrollOffset + page);
      gClient.DrawHelpWindow(dc, RECT{0, gInfoScrollOffset, kHelpWidth, gInfoScrollOffset + page}, gHelpDetailed);
    }
    gClient.DrawHelpFooter(dc, RECT{0, body, client.right, client.bottom});
    gClient.DrawHelpNavigation(dc, HelpNavigationRect(window), gInfoDpi, gHelpDetailed, false, false);
  });
}

template <typename Check>
void RunHelpWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_help;
  std::ofstream textMetrics(directory / L"help-text-metrics.txt");
  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    bool bodiesFit = true, titlesFit = true, spacingFits = true;
    check(("help_readable_full_image_" + std::to_string(dpi)).c_str(), SaveReleaseFixture(
        directory / (L"help-full-" + std::to_wstring(dpi) + L".png"),
        plane_pet_dpi::Scale(kWidth, dpi), plane_pet_dpi::Scale(kContentHeight, dpi), [&](HDC dc) {
      plane_pet_dpi::LogicalCanvas canvas(dc, kWidth, kContentHeight,
          plane_pet_dpi::Scale(kWidth, dpi), plane_pet_dpi::Scale(kContentHeight, dpi));
      gClient.DrawHelpWindow(dc, RECT{0, 0, kWidth, kContentHeight});
      const auto fits = [&](const wchar_t *text, int size, int weight, int width, int height, bool wrap,
                            int *measuredResult = nullptr) {
        HFONT font = CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
        if (!font) return false;
        const auto old = SelectObject(dc, font);
        RECT measured{0, 0, width, 0};
        const int measuredHeight = DrawTextW(dc, text, -1, &measured,
            DT_CALCRECT | DT_LEFT | (wrap ? DT_WORDBREAK : DT_SINGLELINE));
        if (measuredResult) *measuredResult = measuredHeight;
        SelectObject(dc, old); DeleteObject(font);
        return measuredHeight > 0 && measuredHeight <= height && measured.right <= width;
      };
      titlesFit = fits(kTitle, 32, FW_BOLD, kWidth - 52, 42, false) &&
          fits(kSubtitle, 20, FW_NORMAL, kWidth - 52, 32, false);
      for (const auto &section : kSections) {
        titlesFit = titlesFit && fits(section.heading, 23, FW_BOLD, kWidth - 52, 34, false);
        const int inset = &section - kSections == kDetailedStatusSection ? kDetailedStatusInset : 0;
        int measuredHeight = 0;
        const bool bodyFit = fits(section.body, kBodyFontSize, FW_NORMAL, kWidth - 66,
                                 section.bodyHeight - inset, true, &measuredHeight);
        textMetrics << (&section - kSections + 1) << ' ' << dpi << ' ' << measuredHeight << '\n';
        const int clearance = section.bodyHeight - inset - measuredHeight;
        spacingFits = spacingFits && clearance >= 16 && clearance <= 64;
        bodiesFit = bodiesFit && bodyFit;
        if (!bodyFit) check(("help_body_overflow_section_" + std::to_string(&section - kSections + 1) +
                            "_dpi_" + std::to_string(dpi)).c_str(), false);
      }
      for (const auto &section : kBriefSections) {
        titlesFit = titlesFit && fits(section.heading, 23, FW_BOLD, kWidth - 52, 34, false);
        bodiesFit = bodiesFit && fits(section.body, kBodyFontSize, FW_NORMAL, kWidth - 66,
                                     section.bodyHeight, true);
      }
      for (const auto *label : kBriefStatusLabels)
        bodiesFit = bodiesFit && fits(label, kBodyFontSize, FW_NORMAL,
                                     kBriefStatusPitch - 44, kBriefStatusRowHeight, false);
      bodiesFit = bodiesFit && fits(kBriefNotice, kBodyFontSize, FW_NORMAL, kWidth - 66, kBriefNoticeHeight, true);
      titlesFit = titlesFit && fits(kBriefTitle, 32, FW_BOLD, kWidth - 52, 42, false) &&
          fits(kBriefSubtitle, 20, FW_NORMAL, kWidth - 52, 32, false) &&
          fits(kDetailLink, 20, FW_BOLD, kNavigationWidth, kNavigationHeight, false) &&
          fits(kBackLink, 20, FW_BOLD, kNavigationWidth, kNavigationHeight, false);
    }));
    check(("help_all_bodies_fit_no_clipping_" + std::to_string(dpi)).c_str(), bodiesFit);
    check(("help_headings_and_intro_fit_" + std::to_string(dpi)).c_str(), titlesFit);
    check(("help_detailed_section_spacing_readable_not_excessive_" + std::to_string(dpi)).c_str(), spacingFits);
    bool green = false, red = false, crescent = false, dark = false;
    check(("help_brief_status_artwork_image_" + std::to_string(dpi)).c_str(), SaveReleaseFixture(
        directory / (L"help-status-legend-" + std::to_wstring(dpi) + L".png"),
        plane_pet_dpi::Scale(kWidth, dpi), plane_pet_dpi::Scale(60, dpi), [&](HDC dc) {
      plane_pet_dpi::LogicalCanvas canvas(dc, kWidth, 60,
          plane_pet_dpi::Scale(kWidth, dpi), plane_pet_dpi::Scale(60, dpi));
      HBRUSH background = CreateSolidBrush(RGB(9, 13, 22));
      RECT area{0, 0, kWidth, 60}; FillRect(dc, &area, background); DeleteObject(background);
      gClient.DrawBriefStatusLegend(dc, 12); GdiFlush();
      green = GetPixel(dc, 50, 25) == RGB(54, 221, 148);
      red = GetPixel(dc, 240, 25) == RGB(255, 59, 77);
      for (int y = 16; y < 34; ++y) for (int x = 421; x < 439; ++x) {
        const auto color = GetPixel(dc, x, y);
        crescent = crescent || color == RGB(255, 255, 255);
        dark = dark || color == RGB(0, 0, 0);
      }
    }));
    check(("help_brief_uses_green_red_and_white_moon_artwork_" + std::to_string(dpi)).c_str(),
        green && red && crescent && dark);
  }
  check("help_brief_status_row_within_section", kBriefStatusOffset + kBriefStatusRowHeight <=
      kBriefSections[kBriefStatusSection].bodyHeight && 38 + 3 * kBriefStatusPitch <= kWidth - 28);
  check("help_detailed_status_row_separate_from_body", kDetailedStatusInset >= kBriefStatusRowHeight + 8 &&
      kSections[kDetailedStatusSection].bodyHeight > kDetailedStatusInset);
  check("help_complete_five_brief_thirteen_detailed_sections",
      std::size(kBriefSections) == 5 && std::size(kSections) == 13);
  // Full-size section fixtures make every paragraph reviewable without
  // shrinking the complete, long document to an unreadable thumbnail.
  int sectionTop = kFirstSectionY;
  for (size_t index = 0; index < std::size(kSections); ++index) {
    const int height = kSectionSpacing + kSections[index].bodyHeight;
    check(("help_detailed_section_image_" + std::to_string(index + 1)).c_str(), SaveReleaseFixture(
        directory / (L"help-section-" + std::to_wstring(index + 1) + L".png"),
        kWidth, height, [&](HDC dc) {
      const int saved = SaveDC(dc);
      SetWindowOrgEx(dc, 0, sectionTop, nullptr);
      gClient.DrawHelpWindow(dc, RECT{0, sectionTop, kWidth, sectionTop + height});
      RestoreDC(dc, saved);
    }));
    sectionTop += height;
  }
  ShowInfoWindow(window, InfoWindowMode::Help);
  check("help_rewritten_actual_window_opens", gInfoWindow && IsWindowVisible(gInfoWindow));
  if (gInfoWindow) {
    check("help_defaults_to_brief", !gHelpDetailed && gInfoScrollOffset == 0 &&
        InfoContentHeight() == kBriefContentHeight && gHelpNavigation && IsWindowVisible(gHelpNavigation));
    const HWND sameWindow = gInfoWindow;
    RECT before{}, after{}; GetWindowRect(gInfoWindow, &before);
    check("help_brief_normal_page_fits_without_scroll", kBriefContentHeight <= kHelpViewportHeight - kFooterHeight);
    check("help_brief_initial_view", SaveHelpViewportFixture(directory / L"help-brief-initial.png", gInfoWindow));
    RECT navigation = HelpNavigationRect(gInfoWindow), client{}; GetClientRect(gInfoWindow, &client);
    check("help_detail_button_bottom_right_with_margin", navigation.right < client.right &&
        navigation.bottom < client.bottom && navigation.left > client.right / 2 &&
        navigation.top >= client.bottom - HelpFooterPixels(gInfoWindow));
    // Exercise the real native button, including pointer-release cancellation.
    SendMessageW(gHelpNavigation, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    SendMessageW(gHelpNavigation, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-10, -10));
    SendMessageW(gHelpNavigation, WM_LBUTTONUP, 0, MAKELPARAM(-10, -10));
    check("help_release_outside_does_not_navigate", !gHelpDetailed);
    SendMessageW(gHelpNavigation, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    SendMessageW(gHelpNavigation, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    check("help_pointer_click_opens_details", gHelpDetailed && gInfoScrollOffset == 0);
    check("help_details_initial_view", SaveHelpViewportFixture(directory / L"help-details-initial.png", gInfoWindow));
    GetWindowRect(gInfoWindow, &after);
    check("help_navigation_preserves_window_and_geometry", gInfoWindow == sameWindow && EqualRect(&before, &after));
    const int expectedBottom = std::max(0, kContentHeight - InfoPageHeight(gInfoWindow));
    bool pageScrollsForward = true;
    int pages = 0;
    while (gInfoScrollOffset < expectedBottom && pages++ < 100) {
      const int previous = gInfoScrollOffset;
      SendMessageW(gInfoWindow, WM_VSCROLL, SB_PAGEDOWN, 0);
      if (gInfoScrollOffset <= previous) { pageScrollsForward = false; break; }
    }
    check("help_long_document_page_scroll_reaches_end", pageScrollsForward &&
        pages > 1 && pages < 100 && gInfoScrollOffset == expectedBottom);
    SendMessageW(gInfoWindow, WM_VSCROLL, SB_BOTTOM, 0);
    check("help_final_section_reachable", gInfoScrollOffset == expectedBottom &&
        InfoContentHeight() == kContentHeight);
    check("help_bottom_repaints", RedrawWindow(gInfoWindow, nullptr, nullptr,
        RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
    navigation = HelpNavigationRect(gInfoWindow);
    RECT actualButton{}; GetWindowRect(gHelpNavigation, &actualButton);
    MapWindowPoints(HWND_DESKTOP, gInfoWindow, reinterpret_cast<POINT *>(&actualButton), 2);
    check("help_return_button_stays_visible_after_scroll", EqualRect(&navigation, &actualButton) &&
        navigation.top >= client.bottom - HelpFooterPixels(gInfoWindow) && navigation.left < client.right / 2);
    check("help_details_bottom_view", SaveHelpViewportFixture(directory / L"help-details-bottom.png", gInfoWindow));
    check("help_last_page_image", SaveReleaseFixture(directory / L"help-last-page.png", kWidth, 740, [&](HDC dc) {
      const int scroll = std::max(0, kContentHeight - 740);
      const int saved = SaveDC(dc); SetWindowOrgEx(dc, 0, scroll, nullptr);
      gClient.DrawHelpWindow(dc, RECT{0, scroll, kWidth, scroll + 740});
      RestoreDC(dc, saved);
    }));
    SendMessageW(gInfoWindow, WM_VSCROLL, SB_TOP, 0);
    check("help_scroll_back_to_start", gInfoScrollOffset == 0);
    SendMessageW(gHelpNavigation, BM_CLICK, 0, 0);
    check("help_return_button_goes_back_to_brief", !gHelpDetailed && gInfoScrollOffset == 0);
    SetFocus(gInfoWindow);
    MSG key{}; key.hwnd = gInfoWindow; key.message = WM_KEYDOWN; key.wParam = VK_TAB;
    check("help_tab_focuses_navigation", HandleHelpDialogMessage(&key) && GetFocus() == gHelpNavigation);
    key.hwnd = gHelpNavigation; key.wParam = VK_RETURN;
    check("help_enter_opens_details", HandleHelpDialogMessage(&key) && gHelpDetailed);
    key.wParam = VK_ESCAPE;
    check("help_escape_returns_to_brief", HandleHelpDialogMessage(&key) && !gHelpDetailed);
    MSG unrelated{}; unrelated.hwnd = window; unrelated.message = WM_KEYDOWN; unrelated.wParam = VK_ESCAPE;
    check("help_keyboard_route_ignores_game_and_pet", !HandleHelpDialogMessage(&unrelated));
    SendMessageW(gHelpNavigation, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(gHelpNavigation, WM_KEYUP, VK_SPACE, 0);
    check("help_space_activates_focused_native_button", gHelpDetailed);
    SendMessageW(gHelpNavigation, BM_CLICK, 0, 0);
    SendMessageW(gHelpNavigation, BM_CLICK, 0, 0);
    SendMessageW(gInfoWindow, WM_VSCROLL, SB_BOTTOM, 0);
    ShowInfoWindow(window, InfoWindowMode::Help);
    check("help_menu_reopens_brief_without_new_window", gInfoWindow == sameWindow && !gHelpDetailed && gInfoScrollOffset == 0);
    const DWORD handlesBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD usersBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 40; ++i) SendMessageW(gHelpNavigation, BM_CLICK, 0, 0);
    check("help_repeated_switch_no_window_or_handle_leak", !gHelpDetailed && gInfoWindow == sameWindow &&
        GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handlesBefore &&
        GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= usersBefore);

    const UINT previousDpi = gInfoDpi;
    for (const UINT dpi : {96U, 120U, 144U, 192U}) {
      const auto named = [&](const char *label, bool result) {
        check((std::string(label) + "_" + std::to_string(dpi)).c_str(), result);
      };
      RECT desired{30, 30, 30 + plane_pet_dpi::Scale(kWidth + 20, dpi),
                            30 + plane_pet_dpi::Scale(740 + 30, dpi)};
      SendMessageW(gInfoWindow, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&desired));
      SetHelpPage(gInfoWindow, false);
      named("help_brief_viewport", SaveHelpViewportFixture(directory / (L"help-brief-" + std::to_wstring(dpi) + L".png"), gInfoWindow));
      GetClientRect(gInfoWindow, &client); GetWindowRect(gInfoWindow, &before);
      navigation = HelpNavigationRect(gInfoWindow);
      named("help_dpi_button_within_client", navigation.left > 0 && navigation.right < client.right &&
          navigation.top > client.bottom - HelpFooterPixels(gInfoWindow) && navigation.bottom < client.bottom);
      SendMessageW(gHelpNavigation, BM_CLICK, 0, 0); GetWindowRect(gInfoWindow, &after);
      named("help_dpi_navigation_preserves_window", gHelpDetailed && EqualRect(&before, &after));
      SendMessageW(gInfoWindow, WM_VSCROLL, SB_BOTTOM, 0);
      named("help_detailed_viewport", SaveHelpViewportFixture(directory / (L"help-details-" + std::to_wstring(dpi) + L".png"), gInfoWindow));
      named("help_dpi_last_section_accessible", gInfoScrollOffset == std::max(0, kContentHeight - InfoPageHeight(gInfoWindow)));
    }
    // A monitor-limited short window scrolls only the body; both links remain reachable.
    RECT shortWindow{30, 30, 420, 330};
    SendMessageW(gInfoWindow, WM_DPICHANGED, MAKELONG(96, 96), reinterpret_cast<LPARAM>(&shortWindow));
    SetHelpPage(gInfoWindow, false);
    SendMessageW(gInfoWindow, WM_VSCROLL, SB_BOTTOM, 0);
    navigation = HelpNavigationRect(gInfoWindow); GetClientRect(gInfoWindow, &client);
    check("help_short_window_brief_scroll_and_link_reachable", gInfoScrollOffset > 0 &&
        navigation.bottom < client.bottom && navigation.top >= client.bottom - HelpFooterPixels(gInfoWindow));
    SendMessageW(gHelpNavigation, BM_CLICK, 0, 0);
    check("help_short_window_still_navigates", gHelpDetailed && gInfoScrollOffset == 0);
    SendMessageW(gInfoWindow, WM_CLOSE, 0, 0);
    gInfoDpi = previousDpi;
  }
  check("help_close_keeps_pet", !gInfoWindow && !gHelpNavigation && IsWindow(window));
  ShowInfoWindow(window, InfoWindowMode::Help);
  check("help_fresh_open_resets_brief", gInfoWindow && !gHelpDetailed && gInfoScrollOffset == 0);
  if (gInfoWindow) SendMessageW(gInfoWindow, kEmergencyHideMessage, 0, 0);
  check("help_emergency_hide_closes_both_pages", !gInfoWindow && !gHelpNavigation && IsWindow(window));
}
