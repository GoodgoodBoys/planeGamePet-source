#pragma once
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace plane_pet_history {
struct Entry { uint64_t timestampMs = 0; uint8_t ownHealth = 0, peerHealth = 0; int8_t outcome = 0; };
struct Data {
  uint32_t total = 0, wins = 0, losses = 0, draws = 0;
  uint64_t lastRound = 0;
  std::vector<Entry> entries;
};
enum class ReadStatus { Missing, Valid, Invalid, IoError };
inline bool Number(const std::string &s, uint64_t &value) {
  if (s.empty()) return false;
  auto r = std::from_chars(s.data(), s.data() + s.size(), value);
  return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
inline bool Parse(const std::string &bytes, Data &result) {
  std::istringstream lines(bytes);
  std::string line, token;
  if (!std::getline(lines, line)) return false;
  std::istringstream head(line);
  std::vector<std::string> fields;
  while (head >> token) fields.push_back(token);
  if (fields.size() < 6 || fields[0] != "PLANE_PET_HISTORY") return false;
  uint64_t version = 0, values[5]{};
  if (!Number(fields[1], version) || version < 1 || version > 2 ||
      fields.size() != (version == 1 ? 6U : 7U)) return false;
  for (size_t i = 2; i < fields.size(); ++i) if (!Number(fields[i], values[i - 2])) return false;
  for (int i = 0; i < 4; ++i) if (values[i] > UINT32_MAX) return false;
  if (values[1] + values[2] + values[3] != values[0]) return false;
  Data data;
  data.total = static_cast<uint32_t>(values[0]); data.wins = static_cast<uint32_t>(values[1]);
  data.losses = static_cast<uint32_t>(values[2]); data.draws = static_cast<uint32_t>(values[3]);
  data.lastRound = values[4];
  const size_t expected = static_cast<size_t>(std::min<uint64_t>(10, data.total));
  for (size_t i = 0; i < expected; ++i) {
    if (!std::getline(lines, line)) return false;
    std::istringstream row(line);
    std::string stamp, own, peer, outcome, extra;
    uint64_t timestamp = 0, ownHp = 0, peerHp = 0;
    if (!(row >> stamp >> own >> peer >> outcome) || (row >> extra) ||
        !Number(stamp, timestamp) || !Number(own, ownHp) || !Number(peer, peerHp) ||
        ownHp > 3 || peerHp > 3 || (outcome != "-1" && outcome != "0" && outcome != "1")) return false;
    data.entries.push_back({timestamp, static_cast<uint8_t>(ownHp), static_cast<uint8_t>(peerHp),
        static_cast<int8_t>(outcome == "-1" ? -1 : outcome == "1" ? 1 : 0)});
  }
  // A partial final record is corruption, even when the stream has reached EOF.
  while (std::getline(lines, line)) if (line.find_first_not_of(" \t\r") != std::string::npos) return false;
  result = std::move(data);
  return true;
}
inline ReadStatus Read(const std::filesystem::path &path, Data &data, std::string *raw = nullptr) {
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  if (ec) return ReadStatus::IoError;
  if (!exists) return ReadStatus::Missing;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return ReadStatus::IoError;
  if (size > 65536) return ReadStatus::Invalid;
  std::ifstream input(path, std::ios::binary);
  if (!input) return ReadStatus::IoError;
  std::string bytes(static_cast<size_t>(size), '\0');
  if (size && !input.read(bytes.data(), static_cast<std::streamsize>(size))) return ReadStatus::IoError;
  if (raw) *raw = bytes;
  return Parse(bytes, data) ? ReadStatus::Valid : ReadStatus::Invalid;
}
inline std::string Encode(const Data &d) {
  std::ostringstream out;
  // Keep v2 on disk so existing update rollback builds can still read it.
  out << "PLANE_PET_HISTORY 2 " << d.total << ' ' << d.wins << ' ' << d.losses << ' ' << d.draws
      << ' ' << d.lastRound << '\n';
  for (const auto &e : d.entries) out << e.timestampMs << ' ' << unsigned(e.ownHealth) << ' '
      << unsigned(e.peerHealth) << ' ' << int(e.outcome) << '\n';
  return out.str();
}
inline bool AtomicWrite(const std::filesystem::path &path, const std::string &bytes) {
  std::error_code ec;
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) return false;
  const std::filesystem::path temp = path.wstring() + L".tmp";
  HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
      written == bytes.size() && FlushFileBuffers(f);
  CloseHandle(f);
  if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  if (!ok) DeleteFileW(temp.c_str());
  return ok;
}
inline std::filesystem::path Backup(const std::filesystem::path &path) { return path.wstring() + L".bak"; }
// Never rotate a corrupt primary over its last known-good backup.
inline bool Save(const std::filesystem::path &path, const Data &data, bool &blocked) {
  if (blocked) return false;
  Data checked; std::string old;
  const auto status = Read(path, checked, &old);
  if (status != ReadStatus::Valid && status != ReadStatus::Missing) { blocked = true; return false; }
  const auto bytes = Encode(data);
  if (!Parse(bytes, checked)) return false;
  if (!AtomicWrite(Backup(path), status == ReadStatus::Valid ? old : bytes)) return false;
  return AtomicWrite(path, bytes);
}
// Recover without destroying evidence. No automatic empty-file replacement.
inline bool Load(const std::filesystem::path &path, Data &data, bool &blocked, bool &recovered) {
  blocked = recovered = false;
  const auto status = Read(path, data);
  if (status == ReadStatus::Valid) return true;
  Data backup; std::string bytes;
  const auto backupStatus = Read(Backup(path), backup, &bytes);
  if (status == ReadStatus::Missing && backupStatus == ReadStatus::Missing) { data = {}; return true; }
  if (backupStatus == ReadStatus::Valid) {
    data = std::move(backup);
    bool preserved = status == ReadStatus::Missing;
    if (!preserved) {
      for (unsigned i = 0; i < 100; ++i) {
        const auto copy = path.wstring() + L".corrupt." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(i);
        if (CopyFileW(path.c_str(), copy.c_str(), TRUE)) { preserved = true; break; }
        if (GetLastError() != ERROR_FILE_EXISTS) break;
      }
    }
    recovered = true;
    if (preserved && AtomicWrite(path, bytes)) return true;
  } else data = {};
  blocked = true;
  return false;
}
inline bool Clear(const std::filesystem::path &path, bool &primaryCleared) {
  primaryCleared = false;
  const auto bytes = Encode({});
  // Empty the recovery source too; restarting must not resurrect cleared data.
  if (!AtomicWrite(Backup(path), bytes) || !AtomicWrite(path, bytes)) return false;
  primaryCleared = true;
  std::error_code ec;
  auto parent = path.parent_path(); if (parent.empty()) parent = L".";
  const auto prefix = path.filename().wstring() + L".corrupt.";
  bool ok = true;
  for (std::filesystem::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().filename().wstring().rfind(prefix, 0) == 0) {
      std::error_code removed; std::filesystem::remove(it->path(), removed); ok &= !removed;
    }
  }
  return ok && !ec;
}
}  // namespace plane_pet_history
