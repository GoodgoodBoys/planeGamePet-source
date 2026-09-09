#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
#endif

#include "../common/pairing_protocol.h"
#include "../common/pc_motion_protocol.h"
#include "../common/pc_battle_protocol.h"
#include "../common/dnd_protocol.h"
#include "../shared/plane_protocol.h"
#include "../shared/plane_sim.h"

namespace {

using Clock = std::chrono::steady_clock;
constexpr uint32_t kInviteTimeoutSeconds = 300;

struct Config {
  uint16_t port = 32110;
  uint32_t matchSeconds = 180;
  uint32_t inviteSeconds = kInviteTimeoutSeconds;
  std::string bindAddress = "127.0.0.1";
  pcpair::AuthKey networkKey{};
  bool valid = true;
  std::filesystem::path storePath;
};

struct Binding {
  uint32_t id = 0;
  uint32_t devices[2]{};
  uint32_t tokenLow[2]{};
  uint32_t tokenHigh[2]{};
  uint32_t pendingRequest[2]{};
  pcpair::AppVersion pendingVersion[2]{};
  Clock::time_point provisionalDeadline{};
};

struct Waiter {
  uint32_t deviceId = 0;
  uint32_t requestId = 0;
  uint32_t pairingCode = 0;
  sockaddr_in endpoint{};
  Clock::time_point lastSeen{};
  pcpair::AppVersion appVersion{};
};

struct SourceRate {
  uint32_t address = 0;
  uint16_t port = 0;
  uint32_t packets = 0;
  Clock::time_point windowStarted{};
  Clock::time_point lastSeen{};
};

struct Client {
  bool active = false;
  sockaddr_in endpoint{};
  uint32_t deviceId = 0;
  uint32_t session = 0;
  uint32_t lastSequence = 0;
  uint32_t lastInputSequence = 0;
  uint32_t resumeRequestId = 0;
  uint32_t lastOperationId = 0;
  bool supportsDnd = false;
  bool supportsMotion = false;
  pcmotion::InputQueue motionInputs;
  Clock::time_point lastMotionProcessed{};
  bool doNotDisturb = false;
  uint32_t dndRevision = 0;
  uint32_t blockedOperation = 0;
  pcpair::InviteBlock blockedReason = pcpair::InviteBlock::None;
  uint8_t slot = 0;
  uint8_t input = 0;
  Clock::time_point lastSeen{};
  Clock::time_point lastEmote{};
  pcpair::AppVersion appVersion{};
};

struct Room {
  uint32_t bindingId = 0;
  std::array<Client, 2> clients{};
  plink::WorldState world{};
  pcmotion::World motion;
  pcbattle::Authority battle;
  bool battleEnabled = false;
  bool motionEnabled = false;
  bool syncFailed = false;
  plink::GamePhase phase = plink::GamePhase::Menu;
  uint8_t inviterSlot = 0;
  uint8_t winnerSlot = 0;
  plink::MatchEndReason endReason = plink::MatchEndReason::None;
  uint64_t roundId = 0;
  uint64_t inviteId = 0;
  bool dndInterrupted = false;
  uint8_t dndInterruptedInviter = 0;
  Clock::time_point phaseStarted{};
  Clock::time_point matchStarted{};
};

struct UnboundTombstone {
  Binding binding{};
  Clock::time_point expires{};
};

struct CancelledRequest {
  uint32_t deviceId = 0;
  uint32_t requestId = 0;
  Clock::time_point expires{};
};

bool SameEndpoint(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

uint32_t MillisSince(const Clock::time_point &start) {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start)
          .count());
}

uint32_t SecureRandomU32() {
  uint32_t value = 0;
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    value = static_cast<uint32_t>(GetTickCount64()) ^ GetCurrentProcessId();
  }
#else
  if (getrandom(&value, sizeof(value), 0) !=
      static_cast<ssize_t>(sizeof(value))) {
    std::random_device random;
    value = (static_cast<uint32_t>(random()) << 16U) ^
            static_cast<uint32_t>(random()) ^
            static_cast<uint32_t>(getpid()) ^
            static_cast<uint32_t>(Clock::now().time_since_epoch().count());
  }
#endif
  return value == 0 ? 1U : value;
}

uint64_t SecureRandomU64() {
  return static_cast<uint64_t>(SecureRandomU32()) |
         (static_cast<uint64_t>(SecureRandomU32()) << 32U);
}

std::filesystem::path DefaultStorePath() {
#ifdef _WIN32
  wchar_t localAppData[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
  std::filesystem::path root = length > 0 && length < std::size(localAppData)
                                   ? std::filesystem::path(localAppData)
                                   : std::filesystem::current_path();
  return root / L"PlanePet" / L"server_bindings.db";
#else
  return "/var/lib/plane-pet/server_bindings.db";
#endif
}

bool AtomicReplace(const std::filesystem::path &source,
                   const std::filesystem::path &target) {
#ifdef _WIN32
  return MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
  std::error_code error;
  std::filesystem::rename(source, target, error);
  return !error;
#endif
}

bool CopyReplace(const std::filesystem::path &source,
                 const std::filesystem::path &target) {
#ifdef _WIN32
  return CopyFileW(source.c_str(), target.c_str(), FALSE) != FALSE;
#else
  std::error_code error;
  std::filesystem::copy_file(
      source, target, std::filesystem::copy_options::overwrite_existing, error);
  return !error;
#endif
}

Config ParseArguments(int argc, char **argv) {
  Config config;
  config.storePath = DefaultStorePath();
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--port=", 0) == 0) {
      config.port = static_cast<uint16_t>(
          std::clamp(atoi(arg.substr(7).c_str()), 1, 65535));
    } else if (arg.rfind("--seconds=", 0) == 0) {
      const int seconds = atoi(arg.substr(10).c_str());
      config.matchSeconds = seconds <= 0
          ? 180U : static_cast<uint32_t>(std::clamp(seconds, 5, 180));
    } else if (arg.rfind("--invite-seconds=", 0) == 0) {
      const int seconds = atoi(arg.substr(17).c_str());
      config.inviteSeconds = seconds <= 0
          ? kInviteTimeoutSeconds
          : static_cast<uint32_t>(
                std::clamp(seconds, 1,
                           static_cast<int>(kInviteTimeoutSeconds)));
    } else if (arg.rfind("--store=", 0) == 0) {
      config.storePath = std::filesystem::u8path(arg.substr(8));
    } else if (arg.rfind("--bind=", 0) == 0) {
      config.bindAddress = arg.substr(7);
    } else if (arg.rfind("--network-key=", 0) == 0) {
      config.valid = config.valid &&
          pcpair::ParseAuthKey(arg.substr(14), config.networkKey);
    }
  }
  if (config.bindAddress != "127.0.0.1" &&
      config.bindAddress != "localhost" && !config.networkKey.Enabled()) {
    std::fprintf(stderr,
                 "non-loopback bind requires --network-key=<32 hex chars>\n");
    config.valid = false;
  }
  return config;
}

class PetServer {
 public:
  explicit PetServer(Config config) : config_(std::move(config)) {}

  bool Run() {
    if (!config_.valid) return false;
#ifdef _WIN32
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return false;
#endif
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) return false;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    if (config_.bindAddress == "localhost") {
      local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (inet_pton(AF_INET, config_.bindAddress.c_str(),
                         &local.sin_addr) != 1) {
      std::fprintf(stderr, "invalid bind address: %s\n",
                   config_.bindAddress.c_str());
      return false;
    }
    local.sin_port = htons(config_.port);
    if (bind(socket_, reinterpret_cast<const sockaddr *>(&local),
             sizeof(local)) != 0) {
#ifdef _WIN32
      std::fprintf(stderr, "bind failed: %d\n", WSAGetLastError());
#else
      std::fprintf(stderr, "bind failed: %d\n", errno);
#endif
      return false;
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(socket_, FIONBIO, &nonBlocking);
#else
    const int flags = fcntl(socket_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
      std::fprintf(stderr, "failed to set non-blocking socket: %d\n", errno);
      return false;
    }
#endif
    start_ = Clock::now();
    LoadBindings();
    std::printf(
        "PC_PET_SERVER_READY %s:%u bindings=%zu match=%us invite=%us auth=%u\n",
        config_.bindAddress.c_str(), config_.port, bindings_.size(),
        config_.matchSeconds, config_.inviteSeconds,
        config_.networkKey.Enabled() ? 1U : 0U);
    std::fflush(stdout);

    auto nextTick = Clock::now();
    while (true) {
      ReceiveAll();
      const auto now = Clock::now();
      int catchup = 0;
      while (now >= nextTick && catchup < 4) {
        Tick();
        nextTick += std::chrono::nanoseconds(1000000000 / pcmotion::kHz);
        ++catchup;
      }
      if (now > nextTick + std::chrono::milliseconds(200)) nextTick = now;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

 private:
  Binding *FindBinding(uint32_t bindingId) {
    for (Binding &binding : bindings_)
      if (binding.id == bindingId) return &binding;
    return nullptr;
  }

  Binding *FindBindingByDevice(uint32_t deviceId) {
    for (Binding &binding : bindings_)
      if (binding.devices[0] == deviceId || binding.devices[1] == deviceId)
        return &binding;
    return nullptr;
  }

  int BindingSlot(const Binding &binding, uint32_t deviceId) const {
    if (binding.devices[0] == deviceId) return 0;
    if (binding.devices[1] == deviceId) return 1;
    return -1;
  }

  bool ValidToken(const Binding &binding, int slot,
                  const pcpair::Message &message) const {
    return slot >= 0 && slot < 2 &&
           binding.tokenLow[slot] == message.tokenLow &&
           binding.tokenHigh[slot] == message.tokenHigh;
  }

  Room &GetRoom(uint32_t bindingId) {
    for (Room &room : rooms_)
      if (room.bindingId == bindingId) return room;
    Room room;
    room.bindingId = bindingId;
    room.phaseStarted = Clock::now();
    plink::InitializeWorld(room.world);
    rooms_.push_back(room);
    return rooms_.back();
  }

  uint32_t UniqueBindingId() {
    for (;;) {
      const uint32_t candidate = SecureRandomU32();
      if (FindBinding(candidate) == nullptr) return candidate;
    }
  }

  uint32_t UniqueSession() {
    for (;;) {
      const uint32_t candidate = SecureRandomU32();
      bool used = false;
      for (const Room &room : rooms_)
        for (const Client &client : room.clients)
          used = used || (client.active && client.session == candidate);
      if (!used) return candidate;
    }
  }

  bool LoadBindingsFrom(const std::filesystem::path &path,
                        std::vector<Binding> &loaded) const {
    std::ifstream input(path);
    if (!input) return false;
    std::string magic;
    uint32_t version = 0;
    if (!(input >> magic >> version) || magic != "PLANE_PET_BINDINGS" ||
        version != 1) {
      return false;
    }
    Binding binding;
    while (input >> binding.id >> binding.devices[0] >> binding.devices[1] >>
           binding.tokenLow[0] >> binding.tokenHigh[0] >>
           binding.tokenLow[1] >> binding.tokenHigh[1] >>
           binding.pendingRequest[0] >> binding.pendingRequest[1]) {
      if (binding.id == 0 || binding.devices[0] == 0 ||
          binding.devices[1] == 0 || binding.devices[0] == binding.devices[1]) {
        return false;
      }
      for (const Binding &existing : loaded) {
        if (existing.id == binding.id ||
            existing.devices[0] == binding.devices[0] ||
            existing.devices[0] == binding.devices[1] ||
            existing.devices[1] == binding.devices[0] ||
            existing.devices[1] == binding.devices[1]) {
          return false;
        }
      }
      if (binding.pendingRequest[0] != 0 || binding.pendingRequest[1] != 0)
        binding.provisionalDeadline = Clock::now() + std::chrono::seconds(60);
      loaded.push_back(binding);
      binding = Binding{};
    }
    return input.eof();
  }

  void LoadBindings() {
    std::error_code error;
    const bool primaryExists =
        std::filesystem::exists(config_.storePath, error);
    std::vector<Binding> loaded;
    if (primaryExists && LoadBindingsFrom(config_.storePath, loaded)) {
      bindings_ = std::move(loaded);
      return;
    }
    if (primaryExists) {
      std::fprintf(stderr, "binding store is invalid: %s\n",
                   config_.storePath.string().c_str());
      std::filesystem::path corrupt = config_.storePath;
      corrupt += ".corrupt";
      AtomicReplace(config_.storePath, corrupt);
    }
    std::filesystem::path backup = config_.storePath;
    backup += ".bak";
    loaded.clear();
    if (LoadBindingsFrom(backup, loaded)) {
      bindings_ = std::move(loaded);
      if (SaveBindings()) {
        std::fprintf(stderr, "binding store recovered from backup\n");
      }
    }
  }

  bool SaveBindings() {
    std::error_code error;
    const auto parent = config_.storePath.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    std::filesystem::path temporary = config_.storePath;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "PLANE_PET_BINDINGS 1\n";
    for (const Binding &binding : bindings_) {
      output << binding.id << ' ' << binding.devices[0] << ' '
             << binding.devices[1] << ' ' << binding.tokenLow[0] << ' '
             << binding.tokenHigh[0] << ' ' << binding.tokenLow[1] << ' '
             << binding.tokenHigh[1] << ' ' << binding.pendingRequest[0] << ' '
             << binding.pendingRequest[1] << '\n';
    }
    output.close();
    if (!output) return false;
    if (!AtomicReplace(temporary, config_.storePath)) {
      return false;
    }
    std::filesystem::path backup = config_.storePath;
    backup += ".bak";
    std::filesystem::path backupTemporary = config_.storePath;
    backupTemporary += ".bak.tmp";
    if (!CopyReplace(config_.storePath, backupTemporary) ||
        !AtomicReplace(backupTemporary, backup)) {
      std::filesystem::path stale = config_.storePath;
      stale += ".bak.stale";
      AtomicReplace(backup, stale);
    }
    return true;
  }

  void SendControl(const sockaddr_in &endpoint,
                   const pcpair::Message &message) {
    uint8_t bytes[pcpair::kMessageSize]{};
    if (!pcpair::Serialize(message, bytes, sizeof(bytes), config_.networkKey))
      return;
    sendto(socket_, reinterpret_cast<const char *>(bytes), sizeof(bytes), 0,
           reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint));
  }

  void SendStatus(const sockaddr_in &endpoint, const pcpair::Message &request,
                  pcpair::Status status) {
    pcpair::Message response;
    response.type = pcpair::MessageType::Status;
    response.deviceId = request.deviceId;
    response.requestId = request.requestId;
    response.pairingCode = request.pairingCode;
    response.bindingId = request.bindingId;
    response.status = status;
    response.wireVersion = request.wireVersion ? request.wireVersion : pcpair::WireVersionFor(request.appVersion);
    SendControl(endpoint, response);
  }

  void SendMatched(const sockaddr_in &endpoint, const Binding &binding,
                   int slot, uint32_t requestId, uint32_t code) {
    pcpair::Message response;
    response.type = pcpair::MessageType::Status;
    response.deviceId = binding.devices[slot];
    response.requestId = requestId;
    response.pairingCode = code;
    response.bindingId = binding.id;
    response.tokenLow = binding.tokenLow[slot];
    response.tokenHigh = binding.tokenHigh[slot];
    response.peerDeviceId = binding.devices[1 - slot];
    response.status = pcpair::Status::Matched;
    response.assignedSlot = static_cast<uint8_t>(slot + 1);
    response.appVersion = binding.pendingVersion[1 - slot];
    response.wireVersion = pcpair::WireVersionFor(binding.pendingVersion[slot]);
    response.compatibilityFlags = CompatibilityFlags(
        binding.pendingVersion[slot], binding.pendingVersion[1 - slot]);
    SendControl(endpoint, response);
  }

  static uint8_t CompatibilityFlags(const pcpair::AppVersion &local,
                                    const pcpair::AppVersion &peer) {
    uint8_t flags = pcpair::ScopedActionsSupported | pcpair::kDndSupported | pcmotion::kSupported;
    if (!peer.Known()) return flags;
    flags |= pcpair::PeerVersionKnown;
    if (local.Known()) {
      const auto localTuple =
          std::array<uint8_t, 4>{local.releaseEpoch, local.major, local.minor, local.patch};
      const auto peerTuple =
          std::array<uint8_t, 4>{peer.releaseEpoch, peer.major, peer.minor, peer.patch};
      if (!pcpair::SameMajorFamily(local, peer)) flags |= pcpair::MajorMismatch;
      if (localTuple < peerTuple)
        flags |= pcpair::LocalUpdateRequired;
      else if (peerTuple < localTuple)
        flags |= pcpair::PeerUpdateRequired;
    }
    return flags;
  }

  void SendCompatibility(Room &room) {
    for (size_t index = 0; index < room.clients.size(); ++index) {
      Client &client = room.clients[index];
      if (!client.active) continue;
      const Client &peer = room.clients[1U - index];
      pcpair::Message response;
      response.type = pcpair::MessageType::Status;
      response.deviceId = client.deviceId;
      response.requestId = client.resumeRequestId;
      response.bindingId = room.bindingId;
      response.status = pcpair::Status::Matched;
      response.assignedSlot = client.slot;
      response.appVersion = peer.appVersion;
      response.wireVersion = pcpair::WireVersionFor(client.appVersion);
      response.compatibilityFlags =
          CompatibilityFlags(client.appVersion, peer.appVersion);
      SendControl(client.endpoint, response);
    }
  }

  bool MajorVersionsCompatible(const Room &room) const {
    const pcpair::AppVersion &left = room.clients[0].appVersion;
    const pcpair::AppVersion &right = room.clients[1].appVersion;
    return pcpair::SameMajorFamily(left, right);
  }

  void RemoveWaiter(uint32_t deviceId, uint32_t requestId = 0) {
    waiters_.erase(
        std::remove_if(waiters_.begin(), waiters_.end(),
                       [deviceId, requestId](const Waiter &waiter) {
                         return waiter.deviceId == deviceId &&
                                (requestId == 0 || waiter.requestId == requestId);
                       }),
        waiters_.end());
  }

  void HandlePairStart(const sockaddr_in &endpoint,
                       const pcpair::Message &message) {
    for (const auto &cancelled : cancelledRequests_) {
      if (cancelled.deviceId == message.deviceId &&
          cancelled.requestId == message.requestId && cancelled.expires > Clock::now()) {
        SendStatus(endpoint, message, pcpair::Status::Cancelled);
        return;
      }
    }
    if (message.deviceId == 0 || message.requestId == 0 ||
        message.pairingCode == 0 || message.pairingCode > 999999U) {
      SendStatus(endpoint, message, pcpair::Status::Invalid);
      return;
    }
    if (Binding *existing = FindBindingByDevice(message.deviceId)) {
      const int slot = BindingSlot(*existing, message.deviceId);
      if (slot >= 0 && existing->pendingRequest[slot] == message.requestId)
        SendMatched(endpoint, *existing, slot, message.requestId,
                    message.pairingCode);
      else
        SendStatus(endpoint, message, pcpair::Status::AlreadyBound);
      return;
    }
    for (Waiter &waiter : waiters_) {
      if (waiter.deviceId == message.deviceId &&
          waiter.requestId == message.requestId &&
          waiter.pairingCode == message.pairingCode) {
        waiter.endpoint = endpoint;
        waiter.lastSeen = Clock::now();
        waiter.appVersion = message.appVersion;
        SendStatus(endpoint, message, pcpair::Status::Waiting);
        return;
      }
    }
    RemoveWaiter(message.deviceId);
    const auto peer = std::find_if(
        waiters_.begin(), waiters_.end(), [&message](const Waiter &waiter) {
          return waiter.pairingCode == message.pairingCode &&
                 waiter.deviceId != message.deviceId;
        });
    if (peer == waiters_.end()) {
      if (waiters_.size() >= 4096) {
        SendStatus(endpoint, message, pcpair::Status::Invalid);
        return;
      }
      Waiter waiter;
      waiter.deviceId = message.deviceId;
      waiter.requestId = message.requestId;
      waiter.pairingCode = message.pairingCode;
      waiter.endpoint = endpoint;
      waiter.lastSeen = Clock::now();
      waiter.appVersion = message.appVersion;
      waiters_.push_back(waiter);
      SendStatus(endpoint, message, pcpair::Status::Waiting);
      std::printf("PAIR_WAIT device=%u\n", message.deviceId);
      return;
    }
    const Waiter first = *peer;
    waiters_.erase(peer);
    Binding binding;
    binding.id = UniqueBindingId();
    binding.devices[0] = first.deviceId;
    binding.devices[1] = message.deviceId;
    for (int i = 0; i < 2; ++i) {
      binding.tokenLow[i] = SecureRandomU32();
      binding.tokenHigh[i] = SecureRandomU32();
    }
    binding.pendingRequest[0] = first.requestId;
    binding.pendingRequest[1] = message.requestId;
    binding.pendingVersion[0] = first.appVersion;
    binding.pendingVersion[1] = message.appVersion;
    binding.provisionalDeadline = Clock::now() + std::chrono::seconds(60);
    bindings_.push_back(binding);
    if (!SaveBindings()) {
      bindings_.pop_back();
      pcpair::Message firstRequest;
      firstRequest.deviceId = first.deviceId;
      firstRequest.requestId = first.requestId;
      firstRequest.pairingCode = first.pairingCode;
      firstRequest.appVersion = first.appVersion;
      SendStatus(first.endpoint, firstRequest, pcpair::Status::StorageError);
      SendStatus(endpoint, message, pcpair::Status::StorageError);
      std::fprintf(stderr, "PAIR_STORE_FAILED devices=%u:%u\n",
                   first.deviceId, message.deviceId);
      return;
    }
    SendMatched(first.endpoint, bindings_.back(), 0, first.requestId,
                first.pairingCode);
    SendMatched(endpoint, bindings_.back(), 1, message.requestId,
                message.pairingCode);
    std::printf("PAIR_MATCH binding=%u devices=%u:%u\n",
                binding.id, binding.devices[0], binding.devices[1]);
  }

  void HandlePairCancel(const sockaddr_in &endpoint,
                         const pcpair::Message &message) {
    if (message.deviceId == 0 || message.requestId == 0) {
      SendStatus(endpoint, message, pcpair::Status::Invalid);
      return;
    }
    // A match isn't permanent until both clients have durably saved its
    // credentials and resumed. Cancel must also revoke this provisional pair.
    if (Binding *binding = FindBindingByDevice(message.deviceId)) {
      const int slot = BindingSlot(*binding, message.deviceId);
      if (slot >= 0 && binding->pendingRequest[slot] == message.requestId) {
        pcpair::Message revoke = message;
        revoke.bindingId = binding->id;
        revoke.tokenLow = binding->tokenLow[slot];
        revoke.tokenHigh = binding->tokenHigh[slot];
        HandleUnbind(endpoint, revoke);
        if (FindBindingByDevice(message.deviceId) != nullptr) return;
      }
    }
    RemoveWaiter(message.deviceId, message.requestId);
    cancelledRequests_.push_back(
        {message.deviceId, message.requestId, Clock::now() + std::chrono::seconds(60)});
    if (cancelledRequests_.size() > 4096) cancelledRequests_.erase(cancelledRequests_.begin());
    SendStatus(endpoint, message, pcpair::Status::Cancelled);
    std::printf("PAIR_CANCEL device=%u request=%u\n", message.deviceId,
                message.requestId);
  }

  void HandleResume(const sockaddr_in &endpoint,
                    const pcpair::Message &message) {
    Binding *binding = FindBinding(message.bindingId);
    const int slot = binding == nullptr ? -1 : BindingSlot(*binding, message.deviceId);
    if (binding == nullptr || !ValidToken(*binding, slot, message)) {
      SendStatus(endpoint, message, pcpair::Status::BindingMissing);
      return;
    }
    Room &room = GetRoom(binding->id);
    Client &client = room.clients[slot];
    const bool newProcess = !client.active ||
                            client.deviceId != message.deviceId ||
                            client.resumeRequestId != message.requestId;
    if (newProcess) {
      if (client.active && client.deviceId == message.deviceId) {
        if (room.phase == plink::GamePhase::Waiting) {
          EnterMenu(room, "client_restarted_during_invite");
        } else if (room.phase == plink::GamePhase::Countdown ||
                   room.phase == plink::GamePhase::Playing) {
          if (room.battleEnabled) {
            // A new process/session is not proof of a voluntary forfeit.
            EndSyncFailure(room);
          } else {
          const Client &peer = room.clients[1 - slot];
          const bool peerOnline = peer.active &&
              Clock::now() - peer.lastSeen < std::chrono::seconds(5);
          room.winnerSlot = peerOnline
              ? static_cast<uint8_t>((1 - slot) + 1) : 0;
          room.endReason = plink::MatchEndReason::PlayerDisconnected;
          room.phase = plink::GamePhase::Finished;
          room.phaseStarted = Clock::now();
          }
          std::printf("MATCH_END binding=%u reason=client_restarted slot=%u\n",
                      room.bindingId, static_cast<unsigned>(slot + 1));
        }
      }
      client = Client{};
      client.active = true;
      client.deviceId = message.deviceId;
      client.slot = static_cast<uint8_t>(slot + 1);
      client.session = UniqueSession();
      client.resumeRequestId = message.requestId;
      std::printf("RESUME binding=%u device=%u slot=%u\n", binding->id,
                  message.deviceId, client.slot);
    }
    client.endpoint = endpoint;
    client.lastSeen = Clock::now();
    client.appVersion = message.appVersion;
    client.supportsDnd = (message.compatibilityFlags & pcpair::kDndSupported) != 0;
    client.supportsMotion = client.supportsDnd && pcpair::GameProtocolFor(message.appVersion) >= 2 &&
        (message.compatibilityFlags & pcmotion::kSupported) != 0;
    if (binding->pendingRequest[slot] != 0) {
      const uint32_t pendingRequest = binding->pendingRequest[slot];
      binding->pendingRequest[slot] = 0;
      if (!SaveBindings()) binding->pendingRequest[slot] = pendingRequest;
    }
    if (binding->pendingRequest[0] == 0 && binding->pendingRequest[1] == 0)
      binding->provisionalDeadline = Clock::time_point{};
    SendWelcome(client);
    if (!MajorVersionsCompatible(room) &&
        room.phase != plink::GamePhase::Menu) {
      EnterMenu(room, "major_version_mismatch");
    }
    SendCompatibility(room);
  }

  void HandleGoodbye(const pcpair::Message &message) {
    RemoveWaiter(message.deviceId);
    Binding *binding = FindBinding(message.bindingId);
    const int slot = binding == nullptr ? -1 : BindingSlot(*binding, message.deviceId);
    if (binding == nullptr || !ValidToken(*binding, slot, message)) return;
    Room &room = GetRoom(binding->id);
    if (room.phase == plink::GamePhase::Countdown || room.phase == plink::GamePhase::Playing) {
      // An authenticated, explicit Goodbye retains the voluntary-exit rule.
      const auto &peer = room.clients[1 - slot];
      room.winnerSlot = peer.active && Clock::now() - peer.lastSeen < std::chrono::seconds(10)
          ? static_cast<uint8_t>(2 - slot) : 0;
      room.endReason = plink::MatchEndReason::PlayerDisconnected;
      room.phase = plink::GamePhase::Finished;
      room.phaseStarted = Clock::now();
    }
    room.clients[slot] = Client{};
    if (room.phase == plink::GamePhase::Waiting) EnterMenu(room, "peer_goodbye");
    std::printf("GOODBYE binding=%u device=%u\n", binding->id,
                message.deviceId);
  }

  void HandleUnbind(const sockaddr_in &endpoint,
                    const pcpair::Message &message) {
    Binding *binding = FindBinding(message.bindingId);
    const int slot =
        binding == nullptr ? -1 : BindingSlot(*binding, message.deviceId);
    if (binding == nullptr) {
      for (const UnboundTombstone &tombstone : unboundTombstones_) {
        const int recentSlot = BindingSlot(tombstone.binding,
                                           message.deviceId);
        if (tombstone.binding.id == message.bindingId &&
            ValidToken(tombstone.binding, recentSlot, message)) {
          SendStatus(endpoint, message, pcpair::Status::Unbound);
          return;
        }
      }
      SendStatus(endpoint, message, pcpair::Status::BindingMissing);
      return;
    }
    if (!ValidToken(*binding, slot, message)) {
      SendStatus(endpoint, message, pcpair::Status::BindingMissing);
      return;
    }
    const Binding removed = *binding;
    struct NoticeTarget {
      sockaddr_in endpoint{};
      uint32_t deviceId = 0;
    };
    std::vector<NoticeTarget> targets;
    for (const Room &room : rooms_) {
      if (room.bindingId != removed.id) continue;
      for (const Client &client : room.clients) {
        if (client.active)
          targets.push_back({client.endpoint, client.deviceId});
      }
    }
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(),
                       [&removed](const Binding &candidate) {
                         return candidate.id == removed.id;
                       }),
        bindings_.end());
    if (!SaveBindings()) {
      bindings_.push_back(removed);
      SendStatus(endpoint, message, pcpair::Status::StorageError);
      return;
    }
    rooms_.erase(
        std::remove_if(rooms_.begin(), rooms_.end(),
                       [&removed](const Room &room) {
                         return room.bindingId == removed.id;
                       }),
        rooms_.end());
    unboundTombstones_.push_back(
        {removed, Clock::now() + std::chrono::seconds(30)});
    if (unboundTombstones_.size() > 256U)
      unboundTombstones_.erase(unboundTombstones_.begin());
    bool callerNotified = false;
    for (const NoticeTarget &target : targets) {
      pcpair::Message response;
      response.type = pcpair::MessageType::Status;
      response.deviceId = target.deviceId;
      response.bindingId = removed.id;
      response.status = pcpair::Status::Unbound;
      for (int repeat = 0; repeat < 3; ++repeat)
        SendControl(target.endpoint, response);
      if (target.deviceId == message.deviceId) callerNotified = true;
    }
    if (!callerNotified)
      SendStatus(endpoint, message, pcpair::Status::Unbound);
    std::printf("UNBOUND binding=%u requested_by=%u\n", removed.id,
                message.deviceId);
  }

  void HandleControl(const sockaddr_in &endpoint,
                     const pcpair::Message &message) {
    if (message.type == pcpair::MessageType::Start)
      HandlePairStart(endpoint, message);
    else if (message.type == pcpair::MessageType::Cancel)
      HandlePairCancel(endpoint, message);
    else if (message.type == pcpair::MessageType::Resume)
      HandleResume(endpoint, message);
    else if (message.type == pcpair::MessageType::Goodbye)
      HandleGoodbye(message);
    else if (message.type == pcpair::MessageType::Unbind)
      HandleUnbind(endpoint, message);
  }

  struct LocatedClient {
    Room *room = nullptr;
    Client *client = nullptr;
  };

  LocatedClient FindClient(const sockaddr_in &endpoint, uint32_t session) {
    for (Room &room : rooms_)
      for (Client &client : room.clients)
        if (client.active && client.session == session &&
            SameEndpoint(client.endpoint, endpoint))
          return {&room, &client};
    return {};
  }

  uint8_t OnlineMask(const Room &room) const {
    uint8_t mask = 0;
    const auto now = Clock::now();
    for (const Client &client : room.clients)
      if (client.active && now - client.lastSeen < std::chrono::seconds(5))
        mask |= static_cast<uint8_t>(1U << (client.slot - 1U));
    return mask;
  }

  bool AllowControl(const sockaddr_in &endpoint) {
    const auto now = Clock::now();
    sourceRates_.erase(
        std::remove_if(sourceRates_.begin(), sourceRates_.end(),
                       [now](const SourceRate &rate) {
                         return now - rate.lastSeen > std::chrono::minutes(2);
                       }),
        sourceRates_.end());
    SourceRate *found = nullptr;
    // Each authenticated WSS connection owns a distinct loopback UDP socket.
    // Keep the per-IP limit for direct LAN traffic, but don't merge the gateway.
    const uint16_t sourcePort =
        (ntohl(endpoint.sin_addr.s_addr) >> 24U) == 127U ? endpoint.sin_port : 0;
    for (SourceRate &rate : sourceRates_) {
      if (rate.address == endpoint.sin_addr.s_addr && rate.port == sourcePort) {
        found = &rate;
        break;
      }
    }
    if (found == nullptr) {
      if (sourceRates_.size() >= 2048) return false;
      sourceRates_.push_back(
          {endpoint.sin_addr.s_addr, sourcePort, 0, now, now});
      found = &sourceRates_.back();
    }
    found->lastSeen = now;
    if (now - found->windowStarted >= std::chrono::seconds(1)) {
      found->windowStarted = now;
      found->packets = 0;
    }
    if (found->packets >= 40) return false;
    ++found->packets;
    return true;
  }

  void ReceiveAll() {
    for (unsigned processed = 0; processed < 512; ++processed) {
      uint8_t bytes[pcpair::kMaxDatagramSize]{};
      sockaddr_in from{};
#ifdef _WIN32
      int fromLength = sizeof(from);
#else
      socklen_t fromLength = sizeof(from);
#endif
      const int received = recvfrom(
          socket_, reinterpret_cast<char *>(bytes), sizeof(bytes), 0,
          reinterpret_cast<sockaddr *>(&from), &fromLength);
      if (received < 0) {
#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) ++invalidPackets_;
#else
        if (errno != EWOULDBLOCK && errno != EAGAIN) ++invalidPackets_;
#endif
        return;
      }
      // Readiness probe: loopback only, fixed-size echo, no identity, binding,
      // telemetry or simulation mutation. Public gameplay still uses auth.
      if (received == 24 && (ntohl(from.sin_addr.s_addr) >> 24U) == 127U &&
          std::memcmp(bytes, "PPPROBE1", 8) == 0) {
        std::memcpy(bytes, "PPREADY1", 8);
        sendto(socket_, reinterpret_cast<const char *>(bytes), received, 0,
               reinterpret_cast<const sockaddr *>(&from), sizeof(from));
        continue;
      }
      pcpair::Message control;
      if (pcpair::Parse(bytes, static_cast<size_t>(received), control,
                        config_.networkKey)) {
        if (!AllowControl(from)) {
          ++invalidPackets_;
          continue;
        }
        HandleControl(from, control);
        continue;
      }
      size_t packetLength = static_cast<size_t>(received);
      if (!pcpair::VerifyAndStripPacketAuth(bytes, packetLength,
                                            config_.networkKey)) {
        ++invalidPackets_;
        continue;
      }
      plink::PacketHeader header;
      const uint8_t *payload = nullptr;
      if (!plink::ParsePacket(bytes, packetLength, header, payload)) {
        ++invalidPackets_;
        continue;
      }
      LocatedClient located = FindClient(from, header.session);
      if (located.client == nullptr) {
        ++invalidPackets_;
        continue;
      }
      Client &client = *located.client;
      // Tick-tagged v3 inputs are idempotent. A newer ping/packet must not
      // discard an older datagram that still contains missing simulation ticks.
      if (header.sequence <= client.lastSequence && header.type != pcbattle::kInputType) continue;
      client.lastSequence = std::max(client.lastSequence, header.sequence);
      client.lastSeen = Clock::now();
      if (header.type == pcpair::kDndPreferenceType && client.supportsDnd) {
        pcpair::DndPreference preference;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (pcpair::ReadDndPreference(reader, preference))
          ApplyDnd(*located.room, client, preference);
      } else if (header.type == pcbattle::kInputType && client.supportsMotion && pcpair::GameProtocolFor(client.appVersion) == 3) {
        pcbattle::Batch batch;
        plink::PayloadReader reader(payload, header.payloadLength);
        Room &room = *located.room;
        if (pcbattle::ReadBatch(reader, batch) && room.battleEnabled &&
            room.phase == plink::GamePhase::Playing && batch.round == room.roundId &&
            batch.first <= pcbattle::kMaxTicks && batch.count <= pcbattle::kMaxTicks - batch.first + 1) {
          bool valid = true;
          // Validate atomically against a copy: no partial malicious batch.
          auto proposed = room.battle.inputs[client.slot - 1];
          for (unsigned i = 0; i < batch.count; ++i)
            valid = proposed.Put(batch.first + i, batch.commands[i], room.battle.world.tick,
                std::min<uint32_t>(pcbattle::kMaxTicks, room.battle.budget + pcbattle::kFutureAllowance)) && valid;
          if (valid) room.battle.inputs[client.slot - 1] = proposed;
          else ++invalidPackets_;
        } else ++invalidPackets_;
      } else if (header.type == pcmotion::kInputType && client.supportsMotion && !located.room->battleEnabled) {
        pcmotion::Batch batch;
        plink::PayloadReader reader(payload, header.payloadLength);
        Room &room = *located.room;
        if (pcmotion::ReadBatch(reader, batch) && room.motionEnabled &&
            room.phase == plink::GamePhase::Playing && batch.round == room.roundId) {
          for (unsigned i = 0; i < batch.count; ++i)
            client.motionInputs.Put(batch.first + i, batch.commands[i]);
        } else ++invalidPackets_;
      } else if (header.type == pcmotion::kAbortType && client.supportsMotion) {
        uint32_t lo = 0, hi = 0;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (reader.U32(lo) && reader.U32(hi) && reader.Done() &&
            (uint64_t(lo) | (uint64_t(hi) << 32)) == located.room->roundId)
          EndSyncFailure(*located.room);
      } else if (header.type == plink::PacketType::Input && !client.supportsMotion) {
        plink::InputPayload input;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (!plink::ReadInput(reader, input)) {
          ++invalidPackets_;
          continue;
        }
        client.input = static_cast<uint8_t>(input.current & 0x0FU);
        client.lastInputSequence = header.sequence;
      } else if (header.type == plink::PacketType::Ping) {
        uint32_t sentAt = 0;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (plink::ReadTimestamp(reader, sentAt)) SendPong(client, sentAt);
      } else if (header.type == pcpair::kScopedActionPacketType) {
        pcpair::ScopedAction action;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (pcpair::ReadScopedAction(reader, action)) {
          Room &room = *located.room;
          if (action.operationId != client.lastOperationId &&
              action.context.phase == room.phase &&
              action.context.roundId == room.roundId &&
              action.context.inviteId == room.inviteId) {
            HandleAction(room, client, action.action, action.operationId);
          }
          client.lastOperationId = action.operationId;
          uint8_t ackBytes[plink::kMaxPacketSize]{};
          plink::PacketWriter ack(ackBytes, sizeof(ackBytes), pcpair::kActionAckPacketType,
                                 client.session, ++serverSequence_, client.lastSequence, serverTick_);
          if (ack.U32(action.operationId)) Send(client, ackBytes, ack.Finish());
        }
      } else if (header.type == plink::PacketType::Action) {
        plink::ActionPayload action;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (plink::ReadAction(reader, action))
          HandleAction(*located.room, client, action.action);
      }
    }
  }

  void ApplyDnd(Room &room, Client &client, const pcpair::DndPreference &preference) {
    if (client.dndRevision == 0 || static_cast<int32_t>(preference.revision - client.dndRevision) > 0) {
      client.dndRevision = preference.revision;
      client.doNotDisturb = preference.enabled;
      if (client.doNotDisturb && room.phase == plink::GamePhase::Waiting &&
          room.inviterSlot != client.slot) {
        const auto inviter = room.inviterSlot;
        EnterMenu(room, "invite_interrupted_dnd", plink::MatchEndReason::InviteRejected);
        room.dndInterrupted = true;
        room.dndInterruptedInviter = inviter;
      }
    }
    SendSnapshots(room);
  }

  void BlockInvite(Room &room, Client &client, uint32_t operation, pcpair::InviteBlock reason) {
    if (client.supportsDnd) {
      client.blockedOperation = operation;
      client.blockedReason = reason;
      SendSnapshots(room);
    } else {
      // Legacy UI understands only Waiting -> Menu/InviteRejected. Send this
      // feedback to the sender only, without creating a real invitation or
      // waking the recipient. New clients get the explicit PC-only outcome.
      Room feedback = room;
      feedback.clients[client.slot == 1 ? 1 : 0].active = false;
      feedback.inviterSlot = client.slot;
      feedback.inviteId = SecureRandomU64();
      feedback.phase = plink::GamePhase::Waiting;
      feedback.phaseStarted = Clock::now();
      feedback.dndInterrupted = false;
      SendSnapshots(feedback);
      EnterMenu(feedback, "legacy_invite_unavailable", plink::MatchEndReason::InviteRejected);
      SendSnapshots(feedback);
    }
  }

  void HandleAction(Room &room, Client &client, plink::PlayerAction action, uint32_t operation = 0) {
    const auto now = Clock::now();
    if (!MajorVersionsCompatible(room)) return;
    // A v2 movement peer must never silently fight under the v1 rules.
    if (room.clients[0].supportsMotion != room.clients[1].supportsMotion &&
        (action == plink::PlayerAction::Invite || action == plink::PlayerAction::Accept)) return;
    if (action == plink::PlayerAction::Invite &&
        room.phase == plink::GamePhase::Menu && OnlineMask(room) == 0x03U) {
      const Client &recipient = room.clients[client.slot == 1 ? 1 : 0];
      if (recipient.supportsDnd && (recipient.dndRevision == 0 || recipient.doNotDisturb)) {
        BlockInvite(room, client, operation, recipient.dndRevision == 0
            ? pcpair::InviteBlock::Synchronizing : pcpair::InviteBlock::DoNotDisturb);
        return;
      }
      room.dndInterrupted = false;
      room.inviterSlot = client.slot;
      room.inviteId = SecureRandomU64();
      room.roundId = 0;
      room.winnerSlot = 0;
      room.endReason = plink::MatchEndReason::None;
      room.phase = plink::GamePhase::Waiting;
      room.phaseStarted = now;
    } else if (action == plink::PlayerAction::Accept &&
               room.phase == plink::GamePhase::Waiting &&
               client.slot != room.inviterSlot) {
      if (client.supportsDnd && client.doNotDisturb) {
        const auto inviter = room.inviterSlot;
        EnterMenu(room, "accept_blocked_dnd", plink::MatchEndReason::InviteRejected);
        room.dndInterrupted = true;
        room.dndInterruptedInviter = inviter;
        SendSnapshots(room);
        return;
      }
      plink::InitializeWorld(room.world);
      room.motion = pcmotion::World{};
      room.motionEnabled = room.clients[0].supportsMotion && room.clients[1].supportsMotion;
      room.battle = pcbattle::Authority{};
      room.battleEnabled = room.motionEnabled && pcpair::GameProtocolFor(room.clients[0].appVersion) == 3 &&
          pcpair::GameProtocolFor(room.clients[1].appVersion) == 3;
      room.syncFailed = false;
      for (auto &member : room.clients) {
        member.motionInputs = pcmotion::InputQueue{};
        member.lastMotionProcessed = {};
      }
      room.roundId = SecureRandomU64();
      room.endReason = plink::MatchEndReason::None;
      room.phase = plink::GamePhase::Countdown;
      room.phaseStarted = now;
      room.matchStarted = Clock::time_point{};
    } else if (action == plink::PlayerAction::ReturnToMenu &&
               (room.phase == plink::GamePhase::Waiting ||
                room.phase == plink::GamePhase::Finished)) {
      const bool rejected = room.phase == plink::GamePhase::Waiting &&
                            client.slot != room.inviterSlot;
      EnterMenu(room, rejected ? "invite_rejected" : "cancel_or_return",
                rejected ? plink::MatchEndReason::InviteRejected
                         : plink::MatchEndReason::None);
    } else if (plink::IsQuickEmote(action) &&
               room.phase == plink::GamePhase::Menu &&
               OnlineMask(room) == 0x03U &&
               (client.lastEmote.time_since_epoch().count() == 0 ||
                now - client.lastEmote >= std::chrono::milliseconds(700))) {
      client.lastEmote = now;
      const size_t peerIndex = client.slot == 1 ? 1U : 0U;
      if (room.clients[peerIndex].active)
        SendAction(room.clients[peerIndex], action);
    }
  }

  void EnterMenu(
      Room &room, const char *reason,
      plink::MatchEndReason outcome = plink::MatchEndReason::None) {
    room.phase = plink::GamePhase::Menu;
    room.inviterSlot = 0;
    room.winnerSlot = 0;
    room.endReason = outcome;
    room.dndInterrupted = false;
    room.phaseStarted = Clock::now();
    plink::InitializeWorld(room.world);
    room.motion = pcmotion::World{};
    room.battle = pcbattle::Authority{};
    room.battleEnabled = false;
    room.motionEnabled = false;
    room.syncFailed = false;
    std::printf("MENU binding=%u reason=%s\n", room.bindingId, reason);
  }

  void EndSyncFailure(Room &room) {
    if (!room.motionEnabled || (room.phase != plink::GamePhase::Playing &&
        !(room.battleEnabled && room.phase == plink::GamePhase::Countdown))) return;
    room.syncFailed = true;
    room.winnerSlot = 0;
    room.endReason = plink::MatchEndReason::ServerUnavailable;
    room.phase = plink::GamePhase::Finished;
    room.phaseStarted = Clock::now();
    std::printf("SYNC_ENDED binding=%u round=%llu\n", room.bindingId,
        static_cast<unsigned long long>(room.roundId));
    SendSnapshots(room);
  }

  void TickRoom(Room &room, const Clock::time_point &now) {
    for (Client &client : room.clients) {
      if (client.active && now - client.lastSeen > std::chrono::seconds(10)) {
        std::printf("OFFLINE binding=%u device=%u slot=%u\n", room.bindingId,
                    client.deviceId, client.slot);
        client = Client{};
      }
    }
    if (room.phase == plink::GamePhase::Waiting) {
      if (OnlineMask(room) != 0x03U) {
        EnterMenu(room, "invite_peer_offline");
      } else if (now - room.phaseStarted >=
                 std::chrono::seconds(config_.inviteSeconds)) {
        EnterMenu(room, "invite_timeout",
                  plink::MatchEndReason::InviteTimedOut);
      }
    }
    if ((room.phase == plink::GamePhase::Countdown ||
         room.phase == plink::GamePhase::Playing) &&
        OnlineMask(room) != 0x03U) {
      if (room.battleEnabled) {
        // Heartbeat expiry / missing endpoint is a passive network failure,
        // unlike HandleGoodbye. Both v3 peers receive the same non-result.
        EndSyncFailure(room);
      } else {
      const uint8_t mask = OnlineMask(room);
      room.winnerSlot = mask == 0x01U ? 1 : (mask == 0x02U ? 2 : 0);
      room.endReason = plink::MatchEndReason::PlayerDisconnected;
      room.phase = plink::GamePhase::Finished;
      room.phaseStarted = now;
      }
    }
    if (room.phase == plink::GamePhase::Countdown &&
        now - room.phaseStarted >= std::chrono::seconds(3)) {
      room.phase = plink::GamePhase::Playing;
      room.phaseStarted = room.matchStarted = now;
    }
    if (room.phase == plink::GamePhase::Playing) {
      if (room.battleEnabled) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - room.matchStarted).count();
        const uint32_t elapsedTick = static_cast<uint32_t>(std::max<int64_t>(0, elapsed) * pcbattle::kHz / 1000000);
        room.battle.Advance(elapsedTick, static_cast<uint32_t>(config_.matchSeconds) * pcbattle::kHz);
        if (room.battle.failed) { EndSyncFailure(room); return; }
        room.battle.world.Export(room.world);
      } else if (room.motionEnabled) {
        pcmotion::Command commands[2]{};
        for (unsigned i = 0; i < 2; ++i) {
          auto &member = room.clients[i];
          if (member.motionInputs.Pop(commands[i])) member.lastMotionProcessed = now;
          const auto lastProgress = member.lastMotionProcessed == Clock::time_point{}
              ? room.matchStarted : member.lastMotionProcessed;
          if (now - lastProgress > std::chrono::seconds(2)) {
            EndSyncFailure(room);
            return;
          }
        }
        room.motion.Step(room.world, commands);
      } else if (serverTick_ % 2 == 0) plink::StepWorld(room.world,
                       room.clients[0].active ? room.clients[0].input : 0,
                       room.clients[1].active ? room.clients[1].input : 0,
                       false);
      const bool timeUp = room.battleEnabled
          ? room.battle.world.tick >= static_cast<uint32_t>(config_.matchSeconds) * pcbattle::kHz
          : now - room.matchStarted >= std::chrono::seconds(config_.matchSeconds);
      const bool dead = room.world.players[0].health == 0 ||
                        room.world.players[1].health == 0;
      if (timeUp || dead) {
        if (timeUp) {
          room.winnerSlot = 0;
          room.endReason = plink::MatchEndReason::TimeLimitDraw;
        } else {
          if (room.world.hitCount[0] > room.world.hitCount[1])
            room.winnerSlot = 1;
          else if (room.world.hitCount[1] > room.world.hitCount[0])
            room.winnerSlot = 2;
          else
            room.winnerSlot = 0;
          room.endReason = plink::MatchEndReason::Destroyed;
        }
        room.phase = plink::GamePhase::Finished;
        room.phaseStarted = now;
      }
    }
    if ((serverTick_ % (room.motionEnabled ? 2U : 4U)) == 0U) SendSnapshots(room);
  }

  void Tick() {
    ++serverTick_;
    const auto now = Clock::now();
    cancelledRequests_.erase(
        std::remove_if(cancelledRequests_.begin(), cancelledRequests_.end(),
                       [now](const CancelledRequest &entry) { return now >= entry.expires; }),
        cancelledRequests_.end());
    std::vector<pcpair::Message> expiredPairs;
    for (Binding &binding : bindings_) {
      if (binding.provisionalDeadline.time_since_epoch().count() == 0 ||
          now < binding.provisionalDeadline) continue;
      for (int slot = 0; slot < 2; ++slot) {
        if (binding.pendingRequest[slot] == 0) continue;
        pcpair::Message cancel;
        cancel.deviceId = binding.devices[slot];
        cancel.requestId = binding.pendingRequest[slot];
        expiredPairs.push_back(cancel);
        binding.provisionalDeadline = now + std::chrono::seconds(5);
        break;
      }
    }
    for (const auto &cancel : expiredPairs) {
      sockaddr_in noCaller{};
      noCaller.sin_family = AF_INET;
      HandlePairCancel(noCaller, cancel);
    }
    const auto before = waiters_.size();
    waiters_.erase(
        std::remove_if(waiters_.begin(), waiters_.end(), [now](const Waiter &waiter) {
          return now - waiter.lastSeen >
                 std::chrono::milliseconds(pcpair::kWaitingTimeoutMs);
        }),
        waiters_.end());
    if (before != waiters_.size())
      std::printf("PAIR_EXPIRE count=%zu\n", before - waiters_.size());
    unboundTombstones_.erase(
        std::remove_if(unboundTombstones_.begin(), unboundTombstones_.end(),
                       [now](const UnboundTombstone &tombstone) {
                         return now >= tombstone.expires;
                       }),
        unboundTombstones_.end());
    for (Room &room : rooms_) TickRoom(room, now);
  }

  void SendWelcome(Client &client) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Welcome,
                               client.session, ++serverSequence_,
                               client.lastSequence, serverTick_);
    plink::WelcomePayload welcome;
    welcome.assignedSlot = client.slot;
    welcome.tickRate = client.supportsMotion ? 60 : 30;
    welcome.firePeriodTicks = client.supportsMotion ? 48 : plink::kFirePeriodTicks;
    welcome.serverTimeMs = MillisSince(start_);
    if (plink::WriteWelcome(writer, welcome)) Send(client, bytes, writer.Finish());
  }

  void SendPong(Client &client, uint32_t sentAt) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Pong,
                               client.session, ++serverSequence_,
                               client.lastSequence, serverTick_);
    if (plink::WriteTimestamp(writer, sentAt)) Send(client, bytes, writer.Finish());
  }

  void SendAction(Client &client, plink::PlayerAction action) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Action,
                               client.session, ++serverSequence_,
                               client.lastSequence, serverTick_);
    plink::ActionPayload payload;
    payload.action = action;
    if (plink::WriteAction(writer, payload)) {
      const size_t length = writer.Finish();
      if (length != 0) {
        // The receiver de-duplicates this idempotent server sequence.
        Send(client, bytes, length);
        Send(client, bytes, length);
      }
    }
  }

  void SendSnapshots(Room &room) {
    const auto now = Clock::now();
    uint32_t remainingMs = 0;
    if (room.phase == plink::GamePhase::Waiting) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - room.phaseStarted).count();
      const int64_t durationMs =
          static_cast<int64_t>(config_.inviteSeconds) * 1000;
      remainingMs = elapsed >= durationMs
          ? 0U : static_cast<uint32_t>(durationMs - elapsed);
    } else if (room.phase == plink::GamePhase::Countdown) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - room.phaseStarted).count();
      remainingMs = elapsed >= 3000 ? 0 : static_cast<uint32_t>(3000 - elapsed);
    } else if (room.phase == plink::GamePhase::Playing) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - room.matchStarted).count();
      const uint64_t durationMs =
          static_cast<uint64_t>(config_.matchSeconds) * 1000ULL;
      remainingMs = static_cast<uint64_t>(elapsed) >= durationMs
          ? 0U : static_cast<uint32_t>(durationMs - elapsed);
    }
    uint32_t matchElapsedMs = 0;
    if ((room.phase == plink::GamePhase::Playing ||
         room.phase == plink::GamePhase::Finished) &&
        room.matchStarted.time_since_epoch().count() != 0) {
      const auto end = room.phase == plink::GamePhase::Finished
                           ? room.phaseStarted
                           : now;
      matchElapsedMs = static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(end - room.matchStarted)
              .count());
    }
    for (Client &client : room.clients) {
      if (!client.active) continue;
      if (!client.supportsDnd) {
        uint8_t metaBytes[plink::kMaxPacketSize]{};
        plink::PacketWriter metaWriter(
            metaBytes, sizeof(metaBytes), pcpair::kRoundMetaPacketType,
            client.session, ++serverSequence_, client.lastSequence,
            serverTick_);
        pcpair::RoundMeta meta;
        meta.roundId = room.roundId;
        meta.inviteId = room.inviteId;
        meta.phase = room.phase;
        if (pcpair::WriteRoundMeta(metaWriter, meta))
          Send(client, metaBytes, metaWriter.Finish());
      }
      plink::SnapshotPayload snapshot;
      plink::MakeSnapshot(room.world, MillisSince(start_),
                          client.lastInputSequence, snapshot);
      snapshot.phase = room.phase;
      snapshot.inviterSlot = room.inviterSlot;
      snapshot.winnerSlot = room.winnerSlot;
      snapshot.endReason = room.endReason;
      snapshot.onlineMask = OnlineMask(room);
      snapshot.phaseRemainingMs = remainingMs;
      snapshot.matchElapsedMs = matchElapsedMs;
      uint8_t bytes[plink::kMaxPacketSize]{};
      if (client.supportsDnd) {
        pcpair::PresenceSnapshot state;
        state.acknowledgedRevision = client.dndRevision;
        for (const auto &member : room.clients) {
          if (!member.active || !(snapshot.onlineMask & (1U << (member.slot - 1)))) continue;
          const auto bit = static_cast<uint8_t>(1U << (member.slot - 1));
          if (member.supportsDnd) state.capableMask |= bit;
          if (member.supportsDnd && member.dndRevision != 0) {
            state.knownMask |= bit;
            if (member.doNotDisturb) state.enabledMask |= bit;
          }
        }
        state.interruptedByDnd = room.dndInterrupted;
        state.interruptedInviter = room.dndInterruptedInviter;
        state.blockedOperation = client.blockedOperation;
        state.blockedReason = client.blockedReason;
        state.roundId = room.roundId;
        state.inviteId = room.inviteId;
        state.game = snapshot;
        if (client.supportsMotion && pcpair::GameProtocolFor(client.appVersion) == 3) {
          pcbattle::State battle;
          battle.presence = state; battle.world = room.battle.world; battle.failed = room.syncFailed;
          plink::PacketWriter writer(bytes, sizeof(bytes), pcbattle::kStateType,
              client.session, ++serverSequence_, client.lastSequence, serverTick_);
          if (pcbattle::WriteState(writer, battle)) Send(client, bytes, writer.Finish());
          if (room.battleEnabled && room.phase == plink::GamePhase::Playing) {
            pcbattle::Batch relay; relay.round = room.roundId;
            relay.first = room.battle.world.tick + 1;
            const auto &peerInputs = room.battle.inputs[client.slot == 1 ? 1 : 0];
            while (relay.count < pcmotion::kBatchLimit &&
                peerInputs.Get(relay.first + relay.count, relay.commands[relay.count])) ++relay.count;
            if (relay.count) {
              plink::PacketWriter rw(bytes, sizeof(bytes), pcbattle::kRelayType,
                  client.session, ++serverSequence_, client.lastSequence, serverTick_);
              if (pcbattle::WriteBatch(rw, relay)) Send(client, bytes, rw.Finish());
            }
          }
          if (room.battleEnabled && room.battle.world.impactCount && room.roundId) {
            pcbattle::Events events; events.round = room.roundId;
            events.count = static_cast<uint8_t>(room.battle.world.impactCount);
            events.hits = room.battle.world.impacts;
            plink::PacketWriter ew(bytes, sizeof(bytes), pcbattle::kImpactType,
                client.session, ++serverSequence_, client.lastSequence, serverTick_);
            if (pcbattle::WriteEvents(ew, events)) Send(client, bytes, ew.Finish());
          }
          continue;
        }
        if (client.supportsMotion) {
          pcmotion::Snapshot motion;
          motion.presence = state;
          motion.presence.game.lastProcessedInput = client.motionInputs.acknowledged;
          motion.tick = room.motion.tick;
          motion.syncFailed = room.syncFailed;
          for (unsigned i = 0; i < 2; ++i) {
            motion.fractions[i * 2] = static_cast<uint8_t>(room.motion.players[i].x % pcmotion::kUnit);
            motion.fractions[i * 2 + 1] = static_cast<uint8_t>(room.motion.players[i].y % pcmotion::kUnit);
          }
          plink::PacketWriter writer(bytes, sizeof(bytes), pcmotion::kSnapshotType,
              client.session, ++serverSequence_, client.lastSequence, serverTick_);
          if (pcmotion::WriteSnapshot(writer, motion)) Send(client, bytes, writer.Finish());
          continue;
        }
        plink::PacketWriter writer(bytes, sizeof(bytes), pcpair::kPresenceSnapshotType,
            client.session, ++serverSequence_, client.lastSequence, serverTick_);
        if (pcpair::WritePresenceSnapshot(writer, state)) Send(client, bytes, writer.Finish());
        continue;
      }
      plink::PacketWriter writer(bytes, sizeof(bytes),
                                 plink::PacketType::Snapshot, client.session,
                                 ++serverSequence_, client.lastSequence,
                                 serverTick_);
      if (plink::WriteSnapshot(writer, snapshot))
        Send(client, bytes, writer.Finish());
    }
  }

  void Send(const Client &client, const uint8_t *bytes, size_t length) {
    uint8_t datagram[pcpair::kMaxDatagramSize]{};
    if (length > plink::kMaxPacketSize) return;
    memcpy(datagram, bytes, length);
    if (!pcpair::AppendPacketAuth(datagram, length, sizeof(datagram),
                                  config_.networkKey)) {
      return;
    }
    sendto(socket_, reinterpret_cast<const char *>(datagram),
           static_cast<int>(length), 0,
           reinterpret_cast<const sockaddr *>(&client.endpoint),
           sizeof(client.endpoint));
  }

  Config config_;
  SOCKET socket_ = INVALID_SOCKET;
  Clock::time_point start_{};
  std::vector<Binding> bindings_;
  std::vector<Waiter> waiters_;
  std::vector<SourceRate> sourceRates_;
  std::vector<Room> rooms_;
  std::vector<UnboundTombstone> unboundTombstones_;
  std::vector<CancelledRequest> cancelledRequests_;
  uint32_t serverTick_ = 0;
  uint32_t serverSequence_ = 0;
  uint64_t invalidPackets_ = 0;
};

}  // namespace

int main(int argc, char **argv) {
  PetServer server(ParseArguments(argc, argv));
  return server.Run() ? 0 : 1;
}
