template <typename Check>
void RunIdleWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_ui;
  double minimum = 1, maximum = -1, sum = 0;
  bool bounded = true, stable = true, distinctSeeds = false;
  for (uint32_t index = 0; index < 10000; ++index) {
    const double turn = IdleAutoTurn(20260907, index);
    minimum = std::min(minimum, turn); maximum = std::max(maximum, turn); sum += turn;
    bounded = bounded && turn >= -kIdleShotMaxTurn && turn <= kIdleShotMaxTurn;
    stable = stable && turn == IdleAutoTurn(20260907, index);
    distinctSeeds = distinctSeeds || turn != IdleAutoTurn(20260908, index);
  }
  check("idle_random_angles_10000_within_15_degrees", bounded);
  check("idle_random_angles_cover_both_sides", minimum < -kIdleShotMaxTurn * .95 &&
      maximum > kIdleShotMaxTurn * .95 && std::abs(sum / 10000) < .005);
  check("idle_random_angle_stable_per_shot", stable);
  check("idle_each_launch_seed_changes_pattern", distinctSeeds);
  struct Pose { double x, y, directionX, directionY; };
  const auto frame = [&](uint32_t elapsed, double heading = 0) {
    std::vector<IdleShot> result;
    ForEachIdleAutoShot(elapsed, 20260907, [&](uint32_t fired) {
      return Pose{100 + fired * .01, 60, std::cos(heading), std::sin(heading)};
    }, [&](IdleShot shot) { result.push_back(shot); });
    return result;
  };
  check("idle_original_fire_cadence_620ms", kIdleShotIntervalMs == 620 &&
      frame(0).size() == 1 && frame(619).size() == 1 && frame(620).size() == 2);
  check("idle_original_lifetime_1120ms", kIdleShotLifetimeMs == 1120 &&
      frame(1119).size() == 2 && frame(1120).size() == 1);
  const auto born = frame(620);
  check("idle_auto_muzzle_uses_firing_pose", std::abs(born.front().x - 119.2) < 1e-9 &&
      born.front().y == 60);
  const auto f1 = frame(700), f2 = frame(800);
  check("idle_auto_flight_keeps_direction_and_speed", f1.size() == 2 && f2.size() == 2 &&
      f1[0].dx == f2[0].dx && f1[0].dy == f2[0].dy &&
      std::abs(f2[0].x - f1[0].x - 10.5 * f1[0].dx) < 1e-8 &&
      std::abs(f2[0].y - f1[0].y - 10.5 * f1[0].dy) < 1e-8);
  bool headings = true;
  for (int degrees = -180; degrees <= 180; ++degrees) {
    const double heading = degrees * 3.14159265358979323846 / 180;
    const auto shot = frame(700, heading).front();
    const double delta = std::atan2(std::cos(heading) * shot.dy - std::sin(heading) * shot.dx,
                                    std::cos(heading) * shot.dx + std::sin(heading) * shot.dy);
    headings = headings && std::abs(delta) <= kIdleShotMaxTurn + 1e-9 &&
        std::abs(std::hypot(shot.dx, shot.dy) - 1) < 1e-9;
  }
  check("idle_auto_361_headings_relative_to_nose", headings);
  bool catchup = true;
  for (uint32_t elapsed : {0U, 620U, 999999U, 86400000U, 0xffffffffU})
    catchup = catchup && frame(elapsed).size() >= 1 && frame(elapsed).size() <= 2;
  check("idle_auto_long_pause_no_catchup_burst", catchup);
  const auto seed = [&]() {
    gClient.SeedDndRenderCase(false, false, true, false, false);
    gClient.ClearDndNotice(); gClient.SetToolbarTestState(true, false);
  };
  const auto down = [&](int x, int y) { SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y)); };
  const auto up = [&](int x, int y) { SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y)); };
  const auto same = [&](const auto &a, const auto &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].dx != b[i].dx || a[i].dy != b[i].dy) return false;
    return true;
  };
  seed();
  const auto beforeClick = gClient.IdleAutoFrameForTest(1000);
  for (int i = 0; i < 20; ++i) { down(15, 130); up(15, 130); }
  check("idle_left_click_cannot_create_or_redirect_shots",
      same(beforeClick, gClient.IdleAutoFrameForTest(1000)) && gClient.ToolbarPendingActionForTest() == 0);
  down(15, 130); gClient.UseClientDragCoordinatesForTest();
  RECT before{}, after{}; GetWindowRect(window, &before);
  gClient.OnMouseMove(40, 140); GetWindowRect(window, &after); up(15, 130);
  check("idle_drag_still_moves_window", after.left == before.left + 25 && after.top == before.top + 10);
  check("idle_drag_does_not_change_fire_sequence", same(beforeClick, gClient.IdleAutoFrameForTest(1000)));
  down(15, 130); ReleaseCapture(); up(15, 130);
  check("idle_capture_loss_does_not_change_fire_sequence", same(beforeClick, gClient.IdleAutoFrameForTest(1000)));
  seed(); gClient.SetToolbarTestState(true, true, kToolbarRememberMs - 20);
  gClient.AllowEmoteForTest(); down(80, 130); up(80, 130);
  gClient.ObserveToolbarForTest(GetTickCount64() + 100);
  check("toolbar_actual_emote_send_refreshes_memory", gClient.ToolbarExpandedForTest());
  gClient.SetToolbarTestState(true, true, kToolbarRememberMs - 20);
  down(80, 130); up(80, 130); gClient.ObserveToolbarForTest(GetTickCount64() + 100);
  check("toolbar_cooldown_click_does_not_refresh_memory", !gClient.ToolbarExpandedForTest());
  seed(); gClient.SeedDndRenderCase(false, false, false, false, false);
  gClient.SetToolbarTestState(true, true, kToolbarRememberMs - 20);
  down(80, 130); up(80, 130); gClient.ObserveToolbarForTest(GetTickCount64() + 100);
  check("toolbar_offline_click_does_not_refresh_memory", !gClient.ToolbarExpandedForTest());
  check("idle_offline_has_automatic_shots", !gClient.IdleAutoFrameForTest(900).empty());
  seed(); gClient.SeedHiddenPairingForTest();
  check("idle_unbound_has_automatic_shots", !gClient.IdleAutoFrameForTest(900).empty());
  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    seed(); gClient.ChangeDpi(dpi);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    COLORREF key{}; BYTE alpha{}; DWORD flags{};
    check(("idle_original_colorkey_restored_" + std::to_string(dpi)).c_str(),
        GetLayeredWindowAttributes(window, &key, &alpha, &flags) && key == kTransparent && flags == LWA_COLORKEY);
    POINT point{10, 145}; ClientToScreen(window, &point);
    check(("idle_os_blank_clickthrough_" + std::to_string(dpi)).c_str(), WindowFromPoint(point) != window);
    check(("idle_blank_not_interactive_" + std::to_string(dpi)).c_str(), !gClient.IsInteractivePetPoint(10, 145));
    RECT rect{}; GetClientRect(window, &rect);
    check(("idle_window_size_unchanged_" + std::to_string(dpi)).c_str(), rect.right == 280 && rect.bottom == 150);
    check(("idle_auto_shoot_image_" + std::to_string(dpi)).c_str(), SaveReleaseFixture(
        directory / (L"idle-auto-shot-" + std::to_wstring(dpi) + L".png"), 280, 150,
        [&](HDC dc) { gClient.Draw(dc, rect); }));
  }
  gClient.SeedDpiRenderCase(false);
}
