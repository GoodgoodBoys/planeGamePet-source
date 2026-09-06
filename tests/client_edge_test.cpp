// Compile the real client into an isolated, headless regression translation unit.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <objidl.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <bcrypt.h>
#include "../common/pairing_protocol.h"
#include "../common/app_version.h"
#include "../desktop/update_manager.h"
#include "../desktop/game_layout.h"
#include "../shared/plane_protocol.h"
#include "../shared/plane_sim.h"
#define private public
#define WinMain RegressionOriginalWinMain
#include "../desktop/main.cpp"
#undef WinMain
#undef private

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  const auto directory = std::filesystem::u8path(argv[1]);
  WSADATA winsock{};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 3;
  PetClient client;
  client.clientId_ = 121;
  client.statePath_ = directory / L"review.binding";
  client.settingsPath_ = directory / L"review.settings";
  client.telemetryEnabled_ = false;
  client.awaitingPairing_ = client.pairingAttempted_ = false;
  client.haveSnapshot_ = true;
  client.snapshot_.phase = plink::GamePhase::Waiting;
  client.snapshot_.inviterSlot = 1;
  client.slot_ = 2;
  client.session_ = 100;
  client.gameMode_ = false;
  client.pendingAction_ = 0;

  // Lower part of Accept lies inside a (hidden) quick-emote rectangle.
  client.OnLeftButtonDown(90, 118);
  if (client.pendingAction_ != 2) return 4;
  client.OnLeftButtonDown(100, 105);
  if (client.pendingAction_ != 2) return 5;
  std::puts("INVITE_BUTTON_LOWER_EDGE_OK");

  // Receive an authoritative Menu snapshot when Cancel wins against Accept.
  SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  client.socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in loopback{};
  loopback.sin_family = AF_INET;
  loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(sender, reinterpret_cast<sockaddr *>(&loopback), sizeof(loopback)) ||
      bind(client.socket_, reinterpret_cast<sockaddr *>(&loopback), sizeof(loopback))) return 6;
  int length = sizeof(loopback);
  getsockname(sender, reinterpret_cast<sockaddr *>(&client.server_), &length);
  sockaddr_in endpoint{};
  getsockname(client.socket_, reinterpret_cast<sockaddr *>(&endpoint), &length);
  u_long nonblocking = 1;
  ioctlsocket(client.socket_, FIONBIO, &nonblocking);
  const auto deliver = [&](plink::GamePhase phase, unsigned sequence) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Snapshot,
                              100, sequence, 0, sequence);
    plink::SnapshotPayload snapshot;
    snapshot.phase = phase;
    snapshot.inviterSlot = phase == plink::GamePhase::Waiting ? 1 : 0;
    snapshot.onlineMask = 3;
    plink::WriteSnapshot(writer, snapshot);
    const auto size = writer.Finish();
    sendto(sender, reinterpret_cast<const char *>(bytes), static_cast<int>(size),
           0, reinterpret_cast<sockaddr *>(&endpoint), sizeof(endpoint));
    Sleep(10);
    client.ReceiveAll();
  };
  deliver(plink::GamePhase::Menu, 1);
  if (client.pendingAction_ != 0) return 7;
  deliver(plink::GamePhase::Waiting, 2);
  if (client.pendingAction_ != 0) return 8;
  std::puts("STALE_ACCEPT_CLEARED_OK");

  client.bindingId_ = 222;
  pcpair::Message stale;
  stale.type = pcpair::MessageType::Status;
  stale.deviceId = client.clientId_;
  stale.status = pcpair::Status::Unbound;
  stale.requestId = 999;
  stale.bindingId = 0;
  client.HandleControl(stale);
  if (client.bindingId_ != 222) return 9;
  std::puts("STALE_UNBOUND_IGNORED_OK");
  client.resumeRequestId_ = 500;
  client.ClearBinding();
  client.bindingId_ = 333;
  stale.status = pcpair::Status::BindingMissing;
  stale.requestId = 500;
  client.HandleControl(stale);
  if (client.bindingId_ != 333) return 20;
  std::puts("PREVIOUS_BINDING_RESUME_NONCE_REJECTED_OK");

  client.telemetryEnabled_ = true;
  client.telemetryUploadChoice_ = 1;
  if (!client.SaveSettings()) return 10;
  HANDLE locked = CreateFileW(client.settingsPath_.c_str(), GENERIC_READ,
                             FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (locked == INVALID_HANDLE_VALUE) return 11;
  client.telemetryEnabled_ = false;
  client.telemetryUploadChoice_ = 0;
  const bool saved = client.PersistTelemetryPreference(false);
  CloseHandle(locked);
  if (!saved) return 12;
  client.LoadSettings();
  if (client.telemetryEnabled_ || client.telemetryUploadChoice_ != 0) return 13;
  std::puts("LOCKED_SETTINGS_OPT_OUT_PERSISTED_OK");
  client.historyPath_ = directory / L"review.history";
  client.eventsPath_ = directory / L"review.events.csv";
  client.historyTotal_ = 7;
  // A directory cannot be replaced by an atomic history file write.
  std::filesystem::create_directory(client.historyPath_);
  if (client.ClearLocalDataFiles() || client.historyTotal_ != 7) return 14;
  std::puts("CLEAR_FAILURE_PRESERVES_HISTORY_OK");
  client.updateManager_.Configure(nullptr, "invalid", directory / L"update.request", true, 0);
  client.snapshot_.phase = plink::GamePhase::Menu;
  { ModalScope modal; if (client.IsUpdateUiAvailable()) return 15; }
  if (!client.IsUpdateUiAvailable()) return 16;
  std::puts("MODAL_DEFERS_UPDATES_OK");
  client.bindingId_ = 0;
  client.BeginPairing(347891);
  if (!std::filesystem::is_regular_file(client.statePath_.wstring() + L".pairing")) return 17;
  const auto request = client.pairingRequestId_;
  client.StopPairing();
  if (!client.pairingCancelPending_ || client.pairingRequestId_ != request) return 18;
  pcpair::Message cancelled;
  cancelled.type = pcpair::MessageType::Status;
  cancelled.deviceId = client.clientId_;
  cancelled.requestId = request;
  cancelled.status = pcpair::Status::Cancelled;
  client.HandleControl(cancelled);
  if (client.pairingCancelPending_ ||
      std::filesystem::exists(client.statePath_.wstring() + L".pairing")) return 19;
  std::puts("PAIR_CANCELLATION_JOURNAL_ACK_OK");
  closesocket(sender);
  closesocket(client.socket_);
  client.socket_ = INVALID_SOCKET;
  WSACleanup();
  return 0;
}
