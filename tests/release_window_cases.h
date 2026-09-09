// Included only in the dedicated isolated release-regression executable.
template <typename Draw>
bool SaveReleaseFixture(const std::filesystem::path &path, int width, int height, Draw draw) {
  HDC screen = GetDC(nullptr);
  HDC memory = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
  ReleaseDC(nullptr, screen);
  if (!memory || !bitmap) {
    if (memory) DeleteDC(memory);
    if (bitmap) DeleteObject(bitmap);
    return false;
  }
  const auto old = SelectObject(memory, bitmap);
  draw(memory);
  GdiFlush();
  SelectObject(memory, old);
  bool saved = false;
  {
    Gdiplus::Bitmap image(bitmap, nullptr);
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    std::vector<unsigned char> storage(size);
    auto *encoders = reinterpret_cast<Gdiplus::ImageCodecInfo *>(storage.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) == Gdiplus::Ok) {
      for (UINT index = 0; index < count; ++index)
        if (wcscmp(encoders[index].MimeType, L"image/png") == 0)
          saved = image.Save(path.c_str(), &encoders[index].Clsid, nullptr) == Gdiplus::Ok;
    }
  }
  DeleteObject(bitmap);
  DeleteDC(memory);
  return saved;
}

#include "invite_window_cases.h"
#include "dnd_window_cases.h"
#include "pet_text_window_cases.h"
#include "history_window_cases.h"
#include "game_heart_window_cases.h"
#include "help_window_cases.h"
#include "about_window_cases.h"
#include "toolbar_window_cases.h"
#include "idle_window_cases.h"

bool RunReleaseWindowCases(HWND window) {
  std::ofstream report(std::filesystem::path(Wide(Option("test-report", "release-test.txt"))));
  bool passed = true;
  const auto check = [&](const char *name, bool ok) {
    report << name << '=' << (ok ? "PASS" : "FAIL") << '\n';
    passed = passed && ok;
  };
  check("actual_unicode_command_line", gClient.OwnName() == L"测试 用户");
  const auto value = L"C:\\测试 用户\\引号\"\\";
  const auto command = L"program.exe --state=" + plane_pet_windows::Quote(value);
  check("quote_roundtrip", plane_pet_windows::OptionFrom(command.c_str(), L"state") == value);
  check("option_boundary", plane_pet_windows::OptionFrom(
      L"program.exe --other=--state=bad --state=good", L"state") == L"good");
  check("missing_option", plane_pet_windows::OptionFrom(L"program.exe", L"state", L"fallback") == L"fallback");
  const auto state = std::filesystem::path(Wide(Option("state", "")));
  check("diagnostic_fixture", SaveReleaseFixture(state.parent_path() / L"diagnostics.png", 700, 720,
      [&](HDC dc) { gClient.DrawDiagnosticsWindow(dc, RECT{0, 0, 700, 720}); }));
  check("help_tail_fixture", SaveReleaseFixture(state.parent_path() / L"help-tail.png", 700, 740,
      [&](HDC dc) {
        const int saved = SaveDC(dc);
        SetWindowOrgEx(dc, 0, 740, nullptr);
        gClient.DrawHelpWindow(dc, RECT{0, 740, 700, 1480});
        RestoreDC(dc, saved);
      }));
  check("unicode_profile_saved", std::filesystem::is_regular_file(state));
  const auto networkPath = state.parent_path() / L"网络 状态.status";
  check("network_status_write", plane_pet_network::Write(networkPath,
      plane_pet_network::State::CapacityFull, 503));
  const auto network = plane_pet_network::Read(networkPath);
  check("network_status_read", network.state == plane_pet_network::State::CapacityFull && network.error == 503);
  check("stale_status_not_online", plane_pet_network::Describe(network, network.tick + 46000).find(L"暂无新状态") != std::wstring::npos);
  check("bounded_reconnect_backoff", plane_pet_network::RetryMilliseconds(0, 1) == 1001 &&
      plane_pet_network::RetryMilliseconds(99, 9999) == 32999);
  const auto diagnostics = gClient.DiagnosticText();
  check("diagnostics_redacted", diagnostics.find(L"测试 用户") == std::wstring::npos &&
      diagnostics.find(state.wstring()) == std::wstring::npos);
  AddTray(window);
  RemoveTray();
  SendMessageW(window, gTaskbarCreatedMessage, 0, 0);
  check("taskbar_recreation", gTrayAdded);
  SendMessageW(window, gTaskbarCreatedMessage, 0, 0);
  check("duplicate_taskbar_notification", gTrayAdded);
  SetPropW(window, plane_pet_windows::kPublicWindowProperty, reinterpret_cast<HANDLE>(1));
  gClient.HideByUser();
  check("test_instance_hidden", !IsWindowVisible(window));
  const bool sent = plane_pet_windows::ActivatePublicInstance(GetCurrentProcessId());
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  check("second_launch_activation", sent && IsWindowVisible(window));
  check("unrelated_process_ignored", !plane_pet_windows::ActivatePublicInstance(0xffffffff));
  // A fresh named event can only be signalled by the initialized UI pump.
  HANDLE observer = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  DuplicateHandle(GetCurrentProcess(), observer, GetCurrentProcess(), &gReadyEvent,
                  EVENT_MODIFY_STATE, FALSE, 0);
  gReadyAt = GetTickCount64() + 2000;
  SendMessageW(window, WM_TIMER, kNetworkPumpTimer, 0);
  check("no_early_health_ack", WaitForSingleObject(observer, 0) == WAIT_TIMEOUT);
  gReadyAt = 0;
  { ModalScope scope; SendMessageW(window, WM_TIMER, kNetworkPumpTimer, 0); }
  check("modal_does_not_ack_health", WaitForSingleObject(observer, 0) == WAIT_TIMEOUT);
  SendMessageW(window, WM_TIMER, kNetworkPumpTimer, 0);
  check("initialized_pump_acknowledges", WaitForSingleObject(observer, 0) == WAIT_OBJECT_0);
  CloseHandle(observer);
  RunMenuWindowCases(window, check);
  const UINT originalDpi = gClient.UiDpi();
  const auto outside = plane_pet_dpi::LogicalPoint(POINT{-1, -1}, 560, 300, 280, 150);
  check("dpi_negative_capture_point_stays_outside", outside.x == -1 && outside.y == -1);
  const RECT fitted = plane_pet_dpi::FitWorkArea(RECT{4000, -900, 4560, -600}, RECT{-1920, 0, 0, 1080});
  check("dpi_negative_monitor_and_removed_display", fitted.left == -560 && fitted.top == 0 &&
      fitted.right == 0 && fitted.bottom == 300);
  for (const UINT dpi : {96U, 120U, 144U, 192U}) {
    const auto named = [&](const char *label, bool ok) {
      const auto name = std::string(label) + "_" + std::to_string(dpi);
      check(name.c_str(), ok);
    };
    gClient.SeedDpiRenderCase(false);
    RECT desired{80, 80, 80 + plane_pet_dpi::Scale(kPetWidth, dpi),
                          80 + plane_pet_dpi::Scale(kPetHeight, dpi)};
    SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi), reinterpret_cast<LPARAM>(&desired));
    RECT actual{};
    GetClientRect(window, &actual);
    named("dpi_pet_retains_compact_size", actual.right == kPetWidth &&
        actual.bottom == kPetHeight);
    POINT click{194, 130};
    const POINT logical = gClient.LogicalClientPoint(click);
    gClient.SetToolbarTestState(true);
    named("dpi_pet_emote_hit", std::abs(logical.x - 194) <= 1 && std::abs(logical.y - 130) <= 1 &&
        gClient.IsInteractivePetPoint(logical.x, logical.y));
    named("dpi_pet_image", SaveReleaseFixture(state.parent_path() / (L"pet-" + std::to_wstring(dpi) + L".png"),
        actual.right, actual.bottom, [&](HDC dc) { gClient.Draw(dc, actual); }));
    ShowUpdateWindow(window, false);
    RECT card{};
    GetClientRect(gUpdateWindow, &card);
    named("dpi_update_retains_compact_size", card.right == kUpdateCardWidth &&
        card.bottom == kUpdateCardHeight);
    const LPARAM close = MAKELPARAM(337, 23);
    SendMessageW(gUpdateWindow, WM_LBUTTONDOWN, MK_LBUTTON, close);
    SendMessageW(gUpdateWindow, WM_LBUTTONUP, 0, close);
    named("dpi_update_close_hit", gUpdateWindow == nullptr);
    gClient.SeedDpiRenderCase(true);
    GetClientRect(window, &actual);
    named("dpi_game_retains_compact_size", actual.right == kGameClientWidth &&
        actual.bottom == kGameClientHeight);
    const int width = kGameClientWidth;
    const int height = kGameClientHeight;
    const auto target = plane_pet_dpi::LogicalPoint(POINT{width - 1, height - 1}, width, height,
        kGameClientWidth, kGameClientHeight);
    const GameLayout layout = MakeGameLayout(kGameClientWidth, kGameClientHeight);
    named("dpi_game_full_world_mouse_mapping", target.x == 239 && layout.WorldYFromScreen(target.y) == 319 &&
        layout.WorldYFromScreen(kGameHudHeight) == 0);
    named("dpi_game_image", SaveReleaseFixture(state.parent_path() / (L"game-" + std::to_wstring(dpi) + L".png"),
        width, height, [&](HDC dc) { gClient.Draw(dc, RECT{0, 0, width, height}); }));
    gClient.FinishDpiRenderCase();
    GetClientRect(window, &actual);
    named("dpi_finished_returns_to_compact_pet", !gClient.IsGameMode() && IsWindowVisible(window) &&
        actual.right == kPetWidth && actual.bottom == kPetHeight);
  }
  RunInviteWindowCases(window, state.parent_path(), check);
  RunDndWindowCases(window, state.parent_path(), check);
  RunToolbarWindowCases(window, state.parent_path(), check);
  RunIdleWindowCases(window, state.parent_path(), check);
  RunPetTextWindowCases(window, state.parent_path(), check);
  RunHistoryWindowCases(window, state.parent_path(), check);
  RunGameHeartWindowCases(state.parent_path(), check);
  RunHelpWindowCases(window, state.parent_path(), check);
  RunAboutWindowCases(window, state.parent_path(), check);
  gClient.ChangeDpi(originalDpi);
  RemoveTray();
  return passed && report.good();
}
