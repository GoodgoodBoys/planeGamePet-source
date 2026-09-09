// Real GDI rendering of the invitation cards at every supported DPI test value.
// Included after SaveReleaseFixture in the isolated release regression only.
template <typename Check>
void RunInviteWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_ui;
  HDC metrics = GetDC(window);
  const auto textFits = [&](const wchar_t *text, const RECT &rect, int emPixels) {
    HFONT font = plane_pet_text::CreatePetFont(emPixels, FW_BOLD);
    if (!metrics || !font) {
      if (font) DeleteObject(font);
      return false;
    }
    const auto old = SelectObject(metrics, font);
    SIZE size{};
    const bool measured = GetTextExtentPoint32W(metrics, text,
        static_cast<int>(wcslen(text)), &size) != FALSE;
    SelectObject(metrics, old);
    DeleteObject(font);
    return measured && size.cx <= rect.right - rect.left && size.cy <= rect.bottom - rect.top;
  };
  check("invite_incoming_title_fits", textFits(L"对方邀请你开一局...", kIncomingInviteLayout.message, 14));
  check("invite_waiting_three_dots_fit", textFits(L"等待对方响应...", kOutgoingInviteLayout.message, 14));
  check("invite_300s_countdown_fits", textFits(L"300s", kOutgoingInviteLayout.countdown, 14));
  check("invite_button_labels_fit", textFits(L"接受", kIncomingInviteLayout.primary, 14) &&
      textFits(L"拒绝", kIncomingInviteLayout.secondary, 14) &&
      textFits(L"取消", kOutgoingInviteLayout.primary, 14));
  if (metrics) ReleaseDC(window, metrics);

  const auto sameRect = [](const RECT &a, const RECT &b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
  };
  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    const auto named = [&](const char *name, bool ok) {
      const auto label = std::string(name) + "_" + std::to_string(dpi);
      check(label.c_str(), ok);
    };
    gClient.SeedDpiRenderCase(false);
    gClient.ChangeDpi(dpi);
    RECT before{}, client{};
    GetWindowRect(window, &before);
    GetClientRect(window, &client);
    bool stable = client.right == kPetWidth && client.bottom == kPetHeight;
    for (const uint32_t milliseconds : {300000U, 99000U, 0U}) {
      gClient.SeedInviteRenderCase(false, milliseconds);
      const auto seconds = std::to_wstring(milliseconds / 1000);
      const auto path = directory / (L"invite-outgoing-" + seconds + L"-" + std::to_wstring(dpi) + L".png");
      const auto name = std::string("invite_outgoing_image_") + std::to_string(milliseconds / 1000);
      named(name.c_str(), SaveReleaseFixture(path, client.right, client.bottom,
          [&](HDC dc) { gClient.Draw(dc, client); }));
      RECT after{};
      GetWindowRect(window, &after);
      stable = stable && sameRect(before, after);
    }
    named("invite_outgoing_padding_draggable", gClient.IsInteractivePetPoint(22, 104));
    named("invite_outgoing_hides_disabled_emotes", !gClient.IsQuickEmoteBarVisible() &&
        !gClient.IsInteractivePetPoint(194, 145));

    gClient.SeedInviteRenderCase(true, 300000);
    named("invite_incoming_image", SaveReleaseFixture(
        directory / (L"invite-incoming-" + std::to_wstring(dpi) + L".png"), client.right, client.bottom,
        [&](HDC dc) { gClient.Draw(dc, client); }));
    named("invite_incoming_padding_draggable", gClient.IsInteractivePetPoint(80, 135));
    named("invite_incoming_hides_disabled_emotes", !gClient.IsQuickEmoteBarVisible() &&
        !gClient.IsInteractivePetPoint(194, 145));
    RECT after{};
    GetWindowRect(window, &after);
    named("invite_cards_keep_pet_size_and_position", stable && sameRect(before, after));
    gClient.SeedDpiRenderCase(false);
    named("invite_idle_toolbar_waits_for_hover", !gClient.IsPetToolbarVisible());
    gClient.SetToolbarTestState(true);
    named("invite_idle_restores_emote_bar", gClient.IsQuickEmoteBarVisible() &&
        gClient.IsInteractivePetPoint(194, 140));
  }
}
