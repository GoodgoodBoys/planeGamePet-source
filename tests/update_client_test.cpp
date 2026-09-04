#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "../desktop/update_manager.h"

int main() {
  plane_pet_update::Manager manager;
  const std::filesystem::path request =
      std::filesystem::temp_directory_path() /
      L"plane-pet-update-client-self-test.request";
  manager.Configure(
      nullptr,
      "https://egg-ota-test.oss-cn-beijing.aliyuncs.com/plane-pet/windows/stable/latest.json",
      request, true, 0);
  manager.CheckNow();
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(30);
  plane_pet_update::Snapshot snapshot;
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.Tick(true, false, false, false);
    snapshot = manager.GetSnapshot();
    if (snapshot.state == plane_pet_update::State::Current) {
      std::printf("PLANE_PET_UPDATE_CLIENT_OK latest=1.0.0 signed=1\n");
      return snapshot.latestVersion == L"1.0.0" ? 0 : 2;
    }
    if (snapshot.state == plane_pet_update::State::Error) {
      std::printf("PLANE_PET_UPDATE_CLIENT_FAILED state=error\n");
      return 3;
    }
  } while (std::chrono::steady_clock::now() < deadline);
  std::printf("PLANE_PET_UPDATE_CLIENT_FAILED state=timeout\n");
  return 4;
}
