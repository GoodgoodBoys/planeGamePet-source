#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../common/pairing_protocol.h"
#include "../shared/plane_protocol.h"
#include "../shared/plane_sim.h"

namespace {

using Clock = std::chrono::steady_clock;

struct TestClient {
  SOCKET socket = INVALID_SOCKET;
  sockaddr_in server{};
  uint32_t clientId = 0;
  uint32_t pairingCode = 123456;
  uint32_t pairingRequestId = 0;
  uint32_t bindingId = 0;
  uint32_t tokenLow = 0;
  uint32_t tokenHigh = 0;
  uint32_t session = 0;
  uint32_t sequence = 0;
  uint32_t lastServerSequence = 0;
  uint32_t lastActionSequence = 0;
  uint32_t lastServerTick = 0;
  uint8_t slot = 0;
  uint8_t assignedSlot = 0;
  uint8_t inputHistory[3]{};
  plink::SnapshotPayload snapshot{};
  bool haveSnapshot = false;
  bool unbound = false;
  uint64_t validPackets = 0;
  uint64_t invalidPackets = 0;
  uint64_t socketErrors = 0;
  uint64_t parseErrors = 0;
  uint64_t snapshotErrors = 0;
  uint32_t emoteCount = 0;
  plink::PlayerAction lastEmote = plink::PlayerAction::Invite;
  int lastSocketError = 0;
  uint32_t controlErrors = 0;
  uint8_t lastControlStatus = 0;
};

uint32_t NowMs(const Clock::time_point &start) {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start)
          .count());
}

bool SendRaw(TestClient &client, const uint8_t *bytes, size_t length) {
  return sendto(client.socket, reinterpret_cast<const char *>(bytes),
                static_cast<int>(length), 0,
                reinterpret_cast<const sockaddr *>(&client.server),
                sizeof(client.server)) == static_cast<int>(length);
}

void SendPairStart(TestClient &client) {
  uint8_t bytes[pcpair::kMessageSize]{};
  pcpair::Message message;
  message.type = pcpair::MessageType::Start;
  message.deviceId = client.clientId;
  message.requestId = client.pairingRequestId;
  message.pairingCode = client.pairingCode;
  message.appVersion = pcpair::CurrentAppVersion();
  if (pcpair::Serialize(message, bytes, sizeof(bytes)))
    SendRaw(client, bytes, sizeof(bytes));
}

void SendResume(TestClient &client) {
  uint8_t bytes[pcpair::kMessageSize]{};
  pcpair::Message message;
  message.type = pcpair::MessageType::Resume;
  message.deviceId = client.clientId;
  message.bindingId = client.bindingId;
  message.tokenLow = client.tokenLow;
  message.tokenHigh = client.tokenHigh;
  message.appVersion = pcpair::CurrentAppVersion();
  if (pcpair::Serialize(message, bytes, sizeof(bytes)))
    SendRaw(client, bytes, sizeof(bytes));
}

void SendUnbind(TestClient &client) {
  uint8_t bytes[pcpair::kMessageSize]{};
  pcpair::Message message;
  message.type = pcpair::MessageType::Unbind;
  message.deviceId = client.clientId;
  message.bindingId = client.bindingId;
  message.tokenLow = client.tokenLow;
  message.tokenHigh = client.tokenHigh;
  if (pcpair::Serialize(message, bytes, sizeof(bytes)))
    SendRaw(client, bytes, sizeof(bytes));
}

void SendInput(TestClient &client, uint8_t input, uint32_t nowMs) {
  client.inputHistory[2] = client.inputHistory[1];
  client.inputHistory[1] = client.inputHistory[0];
  client.inputHistory[0] = input;
  uint8_t bytes[plink::kMaxPacketSize]{};
  plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Input,
                             client.session, ++client.sequence,
                             client.lastServerSequence, client.lastServerTick);
  plink::InputPayload payload;
  payload.clientTimeMs = nowMs;
  payload.current = client.inputHistory[0];
  payload.previous1 = client.inputHistory[1];
  payload.previous2 = client.inputHistory[2];
  if (plink::WriteInput(writer, payload)) SendRaw(client, bytes, writer.Finish());
}

void SendPing(TestClient &client, uint32_t nowMs) {
  uint8_t bytes[plink::kMaxPacketSize]{};
  plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Ping,
                             client.session, ++client.sequence,
                             client.lastServerSequence, client.lastServerTick);
  if (plink::WriteTimestamp(writer, nowMs)) SendRaw(client, bytes, writer.Finish());
}

void SendAction(TestClient &client, plink::PlayerAction action) {
  uint8_t bytes[plink::kMaxPacketSize]{};
  plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Action,
                             client.session, ++client.sequence,
                             client.lastServerSequence, client.lastServerTick);
  plink::ActionPayload payload;
  payload.action = action;
  if (plink::WriteAction(writer, payload)) SendRaw(client, bytes, writer.Finish());
}

void SendActionWithSequence(TestClient &client, plink::PlayerAction action,
                            uint32_t sequence) {
  uint8_t bytes[plink::kMaxPacketSize]{};
  plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Action,
                             client.session, sequence,
                             client.lastServerSequence, client.lastServerTick);
  plink::ActionPayload payload;
  payload.action = action;
  if (plink::WriteAction(writer, payload)) SendRaw(client, bytes, writer.Finish());
}

void ReceiveAll(TestClient &client) {
  for (;;) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    sockaddr_in from{};
    int fromLength = sizeof(from);
    const int received = recvfrom(
        client.socket, reinterpret_cast<char *>(bytes), sizeof(bytes), 0,
        reinterpret_cast<sockaddr *>(&from), &fromLength);
    if (received < 0) {
      const int error = WSAGetLastError();
      if (error != WSAEWOULDBLOCK) {
        ++client.invalidPackets;
        ++client.socketErrors;
        client.lastSocketError = error;
      }
      return;
    }
    pcpair::Message control;
    if (pcpair::Parse(bytes, static_cast<size_t>(received), control)) {
      ++client.validPackets;
      if (control.type == pcpair::MessageType::Status &&
          control.deviceId == client.clientId) {
        if (control.status == pcpair::Status::Matched) {
          // Compatibility-only Matched notices intentionally omit credentials.
          // Keep the original matching credentials, as the desktop client does.
          if (control.bindingId != 0 && control.tokenLow != 0 &&
              control.tokenHigh != 0) {
            client.bindingId = control.bindingId;
            client.tokenLow = control.tokenLow;
            client.tokenHigh = control.tokenHigh;
          }
          client.assignedSlot = control.assignedSlot;
        } else if (control.status == pcpair::Status::Unbound) {
          client.unbound = true;
        } else if (control.status == pcpair::Status::BindingMissing ||
                   control.status == pcpair::Status::StorageError ||
                   control.status == pcpair::Status::Invalid) {
          ++client.controlErrors;
          client.lastControlStatus = static_cast<uint8_t>(control.status);
        }
      }
      continue;
    }
    plink::PacketHeader header;
    const uint8_t *payload = nullptr;
    if (!plink::ParsePacket(bytes, static_cast<size_t>(received), header,
                            payload)) {
      ++client.invalidPackets;
      ++client.parseErrors;
      continue;
    }
    ++client.validPackets;
    client.lastServerSequence = header.sequence;
    client.lastServerTick = header.tick;
    if (header.type == plink::PacketType::Welcome) {
      plink::WelcomePayload welcome;
      plink::PayloadReader reader(payload, header.payloadLength);
      if (plink::ReadWelcome(reader, welcome)) {
        client.session = header.session;
        client.assignedSlot = welcome.assignedSlot;
      }
    } else if (header.type == plink::PacketType::Snapshot) {
      plink::SnapshotPayload snapshot;
      plink::PayloadReader reader(payload, header.payloadLength);
      if (plink::ReadSnapshot(reader, snapshot)) {
        client.snapshot = snapshot;
        client.haveSnapshot = true;
      } else {
        ++client.invalidPackets;
        ++client.snapshotErrors;
      }
    } else if (header.type == plink::PacketType::Action) {
      if (header.sequence <= client.lastActionSequence) continue;
      plink::ActionPayload action;
      plink::PayloadReader reader(payload, header.payloadLength);
      if (plink::ReadAction(reader, action) &&
          plink::IsQuickEmote(action.action)) {
        client.lastEmote = action.action;
        client.lastActionSequence = header.sequence;
        ++client.emoteCount;
      } else {
        ++client.invalidPackets;
      }
    }
  }
}

bool Open(TestClient &client, uint16_t port) {
  client.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (client.socket == INVALID_SOCKET) return false;
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  local.sin_port = 0;
  if (bind(client.socket, reinterpret_cast<const sockaddr *>(&local),
           sizeof(local)) != 0) return false;
  u_long enabled = 1;
  if (ioctlsocket(client.socket, FIONBIO, &enabled) != 0) return false;
  client.server.sin_family = AF_INET;
  client.server.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &client.server.sin_addr);
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  const uint16_t port = static_cast<uint16_t>(
      argc > 1 ? std::clamp(atoi(argv[1]), 1, 65535) : 32110);
  const uint32_t code = static_cast<uint32_t>(
      argc > 2 ? strtoul(argv[2], nullptr, 10) : 123456);
  const uint16_t bobPort = static_cast<uint16_t>(
      argc > 3 ? std::clamp(atoi(argv[3]), 1, 65535) : port);
  const bool publicMode = argc > 4 && std::string(argv[4]) == "public";
  const uint32_t deviceBase = static_cast<uint32_t>(
      argc > 5 ? strtoul(argv[5], nullptr, 10) : 6100U);
  WSADATA winsock{};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 1;

  TestClient alice;
  alice.clientId = deviceBase + 1U;
  alice.pairingRequestId = deviceBase + 1001U;
  alice.pairingCode = code;
  alice.slot = 1;
  TestClient bob;
  bob.clientId = deviceBase + 2U;
  bob.pairingRequestId = deviceBase + 1002U;
  bob.pairingCode = code;
  bob.slot = 2;
  if (!Open(alice, port) || !Open(bob, bobPort)) return 2;

  const auto start = Clock::now();
  auto nextJoin = start;
  auto nextInput = start;
  auto nextPing = start;
  auto nextAction = start;
  enum class Stage {
    Joining,
    WaitForEmote,
    Invite,
    CancelInvite,
    WaitForMenu,
    InviteAgain,
    Accept,
    WaitForPlaying,
    WaitForFinished,
    ReturnToMenu,
    WaitForUnbound,
    Complete,
  };
  Stage stage = Stage::Joining;
  bool sawBothOnline = false;
  bool sawEmote = false;
  bool sawWaiting = false;
  bool sawFiveMinuteInvite = false;
  bool sawCancelledMenu = false;
  bool sawCountdown = false;
  bool sawPlaying = false;
  bool sawFinished = false;
  bool sawReturnedMenu = false;
  bool sawTimeLimitDraw = false;
  bool sawUnbound = false;
  bool replaySent = false;
  bool sawReplayRejected = false;
  plink::WorldState damageRuleWorld{};
  plink::InitializeWorld(damageRuleWorld);
  damageRuleWorld.players[0].x = 73;
  damageRuleWorld.players[0].y = 251;
  plink::DamagePlayer(damageRuleWorld, 0, false);
  const bool survivingHitKeptPosition =
      damageRuleWorld.players[0].health == plink::kInitialHealth - 1 &&
      damageRuleWorld.players[0].x == 73 &&
      damageRuleWorld.players[0].y == 251 &&
      damageRuleWorld.players[0].flags == 1;

  while (Clock::now() - start < std::chrono::seconds(30) &&
         stage != Stage::Complete) {
    const auto now = Clock::now();
    const uint32_t nowMs = NowMs(start);
    ReceiveAll(alice);
    ReceiveAll(bob);
    if ((!alice.session || !bob.session) && now >= nextJoin) {
      if (!alice.session) {
        if (alice.bindingId == 0) SendPairStart(alice);
        else SendResume(alice);
      }
      if (!bob.session) {
        if (bob.bindingId == 0) SendPairStart(bob);
        else SendResume(bob);
      }
      nextJoin = now + std::chrono::milliseconds(200);
    }
    if (alice.session && bob.session && now >= nextInput) {
      const uint8_t aliceInput = stage == Stage::WaitForFinished
                                     ? plink::InputLeft
                                     : 0;
      const uint8_t bobInput = stage == Stage::WaitForFinished
                                   ? plink::InputLeft
                                   : 0;
      SendInput(alice, aliceInput, nowMs);
      SendInput(bob, bobInput, nowMs);
      nextInput = now + std::chrono::milliseconds(50);
    }
    if (alice.session && bob.session && now >= nextPing) {
      SendPing(alice, nowMs);
      SendPing(bob, nowMs);
      nextPing = now + std::chrono::milliseconds(500);
    }

    const bool haveBoth = alice.haveSnapshot && bob.haveSnapshot;
    if (haveBoth && alice.snapshot.onlineMask == 0x03U &&
        bob.snapshot.onlineMask == 0x03U) {
      sawBothOnline = true;
    }
    if (stage == Stage::Joining && sawBothOnline && now >= nextAction) {
      SendAction(alice, plink::PlayerAction::EmoteLaugh);
      nextAction = now + std::chrono::milliseconds(150);
      stage = Stage::WaitForEmote;
    } else if (stage == Stage::WaitForEmote &&
               bob.emoteCount == 1 &&
               bob.lastEmote == plink::PlayerAction::EmoteLaugh) {
      sawEmote = true;
      stage = Stage::Invite;
      nextAction = now;
    } else if (stage == Stage::Invite && now >= nextAction) {
      SendAction(alice, plink::PlayerAction::Invite);
      nextAction = now + std::chrono::milliseconds(150);
      if (bob.snapshot.phase == plink::GamePhase::Waiting &&
          bob.snapshot.inviterSlot == 1) {
        sawWaiting = true;
        sawFiveMinuteInvite = bob.snapshot.phaseRemainingMs >= 298000U &&
                              bob.snapshot.phaseRemainingMs <= 300000U;
        stage = Stage::CancelInvite;
      }
    } else if (stage == Stage::CancelInvite && now >= nextAction) {
      if (!replaySent) {
        SendActionWithSequence(alice, plink::PlayerAction::ReturnToMenu, 1);
        replaySent = true;
        nextAction = now + std::chrono::milliseconds(300);
      } else {
        sawReplayRejected = haveBoth &&
            alice.snapshot.phase == plink::GamePhase::Waiting &&
            bob.snapshot.phase == plink::GamePhase::Waiting;
        SendAction(alice, plink::PlayerAction::ReturnToMenu);
        nextAction = now + std::chrono::milliseconds(150);
        stage = Stage::WaitForMenu;
      }
    } else if (stage == Stage::WaitForMenu && haveBoth &&
               alice.snapshot.phase == plink::GamePhase::Menu &&
               bob.snapshot.phase == plink::GamePhase::Menu) {
      sawCancelledMenu = true;
      stage = Stage::InviteAgain;
      nextAction = now;
    } else if (stage == Stage::InviteAgain && now >= nextAction) {
      SendAction(bob, plink::PlayerAction::Invite);
      nextAction = now + std::chrono::milliseconds(150);
      if (alice.snapshot.phase == plink::GamePhase::Waiting &&
          alice.snapshot.inviterSlot == 2) {
        stage = Stage::Accept;
      }
    } else if (stage == Stage::Accept && now >= nextAction) {
      SendAction(alice, plink::PlayerAction::Accept);
      nextAction = now + std::chrono::milliseconds(150);
      if (bob.snapshot.phase == plink::GamePhase::Countdown) {
        sawCountdown = true;
        stage = Stage::WaitForPlaying;
      }
    } else if (stage == Stage::WaitForPlaying && haveBoth &&
               alice.snapshot.phase == plink::GamePhase::Playing &&
               bob.snapshot.phase == plink::GamePhase::Playing) {
      sawPlaying = true;
      if (publicMode) {
        SendUnbind(alice);
        nextAction = now + std::chrono::milliseconds(250);
        stage = Stage::WaitForUnbound;
      } else {
        stage = Stage::WaitForFinished;
      }
    } else if (stage == Stage::WaitForFinished && haveBoth &&
               alice.snapshot.phase == plink::GamePhase::Finished &&
               bob.snapshot.phase == plink::GamePhase::Finished) {
      sawFinished = true;
      sawTimeLimitDraw =
          alice.snapshot.winnerSlot == 0 && bob.snapshot.winnerSlot == 0 &&
          alice.snapshot.endReason == plink::MatchEndReason::TimeLimitDraw &&
          bob.snapshot.endReason == plink::MatchEndReason::TimeLimitDraw;
      stage = Stage::ReturnToMenu;
      nextAction = now;
    } else if (stage == Stage::ReturnToMenu && now >= nextAction) {
      SendAction(alice, plink::PlayerAction::ReturnToMenu);
      nextAction = now + std::chrono::milliseconds(150);
      if (haveBoth && alice.snapshot.phase == plink::GamePhase::Menu &&
          bob.snapshot.phase == plink::GamePhase::Menu) {
        sawReturnedMenu = true;
        stage = Stage::Complete;
      }
    } else if (stage == Stage::WaitForUnbound) {
      if (alice.unbound && bob.unbound) {
        sawUnbound = true;
        stage = Stage::Complete;
      } else if (now >= nextAction) {
        // Mirror the real client's retries across the local UDP/WSS bridge.
        if (!alice.unbound) SendUnbind(alice);
        if (!bob.unbound) SendUnbind(bob);
        nextAction = now + std::chrono::milliseconds(250);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  closesocket(alice.socket);
  closesocket(bob.socket);
  WSACleanup();
  std::printf(
      "PC_PET_TEST online=%u waiting=%u cancel=%u countdown=%u playing=%u "
      "finished=%u returned=%u hit_keeps_position=%u invalid=%llu:%llu "
      "packets=%llu:%llu timeout_draw=%u replay_rejected=%u "
      "invite_300s=%u emote=%u unbound=%u public=%u\n",
      sawBothOnline, sawWaiting, sawCancelledMenu, sawCountdown, sawPlaying,
      sawFinished, sawReturnedMenu, survivingHitKeptPosition,
      static_cast<unsigned long long>(alice.invalidPackets),
      static_cast<unsigned long long>(bob.invalidPackets),
      static_cast<unsigned long long>(alice.validPackets),
      static_cast<unsigned long long>(bob.validPackets),
      sawTimeLimitDraw, sawReplayRejected, sawFiveMinuteInvite, sawEmote,
      sawUnbound, publicMode);
  std::printf(
      "PC_PET_ERRORS socket=%llu:%llu last=%d:%d parse=%llu:%llu "
      "snapshot=%llu:%llu\n",
      static_cast<unsigned long long>(alice.socketErrors),
      static_cast<unsigned long long>(bob.socketErrors), alice.lastSocketError,
      bob.lastSocketError,
      static_cast<unsigned long long>(alice.parseErrors),
      static_cast<unsigned long long>(bob.parseErrors),
      static_cast<unsigned long long>(alice.snapshotErrors),
      static_cast<unsigned long long>(bob.snapshotErrors));
  std::printf("PC_PET_CONTROL_ERRORS count=%u:%u status=%u:%u\n",
              alice.controlErrors, bob.controlErrors,
              alice.lastControlStatus, bob.lastControlStatus);
  const bool commonSuccess = sawBothOnline && sawWaiting &&
      sawFiveMinuteInvite && sawCancelledMenu && sawCountdown && sawPlaying &&
      sawReplayRejected && sawEmote && survivingHitKeptPosition &&
      alice.invalidPackets == 0 && bob.invalidPackets == 0;
  const bool success = commonSuccess &&
      (publicMode ? sawUnbound
                  : sawFinished && sawReturnedMenu && sawTimeLimitDraw);
  std::printf(success ? "PC_PET_INTEGRATION_OK\n"
                      : "PC_PET_INTEGRATION_FAILED\n");
  return success ? 0 : 3;
}
