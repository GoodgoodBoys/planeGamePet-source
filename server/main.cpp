#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
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
#include "../../shared/plane_protocol.h"
#include "../../shared/plane_sim.h"

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
};

struct Waiter {
  uint32_t deviceId = 0;
  uint32_t requestId = 0;
  uint32_t pairingCode = 0;
  sockaddr_in endpoint{};
  Clock::time_point lastSeen{};
};

struct SourceRate {
  uint32_t address = 0;
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
  uint8_t slot = 0;
  uint8_t input = 0;
  Clock::time_point lastSeen{};
  Clock::time_point lastEmote{};
};

struct Room {
  uint32_t bindingId = 0;
  std::array<Client, 2> clients{};
  plink::WorldState world{};
  plink::GamePhase phase = plink::GamePhase::Menu;
  uint8_t inviterSlot = 0;
  uint8_t winnerSlot = 0;
  plink::MatchEndReason endReason = plink::MatchEndReason::None;
  uint64_t roundId = 0;
  uint64_t inviteId = 0;
  Clock::time_point phaseStarted{};
  Clock::time_point matchStarted{};
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
        nextTick += std::chrono::microseconds(33333);
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
    room.phaseStarted = room.matchStarted = Clock::now();
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
    response.status = status;
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
    SendControl(endpoint, response);
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
    bindings_.push_back(binding);
    if (!SaveBindings()) {
      bindings_.pop_back();
      pcpair::Message firstRequest;
      firstRequest.deviceId = first.deviceId;
      firstRequest.requestId = first.requestId;
      firstRequest.pairingCode = first.pairingCode;
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
    RemoveWaiter(message.deviceId, message.requestId);
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
          const Client &peer = room.clients[1 - slot];
          const bool peerOnline = peer.active &&
              Clock::now() - peer.lastSeen < std::chrono::seconds(5);
          room.winnerSlot = peerOnline
              ? static_cast<uint8_t>((1 - slot) + 1) : 0;
          room.endReason = plink::MatchEndReason::PlayerDisconnected;
          room.phase = plink::GamePhase::Finished;
          room.phaseStarted = Clock::now();
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
    if (binding->pendingRequest[slot] != 0) {
      const uint32_t pendingRequest = binding->pendingRequest[slot];
      binding->pendingRequest[slot] = 0;
      if (!SaveBindings()) binding->pendingRequest[slot] = pendingRequest;
    }
    SendWelcome(client);
  }

  void HandleGoodbye(const pcpair::Message &message) {
    RemoveWaiter(message.deviceId);
    Binding *binding = FindBinding(message.bindingId);
    const int slot = binding == nullptr ? -1 : BindingSlot(*binding, message.deviceId);
    if (binding == nullptr || !ValidToken(*binding, slot, message)) return;
    Room &room = GetRoom(binding->id);
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
    if (binding == nullptr || !ValidToken(*binding, slot, message)) {
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
    bool callerNotified = false;
    for (const NoticeTarget &target : targets) {
      pcpair::Message response;
      response.type = pcpair::MessageType::Status;
      response.deviceId = target.deviceId;
      response.bindingId = removed.id;
      response.status = pcpair::Status::Unbound;
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
    for (SourceRate &rate : sourceRates_) {
      if (rate.address == endpoint.sin_addr.s_addr) {
        found = &rate;
        break;
      }
    }
    if (found == nullptr) {
      if (sourceRates_.size() >= 2048) return false;
      sourceRates_.push_back(
          {endpoint.sin_addr.s_addr, 0, now, now});
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
    for (;;) {
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
      if (header.sequence <= client.lastSequence) continue;
      client.lastSequence = header.sequence;
      client.lastSeen = Clock::now();
      if (header.type == plink::PacketType::Input) {
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
      } else if (header.type == plink::PacketType::Action) {
        plink::ActionPayload action;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (plink::ReadAction(reader, action))
          HandleAction(*located.room, client, action.action);
      }
    }
  }

  void HandleAction(Room &room, Client &client, plink::PlayerAction action) {
    const auto now = Clock::now();
    if (action == plink::PlayerAction::Invite &&
        room.phase == plink::GamePhase::Menu && OnlineMask(room) == 0x03U) {
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
      plink::InitializeWorld(room.world);
      room.roundId = SecureRandomU64();
      room.endReason = plink::MatchEndReason::None;
      room.phase = plink::GamePhase::Countdown;
      room.phaseStarted = now;
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
    room.phaseStarted = Clock::now();
    plink::InitializeWorld(room.world);
    std::printf("MENU binding=%u reason=%s\n", room.bindingId, reason);
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
      const uint8_t mask = OnlineMask(room);
      room.winnerSlot = mask == 0x01U ? 1 : (mask == 0x02U ? 2 : 0);
      room.endReason = plink::MatchEndReason::PlayerDisconnected;
      room.phase = plink::GamePhase::Finished;
      room.phaseStarted = now;
    }
    if (room.phase == plink::GamePhase::Countdown &&
        now - room.phaseStarted >= std::chrono::seconds(3)) {
      room.phase = plink::GamePhase::Playing;
      room.phaseStarted = room.matchStarted = now;
    }
    if (room.phase == plink::GamePhase::Playing) {
      plink::StepWorld(room.world,
                       room.clients[0].active ? room.clients[0].input : 0,
                       room.clients[1].active ? room.clients[1].input : 0,
                       false);
      const bool timeUp = now - room.matchStarted >=
                          std::chrono::seconds(config_.matchSeconds);
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
    if ((serverTick_ % 2U) == 0U) SendSnapshots(room);
  }

  void Tick() {
    ++serverTick_;
    const auto now = Clock::now();
    const auto before = waiters_.size();
    waiters_.erase(
        std::remove_if(waiters_.begin(), waiters_.end(), [now](const Waiter &waiter) {
          return now - waiter.lastSeen >
                 std::chrono::milliseconds(pcpair::kWaitingTimeoutMs);
        }),
        waiters_.end());
    if (before != waiters_.size())
      std::printf("PAIR_EXPIRE count=%zu\n", before - waiters_.size());
    for (Room &room : rooms_) TickRoom(room, now);
  }

  void SendWelcome(Client &client) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Welcome,
                               client.session, ++serverSequence_,
                               client.lastSequence, serverTick_);
    plink::WelcomePayload welcome;
    welcome.assignedSlot = client.slot;
    welcome.tickRate = 30;
    welcome.firePeriodTicks = plink::kFirePeriodTicks;
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
    if (room.phase == plink::GamePhase::Playing ||
        room.phase == plink::GamePhase::Finished) {
      const auto end = room.phase == plink::GamePhase::Finished
                           ? room.phaseStarted
                           : now;
      matchElapsedMs = static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(end - room.matchStarted)
              .count());
    }
    for (Client &client : room.clients) {
      if (!client.active) continue;
      {
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
  uint32_t serverTick_ = 0;
  uint32_t serverSequence_ = 0;
  uint64_t invalidPackets_ = 0;
};

}  // namespace

int main(int argc, char **argv) {
  PetServer server(ParseArguments(argc, argv));
  return server.Run() ? 0 : 1;
}
