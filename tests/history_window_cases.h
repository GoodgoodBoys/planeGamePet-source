// Isolated native rendering of the actual history window, with synthetic data.
template <typename Check>
void RunHistoryWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using namespace plane_pet_history_ui;
  const auto timestamp = PetClient::HistoryTimeForTest(1788694380000ULL);
  const auto colon = timestamp.find(L':');
  check("history_time_colon_has_two_side_spaces", colon != std::wstring::npos &&
      colon > 0 && colon + 1 < timestamp.size() && timestamp[colon - 1] == L' ' &&
      timestamp[colon + 1] == L' ');
  check("history_planes_ccw_45_degrees", std::abs(kPlaneAngle + std::acos(-1.0) / 4.0) < 1e-9);
  check("history_selected_a_embedded_atlas_and_alpha", gClient.HistoryHeartResourceForTest());
  constexpr uint8_t own[] = {2, 0, 1, 0, 0, 2, 3, 3, 2, 3};
  constexpr uint8_t peer[] = {0, 3, 0, 2, 0, 1, 2, 0, 3, 0};
  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    const auto name = [&](const char *suffix) { return std::string("history_") + suffix + "_" + std::to_string(dpi); };
    const int width = plane_pet_dpi::Scale(kHistoryWidth, dpi);
    const int height = plane_pet_dpi::Scale(kHistoryContentHeight, dpi);
    bool heartsCorrect = true, heartCornersEmpty = true, modelsPresent = true, tailsCorrect = true;
    bool timestampsFit = true, mappingRestored = false;
    gClient.SeedHistoryRenderCase(false);
    check(name("ten_rows_image").c_str(), SaveReleaseFixture(
        directory / (L"history-" + std::to_wstring(dpi) + L".png"), width, height, [&](HDC dc) {
      plane_pet_dpi::LogicalCanvas canvas(dc, kHistoryWidth, kHistoryContentHeight, width, height);
      gClient.DrawHistoryWindow(dc, RECT{0, 0, kHistoryWidth, kHistoryContentHeight}); GdiFlush();
      SIZE windowExt{}, viewportExt{};
      GetWindowExtEx(dc, &windowExt); GetViewportExtEx(dc, &viewportExt);
      mappingRestored = GetMapMode(dc) == MM_ANISOTROPIC &&
          windowExt.cx * viewportExt.cy == windowExt.cy * viewportExt.cx &&
          MulDiv(1000, viewportExt.cx, windowExt.cx) == MulDiv(1000, dpi, 96);
      for (int row = 0; row < 10; ++row) {
        const int top = kFirstRowTop + row * kRowPitch;
        const COLORREF background = row % 2 == 0 ? RGB(20, 26, 39) : RGB(16, 21, 33);
        for (int side = 0; side < 2; ++side) {
          const int firstHeart = side == 0 ? kOwnHeartX : kPeerHeartX;
          const int hp = side == 0 ? own[row] : peer[row];
          for (int slot = 0; slot < plink::kInitialHealth; ++slot) {
            const int cx = firstHeart + slot * kHeartPitch, cy = top + kHeartY;
            const auto expectedColor = [&](int offset) {
              const COLORREF pixel = GetPixel(dc, cx, cy + offset);
              const int r = GetRValue(pixel), g = GetGValue(pixel), b = GetBValue(pixel);
              if (pixel == CLR_INVALID) return false;
              if (slot < hp) return side == 0 ? (b > 200 && g > 100 && r < 50) :
                  (r > 200 && g < 110 && b < 130);
              return r >= 30 && r < 65 && g >= 34 && g < 70 && b >= 38 && b < 85;
            };
            heartsCorrect = heartsCorrect && expectedColor(0) && expectedColor(3);
            // A rectangle would fill these lower corners; a heart does not.
            heartCornersEmpty = heartCornersEmpty &&
                GetPixel(dc, cx - 8, cy + 7) == background &&
                GetPixel(dc, cx + 8, cy + 7) == background;
            // At 125%, GDI rounds a logical center of 187.5 to pixel 188,
            // while GDI+ samples the pixel centered at 187.5 (pixel 187).
            // Search the adjacent physical pixels, not a rounded logical
            // point which can land on the notch's one-pixel border.
            POINT notch{cx, cy - 7}; LPtoDP(dc, &notch, 1);
            const int notchSaved = SaveDC(dc);
            SetMapMode(dc, MM_TEXT); SetWindowOrgEx(dc, 0, 0, nullptr); SetViewportOrgEx(dc, 0, 0, nullptr);
            bool notchOpen = false;
            for (int px = notch.x - 1; px <= notch.x + 1; ++px)
              notchOpen = notchOpen || GetPixel(dc, px, notch.y) == background;
            RestoreDC(dc, notchSaved);
            heartCornersEmpty = heartCornersEmpty && notchOpen;
          }
          const int cx = side == 0 ? kOwnPlaneX : kPeerPlaneX, cy = top + kRowHeight / 2;
          int body = 0, flames = 0, flameX = 0, flameY = 0;
          POINT pixels[] = {{cx - 14, cy - 14}, {cx + 15, cy + 15}, {cx, cy}};
          LPtoDP(dc, pixels, 3);
          const int saved = SaveDC(dc);
          SetMapMode(dc, MM_TEXT); SetWindowOrgEx(dc, 0, 0, nullptr); SetViewportOrgEx(dc, 0, 0, nullptr);
          // Inspect every physical pixel. Sampling logical integer coordinates
          // skips half the pixels at 200%, including one-pixel sprite flames.
          for (int y = pixels[0].y; y < pixels[1].y; ++y) for (int x = pixels[0].x; x < pixels[1].x; ++x) {
            const COLORREF color = GetPixel(dc, x, y);
            const int r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
            if (side == 0 ? (b > r + 45 && b > 100) : (r > b + 50 && r > g + 30 && r > 130)) ++body;
            // Yellow/orange family, rather than one particular yellow texel.
            if (r > 200 && g > 100 && g > b * 1.8) { ++flames; flameX += x; flameY += y; }
          }
          RestoreDC(dc, saved);
          modelsPresent = modelsPresent && body > 15 && flames > 0;
          // Nose points upper-left after CCW rotation; flame is lower-right.
          const int margin = plane_pet_dpi::Scale(2, dpi);
          tailsCorrect = tailsCorrect && flames > 0 &&
              flameX > (pixels[2].x + margin) * flames && flameY > (pixels[2].y + margin) * flames;
        }
      }
      HFONT font = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
          VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
      const auto previous = SelectObject(dc, font);
      SIZE extent{};
      timestampsFit = GetTextExtentPoint32W(dc, timestamp.c_str(), static_cast<int>(timestamp.size()), &extent) &&
          extent.cx <= kHistoryWidth - 30 - kTimeLeft && extent.cy <= kRowHeight;
      SelectObject(dc, previous); DeleteObject(font);
    }));
    check(name("hp_colors_and_grey_slots").c_str(), heartsCorrect);
    check(name("slots_remain_heart_shaped").c_str(), heartCornersEmpty);
    check(name("red_blue_models_present").c_str(), modelsPresent);
    check(name("plane_orientation_and_centers").c_str(), tailsCorrect);
    check(name("timestamp_fits").c_str(), timestampsFit);
    check(name("sprite_preserves_dpi_mapping").c_str(), mappingRestored);
    gClient.SeedHistoryRenderCase(true);
    check(name("empty_image").c_str(), SaveReleaseFixture(
        directory / (L"history-empty-" + std::to_wstring(dpi) + L".png"), width, height, [&](HDC dc) {
      plane_pet_dpi::LogicalCanvas canvas(dc, kHistoryWidth, kHistoryContentHeight, width, height);
      gClient.DrawHistoryWindow(dc, RECT{0, 0, kHistoryWidth, kHistoryContentHeight});
    }));
  }
  for (int state = 0; state < 3; ++state) {
    gClient.SeedHistoryWarningForTest(state == 2, state == 0);
    check(("history_storage_warning_image_" + std::to_string(state)).c_str(), SaveReleaseFixture(
        directory / (L"history-storage-" + std::to_wstring(state) + L".png"),
        kHistoryWidth, kHistoryContentHeight, [&](HDC dc) {
      gClient.DrawHistoryWindow(dc, RECT{0, 0, kHistoryWidth, kHistoryContentHeight});
    }));
  }
  gClient.SeedHistoryRenderCase(false);
  bool handlesStable = false;
  check("history_repeat_render_fixture", SaveReleaseFixture(directory / L"history-repeat.png",
      kHistoryWidth, kHistoryContentHeight, [&](HDC dc) {
    const RECT rect{0, 0, kHistoryWidth, kHistoryContentHeight};
    gClient.DrawHistoryWindow(dc, rect); GdiFlush();
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 20; ++i) gClient.DrawHistoryWindow(dc, rect);
    GdiFlush();
    handlesStable = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before;
  }));
  check("history_regions_and_brushes_no_gdi_leak", handlesStable);
  ShowInfoWindow(window, InfoWindowMode::History);
  check("history_actual_window_opens", gInfoWindow && IsWindowVisible(gInfoWindow));
  if (gInfoWindow) {
    RECT client{}; GetClientRect(gInfoWindow, &client);
    check("history_actual_window_retains_size", client.right == plane_pet_dpi::Scale(kHistoryWidth, gInfoDpi) &&
        client.bottom == plane_pet_dpi::Scale(kHistoryContentHeight, gInfoDpi));
    check("history_actual_window_paints", RedrawWindow(gInfoWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
    SendMessageW(gInfoWindow, WM_CLOSE, 0, 0);
  }
  check("history_close_keeps_pet_alive", !gInfoWindow && IsWindow(window));
  check("history_save_load_counts_and_recent_ten_preserved", gClient.RunHistoryPersistenceSelfTest());
  check("history_change_preserves_combat_heart_colors", gClient.RunColoredHeartHudSelfTest());
}
