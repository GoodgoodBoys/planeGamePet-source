#include "update_test_access.h"
#include <chrono>
#include <cstdio>
#include <future>

using namespace plane_pet_update;
using Access = ManagerTestAccess;

int main() {
  unsigned failures = 0;
  const auto check = [&](const char *name, bool ok) {
    std::printf("%s=%s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
  };
  const auto root = std::filesystem::temp_directory_path() /
      (L"plane-pet-update-state-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  Manager manager;
  manager.Configure(nullptr, "invalid", root / "update.request", true, 0);
  const State states[] = {State::Checking, State::Current, State::Available,
                         State::Required, State::Downloading, State::Error};
  bool dismissed = true;
  for (const State state : states) {
    Access::Seed(manager, state, true, true, true);
    manager.Dismiss();
    const auto snapshot = manager.GetSnapshot();
    dismissed = dismissed && snapshot.state == State::Idle &&
        !snapshot.manual && !snapshot.required && !snapshot.canDownload &&
        snapshot.latestVersion.empty() && !ShouldShowPrompt(snapshot, true);
  }
  check("dismiss_all_states", dismissed);
  Snapshot stale;
  stale.manual = stale.required = true;
  stale.state = State::Idle;
  check("idle_never_reopens_with_stale_flags", !ShouldShowPrompt(stale, true));
  stale.state = State::Disabled;
  check("disabled_never_prompts", !ShouldShowPrompt(stale, true));

  // Synchronize a real background completion after Dismiss, without a flaky
  // public network or timing sleeps deciding whether the race was exercised.
  for (int scenario = 0; scenario < 3; ++scenario) {
    Access::Seed(manager, scenario == 2 ? State::Downloading : State::Checking);
    std::promise<void> release;
    auto gate = release.get_future();
    std::thread late([&, scenario] {
      gate.wait();
      if (scenario == 0) Access::CheckResult(manager, Access::Manifest());
      if (scenario == 1) Access::Error(manager);
      if (scenario == 2) Access::Install(manager, root / "package.download");
    });
    manager.Dismiss();
    release.set_value();
    late.join();
    check(scenario == 0 ? "late_success_ignored" :
          (scenario == 1 ? "late_error_ignored" : "late_install_ignored"),
          manager.GetSnapshot().state == State::Idle &&
          !manager.HasInstallRequest() &&
          !std::filesystem::exists(root / "update.request"));
  }

  Access::Seed(manager, State::Checking, true, true);
  Access::CheckResult(manager, Access::Manifest(plane_pet_version::kString));
  check("required_no_same_version_download",
        manager.GetSnapshot().state == State::Required &&
        !manager.GetSnapshot().canDownload && !manager.AcceptAndDownload());
  Access::Seed(manager, State::Checking, false, false);
  Access::CheckResult(manager, Access::Manifest());
  check("minor_release_optional", manager.GetSnapshot().state == State::Available &&
        !manager.GetSnapshot().required && manager.GetSnapshot().canDownload);
  manager.SetOptionalSnoozeUntil(UINT64_MAX);
  Access::MakeAutomaticCheckDue(manager);
  manager.Tick(true, true, true, false);
  check("snooze_blocks_optional_peer_prompt", manager.GetSnapshot().state == State::Idle);
  manager.Tick(true, true, true, true);
  check("major_requirement_bypasses_snooze", manager.GetSnapshot().required);
  manager.Dismiss();
  // Wait only for the locally rejected URL worker to exit.
  for (int i = 0; i < 20; ++i) {
    Sleep(2);
    manager.Tick(false, false, false, false);
  }

  Access::Seed(manager, State::Idle, false);
  Access::MarkOptionalPeerHandled(manager);
  manager.Tick(true, true, true, true, 0x020000);
  check("minor_to_major_peer_transition_reprompts", manager.GetSnapshot().required);
  manager.Dismiss();
  for (int i = 0; i < 20; ++i) {
    Sleep(2);
    manager.Tick(false, false, false, false);
  }

  Access::Seed(manager, State::Checking);
  Access::SetWorkerBusy(manager, true);
  manager.Dismiss();
  manager.CheckNow();
  Access::CheckResult(manager, Access::Manifest());
  check("recheck_queued_without_reviving_old_worker",
        manager.GetSnapshot().state == State::Checking && manager.GetSnapshot().manual);
  Access::SetWorkerBusy(manager, false);
  manager.Tick(true, false, false, false);
  for (int i = 0; i < 100 && manager.GetSnapshot().state == State::Checking; ++i) Sleep(2);
  check("queued_recheck_completes", manager.GetSnapshot().state == State::Error);
  manager.Dismiss();

  Access::Seed(manager, State::Checking, false);
  Access::SetWorkerBusy(manager, true);
  manager.CheckNow(true);
  Access::CheckResult(manager, Access::Manifest("2.0.0"));
  Access::SetWorkerBusy(manager, false);
  check("manual_joins_automatic_check", manager.GetSnapshot().manual &&
        manager.GetSnapshot().required && manager.GetSnapshot().canDownload);

  Access::Seed(manager, State::Downloading);
  Access::Install(manager, root / "package.download");
  check("completed_download_waits_for_idle", manager.HasInstallRequest());
  manager.Dismiss();
  check("close_revokes_queued_install", !manager.HasInstallRequest());
  std::printf("UPDATE_STATE_TEST failures=%u\n", failures);
  return failures ? 1 : 0;
}
