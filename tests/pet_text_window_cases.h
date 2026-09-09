// Native GDI regression: real font metrics, unclipped multi-line content,
// DC/GDI lifetime, 1:1 old/new comparison, and actual client render fixtures.
template <typename Check>
void RunPetTextWindowCases(HWND window, const std::filesystem::path &directory, Check check) {
  using plane_pet_text::CreatePetFont;
  using plane_pet_text::PetText;
  HDC metrics = GetDC(window);
  for (const int em : {11, 12, 13, 14, 15, 16, 18, 19, 20}) {
    const auto label = "pet_text_font_" + std::to_string(em);
    HFONT font = CreatePetFont(em, FW_BOLD);
    LOGFONTW description{};
    check((label + "_explicit_em_and_cleartype").c_str(), font &&
        GetObjectW(font, sizeof(description), &description) &&
        description.lfHeight == -em && description.lfQuality == CLEARTYPE_QUALITY);
    const auto old = SelectObject(metrics, font);
    TEXTMETRICW measured{};
    wchar_t face[128]{};
    check((label + "_realized_em").c_str(), GetTextMetricsW(metrics, &measured) &&
        measured.tmHeight - measured.tmInternalLeading == em);
    check((label + "_font_available").c_str(), GetTextFaceW(metrics, 128, face) &&
        wcscmp(face, L"Microsoft YaHei UI") == 0);
    SelectObject(metrics, old); DeleteObject(font);
  }
  const auto fits = [&](const wchar_t *text, int width, int height, int em, int weight, bool wrap) {
    HFONT font = CreatePetFont(em, weight);
    const auto old = SelectObject(metrics, font);
    RECT calculated{0, 0, width, height};
    const int result = DrawTextW(metrics, text, -1, &calculated,
        DT_CALCRECT | DT_NOPREFIX | (wrap ? DT_WORDBREAK : DT_SINGLELINE));
    SelectObject(metrics, old); DeleteObject(font);
    return result > 0 && calculated.right <= width && calculated.bottom <= height;
  };
  check("pet_text_pairing_title_fits", fits(L"搜索电脑中", 154, 31, 18, FW_BOLD, false));
  check("pet_text_pairing_code_fits", fits(L"匹配码  123456", 172, 31, 19, FW_BOLD, false));
  check("pet_text_pairing_prompt_fits", fits(L"请输入六位匹配码", 172, 30, 15, FW_NORMAL, false));
  check("pet_text_pairing_digits_fit", fits(L"1  2  3  4  5  6", 170, 33, 20, FW_BOLD, false));
  check("pet_text_pairing_hint_fits", fits(L"点击输入 · Enter", 172, 35, 15, FW_NORMAL, false));
  check("pet_text_pairing_wait_fits", fits(L"等待同码电脑…\n右键或 Esc 停止", 172, 42, 12, FW_NORMAL, true));
  check("pet_text_pairing_connect_fits", fits(L"连接匹配服务…\n右键或 Esc 停止", 172, 42, 12, FW_NORMAL, true));
  check("pet_text_pairing_long_error_fits", fits(L"无法写入身份存档，请检查存档目录权限", 172, 36, 12, FW_NORMAL, true));
  check("pet_text_unbound_dnd_error_fits", fits(L"设置仅本次生效，保存失败", 172, 30, 13, FW_NORMAL, true));
  check("pet_text_connection_notice_fits", fits(L"服务器连接中断\n本局已结束", 152, 39, 14, FW_BOLD, true));
  check("pet_text_settings_notice_fits", fits(L"设置仅本次生效\n无法保存，请重试", 152, 39, 14, FW_BOLD, true));
  check("pet_text_feedback_fits", fits(L"对方拒绝邀请", 204, 24, 15, FW_BOLD, false));
  ReleaseDC(window, metrics);

  check("pet_text_quality_comparison_image", SaveReleaseFixture(directory / L"pet-text-quality.png", 600, 300,
      [&](HDC dc) {
    const HBRUSH background = CreateSolidBrush(kCard);
    RECT canvas{0, 0, 600, 300}; FillRect(dc, &canvas, background); DeleteObject(background);
    for (int row = 0; row < 5; ++row) for (int column = 0; column < 3; ++column) {
      const int em = row < 3 ? 16 : (row == 3 ? 14 : 28);
      const int weight = row == 1 ? FW_NORMAL : FW_BOLD;
      const wchar_t *face = row == 2 ? L"Microsoft YaHei" : L"Microsoft YaHei UI";
      HFONT font = CreateFontW(-em, 0, 0, 0, weight, FALSE, FALSE, FALSE,
          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, 4 + column,
          VARIABLE_PITCH | FF_SWISS, face);
      const auto previous = SelectObject(dc, font);
      SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kWhite);
      RECT area{column * 200, row * 60, (column + 1) * 200, (row + 1) * 60};
      DrawTextW(dc, L"已开启勿扰", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(dc, previous); DeleteObject(font);
    }
  }));

  bool dcRestored = false, handlesStable = false, hasSmoothedEdges = false;
  check("pet_text_comparison_image", SaveReleaseFixture(directory / L"pet-text-before-after.png", 560, 200,
      [&](HDC dc) {
    const HBRUSH background = CreateSolidBrush(kCard);
    RECT canvas{0, 0, 560, 200}; FillRect(dc, &canvas, background); DeleteObject(background);
    PetText(dc, L"1.0.5", RECT{0, 0, 280, 30}, 14, kMuted);
    PetText(dc, L"1.0.6", RECT{280, 0, 560, 30}, 14, kMuted);
    const wchar_t *messages[] = {L"已开启勿扰", L"对方邀请你开一局...", L"等待对方响应...   300s", L"接受      拒绝      取消"};
    for (int row = 0; row < 4; ++row) {
      const RECT area{0, 30 + row * 40, 280, 70 + row * 40};
      const int oldHeight = row == 0 ? 21 : 18;
      HFONT oldFont = CreateFontW(oldHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
          VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
      const auto previous = SelectObject(dc, oldFont);
      SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kWhite);
      RECT oldArea = area;
      DrawTextW(dc, messages[row], -1, &oldArea, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(dc, previous); DeleteObject(oldFont);
      PetText(dc, messages[row], RECT{area.left + 280, area.top, area.right + 280, area.bottom},
          row == 0 ? 16 : 14, kWhite, FW_BOLD);
    }
    const auto previousFont = GetCurrentObject(dc, OBJ_FONT);
    SetBkMode(dc, OPAQUE); SetTextColor(dc, RGB(11, 22, 33));
    const DWORD handlesBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    // An empty rectangle exercises font lifecycle without changing the fixture.
    for (int index = 0; index < 1000; ++index)
      PetText(dc, L"测试", RECT{0, 0, 0, 0}, 14, kWhite);
    GdiFlush();
    dcRestored = GetCurrentObject(dc, OBJ_FONT) == previousFont &&
        GetBkMode(dc) == OPAQUE && GetTextColor(dc) == RGB(11, 22, 33);
    handlesStable = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handlesBefore;
    int blended = 0;
    for (int y = 30; y < 70; ++y) for (int x = 300; x < 540; ++x) {
      const COLORREF color = GetPixel(dc, x, y);
      if (color != CLR_INVALID && color != kCard && color != kWhite) ++blended;
    }
    hasSmoothedEdges = blended > 30;
  }));
  check("pet_text_dc_state_restored", dcRestored);
  check("pet_text_1000_draws_no_gdi_leak", handlesStable);
  check("pet_text_native_smoothed_edges", hasSmoothedEdges);

  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    for (int state = 0; state < 8; ++state) {
      gClient.SeedPetTextRenderCase(state); gClient.ChangeDpi(dpi);
      RECT before{}, after{}, client{}; GetWindowRect(window, &before); GetClientRect(window, &client);
      const auto label = "pet_text_state_" + std::to_string(state) + "_" + std::to_string(dpi);
      check((label + "_image").c_str(), SaveReleaseFixture(directory / (Wide(label) + L".png"),
          client.right, client.bottom, [&](HDC dc) { gClient.Draw(dc, client); }));
      GetWindowRect(window, &after);
      check((label + "_window_stable").c_str(), EqualRect(&before, &after) &&
          client.right == kPetWidth && client.bottom == kPetHeight);
    }
  }
  gClient.SeedDndRenderCase(false, false, true, false, false);
}
