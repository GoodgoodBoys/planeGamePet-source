// Real native controls and the exact resources shipped in the client; isolated
// test profile, loopback discard server, no uploads or user's saved-data changes.
template <typename Check>
void RunAboutWindowCases(HWND owner, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_about;
  const auto privacy = ReadDocument(kPrivacyResource);
  const auto licenses = ReadDocument(kLicensesResource);
  check("about_developer_and_feedback_email", std::wstring(kDeveloper) == L"Ding" &&
      EmailLabel() == (*kFeedbackEmail ? kFeedbackEmail : kMissingEmail));
  check("about_contact_consistent_without_placeholder", EmailLabel() == L"planepet_public@163.com" &&
      privacy.find(EmailLabel()) != std::wstring::npos && privacy.find(L"暂未设置") == std::wstring::npos &&
      privacy.find(L"尚未设置") == std::wstring::npos);
  check("about_privacy_embedded_complete", privacy.find(L"开发者署名：Ding") != std::wstring::npos &&
      privacy.find(L"五、联系与数据请求") != std::wstring::npos &&
      privacy.find(L"不影响正常绑定和对战") != std::wstring::npos &&
      privacy.find(L"反馈邮箱：" + EmailLabel()) != std::wstring::npos &&
      privacy.find(L"# ") == std::wstring::npos && privacy.find(L"{{") == std::wstring::npos);
  check("about_licenses_include_full_runtime_texts", licenses.size() > 50000 &&
      licenses.find(L"GCC RUNTIME LIBRARY EXCEPTION") != std::wstring::npos &&
      licenses.find(L"=== gcc/COPYING.RUNTIME ===") != std::wstring::npos &&
      licenses.find(L"=== mingw-w64/COPYING.MinGW-w64-runtime.txt ===") != std::wstring::npos &&
      licenses.find(L"=== winpthreads/COPYING ===") != std::wstring::npos &&
      licenses.find(L"FITNESS FOR A PARTICULAR PURPOSE") != std::wstring::npos);
  check("about_missing_resource_has_readable_fallback", ReadDocument(32760).find(L"无法读取") != std::wstring::npos);
  const bool consent = gClient.IsTelemetryEnabled();
  const bool binding = gClient.HasBinding();
  const bool initialGame = gClient.IsGameMode();
  const auto save = [&](const std::wstring &name) {
    RECT rect{}; GetClientRect(gWindow, &rect);
    RedrawWindow(gWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    return SaveReleaseFixture(directory / name, rect.right, rect.bottom, [&](HDC dc) {
      PrintWindow(gWindow, dc, PW_CLIENTONLY);
    });
  };
  check("about_native_window_opens", Show(owner));
  if (!gWindow) return;
  const HWND original = gWindow;
  State *state = GetState();
  check("about_modeless_toolwindow", GetWindow(gWindow, GW_OWNER) == owner && IsWindowEnabled(owner) &&
      (GetWindowLongPtrW(gWindow, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) &&
      !(GetWindowLongPtrW(gWindow, GWL_EXSTYLE) & WS_EX_APPWINDOW));
  check("about_home_only_links_visible", state->page == Page::Home &&
      IsWindowVisible(state->privacy) && IsWindowVisible(state->licenses) &&
      !IsWindowVisible(state->reader) && !IsWindowVisible(state->back));
  check("about_home_image", save(L"about-home.png"));
  check("about_no_continuous_repaint", !GetUpdateRect(gWindow, nullptr, FALSE));
  RECT homeFrame{}; GetWindowRect(gWindow, &homeFrame);
  SendMessageW(state->privacy, BM_CLICK, 0, 0);
  check("about_privacy_link_navigates_same_window", gWindow == original && state->page == Page::Privacy &&
      IsWindowVisible(state->reader) && IsWindowVisible(state->back) && !IsWindowVisible(state->privacy));
  std::wstring shown(static_cast<size_t>(GetWindowTextLengthW(state->reader)) + 1, L'\0');
  GetWindowTextW(state->reader, shown.data(), static_cast<int>(shown.size())); shown.pop_back();
  check("about_privacy_reader_exact_text", shown == privacy);
  check("about_privacy_image", save(L"about-privacy.png"));
  SendMessageW(state->reader, EM_SETSEL, 0, -1);
  // Test user keyboard input. EM_REPLACESEL is a programmatic editing API and
  // is intentionally allowed even by a native read-only EDIT control.
  SendMessageW(state->reader, WM_CHAR, L'X', 0);
  SendMessageW(state->reader, WM_CHAR, VK_BACK, 0);
  check("about_reader_cannot_edit_policy", (GetWindowLongPtrW(state->reader, GWL_STYLE) & ES_READONLY) &&
      GetWindowTextLengthW(state->reader) == static_cast<int>(privacy.size()));
  SendMessageW(state->reader, EM_SETSEL, 0, 0);
  SendMessageW(gWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
  check("about_wheel_scrolls_without_clicking_reader", SendMessageW(state->reader, EM_GETFIRSTVISIBLELINE, 0, 0) > 0);
  SendMessageW(state->reader, EM_SETSEL, static_cast<WPARAM>(privacy.size()), static_cast<LPARAM>(privacy.size()));
  SendMessageW(state->reader, EM_SCROLLCARET, 0, 0);
  check("about_privacy_end_reachable", SendMessageW(state->reader, EM_GETFIRSTVISIBLELINE, 0, 0) > 0);
  check("about_privacy_bottom_image", save(L"about-privacy-bottom.png"));
  SendMessageW(state->back, BM_CLICK, 0, 0);
  RECT restored{}; GetWindowRect(gWindow, &restored);
  check("about_back_restores_compact_home", state->page == Page::Home && EqualRect(&homeFrame, &restored));
  SendMessageW(state->licenses, BM_CLICK, 0, 0);
  check("about_licenses_no_32k_truncation", state->page == Page::Licenses &&
      GetWindowTextLengthW(state->reader) == static_cast<int>(licenses.size()));
  check("about_licenses_image", save(L"about-licenses.png"));
  SendMessageW(state->reader, EM_SETSEL, static_cast<WPARAM>(licenses.size()), static_cast<LPARAM>(licenses.size()));
  SendMessageW(state->reader, EM_SCROLLCARET, 0, 0);
  check("about_license_end_reachable", SendMessageW(state->reader, EM_GETFIRSTVISIBLELINE, 0, 0) > 100);
  check("about_licenses_bottom_image", save(L"about-licenses-bottom.png"));
  MSG key{}; key.hwnd = state->reader; key.message = WM_KEYDOWN; key.wParam = VK_ESCAPE;
  check("about_escape_document_returns_home", HandleMessage(&key) && state->page == Page::Home);
  SetFocus(state->privacy); key.hwnd = state->privacy; key.wParam = VK_TAB;
  check("about_tab_reaches_second_link", HandleMessage(&key) && GetFocus() == state->licenses);
  key.hwnd = state->licenses; key.wParam = VK_RETURN;
  check("about_enter_opens_focused_link", HandleMessage(&key) && state->page == Page::Licenses);
  key.hwnd = state->back; key.wParam = VK_RETURN;
  check("about_enter_returns_home", HandleMessage(&key) && state->page == Page::Home);
  SendMessageW(state->licenses, WM_KEYDOWN, VK_SPACE, 0);
  SendMessageW(state->licenses, WM_KEYUP, VK_SPACE, 0);
  check("about_space_activates_link", state->page == Page::Licenses);
  check("about_repeat_menu_resets_home", Show(owner) && gWindow == original && state->page == Page::Home);
  key.hwnd = owner; key.wParam = VK_ESCAPE;
  check("about_keyboard_does_not_capture_pet", !HandleMessage(&key));

  const DWORD gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  const DWORD users = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  for (int i = 0; i < 30; ++i) {
    SendMessageW(state->privacy, BM_CLICK, 0, 0); SendMessageW(state->back, BM_CLICK, 0, 0);
    SendMessageW(state->licenses, BM_CLICK, 0, 0); SendMessageW(state->back, BM_CLICK, 0, 0);
  }
  check("about_navigation_no_handle_leak", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi &&
      GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= users && gWindow == original);

  for (UINT dpi : {96U, 120U, 144U, 192U}) {
    RECT desired{30, 30, 500, 600};
    SendMessageW(gWindow, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&desired));
    LOGFONTW font{}; GetObjectW(state->bodyFont, sizeof(font), &font);
    check(("about_native_font_dpi_" + std::to_string(dpi)).c_str(),
          -font.lfHeight == plane_pet_dpi::Scale(13, dpi));
    LOGFONTW titleFont{}, linkFont{};
    GetObjectW(state->titleFont, sizeof(titleFont), &titleFont);
    GetObjectW(state->linkFont, sizeof(linkFont), &linkFont);
    check(("about_reduced_title_and_link_dpi_" + std::to_string(dpi)).c_str(),
          -titleFont.lfHeight == plane_pet_dpi::Scale(20, dpi) &&
          -linkFont.lfHeight == plane_pet_dpi::Scale(13, dpi));
    const auto px = [&](int value) { return plane_pet_dpi::Scale(value, dpi); };
    RECT homeClient{}, privacyLink{}, licenseLink{};
    GetClientRect(gWindow, &homeClient);
    GetWindowRect(state->privacy, &privacyLink); GetWindowRect(state->licenses, &licenseLink);
    MapWindowPoints(HWND_DESKTOP, gWindow, reinterpret_cast<POINT *>(&privacyLink), 2);
    MapWindowPoints(HWND_DESKTOP, gWindow, reinterpret_cast<POINT *>(&licenseLink), 2);
    // Injecting WM_DPICHANGED exercises our layout but does not change the
    // monitor DPI of the OS-drawn caption. Compare the requested outer frame;
    // check padding separately against the actual client/control rectangles.
    RECT expectedHomeFrame{0, 0, px(380), px(264)}, actualHomeFrame{};
    plane_pet_dpi::Adjust(expectedHomeFrame, WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                          WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, dpi);
    GetWindowRect(gWindow, &actualHomeFrame);
    check(("about_compact_home_footer_layout_dpi_" + std::to_string(dpi)).c_str(),
        actualHomeFrame.right - actualHomeFrame.left <= expectedHomeFrame.right - expectedHomeFrame.left &&
        actualHomeFrame.bottom - actualHomeFrame.top <= expectedHomeFrame.bottom - expectedHomeFrame.top &&
        privacyLink.left > 0 && privacyLink.right + px(8) <= licenseLink.left &&
        licenseLink.right < homeClient.right - px(16) && privacyLink.top == licenseLink.top &&
        privacyLink.top >= px(206 + 8) && licenseLink.bottom <= homeClient.bottom - px(12));
    HDC textDc = GetDC(gWindow);
    const auto oldFont = SelectObject(textDc, state->bodyFont);
    const LONG emailWidth = homeClient.right - px(kMargin) - px(kDetailsValueX);
    RECT emailBounds{0, 0, emailWidth, 0};
    const std::wstring email = EmailLabel();
    DrawTextW(textDc, email.c_str(), -1, &emailBounds, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    check(("about_feedback_email_fits_dpi_" + std::to_string(dpi)).c_str(),
          emailBounds.right <= emailWidth && emailBounds.bottom <= px(22));
    RECT labelBounds{}, descriptionBounds{0, 0, homeClient.right - px(kMargin * 2), 0};
    DrawTextW(textDc, L"反馈邮箱", -1, &labelBounds, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextW(textDc, kDescription, -1, &descriptionBounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    check(("about_contact_columns_and_description_fit_dpi_" + std::to_string(dpi)).c_str(),
        labelBounds.right + px(kMargin + 8) <= px(kDetailsValueX) &&
        descriptionBounds.right <= homeClient.right - px(kMargin * 2) && descriptionBounds.bottom <= px(42));
    SelectObject(textDc, state->linkFont);
    bool linksFit = true;
    for (HWND control : {state->privacy, state->licenses}) {
      wchar_t label[64]{}; GetWindowTextW(control, label, 64);
      RECT bounds{}, area{}; GetClientRect(control, &area);
      DrawTextW(textDc, label, -1, &bounds, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
      linksFit = linksFit && bounds.right + px(8) <= area.right && bounds.bottom + px(4) <= area.bottom;
    }
    check(("about_compact_links_not_clipped_dpi_" + std::to_string(dpi)).c_str(), linksFit);
    SelectObject(textDc, oldFont); ReleaseDC(gWindow, textDc);
    check(("about_home_dpi_image_" + std::to_string(dpi)).c_str(), save(L"about-home-" + std::to_wstring(dpi) + L".png"));
    SendMessageW(state->privacy, BM_CLICK, 0, 0);
    RECT client{}, button{}, reader{}; GetClientRect(gWindow, &client);
    GetWindowRect(state->back, &button); GetWindowRect(state->reader, &reader);
    MapWindowPoints(HWND_DESKTOP, gWindow, reinterpret_cast<POINT *>(&button), 2);
    MapWindowPoints(HWND_DESKTOP, gWindow, reinterpret_cast<POINT *>(&reader), 2);
    check(("about_document_spacing_dpi_" + std::to_string(dpi)).c_str(),
        reader.left > 0 && reader.right < client.right && reader.bottom < button.top &&
        button.left > 0 && button.right < client.right && button.bottom < client.bottom);
    RECT textInset{}; SendMessageW(state->reader, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&textInset));
    check(("about_document_grid_alignment_dpi_" + std::to_string(dpi)).c_str(),
        reader.left + textInset.left == px(kMargin) && reader.top >= px(80) &&
        client.right > homeClient.right && reader.bottom + px(8) <= button.top);
    check(("about_privacy_dpi_image_" + std::to_string(dpi)).c_str(), save(L"about-privacy-" + std::to_wstring(dpi) + L".png"));
    SendMessageW(state->back, BM_CLICK, 0, 0);
  }
  check("about_dpi_no_gdi_leak", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi);
  check("about_no_business_state_mutation", gClient.IsTelemetryEnabled() == consent &&
      gClient.HasBinding() == binding && gClient.IsGameMode() == initialGame);
  SendMessageW(gWindow, WM_CLOSE, 0, 0);
  check("about_x_closes_not_pet", !gWindow && IsWindow(owner));
  Show(owner); key.hwnd = gWindow; key.wParam = VK_ESCAPE;
  check("about_escape_home_closes", HandleMessage(&key) && !gWindow && IsWindow(owner));
  Show(owner); gClient.HideByUser();
  check("about_closes_with_pet_hide", !gWindow && !IsWindowVisible(owner));
  gClient.ShowPet(); Show(owner); gClient.EmergencyHide();
  check("about_emergency_hide_closes_reader", !gWindow && !IsWindowVisible(owner));
  gClient.ShowPet(); Show(owner); gClient.SeedDpiRenderCase(true);
  check("about_game_transition_closes_reader", !gWindow && gClient.IsGameMode());
  gClient.FinishDpiRenderCase();
  ShowInfoWindow(owner, InfoWindowMode::Help); DispatchPetMenuCommand(owner, kMenuAbout);
  check("about_menu_closes_previous_help", !gInfoWindow && gWindow && GetState()->page == Page::Home);
  CloseInfoWindow();
}
