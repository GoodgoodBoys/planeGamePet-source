// Real client storage regressions; isolated files only, network upload disabled.
#define PLANE_PET_STORAGE_SELF_TEST
#define main ExistingClientTestMain
#include "client_edge_test.cpp"
#undef main
#define CHECK(condition) do { ++checks; if (!(condition)) { std::printf("STORAGE_FAIL line=%d %s\n", __LINE__, #condition); return 1; } } while (0)
static std::string Bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}};
}
static void Put(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  unsigned checks = 0;
  const auto directory = std::filesystem::u8path(argv[1]);
  std::filesystem::create_directories(directory);
  using namespace plane_pet_history;
  const std::string valid = "PLANE_PET_HISTORY 2 1 1 0 0 991\n1789000000000 2 0 1\n";
  const std::vector<std::string> bad = {
      "", "bad header", "PLANE_PET_HISTORY 2 1 1 0 0 991\n1 9 0 1\n",
      "PLANE_PET_HISTORY 2 1 1 0 0 991\n1 2",
      "PLANE_PET_HISTORY 2 1 1 0 0 991\n",
      "PLANE_PET_HISTORY 2 1 0 0 0 991\n1 2 0 1\n",
      "PLANE_PET_HISTORY 2 1 1 0 0 991\n-1 2 0 1\n",
      "PLANE_PET_HISTORY 2 4294967296 4294967296 0 0 991\n",
      valid + "partial", valid + "1 2 0 1\n", std::string(65537, 'x')};
  unsigned index = 0;
  for (const auto &damaged : bad) {
    PetClient client;
    client.telemetryEnabled_ = false;
    client.historyPath_ = directory / ("damaged-" + std::to_string(index++) + ".history");
    Put(client.historyPath_, damaged); client.LoadHistory();
    CHECK(!client.historyPersistenceOk_ && client.historyWriteBlocked_);
    client.currentRoundId_ = 992; client.AppendHistoryEntry(1, 2, 0);
    CHECK(!client.historyPersistenceOk_ && Bytes(client.historyPath_) == damaged);
    CHECK(!std::filesystem::exists(Backup(client.historyPath_)));
    CHECK(client.HistoryStorageStatus().find(L"停止覆盖") != std::wstring::npos);
  }
  PetClient client;
  client.telemetryEnabled_ = false; client.historyPath_ = directory / "recover.history";
  Put(client.historyPath_, valid); client.LoadHistory();
  CHECK(client.historyPersistenceOk_ && client.historyTotal_ == 1);
  client.currentRoundId_ = 992; client.AppendHistoryEntry(-1, 0, 2);
  CHECK(client.historyPersistenceOk_ && client.historyTotal_ == 2 && Bytes(Backup(client.historyPath_)) == valid);
  const auto latest = Bytes(client.historyPath_);
  Put(client.historyPath_, "partial"); client.LoadHistory();
  CHECK(client.historyPersistenceOk_ && client.historyRecovered_ && client.historyTotal_ == 1);
  CHECK(Bytes(client.historyPath_) == valid);
  unsigned copies = 0;
  for (const auto &f : std::filesystem::directory_iterator(directory)) {
    if (f.path().filename().wstring().rfind(L"recover.history.corrupt.", 0) == 0) {
      ++copies; CHECK(Bytes(f.path()) == "partial");
    }
  }
  CHECK(copies == 1);
  Put(client.historyPath_, latest); client.LoadHistory();
  Put(client.historyPath_, "corrupted after load"); client.AppendHistoryEntry(0, 1, 1);
  CHECK(!client.historyPersistenceOk_ && Bytes(client.historyPath_) == "corrupted after load");
  CHECK(Bytes(Backup(client.historyPath_)) == valid);
  CHECK(client.ClearLocalDataFiles());
  CHECK(!client.historyWriteBlocked_ && !client.historyRecovered_ && client.historyTotal_ == 0);
  std::filesystem::remove(client.historyPath_); client.LoadHistory();
  CHECK(client.historyPersistenceOk_ && client.historyTotal_ == 0);
  Put(client.historyPath_, valid); client.LoadHistory();
  HANDLE locked = CreateFileW(client.historyPath_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(locked != INVALID_HANDLE_VALUE);
  client.AppendHistoryEntry(1, 2, 0);
  CHECK(!client.historyPersistenceOk_ && Bytes(client.historyPath_) == valid);
  CloseHandle(locked); CHECK(client.SaveHistory());
  Data data; CHECK(Read(client.historyPath_, data) == ReadStatus::Valid && data.total == 2);
  CHECK(Bytes(client.historyPath_).rfind("PLANE_PET_HISTORY 2 ", 0) == 0);
  client.eventsPath_ = directory / "client.events.csv";
  client.LogEvent("declined", 1); CHECK(!std::filesystem::exists(client.eventsPath_));
  client.telemetryEnabled_ = true; client.telemetryUploadChoice_ = 0; client.clientId_ = 123;
  client.LogEvent("synthetic", 7); CHECK(client.eventWriter_.Flush(client.eventsPath_));
  const auto csv = Bytes(client.eventsPath_);
  CHECK(csv.rfind(std::string(plane_pet_events::kHeader) + "\n2,1,", 0) == 0);
  CHECK(csv.find(",123," + std::string(kAppVersion) + ",0,992,synthetic,7") != std::string::npos);
  client.telemetryEnabled_ = false; client.LogEvent("after_disable", 1);
  CHECK(client.eventWriter_.Flush(client.eventsPath_) && Bytes(client.eventsPath_) == csv);
  using namespace plane_pet_events;
  Writer writer;
  const auto path = directory / "rotation.csv", exported = directory / "export.csv";
  const std::string oldRow = "123,7,1.0.2,0,0,old_test,1\n";
  Put(path, std::string(kLegacyHeader) + "\r\n" + oldRow);
  Put(path.wstring() + L".legacy", std::string(kLegacyHeader) + "\n" + oldRow);
  writer.SetLimitForTest(path, 512);
  for (int i = 0; i < 300; ++i) {
    CHECK(writer.Enqueue(path, "2,1,123,7,1.0.3,0,0,event," + std::to_string(i)));
    if (i % 20 == 0) CHECK(writer.Flush(path));
  }
  CHECK(writer.Flush(path)); CHECK(writer.WriterThreadForTest() != GetCurrentThreadId());
  CHECK(Bytes(path.wstring() + L".legacy") == std::string(kLegacyHeader) + "\n" + oldRow);
  CHECK(Bytes(path.wstring() + L".legacy.1") == std::string(kLegacyHeader) + "\r\n" + oldRow);
  for (unsigned i = 0; i < kFileCount; ++i) {
    const auto file = i ? std::filesystem::path(path.wstring() + L"." + std::to_wstring(i)) : path;
    CHECK(std::filesystem::file_size(file) <= 512);
  }
  CHECK(!std::filesystem::exists(path.wstring() + L".4"));
  CHECK(writer.Export(path, exported));
  CHECK(Bytes(exported).find("1,unknown," + oldRow) != std::string::npos);
  CHECK(Bytes(exported).find("event,299") != std::string::npos);
  const auto before = Bytes(path);
  CHECK(!writer.Export(path, path) && Bytes(path) == before);
  CHECK(writer.Clear(path));
  CHECK(!std::filesystem::exists(path) && !std::filesystem::exists(path.wstring() + L".legacy.1"));
  CHECK(writer.Enqueue(path, "2,1,123,7,1.0.3,0,0,new_session,1") && writer.Flush(path));
  CHECK(Bytes(path).find("0,0,event,") == std::string::npos);
  writer.DelayWritesForTest(200);
  const auto started = Clock::now();
  for (int i = 0; i < 10000; ++i) writer.Enqueue(path, "2,1,123,7,1.0.3,0,0,burst,1");
  const auto enqueueMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
  CHECK(enqueueMs < 500 && writer.Dropped() > 0);
  writer.DelayWritesForTest(0); CHECK(writer.Clear(path));
  writer.Stop(); CHECK(!std::filesystem::exists(path)); CHECK(!writer.Enqueue(path, "after_stop"));
  Writer failed;
  const auto invalid = directory / "directory.csv"; std::filesystem::create_directory(invalid);
  CHECK(failed.Enqueue(invalid, "2,1,123,7,1.0.3,0,0,blocked,1"));
  CHECK(failed.Flush(invalid)); CHECK(failed.Errors() > 0 && failed.Dropped() == 1);
  CHECK(client.ClearLocalDataFiles()); client.eventWriter_.Stop();
  CHECK(!std::filesystem::exists(client.eventsPath_));
  std::printf("STORAGE_REGRESSIONS_OK checks=%u enqueue_10000_ms=%lld\n", checks, static_cast<long long>(enqueueMs));
}
