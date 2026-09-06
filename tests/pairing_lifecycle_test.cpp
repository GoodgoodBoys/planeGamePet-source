#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../common/pairing_protocol.h"
#include "../shared/plane_protocol.h"

namespace {

using Clock = std::chrono::steady_clock;

bool TestAuthenticatedProtocol() {
  pcpair::AuthKey key;
  pcpair::AuthKey wrong;
  if (!pcpair::ParseAuthKey("00112233445566778899aabbccddeeff", key) ||
      !pcpair::ParseAuthKey("ffeeddccbbaa99887766554433221100", wrong)) {
    return false;
  }
  pcpair::Message source;
  source.type = pcpair::MessageType::Start;
  source.deviceId = 72;
  source.requestId = 91;
  source.pairingCode = 654321;
  source.appVersion = {1, 2, 3};
  source.compatibilityFlags = pcpair::PeerVersionKnown |
                              pcpair::MajorMismatch |
                              pcpair::LocalUpdateRequired;
  uint8_t control[pcpair::kMessageSize]{};
  pcpair::Message parsed;
  if (!pcpair::Serialize(source, control, sizeof(control), key) ||
      !pcpair::Parse(control, sizeof(control), parsed, key) ||
      parsed.appVersion.major != 1 || parsed.appVersion.minor != 2 ||
      parsed.appVersion.patch != 3 ||
      parsed.compatibilityFlags != source.compatibilityFlags ||
      pcpair::Parse(control, sizeof(control), parsed, wrong)) {
    return false;
  }
  control[12] ^= 1U;
  if (pcpair::Parse(control, sizeof(control), parsed, key)) return false;

  uint8_t packet[pcpair::kMaxDatagramSize]{};
  plink::PacketWriter writer(packet, plink::kMaxPacketSize,
                             plink::PacketType::Ping, 8, 9, 0, 1);
  if (!plink::WriteTimestamp(writer, 1234)) return false;
  size_t length = writer.Finish();
  if (!pcpair::AppendPacketAuth(packet, length, sizeof(packet), key))
    return false;
  size_t verified = length;
  if (!pcpair::VerifyAndStripPacketAuth(packet, verified, key) ||
      verified + pcpair::kAuthTagSize != length) {
    return false;
  }
  packet[5] ^= 1U;
  verified = length;
  return !pcpair::VerifyAndStripPacketAuth(packet, verified, key);
}

struct Peer {
  SOCKET socket = INVALID_SOCKET;
  sockaddr_in server{};
  uint32_t deviceId = 0;
  uint32_t requestId = 0;
  uint32_t code = 0;
  uint32_t bindingId = 0;
  uint32_t tokenLow = 0;
  uint32_t tokenHigh = 0;
  uint32_t session = 0;
  pcpair::Status status = pcpair::Status::None;
  pcpair::AppVersion appVersion = pcpair::CurrentAppVersion();
  pcpair::AppVersion peerAppVersion{};
  uint8_t compatibilityFlags = 0;
  unsigned compatibilityMessages = 0;
  bool matched = false;
  bool online = false;
  bool unbound = false;
};

bool Open(Peer &peer, uint16_t port) {
  peer.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (peer.socket == INVALID_SOCKET) return false;
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  local.sin_port = 0;
  if (bind(peer.socket, reinterpret_cast<const sockaddr *>(&local),
           sizeof(local)) != 0) return false;
  u_long enabled = 1;
  if (ioctlsocket(peer.socket, FIONBIO, &enabled) != 0) return false;
  peer.server.sin_family = AF_INET;
  peer.server.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &peer.server.sin_addr);
  return true;
}

void Close(Peer &peer) {
  if (peer.socket != INVALID_SOCKET) closesocket(peer.socket);
  peer.socket = INVALID_SOCKET;
}

void SendControl(Peer &peer, pcpair::Message message) {
  uint8_t bytes[pcpair::kMessageSize]{};
  message.deviceId = peer.deviceId;
  message.appVersion = peer.appVersion;
  if (!pcpair::Serialize(message, bytes, sizeof(bytes))) return;
  sendto(peer.socket, reinterpret_cast<const char *>(bytes), sizeof(bytes), 0,
         reinterpret_cast<const sockaddr *>(&peer.server), sizeof(peer.server));
}

void SendStart(Peer &peer) {
  pcpair::Message message;
  message.type = pcpair::MessageType::Start;
  message.requestId = peer.requestId;
  message.pairingCode = peer.code;
  SendControl(peer, message);
}

void SendCancel(Peer &peer) {
  pcpair::Message message;
  message.type = pcpair::MessageType::Cancel;
  message.requestId = peer.requestId;
  message.pairingCode = peer.code;
  SendControl(peer, message);
}

void SendResume(Peer &peer) {
  pcpair::Message message;
  message.type = pcpair::MessageType::Resume;
  message.bindingId = peer.bindingId;
  message.tokenLow = peer.tokenLow;
  message.tokenHigh = peer.tokenHigh;
  SendControl(peer, message);
}

void SendUnbind(Peer &peer) {
  pcpair::Message message;
  message.type = pcpair::MessageType::Unbind;
  message.bindingId = peer.bindingId;
  message.tokenLow = peer.tokenLow;
  message.tokenHigh = peer.tokenHigh;
  SendControl(peer, message);
}

void Receive(Peer &peer) {
  for (;;) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    sockaddr_in from{};
    int fromLength = sizeof(from);
    const int received = recvfrom(peer.socket, reinterpret_cast<char *>(bytes),
                                  sizeof(bytes), 0,
                                  reinterpret_cast<sockaddr *>(&from),
                                  &fromLength);
    if (received < 0) return;
    pcpair::Message control;
    if (pcpair::Parse(bytes, static_cast<size_t>(received), control)) {
      if (control.type == pcpair::MessageType::Status &&
          control.deviceId == peer.deviceId) {
        peer.status = control.status;
        if (control.status == pcpair::Status::Matched) {
          peer.matched = true;
          peer.bindingId = control.bindingId;
          // Pairing responses carry credentials. Resume-time compatibility
          // messages deliberately do not repeat those secrets.
          if (control.tokenLow != 0 || control.tokenHigh != 0) {
            peer.tokenLow = control.tokenLow;
            peer.tokenHigh = control.tokenHigh;
          }
          peer.peerAppVersion = control.appVersion;
          peer.compatibilityFlags = control.compatibilityFlags;
          ++peer.compatibilityMessages;
        } else if (control.status == pcpair::Status::Unbound) {
          peer.unbound = true;
        }
      }
      continue;
    }
    plink::PacketHeader header;
    const uint8_t *payload = nullptr;
    if (!plink::ParsePacket(bytes, static_cast<size_t>(received), header,
                            payload)) continue;
    if (header.type == plink::PacketType::Welcome) {
      plink::WelcomePayload welcome;
      plink::PayloadReader reader(payload, header.payloadLength);
      if (plink::ReadWelcome(reader, welcome)) peer.session = header.session;
    } else if (header.type == plink::PacketType::Snapshot) {
      plink::SnapshotPayload snapshot;
      plink::PayloadReader reader(payload, header.payloadLength);
      if (plink::ReadSnapshot(reader, snapshot) && snapshot.onlineMask == 0x03U)
        peer.online = true;
    }
  }
}

bool AwaitWaiting(Peer &peer, std::chrono::milliseconds timeout) {
  const auto deadline = Clock::now() + timeout;
  auto nextSend = Clock::now();
  while (Clock::now() < deadline) {
    Receive(peer);
    if (peer.matched) return false;
    if (peer.status == pcpair::Status::Waiting) return true;
    if (Clock::now() >= nextSend) {
      SendStart(peer);
      nextSend = Clock::now() + std::chrono::milliseconds(150);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool AwaitMatch(Peer &first, Peer &second,
                std::chrono::milliseconds timeout) {
  const auto deadline = Clock::now() + timeout;
  auto nextSend = Clock::now();
  while (Clock::now() < deadline) {
    Receive(first);
    Receive(second);
    if (first.matched && second.matched && first.bindingId == second.bindingId)
      return true;
    if (Clock::now() >= nextSend) {
      if (!first.matched) SendStart(first);
      if (!second.matched) SendStart(second);
      nextSend = Clock::now() + std::chrono::milliseconds(150);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool AwaitCancelled(Peer &peer) {
  const auto deadline = Clock::now() + std::chrono::seconds(2);
  auto nextSend = Clock::now();
  while (Clock::now() < deadline) {
    Receive(peer);
    if (peer.status == pcpair::Status::Cancelled) return true;
    if (Clock::now() >= nextSend) {
      SendCancel(peer);
      nextSend = Clock::now() + std::chrono::milliseconds(100);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool AwaitResume(Peer &first, Peer &second) {
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  auto nextSend = Clock::now();
  while (Clock::now() < deadline) {
    Receive(first);
    Receive(second);
    if (first.session != 0 && second.session != 0 && first.online && second.online)
      return true;
    if (Clock::now() >= nextSend) {
      SendResume(first);
      SendResume(second);
      nextSend = Clock::now() + std::chrono::milliseconds(150);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool AwaitUnbound(Peer &first, Peer &second) {
  const auto deadline = Clock::now() + std::chrono::seconds(3);
  auto nextSend = Clock::now();
  while (Clock::now() < deadline) {
    Receive(first);
    Receive(second);
    if (first.unbound && second.unbound) return true;
    if (!first.unbound && Clock::now() >= nextSend) {
      SendUnbind(first);
      nextSend = Clock::now() + std::chrono::milliseconds(150);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

bool WriteCredentials(const std::string &path, const Peer &first,
                      const Peer &second) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) return false;
  output << first.deviceId << ' ' << first.bindingId << ' ' << first.tokenLow
         << ' ' << first.tokenHigh << '\n';
  output << second.deviceId << ' ' << second.bindingId << ' ' << second.tokenLow
         << ' ' << second.tokenHigh << '\n';
  return static_cast<bool>(output);
}

bool ReadCredentials(const std::string &path, Peer &first, Peer &second) {
  std::ifstream input(path);
  return static_cast<bool>(
      input >> first.deviceId >> first.bindingId >> first.tokenLow >>
      first.tokenHigh >> second.deviceId >> second.bindingId >> second.tokenLow >>
      second.tokenHigh);
}

int RunLifecycle(uint16_t port, uint32_t code, const std::string &credentials) {
  Peer a, b, c, d, e, f;
  Peer *peers[] = {&a, &b, &c, &d, &e, &f};
  for (int i = 0; i < 6; ++i) {
    peers[i]->deviceId = 8101U + static_cast<uint32_t>(i);
    peers[i]->requestId = 9101U + static_cast<uint32_t>(i);
    peers[i]->code = code;
    if (!Open(*peers[i], port)) return 10 + i;
  }
  const bool aWaited = AwaitWaiting(a, std::chrono::seconds(2));
  const bool cancelled = aWaited && AwaitCancelled(a);
  const bool bWaited = cancelled && AwaitWaiting(b, std::chrono::seconds(2));
  Close(b);
  std::this_thread::sleep_for(
      std::chrono::milliseconds(pcpair::kWaitingTimeoutMs + 700));
  const bool cWaitedAfterDisconnect =
      bWaited && AwaitWaiting(c, std::chrono::seconds(2));
  const bool cdMatched =
      cWaitedAfterDisconnect && AwaitMatch(c, d, std::chrono::seconds(3));
  const bool codeReused = cdMatched && AwaitWaiting(e, std::chrono::seconds(2));
  const bool efMatched =
      codeReused && AwaitMatch(e, f, std::chrono::seconds(3));
  const bool wrote = cdMatched && WriteCredentials(credentials, c, d);
  for (Peer *peer : peers) Close(*peer);
  std::printf(
      "PAIR_LIFECYCLE cancel=%u disconnect_cleanup=%u first_match=%u "
      "code_reuse=%u second_match=%u persisted=%u\n",
      cancelled, cWaitedAfterDisconnect, cdMatched, codeReused, efMatched,
      wrote);
  const bool success = cancelled && cWaitedAfterDisconnect && cdMatched &&
                       codeReused && efMatched && wrote;
  std::printf(success ? "PC_PET_PAIRING_LIFECYCLE_OK\n"
                      : "PC_PET_PAIRING_LIFECYCLE_FAILED\n");
  return success ? 0 : 20;
}

int RunResume(uint16_t port, const std::string &credentials) {
  Peer first, second;
  if (!ReadCredentials(credentials, first, second)) return 30;
  if (!Open(first, port) || !Open(second, port)) return 31;
  const bool resumed = AwaitResume(first, second);
  Close(first);
  Close(second);
  std::printf("PAIR_PERSISTENCE resumed=%u binding=%u\n", resumed,
              first.bindingId);
  std::printf(resumed ? "PC_PET_PAIRING_RESUME_OK\n"
                      : "PC_PET_PAIRING_RESUME_FAILED\n");
  return resumed ? 0 : 32;
}

int RunUnbind(uint16_t port, uint32_t code,
              const std::string &credentials) {
  Peer first, second;
  if (!ReadCredentials(credentials, first, second)) return 40;
  if (!Open(first, port) || !Open(second, port)) return 41;
  const bool resumed = AwaitResume(first, second);
  const bool unbound = resumed && AwaitUnbound(first, second);
  first.requestId = 10101;
  second.requestId = 10102;
  first.code = second.code = code;
  first.matched = second.matched = false;
  first.status = second.status = pcpair::Status::None;
  const bool rebound = unbound &&
      AwaitMatch(first, second, std::chrono::seconds(3));
  Close(first);
  Close(second);
  std::printf("PAIR_UNBIND resumed=%u unbound=%u rebound=%u\n",
              resumed, unbound, rebound);
  std::printf(resumed && unbound && rebound ? "PC_PET_UNBIND_OK\n"
                                            : "PC_PET_UNBIND_FAILED\n");
  return resumed && unbound && rebound ? 0 : 42;
}

int RunStorageError(uint16_t port, uint32_t code) {
  Peer first, second;
  first.deviceId = 11101;
  second.deviceId = 11102;
  first.requestId = 12101;
  second.requestId = 12102;
  first.code = second.code = code;
  if (!Open(first, port) || !Open(second, port)) return 50;
  const auto deadline = Clock::now() + std::chrono::seconds(3);
  auto nextSend = Clock::now();
  while (Clock::now() < deadline &&
         (first.status != pcpair::Status::StorageError ||
          second.status != pcpair::Status::StorageError)) {
    Receive(first);
    Receive(second);
    if (Clock::now() >= nextSend) {
      SendStart(first);
      SendStart(second);
      nextSend = Clock::now() + std::chrono::milliseconds(150);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  const bool rejected = first.status == pcpair::Status::StorageError &&
                        second.status == pcpair::Status::StorageError &&
                        !first.matched && !second.matched;
  Close(first);
  Close(second);
  std::printf("PAIR_STORAGE_ERROR rejected=%u\n", rejected);
  std::printf(rejected ? "PC_PET_STORAGE_ERROR_OK\n"
                       : "PC_PET_STORAGE_ERROR_FAILED\n");
  return rejected ? 0 : 51;
}

bool HasCompatibility(const Peer &peer, uint8_t expectedFlags,
                      pcpair::AppVersion expectedPeer) {
  return peer.compatibilityMessages > 0 &&
         peer.compatibilityFlags == (expectedFlags | pcpair::ScopedActionsSupported) &&
         peer.peerAppVersion.major == expectedPeer.major &&
         peer.peerAppVersion.minor == expectedPeer.minor &&
         peer.peerAppVersion.patch == expectedPeer.patch;
}

int RunVersionCompatibility(uint16_t port, uint32_t code) {
  Peer older, newer;
  older.deviceId = 13101;
  newer.deviceId = 13102;
  older.requestId = 14101;
  newer.requestId = 14102;
  older.code = newer.code = code;
  older.appVersion = {1, 0, 0};
  newer.appVersion = {2, 1, 3};
  if (!Open(older, port) || !Open(newer, port)) return 70;

  const bool matched = AwaitMatch(older, newer, std::chrono::seconds(3));
  const uint8_t olderFlags = pcpair::PeerVersionKnown |
                             pcpair::MajorMismatch |
                             pcpair::LocalUpdateRequired;
  const uint8_t newerFlags = pcpair::PeerVersionKnown |
                             pcpair::MajorMismatch |
                             pcpair::PeerUpdateRequired;
  const bool pairingFlags = matched &&
      HasCompatibility(older, olderFlags, newer.appVersion) &&
      HasCompatibility(newer, newerFlags, older.appVersion);

  older.compatibilityMessages = newer.compatibilityMessages = 0;
  older.compatibilityFlags = newer.compatibilityFlags = 0;
  older.peerAppVersion = newer.peerAppVersion = {};
  const bool resumed = pairingFlags && AwaitResume(older, newer);
  const bool resumeFlags = resumed &&
      HasCompatibility(older, olderFlags, newer.appVersion) &&
      HasCompatibility(newer, newerFlags, older.appVersion);
  const bool unbound = resumeFlags && AwaitUnbound(older, newer);
  Close(older);
  Close(newer);
  std::printf(
      "VERSION_COMPATIBILITY matched=%u pairing_flags=%u resumed=%u "
      "resume_flags=%u unbound=%u\n",
      matched, pairingFlags, resumed, resumeFlags, unbound);
  const bool success = matched && pairingFlags && resumed && resumeFlags &&
                       unbound;
  std::printf(success ? "PC_PET_VERSION_COMPATIBILITY_OK\n"
                      : "PC_PET_VERSION_COMPATIBILITY_FAILED\n");
  return success ? 0 : 71;
}

}  // namespace

int main(int argc, char **argv) {
  if (!TestAuthenticatedProtocol()) {
    std::printf("PC_PET_AUTH_PROTOCOL_FAILED\n");
    return 60;
  }
  std::printf("PC_PET_AUTH_PROTOCOL_OK\n");
  const uint16_t port = static_cast<uint16_t>(
      argc > 1 ? std::clamp(atoi(argv[1]), 1, 65535) : 32110);
  const std::string mode = argc > 2 ? argv[2] : "lifecycle";
  const std::string credentials = argc > 3 ? argv[3] : "pairing_test.creds";
  const uint32_t code = static_cast<uint32_t>(
      argc > 4 ? strtoul(argv[4], nullptr, 10) : 123456);
  WSADATA winsock{};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 1;
  int result = 0;
  if (mode == "resume") result = RunResume(port, credentials);
  else if (mode == "unbind") result = RunUnbind(port, code, credentials);
  else if (mode == "storage_error") result = RunStorageError(port, code);
  else if (mode == "version_compatibility")
    result = RunVersionCompatibility(port, code);
  else result = RunLifecycle(port, code, credentials);
  WSACleanup();
  return result;
}
