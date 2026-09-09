// Exercise actual native HMENU trees and the shared command route. Test-only;
// no production pairing/telemetry records or destructive commands are invoked.
std::wstring MenuLabel(HMENU menu, int position) {
  wchar_t label[128]{};
  GetMenuStringW(menu, static_cast<UINT>(position), label, 128, MF_BYPOSITION);
  return label;
}

std::vector<UINT> MenuOrder(HMENU menu) {
  std::vector<UINT> result;
  for (int i = 0; i < GetMenuItemCount(menu); ++i) {
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU;
    if (!GetMenuItemInfoW(menu, i, TRUE, &item)) return {};
    result.push_back((item.fType & MFT_SEPARATOR) ? 0 :
                    item.hSubMenu ? UINT_MAX : item.wID);
  }
  return result;
}

template <typename Check>
void RunMenuWindowCases(HWND window, const Check &check) {
  PetMenuState state;
  state.bound = state.peerOnline = state.canInvite = state.updatesEnabled = true;
  const auto mainOrder = std::vector<UINT>{kMenuInvite, kMenuHistory,
      kMenuDoNotDisturb, kMenuHide, 0, UINT_MAX, UINT_MAX, kMenuAbout, 0, kMenuExit};
  const auto settingsOrder = std::vector<UINT>{kMenuTelemetry, kMenuExportData,
      kMenuClearLocalData, 0, kMenuUnbind};
  const auto helpOrder = std::vector<UINT>{kMenuHelp, kMenuCheckUpdate, 0,
      kMenuDiagnostics, kMenuExportDiagnostics};
  HMENU menu = BuildPetContextMenu(state);
  check("menu_primary_items_with_about_in_approved_order", menu && MenuOrder(menu) == mainOrder);
  check("menu_about_visible_and_enabled", MenuLabel(menu, 7) == L"关于…" &&
      !(GetMenuState(menu, kMenuAbout, MF_BYCOMMAND) & MF_GRAYED));
  HMENU settings = GetSubMenu(menu, 5), help = GetSubMenu(menu, 6);
  check("menu_named_groups", MenuLabel(menu, 5) == L"设置与隐私" && MenuLabel(menu, 6) == L"帮助与更新");
  check("menu_all_privacy_actions_preserved", settings && MenuOrder(settings) == settingsOrder);
  check("menu_all_help_actions_preserved", help && MenuOrder(help) == helpOrder);
  bool twoLevels = settings && help;
  for (HMENU child : {settings, help})
    for (int i = 0; i < GetMenuItemCount(child); ++i)
      twoLevels = twoLevels && GetSubMenu(child, i) == nullptr;
  check("menu_no_third_level", twoLevels);
  check("menu_enabled_actions", !(GetMenuState(menu, kMenuInvite, MF_BYCOMMAND) & MF_GRAYED) &&
      !(GetMenuState(help, kMenuCheckUpdate, MF_BYCOMMAND) & MF_GRAYED));
  check("menu_privacy_labels_describe_real_scope",
      MenuLabel(settings, 0) == L"匿名使用统计" &&
      MenuLabel(settings, 1) == L"导出统计记录…" &&
      MenuLabel(settings, 2) == L"清除本地战绩与统计…");
  DestroyMenu(menu);
  check("menu_children_destroyed_with_parent", !IsMenu(settings) && !IsMenu(help));

  state.peerOnline = state.canInvite = false;
  menu = BuildPetContextMenu(state);
  check("menu_offline_reason_disabled", MenuLabel(menu, 0) == L"邀请对战（对方离线）" &&
      (GetMenuState(menu, kMenuInvite, MF_BYCOMMAND) & MF_GRAYED));
  DestroyMenu(menu);
  state.peerOnline = true;  // busy/version mismatch is NOT reported as offline
  menu = BuildPetContextMenu(state);
  check("menu_busy_does_not_claim_offline", MenuLabel(menu, 0) == L"邀请对战" &&
      (GetMenuState(menu, kMenuInvite, MF_BYCOMMAND) & MF_GRAYED));
  DestroyMenu(menu);

  state = PetMenuState{};
  state.needsPairing = true;
  menu = BuildPetContextMenu(state);
  check("menu_unbound_replaces_disabled_invite", GetMenuItemID(menu, 0) == kMenuPairingPanel &&
      GetMenuState(menu, kMenuInvite, MF_BYCOMMAND) == UINT_MAX);
  check("menu_pairing_panel_checked", GetMenuState(menu, kMenuPairingPanel, MF_BYCOMMAND) & MF_CHECKED);
  settings = GetSubMenu(menu, 5); help = GetSubMenu(menu, 6);
  check("menu_unbound_unbind_disabled", GetMenuState(settings, kMenuUnbind, MF_BYCOMMAND) & MF_GRAYED);
  check("menu_dual_update_stays_disabled", GetMenuState(help, kMenuCheckUpdate, MF_BYCOMMAND) & MF_GRAYED);
  DestroyMenu(menu);
  state.pairing = true;
  state.pairingPanelVisible = false;
  menu = BuildPetContextMenu(state);
  check("menu_search_stop_and_panel_both_accessible", GetMenuItemCount(menu) == 11 &&
      GetMenuItemID(menu, 0) == kMenuStopPairing && GetMenuItemID(menu, 1) == kMenuPairingPanel &&
      !(GetMenuState(menu, kMenuPairingPanel, MF_BYCOMMAND) & MF_CHECKED));
  DestroyMenu(menu);

  state = PetMenuState{};
  state.bound = state.unbinding = true;
  state.hidden = state.telemetry = state.doNotDisturb = true;
  menu = BuildPetContextMenu(state);
  settings = GetSubMenu(menu, 5);
  check("menu_hidden_tray_has_one_restore_entry", GetMenuItemID(menu, 3) == kMenuShow &&
      MenuLabel(menu, 3) == L"显示悬浮窗" && GetMenuState(menu, kMenuHide, MF_BYCOMMAND) == UINT_MAX);
  check("menu_saved_toggles_checked", (GetMenuState(menu, kMenuDoNotDisturb, MF_BYCOMMAND) & MF_CHECKED) &&
      (GetMenuState(settings, kMenuTelemetry, MF_BYCOMMAND) & MF_CHECKED));
  check("menu_unbinding_label_and_disable", MenuLabel(settings, 4) == L"正在解除绑定…" &&
      (GetMenuState(settings, kMenuUnbind, MF_BYCOMMAND) & MF_GRAYED));
  DestroyMenu(menu);
  state.game = true;
  menu = BuildPetContextMenu(state);
  check("menu_hidden_game_restore_label", MenuLabel(menu, 3) == L"显示游戏窗口");
  DestroyMenu(menu);
  state.hidden = false;
  menu = BuildPetContextMenu(state);
  check("menu_visible_game_hide_label", GetMenuItemID(menu, 3) == kMenuHide &&
      MenuLabel(menu, 3) == L"隐藏游戏窗口");
  DestroyMenu(menu);

  const bool initialDnd = gClient.IsDoNotDisturb();
  DispatchPetMenuCommand(window, 0);
  check("menu_cancel_is_noop", gClient.IsDoNotDisturb() == initialDnd && IsWindow(window));
  DispatchPetMenuCommand(window, kMenuDoNotDisturb);
  check("menu_command_toggle_routes", gClient.IsDoNotDisturb() != initialDnd);
  DispatchPetMenuCommand(window, kMenuDoNotDisturb);
  DispatchPetMenuCommand(window, kMenuHelp);
  check("menu_help_command_routes", gInfoWindow && IsWindowVisible(gInfoWindow) &&
      gInfoWindowMode == InfoWindowMode::Help);
  CloseInfoWindow();
  DispatchPetMenuCommand(window, kMenuDiagnostics);
  check("menu_diagnostics_command_routes", gInfoWindow && IsWindowVisible(gInfoWindow) &&
      gInfoWindowMode == InfoWindowMode::Diagnostics);
  CloseInfoWindow();
  DispatchPetMenuCommand(window, kMenuHistory);
  check("menu_history_command_routes", gInfoWindow && IsWindowVisible(gInfoWindow) &&
      gInfoWindowMode == InfoWindowMode::History);
  CloseInfoWindow();
  DispatchPetMenuCommand(window, kMenuAbout);
  check("menu_about_command_routes", plane_pet_about::gWindow &&
      IsWindowVisible(plane_pet_about::gWindow) &&
      plane_pet_about::GetState()->page == plane_pet_about::Page::Home);
  DispatchPetMenuCommand(window, kMenuHelp);
  check("menu_help_closes_about_reader", !plane_pet_about::gWindow && gInfoWindow);
  CloseInfoWindow();
  DispatchPetMenuCommand(window, kMenuHide);
  check("menu_hide_updates_tray_snapshot", CurrentPetMenuState(window).hidden && !IsWindowVisible(window));
  DispatchPetMenuCommand(window, kMenuShow);
  check("menu_restore_updates_tray_snapshot", !CurrentPetMenuState(window).hidden && IsWindowVisible(window));
  const bool telemetry = gClient.IsTelemetryEnabled();
  const DWORD before = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  int iteration = 0;
  for (; iteration < 300; ++iteration) {
    menu = BuildPetContextMenu(CurrentPetMenuState(window));
    if (!menu) break;
    DestroyMenu(menu);
  }
  const DWORD after = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
  check("menu_rebuild_does_not_change_consent_or_leak_handles", iteration == 300 && before == after &&
      telemetry == gClient.IsTelemetryEnabled() && gModalDepth == 0);
}
