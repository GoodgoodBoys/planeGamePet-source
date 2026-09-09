template <typename Check>
void RunToolbarWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_ui;
  PetToolbarState hover;
  hover.Observe(true, false, false, 1000);
  check("toolbar_initially_hidden", !hover.visible);
  hover.Observe(true, true, true, 1001);
  check("toolbar_does_not_capture_external_drag", !hover.visible);
  hover.Observe(true, true, false, 1010);
  check("toolbar_hover_opens_collapsed", hover.visible && !hover.expanded && hover.Reveal(1010) == 0);
  hover.Toggle(1020);
  const double halfway = hover.Reveal(1110);
  check("toolbar_left_slide_in_progress", halfway > 0 && halfway < 1 && !hover.Settled(1110));
  hover.Toggle(1110);
  check("toolbar_reverse_is_continuous", std::abs(halfway - hover.Reveal(1110)) < 0.001);
  check("toolbar_reverse_finishes_collapsed", hover.Reveal(1290) == 0 && !hover.expanded);
  hover.Observe(true, false, false, 1409);
  check("toolbar_exit_grace_prevents_flicker", hover.visible);
  hover.Observe(true, false, false, 1410);
  check("toolbar_hides_at_400ms", !hover.visible);
  hover.Observe(true, true, false, 1500); hover.Toggle(1510);
  hover.Observe(true, false, false, 1800); hover.Observe(true, true, false, 1801);
  check("toolbar_quick_reentry_preserves_expansion", hover.visible && hover.expanded);
  hover.Observe(false, true, false, 1802);
  check("toolbar_unavailable_hides_preserving_expansion", !hover.visible && hover.expanded);
  hover.Observe(true, true, false, 2000);
  check("toolbar_next_hover_restores_expansion", hover.visible && hover.expanded && hover.Reveal(2000) == 1);
  hover.Observe(true, false, false, 2400);
  check("toolbar_long_exit_hides_but_keeps_expansion", !hover.visible && hover.expanded);
  hover.Observe(true, true, false, 601509);
  check("toolbar_expanded_until_exact_ten_minute_boundary", hover.expanded);
  hover.Observe(true, true, false, 601510);
  check("toolbar_ten_minutes_without_emote_collapses", !hover.expanded);
  hover.Toggle(602000); hover.EmoteSent(1100000);
  hover.Hide(); hover.Observe(false, false, false, 1699999);
  check("toolbar_send_refreshes_ten_minutes_while_hidden", hover.expanded && !hover.visible);
  hover.Observe(true, true, false, 1700000);
  check("toolbar_hidden_expiry_reenters_collapsed", !hover.expanded && hover.visible);
  hover.Toggle(1701000); hover.Toggle(1701200); hover.Hide();
  hover.Observe(true, true, false, 1702000);
  check("toolbar_manual_collapse_is_remembered", !hover.expanded);

  const auto seed = [&](bool self = false, bool peer = false, bool online = true) {
    gClient.SeedDndRenderCase(self, peer, online, false, false);
    gClient.ClearDndNotice();
    gClient.SetToolbarTestState(true, false);
  };
  const auto down = [&](int x, int y) { SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y)); };
  const auto up = [&](int x, int y) { SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); };
  const auto click = [&](int x, int y) { down(x, y); up(x, y); };
  constexpr auto invite = static_cast<uint8_t>(plink::PlayerAction::Invite);
  seed();
  check("toolbar_static_c_embedded", gClient.ToolbarFistLoadedForTest());
  down(146, 131);
  check("toolbar_press_does_not_invite", gClient.ToolbarPendingActionForTest() == 0 && GetCapture() == window);
  up(146, 131);
  check("toolbar_single_release_invites", gClient.ToolbarPendingActionForTest() == invite);
  check("toolbar_pending_invite_hides_actions", !gClient.IsPetToolbarVisible());
  seed(); down(146, 131); up(20, 145);
  check("toolbar_release_outside_cancels", gClient.ToolbarPendingActionForTest() == 0 && !gClient.ToolbarPressedForTest());
  seed(); down(146, 131);
  SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(160, 131)); up(146, 131);
  check("toolbar_drag_back_does_not_invite", gClient.ToolbarPendingActionForTest() == 0);
  seed(); down(146, 131); ReleaseCapture(); up(146, 131);
  check("toolbar_capture_loss_cancels", !gClient.ToolbarPressedForTest() && !gClient.ToolbarPendingActionForTest());
  seed(); click(227, 132);
  check("toolbar_toggle_does_not_invite", gClient.ToolbarExpandedForTest() && !gClient.ToolbarPendingActionForTest());
  check("toolbar_transition_does_not_accept_play_or_emote_click", gClient.ToolbarTargetAt(145, 132) == 0);
  gClient.SetToolbarTestState(true, true);
  click(80, 130);
  check("toolbar_emote_click_uses_existing_emote", gClient.ToolbarOwnEmoteForTest() == 1 && !gClient.ToolbarPendingActionForTest());
  click(227, 132);
  check("toolbar_right_toggle_collapses", !gClient.ToolbarExpandedForTest());
  seed(false, false, false); click(145, 132);
  check("toolbar_offline_play_disabled", !gClient.ToolbarPendingActionForTest() && !gClient.CanInvite());
  click(227, 132);
  check("toolbar_offline_can_inspect_emotes", gClient.ToolbarExpandedForTest());
  seed(true, false); click(145, 132);
  check("toolbar_self_dnd_can_invite", gClient.ToolbarPendingActionForTest() == invite);
  seed(false, true); click(145, 132);
  check("toolbar_peer_dnd_uses_3s_notice", gClient.HasDndNotice() && !gClient.ToolbarPendingActionForTest());
  const auto dndBefore = gClient.ToolbarDndDeadlineForTest();
  click(145, 132);
  check("toolbar_repeat_dnd_click_keeps_notice", gClient.HasDndNotice() &&
      gClient.ToolbarDndDeadlineForTest() == dndBefore && !gClient.ToolbarPendingActionForTest());
  gClient.SetToolbarTestState(false, false);
  check("toolbar_hover_exit_does_not_hide_dnd_cancel", gClient.HasDndNotice() &&
      gClient.IsInteractivePetPoint(220, 82));
  gClient.ClearDndNotice();
  for (int state = 0; state < 6; ++state) {
    seed(); gClient.SeedToolbarBlockedCase(state); click(145, 132);
    check(("toolbar_blocked_state_" + std::to_string(state)).c_str(),
        state == 5 ? !gClient.CanInvite() && !gClient.ToolbarPendingActionForTest() : !gClient.IsPetToolbarVisible());
  }

  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    seed(); gClient.ChangeDpi(dpi);
    RECT before{}, after{}, client{}; GetWindowRect(window, &before); GetClientRect(window, &client);
    const auto named = [&](const char *name, bool ok) {
      check((std::string(name) + "_" + std::to_string(dpi)).c_str(), ok);
    };
    const auto save = [&](const wchar_t *name) {
      return SaveReleaseFixture(directory / (std::wstring(name) + L"-" + std::to_wstring(dpi) + L".png"),
          client.right, client.bottom, [&](HDC dc) { gClient.Draw(dc, client); });
    };
    gClient.SetToolbarTestState(false, false);
    named("toolbar_hidden_image", save(L"toolbar-hidden"));
    named("toolbar_hidden_row_is_clickthrough", !gClient.IsInteractivePetPoint(200, 145) &&
        !gClient.IsInteractivePetPoint(227, 145));
    gClient.SetToolbarTestState(true, false);
    named("toolbar_collapsed_image", save(L"toolbar-collapsed"));
    named("toolbar_collapsed_hit_targets", gClient.ToolbarTargetAt(70, 132) == 1 &&
        gClient.ToolbarTargetAt(207, 132) == 1 && gClient.ToolbarTargetAt(227, 132) == 2);
    gClient.SetToolbarTestState(true, true, 90);
    named("toolbar_half_slide_image", save(L"toolbar-half-slide"));
    gClient.SetToolbarTestState(true, true);
    named("toolbar_expanded_image", save(L"toolbar-expanded"));
    named("toolbar_legacy_gaps_not_hit_surfaces", !gClient.IsPetToolbarSurface(100, 132) &&
        !gClient.IsPetToolbarSurface(138, 132) && !gClient.IsPetToolbarSurface(214, 132));
    for (int index = 0; index < 4; ++index) {
      const RECT button = ToolbarEmoteRect(index), icon = ToolbarEmoteIcon(button);
      named(("toolbar_legacy_emote_geometry_" + std::to_string(index)).c_str(),
          button.left == 64 + index * 38 && button.top == 116 &&
          button.right - button.left == 35 && button.bottom - button.top == 33 &&
          icon.right - icon.left == 23 && icon.bottom - icon.top == 24 &&
          gClient.ToolbarTargetAt(button.left + 8, button.top + 8) == index + 3);
    }
    HDC dc = GetDC(window);
    HFONT font = plane_pet_text::CreatePetFont(16, FW_BOLD);
    const auto old = SelectObject(dc, font); SIZE size{};
    GetTextExtentPoint32W(dc, L"开一局", 3, &size);
    SelectObject(dc, old); DeleteObject(font); ReleaseDC(window, dc);
    named("toolbar_label_fits", size.cx <= kToolbarPlayText.right - kToolbarPlayText.left && size.cy <= 33);
    GetWindowRect(window, &after);
    named("toolbar_keeps_fixed_window_geometry", EqualRect(&before, &after) && client.right == 280 && client.bottom == 150);
    gClient.SeedInviteRenderCase(true, 300000); gClient.SetToolbarTestState(false, false);
    named("toolbar_incoming_card_not_hover_gated", !gClient.IsPetToolbarVisible() && gClient.IsInteractivePetPoint(75, 120));
    gClient.SeedInviteRenderCase(false, 300000);
    named("toolbar_outgoing_cancel_not_hover_gated", !gClient.IsPetToolbarVisible() && gClient.IsInteractivePetPoint(220, 110));
  }
  seed(); down(145, 132); gClient.HideByUser(); up(145, 132);
  check("toolbar_hide_cancels_press_and_capture", !gClient.ToolbarPressedForTest() &&
      !gClient.IsPetToolbarVisible() && !gClient.ToolbarPendingActionForTest() && GetCapture() != window);
  gClient.ShowPet();
  check("toolbar_show_starts_collapsed_hidden", !gClient.IsPetToolbarVisible());
  seed(); gClient.SeedDpiRenderCase(true);
  check("toolbar_never_shows_over_game", !gClient.IsPetToolbarVisible());
  seed();
  // Transparent pixels support hover without becoming click surfaces.
  POINT previous{}; GetCursorPos(&previous);
  POINT transparentPoint{10, 145}; ClientToScreen(window, &transparentPoint);
  SetCursorPos(transparentPoint.x, transparentPoint.y);
  gClient.SetToolbarTestState(false, false); gClient.PollPetToolbar();
  const bool actualHover = gClient.IsPetToolbarVisible();
  SetCursorPos(previous.x, previous.y);
  check("toolbar_real_poll_detects_transparent_area", actualHover);
  check("toolbar_blank_area_remains_clickthrough", !gClient.IsInteractivePetPoint(10, 145));
  const auto beforeGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  HDC metrics = GetDC(window);
  for (int i = 0; i < 100; ++i) {
    gClient.SetToolbarTestState(true, (i % 2) != 0);
    gClient.DrawToolbarForTest(metrics);
  }
  ReleaseDC(window, metrics);
  check("toolbar_repeated_paint_no_gdi_leak", GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= beforeGdi);
  gClient.SeedDpiRenderCase(false);
}
