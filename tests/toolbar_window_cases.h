template <typename Check>
void RunToolbarWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_ui;
  PetToolbarState hover;
  hover.Observe(true, false, false, 1000);
  check("toolbar_initially_hidden", !hover.visible);
  hover.Observe(true, true, true, 1001);
  check("toolbar_does_not_capture_external_drag", !hover.visible);
  hover.Observe(true, true, false, 1010);
  check("toolbar_hover_opens_full_row", hover.visible);
  hover.Observe(true, false, false, 1409);
  check("toolbar_exit_grace_prevents_flicker", hover.visible);
  hover.Observe(true, false, false, 1410);
  check("toolbar_hides_at_400ms", !hover.visible);
  hover.Observe(false, true, false, 1500);
  check("toolbar_unavailable_stays_hidden", !hover.visible);
  hover.Observe(true, true, false, 2000);
  hover.Observe(true, true, false, 700000);
  check("toolbar_no_ten_minute_collapse", hover.visible);
  hover.Reset(); check("toolbar_reset_hides", !hover.visible);
  const auto seed = [&](bool self = false, bool peer = false, bool online = true) {
    gClient.SeedDndRenderCase(self, peer, online, false, false);
    gClient.ClearDndNotice(); gClient.SetToolbarTestState(true);
  };
  const auto down = [&](int x, int y) { SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y)); };
  const auto up = [&](int x, int y) { SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); };
  const auto click = [&](int x, int y) { down(x, y); up(x, y); };
  constexpr auto invite = static_cast<uint8_t>(plink::PlayerAction::Invite);
  seed(); check("toolbar_static_c_embedded", gClient.ToolbarFistLoadedForTest());
  down(65, 130);
  check("toolbar_press_does_not_invite", !gClient.ToolbarPendingActionForTest() && GetCapture() == window);
  up(65, 130);
  check("toolbar_single_release_invites", gClient.ToolbarPendingActionForTest() == invite);
  check("toolbar_pending_invite_hides_actions", !gClient.IsPetToolbarVisible());
  seed(); down(65, 130); up(10, 145);
  check("toolbar_release_outside_cancels", !gClient.ToolbarPendingActionForTest() && !gClient.ToolbarPressedForTest());
  seed(); down(65, 130);
  SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(80, 130)); up(65, 130);
  check("toolbar_drag_back_does_not_invite", !gClient.ToolbarPendingActionForTest());
  seed(); down(65, 130); ReleaseCapture(); up(65, 130);
  check("toolbar_capture_loss_cancels", !gClient.ToolbarPressedForTest() && !gClient.ToolbarPendingActionForTest());
  for (int i = 0; i < 4; ++i) {
    seed(); gClient.AllowEmoteForTest(); const auto b = ToolbarEmoteRect(i);
    click(b.left + 12, b.top + 12);
    check(("toolbar_direct_emote_" + std::to_string(i)).c_str(),
        gClient.ToolbarOwnEmoteForTest() == i + 1 && !gClient.ToolbarPendingActionForTest());
  }
  seed(false, false, false); click(65, 130); click(125, 130);
  check("toolbar_offline_visible_disabled", gClient.IsQuickEmoteBarVisible() &&
      !gClient.ToolbarPendingActionForTest() && !gClient.ToolbarOwnEmoteForTest() && !gClient.CanInvite());
  seed(true, false); click(65, 130);
  check("toolbar_self_dnd_can_invite", gClient.ToolbarPendingActionForTest() == invite);
  seed(false, true); click(65, 130);
  check("toolbar_peer_dnd_uses_3s_notice", gClient.HasDndNotice() && !gClient.ToolbarPendingActionForTest());
  const auto deadline = gClient.ToolbarDndDeadlineForTest(); click(65, 130);
  check("toolbar_repeat_dnd_keeps_deadline", gClient.HasDndNotice() &&
      gClient.ToolbarDndDeadlineForTest() == deadline && !gClient.ToolbarPendingActionForTest());
  gClient.SetToolbarTestState(false);
  check("toolbar_exit_keeps_notice_cancel", gClient.HasDndNotice() && gClient.IsInteractivePetPoint(220, 82));
  gClient.ClearDndNotice();
  for (int state = 0; state < 6; ++state) {
    seed(); gClient.SeedToolbarBlockedCase(state); click(65, 130);
    check(("toolbar_blocked_state_" + std::to_string(state)).c_str(),
        state == 5 ? !gClient.CanInvite() && !gClient.ToolbarPendingActionForTest() : !gClient.IsPetToolbarVisible());
  }
  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    seed(); gClient.ChangeDpi(dpi);
    RECT before{}, after{}, client{}; GetWindowRect(window, &before); GetClientRect(window, &client);
    const auto named = [&](const char *name, bool ok) { check((std::string(name) + "_" + std::to_string(dpi)).c_str(), ok); };
    const auto save = [&](const wchar_t *name) {
      return SaveReleaseFixture(directory / (std::wstring(name) + L"-" + std::to_wstring(dpi) + L".png"),
          client.right, client.bottom, [&](HDC dc) { gClient.Draw(dc, client); });
    };
    gClient.SetToolbarTestState(false); named("toolbar_hidden_image", save(L"toolbar-hidden"));
    named("toolbar_hidden_clickthrough", !gClient.IsInteractivePetPoint(200, 145));
    gClient.SetToolbarTestState(true); named("toolbar_full_row_image", save(L"toolbar-all-actions"));
    named("toolbar_play_left_of_emotes", gClient.ToolbarTargetAt(25, 125) == 1 && kToolbarPlay.right < ToolbarEmoteRect(0).left);
    named("toolbar_gaps_not_hit_surfaces", !gClient.IsPetToolbarSurface(110, 130) &&
        !gClient.IsPetToolbarSurface(149, 130) && !gClient.IsPetToolbarSurface(263, 130));
    named("toolbar_bottom_padding", kToolbarPlay.bottom == 145 && ToolbarEmoteRect(3).bottom == 145);
    for (int index = 0; index < 4; ++index) {
      const RECT b = ToolbarEmoteRect(index), icon = ToolbarEmoteIcon(b);
      named(("toolbar_preserves_emote_size_" + std::to_string(index)).c_str(),
          b.left == 113 + index * 38 && b.top == 112 && b.right - b.left == 35 && b.bottom - b.top == 33 &&
          icon.right - icon.left == 23 && icon.bottom - icon.top == 24 &&
          gClient.ToolbarTargetAt(b.left + 8, b.top + 8) == index + 2);
    }
    HDC dc = GetDC(window); HFONT font = plane_pet_text::CreatePetFont(16, FW_BOLD);
    const auto old = SelectObject(dc, font); SIZE size{};
    GetTextExtentPoint32W(dc, L"开一局", 3, &size);
    SelectObject(dc, old); DeleteObject(font); ReleaseDC(window, dc);
    named("toolbar_label_fits", size.cx <= kToolbarPlayText.right - kToolbarPlayText.left && size.cy <= 33);
    GetWindowRect(window, &after);
    named("toolbar_fixed_window_geometry", EqualRect(&before, &after) && client.right == 280 && client.bottom == 150);
    gClient.SeedInviteRenderCase(true, 300000); gClient.SetToolbarTestState(false);
    named("toolbar_incoming_not_hover_gated", !gClient.IsPetToolbarVisible() && gClient.IsInteractivePetPoint(75, 120));
    gClient.SeedInviteRenderCase(false, 300000);
    named("toolbar_outgoing_cancel_not_hover_gated", !gClient.IsPetToolbarVisible() && gClient.IsInteractivePetPoint(220, 110));
  }
  seed(); down(65, 130); gClient.HideByUser(); up(65, 130);
  check("toolbar_hide_cancels_press", !gClient.ToolbarPressedForTest() && !gClient.IsPetToolbarVisible() &&
      !gClient.ToolbarPendingActionForTest() && GetCapture() != window);
  gClient.ShowPet(); check("toolbar_show_starts_hidden", !gClient.IsPetToolbarVisible());
  seed(); gClient.SeedDpiRenderCase(true);
  check("toolbar_never_shows_over_game", !gClient.IsPetToolbarVisible());
  seed(); POINT previous{}; GetCursorPos(&previous);
  POINT transparentPoint{10, 145}; ClientToScreen(window, &transparentPoint);
  const bool pointerPlaced = SetCursorPos(transparentPoint.x, transparentPoint.y) != FALSE;
  POINT observed{}; GetCursorPos(&observed);
  check("toolbar_real_pointer_placement", pointerPlaced && observed.x == transparentPoint.x && observed.y == transparentPoint.y);
  check("toolbar_real_pointer_button_released", !(GetAsyncKeyState(VK_LBUTTON) & 0x8000));
  check("toolbar_real_window_available", gClient.IsPetToolbarAvailable() && IsWindowVisible(window));
  gClient.SetToolbarTestState(false); gClient.PollPetToolbar();
  const bool actualHover = gClient.IsPetToolbarVisible(); SetCursorPos(previous.x, previous.y);
  check("toolbar_real_poll_detects_transparent_area", actualHover);
  check("toolbar_blank_remains_clickthrough", !gClient.IsInteractivePetPoint(10, 145));
  const auto beforeGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  HDC metrics = GetDC(window);
  for (int i = 0; i < 100; ++i) { gClient.SetToolbarTestState((i % 2) != 0); gClient.DrawToolbarForTest(metrics); }
  ReleaseDC(window, metrics);
  check("toolbar_repeated_paint_no_gdi_leak", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= beforeGdi);
  gClient.SeedDpiRenderCase(false);
}
