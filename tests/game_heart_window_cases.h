// Actual compact HUD rendering, with isolated synthetic server HP snapshots.
template <typename Check>
void RunGameHeartWindowCases(const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_ui;
  const COLORREF background = RGB(27, 31, 43);
  const auto validHp = [&](HDC dc, int ox, int oy, int ownHp, int peerHp) {
    bool valid = true;
    for (int side = 0; side < 2; ++side) for (int slot = 0; slot < 3; ++slot) {
      const int cx = ox + (side == 0 ? kGameOwnHeartX : kGamePeerHeartX) + slot * kGameHeartPitch;
      const int cy = oy + kGameHeartY;
      const bool full = side == 0 ? slot < ownHp : slot >= 3 - peerHp;
      const COLORREF pixel = GetPixel(dc, cx, cy);
      const int r = GetRValue(pixel), g = GetGValue(pixel), b = GetBValue(pixel);
      valid = valid && pixel != CLR_INVALID && (full ?
          (side == 0 ? (b > 200 && g > 100 && r < 50) : (r > 200 && g < 110 && b < 130)) :
          (r >= 30 && r < 65 && g >= 34 && g < 70 && b >= 38 && b < 85));
      valid = valid && GetPixel(dc, cx, cy - 4) == background &&
          GetPixel(dc, cx - 5, cy + 5) == background &&
          GetPixel(dc, cx + 5, cy + 5) == background;
      // One clear pixel between sprites, never a joined health strip.
      if (slot < 2) valid = valid && GetPixel(dc, cx + 7, cy) == background;
    }
    return valid;
  };
  for (uint8_t viewer = 0; viewer < 2; ++viewer) {
    bool statesCorrect = true;
    check(("game_hearts_all_hp_image_view_" + std::to_string(viewer + 1)).c_str(),
        SaveReleaseFixture(directory / (L"combat-hp-view-" + std::to_wstring(viewer + 1) + L".png"),
            4 * plane_pet_ui::kGameClientWidth, 4 * plane_pet_ui::kGameHudHeight, [&](HDC dc) {
      for (uint8_t p0 = 0; p0 <= 3; ++p0) for (uint8_t p1 = 0; p1 <= 3; ++p1) {
        const int x = p1 * plane_pet_ui::kGameClientWidth, y = p0 * plane_pet_ui::kGameHudHeight;
        gClient.DrawGameHudForTest(dc, x, y, viewer, p0, p1); GdiFlush();
        statesCorrect = statesCorrect && validHp(dc, x, y, viewer == 0 ? p0 : p1, viewer == 0 ? p1 : p0);
      }
    }));
    check(("game_hearts_hp_0_to_3_colors_grey_order_and_shape_view_" + std::to_string(viewer + 1)).c_str(), statesCorrect);
  }
  bool casesCorrect = true, handlesStable = false;
  check("game_hearts_phase_and_repaint_fixture", SaveReleaseFixture(directory / L"combat-heart-repaint.png",
      plane_pet_ui::kGameClientWidth, plane_pet_ui::kGameHudHeight, [&](HDC dc) {
    for (const auto phase : {plink::GamePhase::Countdown, plink::GamePhase::Playing, plink::GamePhase::Finished}) {
      // Repaint over full health, then depleted health, so stale filled pixels fail.
      gClient.DrawGameHudForTest(dc, 0, 0, 1, 3, 3, true, phase);
      gClient.DrawGameHudForTest(dc, 0, 0, 1, 0, 1, true, phase); GdiFlush();
      casesCorrect = casesCorrect && validHp(dc, 0, 0, 1, 0);
    }
    gClient.DrawGameHudForTest(dc, 0, 0, 0, 255, 255); GdiFlush();
    casesCorrect = casesCorrect && validHp(dc, 0, 0, 3, 3);
    gClient.DrawGameHudForTest(dc, 0, 0, 1, 0, 0, false); GdiFlush();
    casesCorrect = casesCorrect && validHp(dc, 0, 0, 3, 3);
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 120; ++i)
      gClient.DrawGameHudForTest(dc, 0, 0, i % 2, i % 4, (i + 2) % 4);
    GdiFlush();
    handlesStable = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before;
  }));
  check("game_hearts_countdown_play_finished_no_stale_hp_and_clamp", casesCorrect);
  check("game_hearts_repeated_frame_no_gdi_leak", handlesStable);
}
