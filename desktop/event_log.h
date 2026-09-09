#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace plane_pet_events {
inline constexpr char kHeader[] = "schema_version,release_epoch,timestamp_ms,client_id,app_version,invite_id,round_id,event,value";
inline constexpr char kLegacyHeader[] = "timestamp_ms,client_id,app_version,invite_id,round_id,event,value";
inline constexpr size_t kFileLimit = 1024 * 1024, kQueueLimit = 512;
inline constexpr unsigned kFileCount = 4, kLegacyCount = 16;

// The worker owns all disk operations and only shared state, never PetClient.
// Shutdown is bounded: a stalled disk cannot keep the application alive.
class Writer {
  enum class Kind { Row, Flush, Export, Clear };
  struct Command {
    Kind kind = Kind::Row;
    std::string row;
    std::filesystem::path target;
    std::shared_ptr<std::promise<bool>> done;
    std::shared_ptr<std::atomic<bool>> canceled;
  };
  struct Shared {
    std::mutex mutex;
    std::condition_variable wake, finished;
    std::deque<Command> queue;
    bool stop = false, exited = false;
    std::filesystem::path path;
    std::ofstream file;
    size_t bytes = 0, limit = kFileLimit;
    std::atomic<uint64_t> errors{0}, dropped{0};
#ifdef PLANE_PET_STORAGE_SELF_TEST
    std::atomic<unsigned> delayMs{0};
    std::atomic<DWORD> writerThread{0};
#endif
  };
 public:
  Writer() = default;
  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;
  ~Writer() { Stop(); }
  bool Enqueue(const std::filesystem::path &path, std::string row) {
    if (path.empty() || stopped_) return false;
    Start(path);
    if (state_->path != path || row.size() > 1024 || row.find_first_of("\r\n") != std::string::npos) {
      ++state_->dropped; return false;
    }
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->stop || state_->queue.size() >= kQueueLimit) { ++state_->dropped; return false; }
      Command cmd; cmd.row = std::move(row); state_->queue.push_back(std::move(cmd));
    }
    state_->wake.notify_one(); return true;
  }
  bool Flush(const std::filesystem::path &path) { return Control(path, Kind::Flush, {}); }
  bool Export(const std::filesystem::path &path, const std::filesystem::path &target) {
    return Control(path, Kind::Export, target);
  }
  bool Clear(const std::filesystem::path &path) { return path.empty() || Control(path, Kind::Clear, {}); }
  uint64_t Errors() const { return state_ ? state_->errors.load() : 0; }
  uint64_t Dropped() const { return state_ ? state_->dropped.load() : 0; }
  void Stop() {
    stopped_ = true;
    if (!worker_.joinable()) return;
    {
      std::lock_guard<std::mutex> lock(state_->mutex); state_->stop = true;
    }
    state_->wake.notify_one();
    std::unique_lock<std::mutex> lock(state_->mutex);
    const bool done = state_->finished.wait_for(lock, std::chrono::milliseconds(1500), [&] { return state_->exited; });
    if (!done) {
      state_->dropped += state_->queue.size();
      for (auto &cmd : state_->queue) if (cmd.done) cmd.done->set_value(false);
      state_->queue.clear();
    }
    lock.unlock();
    if (done) worker_.join(); else worker_.detach();
  }
#ifdef PLANE_PET_STORAGE_SELF_TEST
  void DelayWritesForTest(unsigned ms) { state_->delayMs.store(ms); }
  DWORD WriterThreadForTest() const { return state_->writerThread.load(); }
  void SetLimitForTest(const std::filesystem::path &path, size_t bytes) {
    // Only before the first row, while the worker has no disk operations.
    Start(path); std::lock_guard<std::mutex> lock(state_->mutex); state_->limit = bytes;
  }
#endif
 private:
  static std::filesystem::path Rotated(const Shared &s, unsigned i) {
    return i == 0 ? s.path : std::filesystem::path(s.path.wstring() + L"." + std::to_wstring(i));
  }
  static std::filesystem::path Legacy(const Shared &s, unsigned i) {
    return s.path.wstring() + L".legacy" + (i ? L"." + std::to_wstring(i) : L"");
  }
  static bool Exists(const std::filesystem::path &path, bool &exists) {
    std::error_code ec; exists = std::filesystem::exists(path, ec); return !ec;
  }
  static bool Prepare(Shared &s) {
    if (s.file.is_open()) return bool(s.file);
    std::error_code ec;
    if (!s.path.parent_path().empty()) std::filesystem::create_directories(s.path.parent_path(), ec);
    if (ec) return false;
    bool exists = false; if (!Exists(s.path, exists)) return false;
    s.bytes = exists ? static_cast<size_t>(std::filesystem::file_size(s.path, ec)) : 0;
    if (ec) return false;
    if (s.bytes) {
      std::ifstream previous(s.path, std::ios::binary);
      std::string header; std::getline(previous, header);
      // Preserve an incomplete last row too; never append into a torn record.
      previous.clear(); previous.seekg(-1, std::ios::end); char last = 0; previous.get(last);
      if (!previous) return false;
      previous.close();
      if (header != kHeader || last != '\n' || s.bytes > s.limit) {
        bool moved = false;
        for (unsigned i = 0; i < kLegacyCount; ++i) {
          bool present; const auto archive = Legacy(s, i);
          if (!Exists(archive, present)) return false;
          if (!present) { std::filesystem::rename(s.path, archive, ec); moved = !ec; break; }
        }
        if (!moved) return false; // Never overwrite an existing legacy archive.
        s.bytes = 0;
      }
    }
    s.file.clear(); s.file.open(s.path, std::ios::binary | std::ios::app);
    if (!s.file) return false;
    if (!s.bytes) { s.file << kHeader << '\n'; s.bytes = sizeof(kHeader); }
    return bool(s.file);
  }
  static bool Rotate(Shared &s) {
    s.file.close(); s.file.clear();
    std::error_code ec;
    std::filesystem::remove(Rotated(s, kFileCount - 1), ec); if (ec) return false;
    for (unsigned i = kFileCount - 1; i > 0; --i) {
      bool exists; if (!Exists(Rotated(s, i - 1), exists)) return false;
      if (exists) { std::filesystem::rename(Rotated(s, i - 1), Rotated(s, i), ec); if (ec) return false; }
    }
    return Prepare(s);
  }
  static bool Write(Shared &s, const std::string &row) {
#ifdef PLANE_PET_STORAGE_SELF_TEST
    s.writerThread.store(GetCurrentThreadId());
    std::this_thread::sleep_for(std::chrono::milliseconds(s.delayMs.load()));
#endif
    if (!Prepare(s)) return false;
    if (s.bytes + row.size() + 1 > s.limit && !Rotate(s)) return false;
    s.file << row << '\n'; s.bytes += row.size() + 1; return bool(s.file);
  }
  static std::vector<std::filesystem::path> Sources(const Shared &s) {
    std::vector<std::filesystem::path> result;
    for (unsigned i = 0; i < kLegacyCount; ++i) result.push_back(Legacy(s, i));
    for (unsigned i = kFileCount; i > 0; --i) result.push_back(Rotated(s, i - 1));
    return result;
  }
  static bool FlushFile(Shared &s) {
    if (!s.file.is_open()) return true;
    s.file.flush(); return bool(s.file);
  }
  static bool ExportFiles(Shared &s, const Command &cmd) {
    if (!FlushFile(s)) return false;
    std::error_code ec;
    auto target = std::filesystem::absolute(cmd.target, ec).lexically_normal(); if (ec) return false;
    const auto temp = std::filesystem::path(target.wstring() + L".plane-pet-export.tmp");
    const auto sources = Sources(s);
    for (const auto &source : sources) {
      const auto full = std::filesystem::absolute(source, ec).lexically_normal(); if (ec) return false;
      if (_wcsicmp(full.c_str(), target.c_str()) == 0 || _wcsicmp(full.c_str(), temp.c_str()) == 0) return false;
      bool exists; if (!Exists(source, exists)) return false;
      if (exists && std::filesystem::exists(target, ec) && std::filesystem::equivalent(source, target, ec)) return false;
      if (ec) return false;
    }
    std::ofstream output(temp, std::ios::binary | std::ios::trunc); if (!output) return false;
    output << kHeader << '\n'; bool ok = true, found = false;
    for (const auto &source : sources) {
      if (cmd.canceled->load()) { ok = false; break; }
      bool exists; if (!Exists(source, exists)) { ok = false; break; } if (!exists) continue;
      std::ifstream input(source, std::ios::binary); std::string line;
      if (!input || !std::getline(input, line)) { ok = false; break; }
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const bool legacy = line == kLegacyHeader;
      if (!legacy && line != kHeader) { ok = false; break; }
      found = true;
      while (std::getline(input, line)) {
        if (cmd.canceled->load() || line.size() > 1024) { ok = false; break; }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (std::count(line.begin(), line.end(), ',') != (legacy ? 6 : 8)) { ok = false; break; }
        output << (legacy ? "1,unknown," : "") << line << '\n';
      }
      if (input.bad() || !ok) { ok = false; break; }
    }
    output.close(); ok = ok && found && bool(output) && !cmd.canceled->load();
    if (ok) ok = MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
  }
  static bool ClearFiles(Shared &s, const Command &cmd) {
    if (s.file.is_open()) s.file.close();
    s.file.clear(); s.bytes = 0;
    bool ok = true;
    for (const auto &path : Sources(s)) {
      if (cmd.canceled->load()) return false;
      std::error_code ec; std::filesystem::remove(path, ec); ok &= !ec;
    }
    return ok;
  }
  void Start(const std::filesystem::path &path) {
    if (state_) return;
    state_ = std::make_shared<Shared>(); state_->path = path;
    worker_ = std::thread([s = state_] {
      for (;;) {
        Command cmd;
        {
          std::unique_lock<std::mutex> lock(s->mutex);
          s->wake.wait(lock, [&] { return s->stop || !s->queue.empty(); });
          if (s->queue.empty() && s->stop) break;
          cmd = std::move(s->queue.front()); s->queue.pop_front();
        }
        bool ok = false;
        try {
          if (!cmd.canceled || !cmd.canceled->load()) {
            switch (cmd.kind) {
              case Kind::Row: ok = Write(*s, cmd.row) && FlushFile(*s); break;
              case Kind::Flush: ok = FlushFile(*s); break;
              case Kind::Export: ok = ExportFiles(*s, cmd); break;
              case Kind::Clear: ok = ClearFiles(*s, cmd); break;
            }
          }
        } catch (...) { ok = false; }
        if (!ok) {
          ++s->errors;
          if (cmd.kind == Kind::Row) { ++s->dropped; s->file.close(); s->file.clear(); }
        }
        if (cmd.done) cmd.done->set_value(ok);
      }
      if (!FlushFile(*s)) ++s->errors;
      if (s->file.is_open()) s->file.close();
      { std::lock_guard<std::mutex> lock(s->mutex); s->exited = true; }
      s->finished.notify_all();
    });
  }
  bool Control(const std::filesystem::path &path, Kind kind, const std::filesystem::path &target) {
    if (path.empty() || stopped_) return false;
    Start(path); if (state_->path != path) return false;
    auto done = std::make_shared<std::promise<bool>>(); auto future = done->get_future();
    auto canceled = std::make_shared<std::atomic<bool>>(false);
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->stop || state_->queue.size() >= kQueueLimit + 2) return false;
      state_->queue.push_back({kind, {}, target, done, canceled});
    }
    state_->wake.notify_one();
    if (future.wait_for(std::chrono::milliseconds(2000)) != std::future_status::ready) {
      canceled->store(true); ++state_->errors; return false;
    }
    return future.get();
  }
  std::shared_ptr<Shared> state_;
  std::thread worker_;
  bool stopped_ = false;
};
}  // namespace plane_pet_events
