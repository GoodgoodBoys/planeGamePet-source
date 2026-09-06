// Included only by the dedicated window regression build, inside main.cpp's
// namespace so the test exercises the real window procedure and client loop.
bool RunUpdateWindowSelfTest(HWND owner) {
  std::ofstream report(std::filesystem::path(
      Wide(Option("test-report", "update-window-test.txt"))));
  bool allOk = true;
  const auto record = [&](const char *name, bool ok) {
    report << name << '=' << (ok ? "PASS" : "FAIL") << '\n';
    report.flush();
    allOk = allOk && ok;
  };
  const auto pump = [](int cycles) {
    for (int index = 0; index < cycles; ++index) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return;
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      gClient.Update();
      Sleep(2);
    }
  };
  const auto closeX = [] {
    if (gUpdateWindow == nullptr) return;
    const LPARAM point = MAKELPARAM(337, 23);
    SendMessageW(gUpdateWindow, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(gUpdateWindow, WM_LBUTTONUP, 0, point);
  };
  for (int attempt = 0; attempt < 6; ++attempt) {
    gClient.ManualUpdateCheck();
    record("manual_open", gUpdateWindow != nullptr &&
                           IsWindowVisible(gUpdateWindow));
    // The invalid test URL is rejected locally: no public request is sent.
    pump(25);
    record("check_error_visible", gClient.UpdateSnapshot().state ==
                                      plane_pet_update::State::Error);
    closeX();
    record("x_destroys_window", gUpdateWindow == nullptr);
    pump(25);
    record("x_stays_closed", gUpdateWindow == nullptr);
    record("dismiss_state_idle", gClient.UpdateSnapshot().state ==
                                     plane_pet_update::State::Idle);
  }
  gClient.ManualUpdateCheck();
  closeX();
  pump(50);
  record("close_during_check", gUpdateWindow == nullptr);
  gClient.ManualUpdateCheck();
  pump(25);
  if (gUpdateWindow != nullptr) SendMessageW(gUpdateWindow, WM_CLOSE, 0, 0);
  pump(25);
  record("wm_close_stays_closed", gUpdateWindow == nullptr);
  using Access = plane_pet_update::ManagerTestAccess;
  using State = plane_pet_update::State;
  auto &manager = gClient.UpdateManagerForTest();
  const State states[] = {State::Checking, State::Current, State::Available,
                         State::Required, State::Downloading, State::Error};
  for (const State state : states) {
    Access::Seed(manager, state, true, state == State::Required,
                 state == State::Available);
    gClient.Update();
    record("state_prompt_opens", gUpdateWindow != nullptr);
    closeX();
    Access::CheckResult(manager, Access::Manifest());
    Access::Error(manager);
    pump(15);
    record("all_states_x_stays_closed", gUpdateWindow == nullptr);
  }
  Access::Seed(manager, State::Current);
  gClient.Update();
  const LPARAM edge = MAKELPARAM(352, 8);
  SendMessageW(gUpdateWindow, WM_LBUTTONDOWN, MK_LBUTTON, edge);
  SendMessageW(gUpdateWindow, WM_LBUTTONUP, 0, edge);
  pump(10);
  record("close_hit_area_edge", gUpdateWindow == nullptr);
  Access::Seed(manager, State::Current);
  gClient.Update();
  const HWND card = gUpdateWindow;
  SendMessageW(card, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(337, 23));
  SendMessageW(card, WM_LBUTTONUP, 0, MAKELPARAM(180, 100));
  record("drag_out_cancels_click", IsWindow(card));
  SendMessageW(card, WM_KEYDOWN, VK_ESCAPE, 0);
  pump(10);
  record("escape_stays_closed", gUpdateWindow == nullptr);

  gClient.SetUpdateTestGamePhase(plink::GamePhase::Playing);
  Access::Seed(manager, State::Available, true, false, true);
  pump(10);
  record("game_defers_prompt", gUpdateWindow == nullptr);
  gClient.SetUpdateTestGamePhase(plink::GamePhase::Menu);
  pump(10);
  record("return_idle_restores_prompt", gUpdateWindow != nullptr);
  gClient.SetUpdateTestGamePhase(plink::GamePhase::Waiting);
  pump(10);
  record("invitation_suspends_prompt", gUpdateWindow == nullptr);
  gClient.SetUpdateTestGamePhase(plink::GamePhase::Menu);
  pump(10);
  record("invitation_end_restores_prompt", gUpdateWindow != nullptr);
  closeX();
  Access::Seed(manager, State::Downloading);
  const auto request = std::filesystem::path(
      Wide(Option("test-report", "update-window-test.txt"))).parent_path();
  Access::Install(manager, request / "not-a-real-update.download");
  gClient.SetUpdateTestGamePhase(plink::GamePhase::Playing);
  SendMessageW(owner, plane_pet_update::kInstallMessage, 0, 0);
  record("install_does_not_interrupt_game", IsWindow(owner));
  manager.Dismiss();
  SendMessageW(owner, plane_pet_update::kInstallMessage, 0, 0);
  record("dismiss_ignores_queued_install", IsWindow(owner));
  gClient.SetUpdateTestGamePhase(plink::GamePhase::Menu);
  Access::Seed(manager, State::Checking);
  gClient.Update();
  gClient.HideByUser();
  Access::Error(manager);
  pump(10);
  record("hide_cancels_late_prompt", gUpdateWindow == nullptr &&
                                   !IsWindowVisible(owner));
  gClient.ShowPet();
  record("owner_survives", IsWindow(owner) && IsWindowVisible(owner));
  return allOk && report.good();
}
