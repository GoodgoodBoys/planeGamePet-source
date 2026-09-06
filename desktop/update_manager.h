#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace plane_pet_update {

constexpr UINT kChangedMessage = WM_APP + 40;
constexpr UINT kInstallMessage = WM_APP + 41;

enum class State {
  Disabled,
  Idle,
  Checking,
  Current,
  Available,
  Required,
  Downloading,
  Error,
};

struct Snapshot {
  State state = State::Disabled;
  std::wstring currentVersion;
  std::wstring latestVersion;
  std::wstring summary[3];
  std::wstring message;
  unsigned progressPercent = 0;
  bool manual = false;
  bool required = false;
  bool canDownload = false;
  uint64_t generation = 0;
};

// Idle/Disabled are never prompts, even if stale intent flags are present.
bool ShouldShowPrompt(const Snapshot &snapshot, bool petVisible);

class Manager {
 public:
  struct Manifest {
    std::string version;
    std::string minimumVersion;
    std::string artifactUrl;
    std::string sha256;
    uint64_t size = 0;
    std::wstring summary[3];
  };

  Manager() = default;
  ~Manager();
  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;

  void Configure(HWND owner, std::string manifestUrl,
                 std::filesystem::path requestPath, bool enabled,
                 uint64_t optionalSnoozeUntilMs);
  void Tick(bool idleUiAvailable, bool peerVersionDiffers,
            bool localUpdateAvailable, bool peerMajorMismatch,
            uint32_t peerVersionKey = 0);
  void CheckNow(bool requiredByPeer = false);
  bool AcceptAndDownload();
  void Dismiss();
  void SetOptionalSnoozeUntil(uint64_t unixTimeMs);
  Snapshot GetSnapshot() const;
  bool Enabled() const;
  bool HasInstallRequest() const;
  void ReportInstallFailure(const std::wstring &message);

 private:
#ifdef PLANE_PET_UPDATE_SELF_TEST
  friend struct ManagerTestAccess;
#endif
  void StartCheck(bool manual, bool requiredByPeer);
  void CheckWorker();
  void PublishCheckResult(const Manifest &manifest);
  void DownloadWorker(Manifest manifest);
  void SetError(const std::wstring &message);
  void PublishInstallRequest(const Manifest &manifest,
                             const std::filesystem::path &package);
  void JoinFinishedWorker();

  HWND owner_ = nullptr;
  std::string manifestUrl_;
  std::filesystem::path requestPath_;
  mutable std::mutex mutex_;
  Snapshot snapshot_{};
  Manifest manifest_{};
  std::thread worker_;
  std::atomic<bool> workerRunning_{false};
  std::atomic<bool> cancel_{false};
  std::atomic<bool> installRequestReady_{false};
  std::atomic<unsigned> downloadProgress_{0};
  uint64_t optionalSnoozeUntilMs_ = 0;
  uint64_t nextAutomaticCheckMs_ = 0;
  bool peerRequirementHandled_ = false;
  bool lastPeerMajorRequirement_ = false;
  uint32_t lastPeerVersionKey_ = 0;
  bool pendingManualCheck_ = false;
  bool pendingRequiredCheck_ = false;
};

}  // namespace plane_pet_update
