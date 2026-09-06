// Compile this against a preserved version's update_manager.cpp/header to
// validate that actual older update code accepts the newly signed release.
#include "../desktop/update_manager.cpp"
#include <chrono>
#include <cstdio>
#include <iterator>
#include <thread>

std::vector<uint8_t> Read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("missing probe input");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char **argv) {
  using namespace plane_pet_update;
  try {
    if (argc >= 5 && std::string(argv[1]) == "offline") {
      auto manifestBytes = Read(argv[2]);
      const auto signatureText = Read(std::string(argv[2]) + ".sig");
      const auto artifact = Read(argv[3]);
      std::vector<uint8_t> signature;
      Manager::Manifest manifest;
      std::array<uint8_t, 32> digest{};
      if (!DecodeBase64(std::string(signatureText.begin(), signatureText.end()), signature) ||
          !VerifyManifestSignature(manifestBytes, signature) ||
          !ParseManifest(manifestBytes, manifest) || manifest.version != argv[4] ||
          manifest.minimumVersion != "1.0.0" || artifact.size() != manifest.size ||
          !Sha256(artifact.data(), artifact.size(), digest) || Hex(digest) != manifest.sha256)
        return 2;
      manifestBytes.front() ^= 1;
      if (VerifyManifestSignature(manifestBytes, signature)) return 3;
      std::printf("UPDATE_OFFLINE_PROBE_OK client=%s latest=%s rsa=1 sha256=1 size=1 tamper_rejected=1\n",
                  plane_pet_version::kString, manifest.version.c_str());
      return 0;
    }
    if (argc >= 6 && std::string(argv[1]) == "online") {
      const std::string expected = argv[3];
      const bool download = std::string(argv[5]) == "download";
      const bool shouldBeNew = CompareVersion(plane_pet_version::kString, expected) < 0;
      const auto root = std::filesystem::absolute(argv[4]);
      Manager manager;
      manager.Configure(nullptr, argv[2], root / L"update.request", true, 0);
      manager.CheckNow();
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
      bool accepted = false;
      do {
        manager.Tick(true, false, false, false);
        const Snapshot snapshot = manager.GetSnapshot();
        if (snapshot.state == State::Error) {
          std::printf("UPDATE_ONLINE_PROBE_ERROR message=%s\n", Utf8(snapshot.message).c_str());
          return 4;
        }
        if (!shouldBeNew && snapshot.state == State::Current && Utf8(snapshot.latestVersion) == expected) {
          std::printf("UPDATE_ONLINE_CURRENT_OK client=%s latest=%s\n", plane_pet_version::kString, expected.c_str());
          return 0;
        }
        if (snapshot.state == State::Available && !accepted) {
          if (!shouldBeNew || snapshot.required || !snapshot.canDownload ||
              Utf8(snapshot.latestVersion) != expected || snapshot.summary[0].empty()) return 5;
          if (!download) {
            std::printf("UPDATE_ONLINE_AVAILABLE_OK client=%s latest=%s optional=1 notes=1\n",
                        plane_pet_version::kString, expected.c_str());
            return 0;
          }
          // Wait for the check worker's completion guard before accepting.
          if (manager.AcceptAndDownload()) {
            accepted = true;
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
          }
        }
        if (accepted && manager.HasInstallRequest()) {
          if (!std::filesystem::is_regular_file(root / L"update.request")) return 6;
          std::printf("UPDATE_ONLINE_DOWNLOAD_OK client=%s latest=%s optional=1 notes=1 signed_request=1\n",
                      plane_pet_version::kString, expected.c_str());
          return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      } while (std::chrono::steady_clock::now() < deadline);
      return 7;
    }
    std::puts("Usage: offline MANIFEST ARTIFACT VERSION | online URL VERSION OUTPUT check|download");
    return 1;
  } catch (const std::exception &error) {
    std::printf("UPDATE_PROBE_FAILED %s\n", error.what());
    return 8;
  }
}
