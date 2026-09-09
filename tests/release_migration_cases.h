#pragma once

// Synthetic historical formats, never the user's real profile or credentials.
int RunReleaseMigrationCases(const std::filesystem::path &root) {
  const auto dir = root / L"manual-migration";
  std::filesystem::create_directories(dir);
  const auto read = [](const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), {});
  };
  const auto write = [](const std::filesystem::path &path, const std::string &text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    return static_cast<bool>(stream);
  };
  for (int settingsVersion = 1; settingsVersion <= 3; ++settingsVersion) {
    for (int historyVersion = 1; historyVersion <= 2; ++historyVersion) {
      PetClient migrated;
      migrated.statePath_ = dir / L"public-single.binding";
      migrated.settingsPath_ = dir / L"public-single.settings";
      migrated.historyPath_ = dir / L"public-single.history";
      const std::string binding = "PLANE_PET_CLIENT 1 121 222 333 444 122 2\n";
      const std::string settings = settingsVersion == 1 ? "PLANE_PET_SETTINGS 1 0 1\n"
          : settingsVersion == 2 ? "PLANE_PET_SETTINGS 2 0 0 1 0\n"
          : "PLANE_PET_SETTINGS 3 0 0 1 0 1790000000000\n";
      const std::string history = "PLANE_PET_HISTORY " + std::to_string(historyVersion) +
          " 3 1 1 1" + (historyVersion == 2 ? " 789\n" : "\n") +
          "1789000000000 2 0 1\n1788999990000 0 1 -1\n1788999980000 1 1 0\n";
      if (!write(migrated.statePath_, binding) || !write(migrated.settingsPath_, settings) ||
          !write(migrated.historyPath_, history)) return 801;
      migrated.LoadState();
      migrated.LoadSettings();
      migrated.LoadHistory();
      if (migrated.clientId_ != 121 || migrated.bindingId_ != 222 ||
          migrated.tokenLow_ != 333 || migrated.tokenHigh_ != 444 ||
          migrated.peerDeviceId_ != 122 || migrated.slot_ != 2 ||
          migrated.telemetryEnabled_ || !migrated.doNotDisturb_ ||
          !migrated.telemetryChoiceKnown_ || migrated.historyTotal_ != 3 ||
          migrated.historyWins_ != 1 || migrated.historyLosses_ != 1 ||
          migrated.historyDraws_ != 1 || migrated.recentHistory_.size() != 3 ||
          migrated.recentHistory_[0].ownHealth != 2 ||
          (settingsVersion >= 2 && (migrated.pairingPanelVisible_ || migrated.telemetryUploadChoice_ != 0)) ||
          (settingsVersion == 3 && migrated.updateSnoozeUntilMs_ != 1790000000000ULL) ||
          (historyVersion == 2 && migrated.lastRecordedRoundId_ != 789)) return 802;
      if (read(migrated.statePath_) != binding || read(migrated.settingsPath_) != settings ||
          read(migrated.historyPath_) != history) return 803;
      if (!migrated.SaveState() || !migrated.SaveSettings() || !migrated.SaveHistory()) return 804;
      auto savedBinding = read(migrated.statePath_);
      // Native Windows text output uses CRLF; compare values, not line endings.
      savedBinding.erase(std::remove(savedBinding.begin(), savedBinding.end(), '\r'), savedBinding.end());
      if (savedBinding != binding) return 805;
    }
  }
  std::puts("PUBLIC_1_0_0_MANUAL_MIGRATION_6_HISTORICAL_FORMATS_OK");
  return 0;
}
