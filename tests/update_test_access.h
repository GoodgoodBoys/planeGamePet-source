#pragma once
#ifndef PLANE_PET_UPDATE_SELF_TEST
#error Update test access must never be included in a release build.
#endif
#include "../desktop/update_manager.h"
#include "../common/app_version.h"

namespace plane_pet_update {
// Simulate transport completion at controlled times. Release code always
// reaches these publication methods through HTTPS, signature/hash validation.
struct ManagerTestAccess {
  static std::string FutureVersion() {
    if (plane_pet_version::kPatch == 255)
      return std::to_string(static_cast<unsigned>(plane_pet_version::kMajor) +
          (plane_pet_version::kMinor == 255 ? 1U : 0U)) + "." +
          std::to_string(plane_pet_version::kMinor == 255 ? 0U :
          static_cast<unsigned>(plane_pet_version::kMinor) + 1U) + ".0";
    return std::to_string(plane_pet_version::kMajor) + "." +
        std::to_string(plane_pet_version::kMinor) + "." +
        std::to_string(static_cast<unsigned>(plane_pet_version::kPatch) + 1U);
  }
  static std::string FutureMajorVersion() {
    return std::to_string(static_cast<unsigned>(plane_pet_version::kMajor) + 1U) + ".0.0";
  }
  static void Seed(Manager &manager, State state, bool manual = true,
                   bool required = false, bool downloadable = false) {
    if (manager.worker_.joinable()) manager.worker_.join();
    std::lock_guard<std::mutex> lock(manager.mutex_);
    const uint64_t generation = manager.snapshot_.generation;
    manager.snapshot_ = Snapshot{};
    manager.snapshot_.state = state;
    manager.snapshot_.manual = manual;
    manager.snapshot_.required = required;
    manager.snapshot_.canDownload = downloadable;
    manager.snapshot_.currentVersion = plane_pet_version::kWideString;
    const std::string future = FutureVersion();
    manager.snapshot_.latestVersion = downloadable
        ? std::wstring(future.begin(), future.end()) : plane_pet_version::kWideString;
    manager.snapshot_.message = L"窗口回归测试";
    manager.snapshot_.generation = generation + 1;
    manager.cancel_.store(false);
    manager.installRequestReady_.store(false);
    manager.pendingManualCheck_ = manager.pendingRequiredCheck_ = false;
  }
  static Manager::Manifest Manifest(const std::string &version = FutureVersion()) {
    Manager::Manifest manifest;
    manifest.version = version;
    manifest.minimumVersion = "1.0.0";
    manifest.artifactUrl = "invalid";
    manifest.sha256 = std::string(64, 'a');
    manifest.size = 1;
    manifest.summary[0] = L"测试变更";
    return manifest;
  }
  static void CheckResult(Manager &manager, const Manager::Manifest &manifest) {
    manager.PublishCheckResult(manifest);
  }
  static void Error(Manager &manager) { manager.SetError(L"迟到的网络错误"); }
  static void Install(Manager &manager, const std::filesystem::path &package) {
    manager.PublishInstallRequest(Manifest(), package);
  }
  static void SetWorkerBusy(Manager &manager, bool busy) {
    manager.workerRunning_.store(busy);
  }
  static void MakeAutomaticCheckDue(Manager &manager) {
    manager.nextAutomaticCheckMs_ = 0;
  }
  static bool ShortAutomaticRetryScheduled(Manager &manager) {
    return manager.failedAutomaticChecks_ == 1 && manager.nextAutomaticCheckMs_ != 0;
  }
  static void MarkOptionalPeerHandled(Manager &manager) {
    manager.peerRequirementHandled_ = true;
    manager.lastPeerMajorRequirement_ = false;
    manager.lastPeerVersionKey_ = 0x010002;
  }
};
}  // namespace plane_pet_update
