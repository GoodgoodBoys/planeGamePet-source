// Exercise the actual game renderer and embedded assets, not a mock diagram.
#define main IncludedClientRegressionMain
#include "client_edge_test.cpp"
#undef main

bool SaveBattleRender(PetClient &client, const std::filesystem::path &path) {
  BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = kGameClientWidth; info.bmiHeader.biHeight = -kGameClientHeight;
  info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
  void *pixels = nullptr; HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  bool ok = false;
  if (dc && bitmap && pixels) {
    auto old = SelectObject(dc, bitmap);
    client.DrawGameWindow(dc, kGameClientWidth, kGameClientHeight); GdiFlush();
    BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + kGameClientWidth * kGameClientHeight * 4;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(&header), sizeof(header));
    output.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    output.write(static_cast<const char *>(pixels), kGameClientWidth * kGameClientHeight * 4);
    ok = output.good(); SelectObject(dc, old);
  }
  if (bitmap) DeleteObject(bitmap);
  if (dc) DeleteDC(dc);
  return ok;
}
int main(int argc, char **argv) {
  if (argc != 2 || !gGdiPlusSession.ready()) return 1;
  const auto directory = std::filesystem::u8path(argv[1]);
  std::filesystem::create_directories(directory);
  std::ofstream report(directory / "contacts.csv");
  report << "side,case,hit,tick,alpha,shown_hp,bullet_count,contact_x,contact_y\n";
  const uint8_t keys[]{0, plink::InputDown, plink::InputUp, plink::InputLeft,
      plink::InputRight, plink::InputLeft | plink::InputDown, plink::InputRight | plink::InputUp};
  unsigned files = 0;
  for (unsigned side = 0; side < 2; ++side) for (unsigned test = 0; test < std::size(keys); ++test) {
    PetClient client; client.slot_ = static_cast<uint8_t>(side + 1);
    if (!client.PetPlaneBitmap(false) || !client.PetPlaneBitmap(true) || !client.HeartAtlasBitmap()) return 2;
    client.haveSnapshot_ = client.battleSupported_ = client.motionSupported_ = true;
    client.snapshot_.phase = plink::GamePhase::Playing; client.snapshot_.onlineMask = 3;
    client.snapshot_.phaseRemainingMs = 175000; client.rttMs_ = 150;
    pcbattle::World base; base.tick = 60;
    const auto world = [side](int x, int y) { return pcbattle::Point{
        int(side ? 239 - x : x) * pcbattle::kUnit, int(side ? 319 - y : y) * pcbattle::kUnit}; };
    base.players[side] = world(120, 220);
    const int offset = keys[test] & plink::InputRight ? 2 : (keys[test] & plink::InputLeft ? -2 : 0);
    base.bullets[0] = {true, static_cast<uint16_t>(side ? 1 : 2), static_cast<uint8_t>(side ? 1 : 2), world(120 + offset, 197)};
    client.battlePredictor_.Reset(base, side);
    for (unsigned tick = 0; tick < 20 && !client.battlePredictor_.current.impactCount; ++tick)
      if (!client.battlePredictor_.Push({keys[test]})) return 3;
    const auto &current = client.battlePredictor_.current;
    const bool hit = current.impactCount != 0;
    const double time = hit ? current.impacts[0].fraction / 65535.0 : .5;
    const double samples[]{std::max(0.0, time - .08), std::min(1.0, time + .08), 1.0};
    for (unsigned frame = 0; frame < 3; ++frame) {
      const double alpha = samples[frame]; client.motionRemainder_ = static_cast<int64_t>(alpha * 1000000);
      const auto sample = pcbattle::Sample(client.battlePredictor_, alpha);
      unsigned count = 0; for (const auto &b : sample.bullets) if (b.active) ++count;
      report << side << ',' << test << ',' << hit << ',' << current.tick << ',' << alpha << ','
          << int(sample.health[side]) << ',' << count << ','
          << (hit ? current.impacts[0].contact.x / 256.0 : 0) << ','
          << (hit ? current.impacts[0].contact.y / 256.0 : 0) << '\n';
      if (hit && time > 0 && frame == 0 && sample.health[side] != 3) return 4;
      if (hit && frame > 0 && sample.health[side] != 2) return 5;
      const auto name = "side-" + std::to_string(side) + "-case-" + std::to_string(test) + "-frame-" + std::to_string(frame) + ".bmp";
      if (!SaveBattleRender(client, directory / name)) return 6;
      ++files;
    }
  }
  for (unsigned side = 0; side < 2; ++side) for (unsigned result = 0; result < 3; ++result) {
    PetClient client; client.slot_ = static_cast<uint8_t>(side + 1);
    client.haveSnapshot_ = client.battleSupported_ = client.motionSupported_ = true;
    client.snapshot_.phase = plink::GamePhase::Finished; client.snapshot_.onlineMask = 3;
    client.snapshot_.players[0] = {120, 264, static_cast<uint8_t>(result ? 2 : 0), 1};
    client.snapshot_.players[1] = {119, 55, static_cast<uint8_t>(result ? 2 : 1), 1};
    client.snapshot_.winnerSlot = result ? 0 : 2;
    client.snapshot_.endReason = result == 2 ? plink::MatchEndReason::ServerUnavailable :
        (result ? plink::MatchEndReason::TimeLimitDraw : plink::MatchEndReason::Destroyed);
    client.syncFailed_ = result == 2; client.rttMs_ = 150;
    pcbattle::World speculative; speculative.tick = 80;
    speculative.health[0] = speculative.health[1] = 0;
    speculative.players[0].x += 10 * pcbattle::kUnit;
    client.battlePredictor_.Reset(speculative, side);
    const auto name = "final-side-" + std::to_string(side) + "-result-" + std::to_string(result);
    const auto path = directory / (name + ".bmp"), reference = directory / (name + "-authority.bmp");
    if (!SaveBattleRender(client, path)) return 7;
    client.battleSupported_ = client.motionSupported_ = false;
    if (!SaveBattleRender(client, reference)) return 8;
    std::ifstream actualFile(path, std::ios::binary), expectedFile(reference, std::ios::binary);
    const std::string actual((std::istreambuf_iterator<char>(actualFile)), {});
    const std::string expected((std::istreambuf_iterator<char>(expectedFile)), {});
    if (actual != expected) return 9; // actual renderer must exactly match authority-only reference
    files += 2;
  }
  std::printf("BATTLE_REAL_RENDERER_CONTACT_HP_AND_FINAL_AUTHORITY_PIXEL_EQUAL_OK frames=%u path=%s\n", files, argv[1]);
}
