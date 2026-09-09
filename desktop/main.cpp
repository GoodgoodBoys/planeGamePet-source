#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <objidl.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
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
#include <string>
#include <utility>
#include <vector>

#include <bcrypt.h>

#include "../common/pairing_protocol.h"
#include "../common/app_version.h"
#include "../common/windows_arguments.h"
#include "../common/windows_activation.h"
#include "../common/windows_dpi.h"
#include "../common/network_status.h"
#include "update_manager.h"
#ifdef PLANE_PET_UPDATE_WINDOW_SELF_TEST
#include "../tests/update_test_access.h"
#endif
#include "game_layout.h"
#include "game_effects.h"
#include "combat_feedback.h"
#include "result_title.h"
#include "pet_layout.h"
#include "pet_toolbar.h"
#include "pet_idle.h"
#include "pet_text.h"
#include "history_layout.h"
#include "heart_assets.h"
#include "help_content.h"
#include "about_window.h"
#include "dnd_badge.h"
#include "../common/dnd_protocol.h"
#include "../common/pc_motion_protocol.h"
#include "../common/pc_motion_view.h"
#include "../common/pc_battle_protocol.h"
#include "../common/pc_battle_view.h"
#include "../shared/plane_protocol.h"
#include "../shared/plane_sim.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kNetworkPumpTimer = 71;
constexpr UINT kEmergencyHideMessage = WM_APP + 2;
constexpr UINT kMenuShow = 2001;
constexpr UINT kMenuHide = 2002;
constexpr UINT kMenuExit = 2003;
constexpr UINT kMenuInvite = 2004;
constexpr UINT kMenuStopPairing = 2005;
constexpr UINT kMenuHistory = 2006;
constexpr UINT kMenuHelp = 2007;
constexpr UINT kMenuUnbind = 2008;
constexpr UINT kMenuDoNotDisturb = 2009;
constexpr UINT kMenuClearLocalData = 2010;
constexpr UINT kMenuExportData = 2011;
constexpr UINT kMenuTelemetry = 2012;
constexpr UINT kMenuPairingPanel = 2013;
constexpr UINT kMenuCheckUpdate = 2014;
constexpr UINT kMenuDiagnostics = 2015;
constexpr UINT kMenuExportDiagnostics = 2016;
constexpr UINT kMenuAbout = 2017;
constexpr int kBossHotkeyId = 1;
constexpr COLORREF kTransparent = RGB(1, 2, 3);
constexpr COLORREF kBlue = RGB(0, 180, 255);
constexpr COLORREF kRed = RGB(255, 70, 90);
constexpr COLORREF kGreen = RGB(45, 220, 125);
constexpr COLORREF kYellow = RGB(255, 215, 45);
constexpr COLORREF kWhite = RGB(245, 248, 255);
constexpr COLORREF kMuted = RGB(186, 197, 218);
constexpr COLORREF kCard = RGB(19, 24, 36);
constexpr COLORREF kCardEdge = RGB(64, 82, 112);
constexpr COLORREF kBlueLight = RGB(120, 232, 255);
constexpr int kPetWidth = 280;
constexpr int kPetHeight = 150;
using plane_pet_ui::GameLayout;
using plane_pet_ui::MakeGameLayout;
constexpr int kGameHudHeight = plane_pet_ui::kGameHudHeight;
constexpr int kGamePlayHeight = plane_pet_ui::kGamePlayHeight;
constexpr int kGameClientWidth = plane_pet_ui::kGameClientWidth;
constexpr int kGameClientHeight = plane_pet_ui::kGameClientHeight;
constexpr uint32_t kInviteFeedbackSeconds = 5;
constexpr uint32_t kEmoteDisplayMilliseconds = 3500;
constexpr wchar_t kWindowClassName[] = L"PlanePetLocalTestWindow";
constexpr wchar_t kInfoWindowClassName[] = L"PlanePetInfoWindow";
constexpr wchar_t kUpdateWindowClassName[] = L"PlanePetUpdateCardWindow";
constexpr const char *kAppVersion = plane_pet_version::kString;
constexpr char kTelemetryMagic[] = "PPTELEM1\n";
constexpr int kUpdateInstallExitCode = 73;

void ShowUpdateWindow(HWND owner, bool activate);
void CloseUpdateWindow();
bool RepositionUpdateWindow(HWND owner);
bool RefreshUpdateWindowContent(uint64_t generation, uint32_t progress);
void RefreshHistoryInfoWindow();
int gProcessExitCode = 0;
unsigned gModalDepth = 0;
bool gBossHotkeyAvailable = true;
UINT gTaskbarCreatedMessage = 0;
HANDLE gReadyEvent = nullptr;
ULONGLONG gReadyAt = 0;
struct ModalScope {
  ModalScope() { ++gModalDepth; }
  ~ModalScope() { --gModalDepth; }
};
int PetMessageBoxW(HWND owner, LPCWSTR text, LPCWSTR title, UINT type) {
  ModalScope scope;
  return ::MessageBoxW(owner, text, title, type);
}
BOOL PetTrackPopupMenu(HMENU menu, UINT flags, int x, int y, int reserved,
                    HWND owner, const RECT *rect) {
  ModalScope scope;
  return ::TrackPopupMenu(menu, flags, x, y, reserved, owner, rect);
}
BOOL PetGetSaveFileNameW(LPOPENFILENAMEW dialog) {
  ModalScope scope;
  return ::GetSaveFileNameW(dialog);
}

class GdiPlusSession {
 public:
  GdiPlusSession() {
    Gdiplus::GdiplusStartupInput input;
    ready_ = Gdiplus::GdiplusStartup(&token_, &input, nullptr) ==
             Gdiplus::Ok;
  }
  ~GdiPlusSession() {
    if (ready_) Gdiplus::GdiplusShutdown(token_);
  }
  bool ready() const { return ready_; }

  GdiPlusSession(const GdiPlusSession &) = delete;
  GdiPlusSession &operator=(const GdiPlusSession &) = delete;

 private:
  ULONG_PTR token_ = 0;
  bool ready_ = false;
};

GdiPlusSession gGdiPlusSession;

struct PetAnimationPose {
  double x = 0.0;
  double y = 0.0;
  double angle = 0.0;
  double directionX = 0.0;
  double directionY = -1.0;
};

struct PetFormation {
  PetAnimationPose blue{};
  PetAnimationPose red{};
};

struct HistoryEntry {
  uint64_t timestampMs = 0;
  uint8_t ownHealth = 0;
  uint8_t peerHealth = 0;
  int8_t outcome = 0;
};

std::string Option(const char *name, const char *fallback) {
  const std::string key(name);
  const std::wstring wideKey(key.begin(), key.end());
  // Internal narrow strings are UTF-8, including paths later passed to Wide.
  const std::wstring missing = L"\x1";
  const auto value = plane_pet_windows::Option(wideKey.c_str(), missing.c_str());
  return value == missing ? fallback : plane_pet_windows::Utf8(value);
}

std::wstring Wide(const std::string &text) {
  if (text.empty()) return L"";
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1,
                                         nullptr, 0);
  std::wstring result(static_cast<size_t>(std::max(1, length)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), length);
  if (!result.empty() && result.back() == L'\0') result.pop_back();
  return result;
}

uint32_t MillisSince(const Clock::time_point &start) {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start)
          .count());
}

uint32_t SecureRandomU32() {
  uint32_t value = 0;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    value = static_cast<uint32_t>(GetTickCount64()) ^ GetCurrentProcessId();
  }
  return value == 0 ? 1U : value;
}

uint64_t SecureRandomU64() {
  uint64_t value = 0;
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
    value = (static_cast<uint64_t>(GetTickCount64()) << 32U) ^
            SecureRandomU32();
  }
  return value == 0 ? 1ULL : value;
}

bool SameEndpoint(const sockaddr_in &a, const sockaddr_in &b) {
  return a.sin_family == b.sin_family && a.sin_port == b.sin_port &&
         a.sin_addr.s_addr == b.sin_addr.s_addr;
}

bool DefaultBobInstance() {
  char path[MAX_PATH]{};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  std::string name = path;
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return name.find("bob") != std::string::npos;
}

void CloseInfoWindow();

void EnableCrispDpiAwareness() {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32 == nullptr) return;
  using SetContext = BOOL(WINAPI *)(HANDLE);
  const FARPROC contextProc =
      GetProcAddress(user32, "SetProcessDpiAwarenessContext");
  SetContext setContext = nullptr;
  static_assert(sizeof(setContext) == sizeof(contextProc));
  std::memcpy(&setContext, &contextProc, sizeof(setContext));
  const HANDLE perMonitorV2 =
      reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));
  if (setContext != nullptr && setContext(perMonitorV2)) return;
  using SetLegacy = BOOL(WINAPI *)();
  const FARPROC legacyProc = GetProcAddress(user32, "SetProcessDPIAware");
  SetLegacy setLegacy = nullptr;
  static_assert(sizeof(setLegacy) == sizeof(legacyProc));
  std::memcpy(&setLegacy, &legacyProc, sizeof(setLegacy));
  if (setLegacy != nullptr) setLegacy();
}

class PetClient {
 public:
  bool Initialize(HWND window) {
    window_ = window;
    dpi_ = plane_pet_dpi::ForWindow(window);
    start_ = Clock::now();
    idleShotSeed_ = SecureRandomU32();
    const bool defaultBob = DefaultBobInstance();
    serverText_ = Option("server", "127.0.0.1:32110");
    if (!pcpair::ParseAuthKey(Option("network-key", ""), networkKey_)) {
      PetMessageBoxW(window_,
                  L"网络密钥格式无效：应为 32 个十六进制字符。",
                  L"Plane Pet", MB_OK | MB_ICONERROR);
      return false;
    }
    slot_ = static_cast<uint8_t>(
        std::clamp(atoi(Option("slot", defaultBob ? "2" : "1").c_str()), 1, 2));
    const uint32_t configuredClientId = static_cast<uint32_t>(
        strtoul(Option("client", "0").c_str(), nullptr, 10));
    const uint32_t startupPairingCode = static_cast<uint32_t>(
        strtoul(Option("code", "123456").c_str(), nullptr, 10));
    ownName_ = Wide(Option("name", slot_ == 1 ? "Alice" : "Bob"));
    peerName_ = Wide(Option("peer", slot_ == 1 ? "Bob" : "Alice"));
    const std::wstring title = L"Plane Pet PC-PC Test - " + ownName_;
    SetWindowTextW(window_, title.c_str());
    petX_ = atoi(Option("pet-x", slot_ == 1 ? "120" : "430").c_str());
    petY_ = atoi(Option("pet-y", slot_ == 1 ? "180" : "350").c_str());
    hiddenByUser_ = atoi(Option("hidden", "0").c_str()) != 0;
    autoInvite_ = atoi(Option("auto-invite", "0").c_str()) != 0;
    autoAccept_ = atoi(Option("auto-accept", "0").c_str()) != 0;
    testAutoUnbind_ =
        atoi(Option("test-auto-unbind", "0").c_str()) != 0;
    const std::wstring configuredState = Wide(Option("state", ""));
    statePath_ = configuredState.empty() ? DefaultStatePath(defaultBob)
                                         : std::filesystem::path(configuredState);
    settingsPath_ = statePath_.parent_path() /
                    (statePath_.stem().wstring() + L".settings");
    telemetryUploadCapable_ =
        atoi(Option("telemetry-upload", "0").c_str()) != 0;
    LoadSettings();
    const std::string telemetryOption = Option("telemetry", "");
    if (!telemetryOption.empty()) {
      telemetryEnabled_ = atoi(telemetryOption.c_str()) != 0;
      telemetryChoiceKnown_ = true;
      if (telemetryUploadCapable_) {
        telemetryUploadChoice_ = telemetryEnabled_ ? 1 : 0;
      }
      PersistTelemetryPreference(telemetryEnabled_);
    } else if (telemetryUploadCapable_ && telemetryUploadChoice_ < 0) {
      const int consent = PetMessageBoxW(
          window_,
          L"是否允许记录并发送匿名使用统计？\n\n"
          L"记录启动与使用时长、匹配和好友在线状态变化、邀请响应、显示/隐藏与勿扰、固定表情类型、对局过程/结果及升级检查/选择/安装结果；不会记录聊天文字、匹配码、姓名、按键、鼠标轨迹、屏幕内容或窗口标题。\n\n"
          L"数据通过加密连接发送，原始事件最多保留 90 天；之后可在右键“设置与隐私 → 匿名使用统计”随时停止。",
          L"Plane Pet - 匿名测试统计", MB_YESNO | MB_ICONINFORMATION);
      telemetryUploadChoice_ = consent == IDYES ? 1 : 0;
      telemetryEnabled_ = consent == IDYES;
      telemetryChoiceKnown_ = true;
      PersistTelemetryPreference(telemetryEnabled_);
    } else if (!telemetryChoiceKnown_) {
      const int consent = PetMessageBoxW(
          window_,
          L"是否允许在本机记录匿名实验事件？\n\n"
          L"仅在本机记录启动与使用时长、匹配/在线变化、邀请、显示/隐藏/勿扰、固定表情类型、对局过程/结果及升级事件；不会记录聊天文字、匹配码、姓名、按键、鼠标轨迹、屏幕内容或窗口标题，也不会自动上传。\n\n"
          L"之后可在右键“设置与隐私”中清除或停止记录。",
          L"Plane Pet - 本地实验数据", MB_YESNO | MB_ICONINFORMATION);
      telemetryEnabled_ = consent == IDYES;
      telemetryChoiceKnown_ = true;
      PersistTelemetryPreference(telemetryEnabled_);
    }
    const std::wstring configuredEvents = Wide(Option("events", ""));
    if (configuredEvents.empty()) {
      eventsPath_ = statePath_.parent_path() /
                    (statePath_.stem().wstring() + L".events.csv");
    } else {
      eventsPath_ = std::filesystem::path(configuredEvents);
    }
    LoadState();
    if (bindingId_ == 0) {
      std::ifstream journal(std::filesystem::path(statePath_.wstring() + L".pairing"));
      uint32_t request = 0;
      if (journal >> request && request != 0) {
        pairingRequestId_ = request;
        pairingCancelPending_ = true;
      }
    } else {
      DeleteFileW((statePath_.wstring() + L".pairing").c_str());
    }
    historyPath_ = statePath_.parent_path() /
                   (statePath_.stem().wstring() + L".history");
    LoadHistory();
    const bool updatesEnabled =
        atoi(Option("update-enabled", "0").c_str()) != 0;
    const std::string updateManifest = Option("update-manifest", "");
    const std::wstring configuredUpdateRequest =
        Wide(Option("update-request", ""));
    updateRequestPath_ = configuredUpdateRequest.empty()
        ? statePath_.parent_path() / L"update.request"
        : std::filesystem::path(configuredUpdateRequest);
    updateManager_.Configure(window_, updateManifest, updateRequestPath_,
                             updatesEnabled, updateSnoozeUntilMs_);
    if (configuredClientId != 0 && configuredClientId != clientId_) {
      clientId_ = configuredClientId;
      ClearBinding();
    }
    if (clientId_ == 0) clientId_ = SecureRandomU32();
    telemetrySessionId_ = SecureRandomU64();
    resumeRequestId_ = SecureRandomU32();
    statePersistenceOk_ = SaveState();
    if (bindingId_ != 0) {
      awaitingPairing_ = false;
      pairingAttempted_ = false;
      pairingCode_ = 0;
    } else if (startupPairingCode == 0 || !statePersistenceOk_ || pairingCancelPending_) {
      awaitingPairing_ = true;
      pairingCode_ = 0;
      if (!statePersistenceOk_)
        pairingError_ = L"无法写入身份存档，请检查存档目录权限";
    } else {
      BeginPairing(startupPairingCode);
    }

    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return false;
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) return false;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(socket_, reinterpret_cast<const sockaddr *>(&local),
             sizeof(local)) != 0) return false;
    u_long enabled = 1;
    ioctlsocket(socket_, FIONBIO, &enabled);
    if (!ResolveServer()) return false;
    BeginUsageSegment();
    LogEvent("app_started", bindingId_ != 0 ? 1 : 0);
    if (atoi(Option("update-installed", "0").c_str()) != 0) {
      pendingUpdateToken_ = Option("update-token", "");
      if (pendingUpdateToken_.size() > 64 ||
          pendingUpdateToken_.find_first_not_of("0123456789-") != std::string::npos)
        pendingUpdateToken_.clear();
    }
    if (atoi(Option("update-rollback", "0").c_str()) != 0)
      LogEvent("update_rollback");
    const auto failurePath = updateRequestPath_.parent_path() / L"update.failure";
    std::ifstream failureFile(failurePath, std::ios::binary);
    std::string failureReason;
    std::getline(failureFile, failureReason);
    failureFile.close();
    if (updateManager_.Enabled() &&
        (!failureReason.empty() || atoi(Option("update-failed", "0").c_str()) != 0)) {
      LogEvent("update_install_failed");
      updateManager_.ReportInstallFailure(failureReason.empty()
          ? L"升级未完成，已重新打开当前版本，请重新检查更新。"
          : Wide(failureReason.substr(0, 1024)));
      // Keep a readable last-failure record without replaying the event on
      // every subsequent ordinary startup.
      MoveFileExW(failurePath.c_str(),
          (updateRequestPath_.parent_path() / L"update.failure.seen").c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    ResetPrediction();
    nextPairing_ = nextInput_ = nextPing_ = nextPrediction_ = Clock::now();
    nextTelemetryHeartbeat_ = Clock::now() + std::chrono::seconds(60);
    SetPetMode(!hiddenByUser_);
    return true;
  }

  void Shutdown() {
    RecordAbandonedMatch();
    EndUsageSegment();
    LogEvent("app_exited", telemetryActiveMillis_);
    if (socket_ != INVALID_SOCKET) {
      if (pairingAttempted_ || pairingCancelPending_) {
        for (int i = 0; i < 3; ++i) SendPairCancel();
      }
      if (bindingId_ != 0) {
        for (int i = 0; i < 3; ++i) SendGoodbye();
      }
    }
    if (socket_ != INVALID_SOCKET) closesocket(socket_);
    socket_ = INVALID_SOCKET;
    WSACleanup();
  }

  void Update() {
    if (updating_) return;
    updating_ = true;
    struct UpdateGuard { bool &active; ~UpdateGuard() { active = false; } } guard{updating_};
    ReceiveAll();
    const auto now = Clock::now();
    PollPetToolbar();
    if (!pendingUpdateToken_.empty() && now >= nextUpdateHealthRead_) {
      nextUpdateHealthRead_ = now + std::chrono::seconds(1);
      std::ifstream marker(updateRequestPath_.parent_path() / L"update.health");
      std::string token;
      std::getline(marker, token);
      if (token == pendingUpdateToken_) {
        pendingUpdateToken_.clear();
        LogEvent("update_install_succeeded");
      }
    }
    const bool updateUiAvailable = IsUpdateUiAvailable();
    updateManager_.Tick(updateUiAvailable, peerVersionDiffers_,
                        localUpdateRequired_, majorVersionMismatch_,
                        (static_cast<uint32_t>(peerAppVersion_.releaseEpoch) << 24U) |
                        (static_cast<uint32_t>(peerAppVersion_.major) << 16U) |
                        (static_cast<uint32_t>(peerAppVersion_.minor) << 8U) |
                        peerAppVersion_.patch);
    const auto update = updateManager_.GetSnapshot();
    if (update.generation != lastUpdateGeneration_) {
      lastUpdateGeneration_ = update.generation;
      const bool stateChanged = update.state != lastUpdateState_;
      lastUpdateState_ = update.state;
      if (stateChanged && update.state == plane_pet_update::State::Checking)
        LogEvent("update_check", update.manual ? 1 : 0);
      else if (stateChanged && update.state == plane_pet_update::State::Downloading)
        LogEvent("update_download_started", update.required ? 1 : 0);
      else if (stateChanged && update.state == plane_pet_update::State::Error &&
               !update.installationFailure)
        LogEvent("update_verify_failed");
      else if (stateChanged &&
               (update.state == plane_pet_update::State::Available ||
                update.state == plane_pet_update::State::Required))
        LogEvent("update_available", update.required ? 1 : 0);
    }
    // Keep pending results until idle; consuming their generation during a
    // game used to lose the prompt forever when returning to the pet.
    if (updateUiAvailable &&
        update.generation != lastUpdatePromptGeneration_) {
      lastUpdatePromptGeneration_ = update.generation;
      if (plane_pet_update::ShouldShowPrompt(update, !hiddenByUser_)) {
        if ((update.required || update.manual) && hiddenByUser_) ShowPet();
        ShowUpdateWindow(window_, update.manual);
      }
    }
    if (updateUiAvailable) {
      if (updateManager_.HasInstallRequest()) {
        BeginUpdateInstall();
        return;
      }
      RepositionUpdateWindow(window_);
      RefreshUpdateWindowContent(update.generation, update.progressPercent);
    } else {
      lastUpdatePromptGeneration_ = 0;
      CloseUpdateWindow();
    }
    if (!inviteFeedback_.empty() && now >= inviteFeedbackUntil_) {
      inviteFeedback_.clear();
      inviteFeedbackUntil_ = Clock::time_point{};
    }
    const uint32_t nowMs = MillisSince(start_);
    if (telemetryEnabled_ && now >= nextTelemetryHeartbeat_) {
      const int64_t state = gameMode_ ? 2 : (hiddenByUser_ ? 1 : 0);
      LogEvent("session_heartbeat", state);
      LogEvent("usage_heartbeat", EnabledUsageMillis());
      if (dndUsageOpen_) LogEvent("dnd_usage_heartbeat", DndUsageMillis());
      nextTelemetryHeartbeat_ = now + std::chrono::seconds(60);
    }
    if (pairingCancelPending_ && now >= nextPairing_) {
      SendPairCancel();
      nextPairing_ = now + std::chrono::milliseconds(500);
    } else if (unbindPending_ && now >= nextUnbind_) {
      SendUnbind();
      nextUnbind_ = now + std::chrono::milliseconds(500);
    } else if (pairingAttempted_ && now >= nextPairing_) {
      SendPairStart();
      nextPairing_ = now + std::chrono::milliseconds(500);
    } else if (bindingId_ != 0 && session_ == 0 && now >= nextPairing_) {
      SendResume();
      nextPairing_ = now + std::chrono::milliseconds(500);
    }
    currentInput_ = Phase() == plink::GamePhase::Playing ? ReadInput() : 0;
    AdvanceMotion(now);
    AdvanceGameEffects(now);
    if (session_ != 0 && now >= nextInput_) {
      SendInput(nowMs);
      nextInput_ = now + std::chrono::milliseconds(motionSupported_ ? 33 : 50);
    }
    if (session_ != 0 && dndSupported_ && now >= nextDndSync_) {
      SendDndPreference();
      nextDndSync_ = now + std::chrono::milliseconds(dndAcknowledged_ == dndRevision_ ? 2000 : 250);
    }
    if (session_ != 0 && now >= nextPing_) {
      SendPing(nowMs);
      nextPing_ = now + std::chrono::seconds(1);
    }
    if (pendingAction_ != 0 && session_ != 0 && now >= nextAction_) {
      if (Phase() != pendingActionContext_.phase ||
          now - pendingActionStarted_ > std::chrono::seconds(5)) {
        pendingAction_ = 0;
      } else {
        SendPendingAction();
      }
      nextAction_ = now + std::chrono::milliseconds(250);
    }
    if (autoInvite_ && !autoInviteSent_ && CanInvite()) {
      autoInviteSent_ = true;
      InviteFromMenu();
    }
    if (autoAccept_ && !autoAcceptSent_ && session_ != 0 &&
        IsIncomingInvite() && !doNotDisturb_ &&
        (!scopedActionsSupported_ || (haveRoundMeta_ && roundMetaPhase_ == Phase()))) {
      autoAcceptSent_ = true;
      LogEvent("invite_accepted", ElapsedMillis(incomingInviteAt_));
      QueueAction(plink::PlayerAction::Accept);
    }
    if (testAutoUnbind_ && !testAutoUnbindSent_ && bindingId_ != 0 &&
        session_ != 0 && Phase() == plink::GamePhase::Menu &&
        OpponentOnline()) {
      testAutoUnbindSent_ = true;
      unbindPending_ = true;
      nextUnbind_ = now;
      LogEvent("test_auto_unbind");
    }
    CheckConnectionTimeout(now);
  }

  void CheckConnectionTimeout(Clock::time_point now) {
    if (lastPacketAt_.time_since_epoch().count() != 0 &&
        now - lastPacketAt_ > std::chrono::seconds(5)) {
      const bool matchWasActive = haveSnapshot_ &&
          (snapshot_.phase == plink::GamePhase::Countdown ||
           snapshot_.phase == plink::GamePhase::Playing);
      // Silence is not a voluntary forfeit or an authoritative defeat. Keep
      // the round eligible for a late server result after reconnecting.
      session_ = 0;
      ResetDndConnection();
      lastServerSequence_ = 0;
      lastActionSequence_ = 0;
      lastServerTick_ = 0;
      lastPacketAt_ = Clock::time_point{};
      pendingAction_ = 0;
      currentInput_ = 0;
      predictionReady_ = false;
      haveSnapshot_ = false;
      if (matchWasActive) {
        abandonedMatch_ = true;
        hiddenByUser_ = false;
        emergencyHidden_ = false;
        returnToPetRequested_ = false;
        connectionNotice_ = L"服务器连接中断\n本局已结束";
        connectionNoticeUntil_ = now + std::chrono::seconds(15);
        nextPairing_ = now + std::chrono::seconds(6);
        LogEvent("game_connection_lost");
        SetPetMode(true);
        FlashWindow(window_, TRUE);
      }
    }
  }

#ifdef PLANE_PET_LATENCY_SELF_TEST
  void WriteDndPublicTestState() const {
    const auto path = std::filesystem::path(Wide(Option("dnd-test-report", "")));
    if (path.empty()) return;
    static Clock::time_point next{};
    if (Clock::now() < next) return;
    next = Clock::now() + std::chrono::milliseconds(100);
    RECT clientRect{}; GetClientRect(window_, &clientRect);
    std::ofstream output(path);
    output << static_cast<int>(Phase()) << ' ' << doNotDisturb_ << ' ' << PeerDndKnown() << ' '
           << peerDnd_ << ' ' << hiddenByUser_ << ' ' << HasDndNotice() << ' ' << gameMode_ << ' '
           << static_cast<int>(ownEmote_) << ' ' << static_cast<int>(peerEmote_) << ' '
           << OpponentOnline() << ' ' << (dndAcknowledged_ == dndRevision_) << ' ' << rttMs_ << ' '
           << CanInvite() << ' ' << clientRect.right << ' ' << clientRect.bottom << '\n';
  }
#endif
#ifdef PLANE_PET_MOTION_SELF_TEST
  void MotionTestFrame() {
    const std::filesystem::path path(Wide(Option("motion-report", "")));
    if (path.empty()) return;
    static std::ofstream output(path);
    static bool invited = false;
    if (!invited && Option("motion-invite", "0") == "1" && CanInvite() &&
        PeerDndKnown() && dndAcknowledged_ == dndRevision_ && !HasDndNotice()) {
      invited = true;
      InviteFromMenu();
    }
    const auto t = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_).count();
    const auto testTick = battleSupported_ ? battlePredictor_.tick : motionPredictor_.sequence;
    const auto testAck = battleSupported_ ? battlePredictor_.confirmed.tick : motionPredictor_.acknowledged;
    const auto testPoint = battleSupported_ ? battlePredictor_.current.players[slot_ - 1] : motionPredictor_.position;
    output << t << ',' << int(Phase()) << ',' << testTick << ','
           << testAck << ',' << testPoint.x << ','
           << testPoint.y << ',' << motionFault_ << ',' << syncFailed_ << ','
           << int(slot_) << ',' << int(snapshot_.players[0].health) << ',' << int(snapshot_.players[1].health)
           << ',' << motionSupported_ << ',' << int(snapshot_.winnerSlot) << '\n';
    output.flush();
    if (battleSupported_ && battlePredictor_.ready && Phase() == plink::GamePhase::Playing) {
      // Test-only evidence from the very same whole-world presentation used by DrawGameWindow.
      static std::ofstream trace(std::filesystem::path(path.wstring() + L".battle.csv"));
      static unsigned captured = 0;
      static uint8_t oldHealth = 3;
      const auto shown = pcbattle::Sample(battlePredictor_, motionRemainder_ / 1000000.0);
      const unsigned own = slot_ - 1;
      double nearest = 9999;
      uint16_t nearId = 0;
      for (const auto &bullet : shown.bullets) if (bullet.active && bullet.owner != slot_) {
        auto tip = bullet.p; tip.y += (bullet.owner == 1 ? -1 : 1) * pcbattle::kProjectileTip;
        auto relative = pcbattle::Point{tip.x - shown.players[own].x, tip.y - shown.players[own].y};
        if (own) relative = {-relative.x, -relative.y};
        for (unsigned row = 0; row < 24; ++row) {
          const double x = relative.x / 256.0, y = relative.y / 256.0;
          const double dx = std::max({0.0, pcbattle::kHull[row][0] - .5 - x, x - pcbattle::kHull[row][1] - .5});
          const double dy = std::max({0.0, int(row) - 17.5 - y, y - int(row) + 16.5});
          const double distance = std::hypot(dx, dy);
          if (distance < nearest) { nearest = distance; nearId = bullet.id; }
        }
      }
      trace << t << ',' << shown.tick << ',' << testAck << ',' << shown.players[own].x << ',' << shown.players[own].y
            << ',' << int(shown.health[own]) << ',' << int(snapshot_.players[own].health) << ','
            << nearId << ',' << nearest << ',' << battleEvents_.count + 0 << '\n';
      trace.flush();
      if (captured < 40 && (nearest < 4 || shown.health[own] != oldHealth)) {
        ++captured;
        SaveMotionTestFrame(std::filesystem::path(path.wstring() + L"-contact-" + std::to_wstring(captured) + L".bmp"));
      }
      oldHealth = shown.health[own];
    }
    if (Phase() == plink::GamePhase::Playing) {
      const auto n = testTick;
      static bool stalled = false;
      if (!stalled && n >= 180 && Option("motion-abort", "0") == "1") {
        stalled = true;
        motionClock_ -= std::chrono::seconds(1); // test only: exercise production stall protection
      }
      // Exercise the production mouse -> game-layout -> target -> wire path.
      // Slot 2 tests the same local coordinates under 180-degree rotation.
      mouseDragging_ = true;
      const int x = n < 360 ? ((n / 60) % 2 ? 215 : 25) : 120;
      const int y = n < 360 ? (n / 90) % 2 ? 190 : 300 :
          (battleSupported_ ? ((n / 120) % 2 ? 230 : 290) : 264);
      SetMouseTarget(x, y + kGameHudHeight);
    }
    static unsigned saved = 0;
    const unsigned desired = Phase() == plink::GamePhase::Finished ? 4U :
        (Phase() == plink::GamePhase::Playing ? std::min(3U, testTick / 120) : 0U);
    if (desired <= saved) return;
    saved = desired;
    SaveMotionTestFrame(std::filesystem::path(path.wstring() + L"-" + std::to_wstring(saved) + L".bmp"));
  }
  void SaveMotionTestFrame(const std::filesystem::path &path) const {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kGameClientWidth; info.bmiHeader.biHeight = -kGameClientHeight;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr; HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (dc && bitmap && pixels) {
      const auto old = SelectObject(dc, bitmap);
      DrawGameWindow(dc, kGameClientWidth, kGameClientHeight); GdiFlush();
      BITMAPFILEHEADER header{}; header.bfType = 0x4D42;
      header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
      header.bfSize = header.bfOffBits + kGameClientWidth * kGameClientHeight * 4;
      std::ofstream frame(path, std::ios::binary);
      frame.write(reinterpret_cast<const char *>(&header), sizeof(header));
      frame.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
      frame.write(static_cast<const char *>(pixels), kGameClientWidth * kGameClientHeight * 4);
      SelectObject(dc, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
  }
#endif
  void Draw(HDC target, const RECT &rect) const {
#ifdef PLANE_PET_MOTION_SELF_TEST
    const auto drawStarted = Clock::now();
#endif
    const int width = std::max<LONG>(1, rect.right - rect.left);
    const int height = std::max<LONG>(1, rect.bottom - rect.top);
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    if (!memory || !bitmap) {
      if (memory) DeleteDC(memory);
      if (bitmap) DeleteObject(bitmap);
      return;
    }
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    RECT full{0, 0, width, height};
    HBRUSH background = CreateSolidBrush(gameMode_ ? RGB(0, 0, 0) : kTransparent);
    FillRect(memory, &full, background);
    DeleteObject(background);
    {
      const int logicalWidth = gameMode_ ? kGameClientWidth : kPetWidth;
      const int logicalHeight = gameMode_ ? kGameClientHeight : kPetHeight;
      plane_pet_dpi::LogicalCanvas canvas(memory, logicalWidth, logicalHeight, width, height);
      if (gameMode_) DrawGameWindow(memory, logicalWidth, logicalHeight);
      else DrawPet(memory, logicalWidth, logicalHeight);
    }
    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
#ifdef PLANE_PET_MOTION_SELF_TEST
    if (!Option("motion-report", "").empty()) {
      static std::ofstream performance(std::filesystem::path(Wide(Option("motion-report", "") + ".paint.csv")));
      performance << gameMode_ << ',' << std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - drawStarted).count() << '\n';
      performance.flush();
    }
#endif
  }

  void DrawHistoryWindow(HDC dc, const RECT &rect) const {
    using namespace plane_pet_history_ui;
    const int width = std::max<LONG>(1, rect.right - rect.left);
    Fill(dc, rect, RGB(9, 13, 22));
    RECT title{24, 12, width - 24, 52};
    Text(dc, L"历史战绩", title, 30, kBlueLight, FW_BOLD,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT summary{20, 58, width - 20, 137};
    RoundPanel(dc, summary, 14, kCard, kCardEdge, 2);
    wchar_t totals[96]{};
    swprintf(totals, std::size(totals), L"总计 %u 局    胜 %u    负 %u    平 %u",
             historyTotal_, historyWins_, historyLosses_, historyDraws_);
    RECT totalRect{36, 63, width - 36, 101};
    Text(dc, totals, totalRect, 22, kWhite, FW_BOLD,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const uint32_t winRate = historyTotal_ == 0
        ? 0U : static_cast<uint32_t>(historyWins_ * 100ULL / historyTotal_);
    wchar_t rate[64]{};
    swprintf(rate, std::size(rate), L"胜率 %u%%", winRate);
    RECT rateRect{36, 100, width - 36, 133};
    Text(dc, rate, rateRect, 18, kMuted, FW_NORMAL,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT recentTitle{24, 146, width - 24, 174};
    Text(dc, L"最近 10 局 · 以双方剩余生命表示", recentTitle, 20,
         kMuted, FW_BOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (recentHistory_.empty()) {
      RECT empty{24, 190, width - 24, 270};
      Text(dc, L"还没有完成过对局", empty, 22, kMuted, FW_NORMAL);
      return;
    }

    for (size_t index = 0; index < recentHistory_.size(); ++index) {
      const HistoryEntry &entry = recentHistory_[index];
      const int top = kFirstRowTop + static_cast<int>(index) * kRowPitch;
      RECT row{20, top, width - 20, top + kRowHeight};
      RoundPanel(dc, row, 10,
                 index % 2 == 0 ? RGB(20, 26, 39) : RGB(16, 21, 33),
                 RGB(39, 50, 68));
      wchar_t number[12]{};
      swprintf(number, std::size(number), L"#%u",
               static_cast<unsigned>(index + 1));
      RECT numberRect{30, top, 70, top + 34};
      Text(dc, number, numberRect, 18, kMuted, FW_NORMAL,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      const wchar_t *result = entry.outcome > 0
          ? L"胜" : (entry.outcome < 0 ? L"负" : L"平");
      const COLORREF resultColor = entry.outcome > 0
          ? kGreen : (entry.outcome < 0 ? kRed : kYellow);
      RECT resultRect{74, top, 108, top + 34};
      Text(dc, result, resultRect, 21, resultColor, FW_BOLD);
      DrawHistoryPlane(dc, kOwnPlaneX, top + kRowHeight / 2, kBlue);
      DrawHistoryHealth(dc, kOwnHeartX, top + kHeartY, entry.ownHealth, kBlue);
      DrawHistoryPlane(dc, kPeerPlaneX, top + kRowHeight / 2, kRed);
      DrawHistoryHealth(dc, kPeerHeartX, top + kHeartY, entry.peerHealth, kRed);
      const std::wstring time = FormatHistoryTime(entry.timestampMs);
      RECT timeRect{kTimeLeft, top, width - 30, top + kRowHeight};
      Text(dc, time, timeRect, 17, kMuted, FW_NORMAL,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!historyPersistenceOk_) {
      RECT warning{24, rect.bottom - 34, width - 24, rect.bottom - 7};
      Text(dc, L"战绩暂未写入磁盘，请检查存档目录权限", warning, 17,
           kYellow, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
  }

  void DrawBriefStatusLegend(HDC dc, int y) const {
    using namespace plane_pet_help;
    for (int index = 0; index < 3; ++index) {
      const int left = 38 + index * kBriefStatusPitch;
      {
        // Reuse the actual status artwork, preserving the help page's DPI
        // and scroll transform. No emoji substitute or stretched bitmap.
        plane_pet_dpi::ImageCanvas canvas(dc);
        auto &graphics = canvas.graphics();
        graphics.TranslateTransform(static_cast<float>(left + 12), static_cast<float>(y + 13));
        const float scale = index == 2 ? 1.25f : 2.5f;
        graphics.ScaleTransform(scale, scale);
        plane_pet_ui::DrawStatusBadge(graphics, {{0, 0}, true, index == 2, index == 1});
      }
      Text(dc, kBriefStatusLabels[index], RECT{left + 32, y, left + kBriefStatusPitch - 12,
          y + kBriefStatusRowHeight}, kBodyFontSize, kMuted, FW_NORMAL,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
  }

  void DrawHelpWindow(HDC dc, const RECT &rect, bool detailed = true) const {
    const int width = std::max<LONG>(1, rect.right - rect.left);
    Fill(dc, rect, RGB(9, 13, 22));
    RECT title{26, 12, width - 26, 54};
    Text(dc, detailed ? plane_pet_help::kTitle : plane_pet_help::kBriefTitle, title, 32, kBlueLight, FW_BOLD,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitle{26, 52, width - 26, 84};
    Text(dc, detailed ? plane_pet_help::kSubtitle : plane_pet_help::kBriefSubtitle, subtitle,
         20, kMuted, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int y = plane_pet_help::kFirstSectionY;
    const auto section = [&](const wchar_t *heading, const wchar_t *body,
                             int bodyHeight = 50, int bodyInset = 0) {
      RECT headingRect{26, y, width - 26, y + 34};
      Text(dc, heading, headingRect, 23, kWhite, FW_BOLD,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      RECT bodyRect{38, y + 36 + bodyInset, width - 28, y + 36 + bodyHeight};
      Text(dc, body, bodyRect, plane_pet_help::kBodyFontSize, kMuted, FW_NORMAL,
           DT_LEFT | DT_TOP | DT_WORDBREAK);
      y += plane_pet_help::kSectionSpacing + bodyHeight;
    };
    if (detailed) {
      int index = 0;
      for (const auto &item : plane_pet_help::kSections) {
        const bool status = index++ == plane_pet_help::kDetailedStatusSection;
        const int bodyTop = y + 36;
        section(item.heading, item.body, item.bodyHeight,
                status ? plane_pet_help::kDetailedStatusInset : 0);
        if (status) DrawBriefStatusLegend(dc, bodyTop);
      }
    } else {
      int index = 0;
      for (const auto &item : plane_pet_help::kBriefSections) {
        const int bodyTop = y + 36;
        section(item.heading, item.body, item.bodyHeight);
        if (index++ == plane_pet_help::kBriefStatusSection)
          DrawBriefStatusLegend(dc, bodyTop + plane_pet_help::kBriefStatusOffset);
      }
      Text(dc, plane_pet_help::kBriefNotice,
           RECT{38, y, width - 28, y + plane_pet_help::kBriefNoticeHeight},
           plane_pet_help::kBodyFontSize, kBlueLight, FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
  }

  void DrawHelpFooter(HDC dc, const RECT &rect) const {
    Fill(dc, rect, RGB(14, 19, 29));
    DrawLine(dc, rect.left, rect.top, rect.right, rect.top, kCardEdge);
  }

  void DrawHelpNavigation(HDC dc, const RECT &rect, UINT dpi, bool detailed,
                          bool pressed, bool focused) const {
    RoundPanel(dc, rect, plane_pet_dpi::Scale(10, dpi),
               pressed ? RGB(32, 68, 85) : RGB(24, 38, 54), kCardEdge);
    const int fontSize = std::min(plane_pet_dpi::Scale(20, dpi),
        std::max(12, MulDiv(20, rect.right - rect.left, plane_pet_help::kNavigationWidth)));
    Text(dc, detailed ? plane_pet_help::kBackLink : plane_pet_help::kDetailLink,
         rect, fontSize, kBlueLight, FW_BOLD);
    if (focused) {
      RECT focus = rect;
      InflateRect(&focus, -plane_pet_dpi::Scale(4, dpi), -plane_pet_dpi::Scale(4, dpi));
      DrawFocusRect(dc, &focus);
    }
  }

  std::wstring DiagnosticText() const {
    const auto statusPath = std::filesystem::path(Wide(Option("network-status", "")));
    const auto status = plane_pet_network::Read(statusPath);
    std::wstring result = L"Plane Pet " + std::wstring(plane_pet_version::kWideString) +
        L"\n\n本机连接：" + (statusPath.empty() ? L"本地/LAN 客户端，无公网隧道状态" :
            plane_pet_network::Describe(status)) + L"\n错误码：" + std::to_wstring(status.error);
    result += L"\n绑定：" + std::wstring(bindingId_ ? L"已绑定" : L"尚未绑定");
    result += L"\n身份存档：" + std::wstring(statePersistenceOk_ ? L"正常" : L"写入失败");
    result += L"\n战绩保存：" + std::wstring(historyPersistenceOk_ ? L"正常" : L"写入失败");
    result += L"\n匿名使用统计：" + std::wstring(telemetryEnabled_ ? L"已启用" : L"已关闭");
    result += L"\n隐藏快捷键：" + std::wstring(gBossHotkeyAvailable ? L"可用" : L"被其他程序占用，请用右键隐藏");
    result += L"\n\n已连接隧道不代表好友在线；好友状态仍以桌宠圆点为准。"
              L"\n游戏直连公网服务器，更新下载遵循系统代理。请检查系统时间及网络；不要关闭证书校验，"
              L"也不要为排障删除绑定文件。\n\n此信息不含设备身份、凭据、匹配码、路径或用户名。";
    return result;
  }

  void DrawDiagnosticsWindow(HDC dc, const RECT &rect) const {
    Fill(dc, rect, RGB(9, 13, 22));
    Text(dc, L"连接与诊断", RECT{26, 12, rect.right - 26, 56}, 30, kBlueLight, FW_BOLD,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(dc, DiagnosticText(), RECT{26, 72, rect.right - 26, 680}, 22, kWhite,
         FW_NORMAL, DT_LEFT | DT_TOP | DT_WORDBREAK);
  }

  void ExportDiagnostics() {
    wchar_t target[32768] = L"PlanePet-diagnostics.txt";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0\0";
    dialog.lpstrFile = target;
    dialog.nMaxFile = static_cast<DWORD>(std::size(target));
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!PetGetSaveFileNameW(&dialog)) return;
    const auto bytes = plane_pet_windows::Utf8(DiagnosticText());
    std::ofstream output(std::filesystem::path(target), std::ios::binary | std::ios::trunc);
    output << bytes;
    output.close();
    if (!output) PetMessageBoxW(window_, L"诊断文件保存失败，请检查所选目录权限。",
        L"导出诊断信息", MB_OK | MB_ICONWARNING);
  }

  void OnLeftButtonDown(int x, int y) {
    const auto dragPetWindow = [&]() {
      SetForegroundWindow(window_);
      petDragHasScreenCursor_ = GetCursorPos(&petDragStartCursor_) != FALSE;
      petDragStartClient_ = POINT{x, y};
      RECT rect{};
      GetWindowRect(window_, &rect);
      petDragStartX_ = rect.left;
      petDragStartY_ = rect.top;
      petWindowDragging_ = true;
      SetCapture(window_);
    };

    if (awaitingPairing_ || pairingAttempted_) {
      SetFocus(window_);
      dragPetWindow();
      return;
    }
    if (gameMode_) {
      if (GameFinishedVisible()) {
        ReturnToPetFromFinished();
      } else {
        mouseDragging_ = true;
        SetMouseTarget(x, y);
        SetCapture(window_);
        SetFocus(window_);
      }
      return;
    }
    const POINT point{x, y};
    if (HasDndNotice() && dndNoticeCancelable_ &&
        PtInRect(&plane_pet_ui::kDndNoticeLayout.primary, point)) {
      ClearDndNotice();
      return;
    }
    if (IsPetToolbarSurface(x, y)) {
      toolbarPressed_ = ToolbarTargetAt(x, y);
      toolbarPressPoint_ = point;
      if (toolbarPressed_) SetCapture(window_);
      InvalidateRect(window_, nullptr, FALSE);
      return;
    }
    if (Phase() == plink::GamePhase::Waiting) {
      if (IsIncomingInvite()) {
        if (doNotDisturb_) return;
        const RECT &accept = plane_pet_ui::kIncomingInviteLayout.primary;
        const RECT &reject = plane_pet_ui::kIncomingInviteLayout.secondary;
        if (PtInRect(&accept, point)) {
          LogEvent("invite_accepted", ElapsedMillis(incomingInviteAt_));
          QueueAction(plink::PlayerAction::Accept);
          return;
        }
        if (PtInRect(&reject, point)) {
          LogEvent("invite_rejected", ElapsedMillis(incomingInviteAt_));
          QueueAction(plink::PlayerAction::ReturnToMenu);
          return;
        }
      } else {
        const RECT &cancel = plane_pet_ui::kOutgoingInviteLayout.primary;
        if (PtInRect(&cancel, point)) {
          LogEvent("invite_canceled", ElapsedMillis(outgoingInviteAt_));
          QueueAction(plink::PlayerAction::ReturnToMenu);
          return;
        }
      }
    }
    dragPetWindow();
  }

  void OnMouseMove(int x, int y) {
    if (toolbarPressed_) {
      if (std::abs(x - toolbarPressPoint_.x) > plane_pet_ui::kToolbarClickSlop ||
          std::abs(y - toolbarPressPoint_.y) > plane_pet_ui::kToolbarClickSlop) {
        toolbarPressed_ = 0;
        if (GetCapture() == window_) ReleaseCapture();
      }
      return;
    }
    if (gameMode_ && mouseDragging_) {
      SetMouseTarget(x, y);
    } else if (!gameMode_ && petWindowDragging_) {
      POINT cursor{};
      int dx = x - petDragStartClient_.x, dy = y - petDragStartClient_.y;
      if (petDragHasScreenCursor_ && GetCursorPos(&cursor)) {
        dx = cursor.x - petDragStartCursor_.x;
        dy = cursor.y - petDragStartCursor_.y;
      }
      petX_ = petDragStartX_ + dx;
      petY_ = petDragStartY_ + dy;
      SetWindowPos(window_, nullptr, petX_, petY_, 0, 0,
                   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  void OnLeftButtonUp(int x = -1, int y = -1) {
    if (toolbarPressed_) {
      const int pressed = toolbarPressed_;
      toolbarPressed_ = 0;
      if (GetCapture() == window_) ReleaseCapture();
      if (std::abs(x - toolbarPressPoint_.x) <= plane_pet_ui::kToolbarClickSlop &&
          std::abs(y - toolbarPressPoint_.y) <= plane_pet_ui::kToolbarClickSlop &&
          ToolbarTargetAt(x, y) == pressed) {
        if (pressed == 1) InviteFromMenu();
        else if (pressed == 2) toolbar_.Toggle(GetTickCount64());
        else SendQuickEmote(pressed - 3);
      }
      InvalidateRect(window_, nullptr, FALSE);
      return;
    }
    if (petWindowDragging_) {
      petWindowDragging_ = false;
      if (GetCapture() == window_) ReleaseCapture();
      return;
    }
    if (!mouseDragging_) return;
    mouseDragging_ = false;
    if (GetCapture() == window_) ReleaseCapture();
  }

  void OnCaptureChanged() {
    toolbarPressed_ = 0;
    mouseDragging_ = false;
    petWindowDragging_ = false;
  }

  UINT UiDpi() const { return dpi_; }
#ifdef PLANE_PET_RELEASE_SELF_TEST
  void SetToolbarTestState(bool visible, bool expanded, uint64_t slideElapsed = 180) {
    ResetPetToolbar();
    toolbar_.Reset();
    if (!visible) return;
    const auto now = GetTickCount64();
    toolbar_.Observe(true, true, false, now);
    if (expanded) toolbar_.Toggle(now - std::min(now, slideElapsed));
  }
  bool ToolbarExpandedForTest() const { return toolbar_.expanded; }
  int ToolbarPressedForTest() const { return toolbarPressed_; }
  uint8_t ToolbarPendingActionForTest() const { return pendingAction_; }
  uint8_t ToolbarOwnEmoteForTest() const { return ownEmote_; }
  Clock::time_point ToolbarDndDeadlineForTest() const { return dndNoticeUntil_; }
  bool ToolbarFistLoadedForTest() const {
    return EmbeddedBitmap(plane_pet_ui::kToolbarFistResource, challengeFistImage_) != nullptr;
  }
  std::vector<plane_pet_ui::IdleShot> IdleAutoFrameForTest(uint32_t elapsed) const {
    std::vector<plane_pet_ui::IdleShot> result;
    plane_pet_ui::ForEachIdleAutoShot(elapsed, idleShotSeed_, [&](uint32_t fired) {
      return OpponentOnline() ? PetFormationAt(fired).blue : PetPoseAt(fired);
    }, [&](const auto &shot) { result.push_back(shot); });
    return result;
  }
  void AllowEmoteForTest() { lastEmoteSentAt_ = Clock::time_point{}; }
  void UseClientDragCoordinatesForTest() { petDragHasScreenCursor_ = false; }
  void ObserveToolbarForTest(uint64_t now) { toolbar_.Observe(true, true, false, now); }
  void SeedHiddenPairingForTest() { awaitingPairing_ = true; pairingPanelVisible_ = false; }
  void DrawToolbarForTest(HDC dc) const { DrawPetToolbar(dc); }
  void SeedToolbarBlockedCase(int state) {
    if (state == 0) awaitingPairing_ = true;
    else if (state == 1) pairingAttempted_ = true;
    else if (state == 2) pairingCancelPending_ = true;
    else if (state == 3) unbindPending_ = true;
    else if (state == 4) pendingAction_ = static_cast<uint8_t>(plink::PlayerAction::Invite);
    else if (state == 5) majorVersionMismatch_ = true;
  }
  void SeedInviteRenderCase(bool incoming, uint32_t remainingMs) {
    awaitingPairing_ = pairingAttempted_ = false;
    haveSnapshot_ = true;
    slot_ = 1;
    snapshot_.phase = plink::GamePhase::Waiting;
    snapshot_.inviterSlot = incoming ? 2 : 1;
    snapshot_.onlineMask = 3;
    snapshot_.phaseRemainingMs = remainingMs;
    pendingAction_ = 0;
    ownEmote_ = peerEmote_ = 0;
    connectionNotice_.clear();
    inviteFeedback_.clear();
    start_ = Clock::now() - std::chrono::milliseconds(1140);  // three waiting dots
  }
  void FinishDpiRenderCase() {
    snapshot_.phase = plink::GamePhase::Finished;
    CloseRequested();
  }
  void SeedDpiRenderCase(bool game) {
    ResetDndConnection(); doNotDisturb_ = false;
    unbindPending_ = hiddenByUser_ = false;
    // Each DPI fixture is independent of the preceding finish/return action.
    pendingAction_ = 0; pairingCancelPending_ = returnToPetRequested_ = false;
    start_ = Clock::now() - std::chrono::milliseconds(350);
    idleShotSeed_ = 20260907;
    haveSnapshot_ = true;
    awaitingPairing_ = pairingAttempted_ = false;
    predictionReady_ = false;
    slot_ = 2;
    snapshot_ = plink::SnapshotPayload{};
    snapshot_.onlineMask = 3;
    snapshot_.phase = game ? plink::GamePhase::Playing : plink::GamePhase::Menu;
    snapshot_.phaseRemainingMs = 178000;
    snapshot_.players[0] = {120, plink::kPlayerOneMaxY, 3, 1};
    snapshot_.players[1] = {119, plink::kPlayerTwoMinY, 3, 1};
    snapshot_.bulletCount = 2;
    snapshot_.bullets[0] = {1, 2, 159, 99};
    snapshot_.bullets[1] = {2, 1, 79, 219};
    ownEmote_ = 1; peerEmote_ = 4;
    ownEmoteStartedAt_ = peerEmoteStartedAt_ = Clock::now();
    ownEmoteUntil_ = peerEmoteUntil_ = Clock::now() + std::chrono::seconds(10);
    rttMs_ = 42;
    if (game) SetGameMode(); else SetPetMode(true);
  }
  void SeedDndRenderCase(bool self, bool peer, bool online, bool bubbles, bool notice) {
    SeedDpiRenderCase(false);
    session_ = 100; pendingAction_ = 0; pairingCancelPending_ = false;
    scopedActionsSupported_ = false; majorVersionMismatch_ = false;
    doNotDisturb_ = self;
    dndSupported_ = peerDndKnown_ = peerDndCapable_ = true;
    dndAcknowledged_ = dndRevision_; dndPresenceAt_ = Clock::now(); peerDnd_ = peer;
    snapshot_.onlineMask = online ? 3 : static_cast<uint8_t>(1U << (slot_ - 1U));
    if (!bubbles) ownEmote_ = peerEmote_ = 0;
    connectionNotice_.clear(); inviteFeedback_.clear();
    if (notice) ShowDndNotice(L"对方已开启勿扰...", true);
  }
  void SeedPetTextRenderCase(int state) {
    SeedDndRenderCase(false, false, true, false, false);
    pairingError_.clear(); pairingInput_ = L"123456";
    pairingPanelVisible_ = true;
    if (state <= 3 || state == 7) {
      awaitingPairing_ = true;
      pairingAttempted_ = state == 1 || state == 2;
      serverConfirmedWaiting_ = state == 1;
      pairingCode_ = 123456;
      if (state == 3) pairingError_ = L"无法写入身份存档，请检查存档目录权限";
      if (state == 7) ShowDndNotice(L"设置仅本次生效，保存失败", false);
    } else if (state == 4) {
      connectionNotice_ = L"服务器连接中断\n本局已结束";
      connectionNoticeUntil_ = Clock::now() + std::chrono::seconds(10);
    } else {
      inviteFeedback_ = state == 5 ? L"对方拒绝邀请" : L"邀请已超时";
      inviteFeedbackUntil_ = Clock::now() + std::chrono::seconds(10);
    }
  }
  void SeedHistoryRenderCase(bool empty) {
    // Synthetic UI fixtures only: never load/save the user's history here.
    ResetHistory(); historyPersistenceOk_ = true;
    if (empty) return;
    historyTotal_ = 15; historyWins_ = 9; historyLosses_ = 3; historyDraws_ = 3;
    constexpr uint8_t own[] = {2, 0, 1, 0, 0, 2, 3, 3, 2, 3};
    constexpr uint8_t peer[] = {0, 3, 0, 2, 0, 1, 2, 0, 3, 0};
    constexpr int8_t outcome[] = {1, -1, 1, -1, 0, 1, 1, 1, -1, 1};
    for (size_t i = 0; i < 10; ++i)
      recentHistory_.push_back({1788694380000ULL - i * 61000ULL, own[i], peer[i], outcome[i]});
  }
  static std::wstring HistoryTimeForTest(uint64_t time) { return FormatHistoryTime(time); }
  bool HistoryHeartResourceForTest() const {
    using namespace plane_pet_hearts;
    auto *bitmap = HeartAtlasBitmap();
    if (bitmap == nullptr) return false;
    for (int state = 0; state < 3; ++state) {
      Gdiplus::Color corner, center;
      if (bitmap->GetPixel(kHeartSourceX[state], kHeartSourceY, &corner) != Gdiplus::Ok ||
          bitmap->GetPixel(kHeartSourceX[state] + kHeartSourceWidth / 2,
              kHeartSourceY + kHeartSourceHeight / 2, &center) != Gdiplus::Ok ||
          corner.GetA() != 0 || center.GetA() < 250) return false;
    }
    return true;
  }
  void DrawGameHudForTest(HDC dc, int ox, int oy, uint8_t viewer,
                         uint8_t player0Hp, uint8_t player1Hp, bool received = true,
                         plink::GamePhase phase = plink::GamePhase::Playing) {
    const auto previous = snapshot_;
    const bool hadSnapshot = haveSnapshot_;
    const auto previousRtt = rttMs_;
    haveSnapshot_ = received;
    snapshot_.phase = phase;
    snapshot_.players[0].health = player0Hp;
    snapshot_.players[1].health = player1Hp;
    snapshot_.phaseRemainingMs = 180000;
    rttMs_ = 37;
    DrawGameHud(dc, 1, ox, oy, viewer);
    snapshot_ = previous;
    haveSnapshot_ = hadSnapshot;
    rttMs_ = previousRtt;
  }
#endif

  POINT LogicalClientPoint(POINT point) const {
    RECT physical{};
    GetClientRect(window_, &physical);
    return plane_pet_dpi::LogicalPoint(point, physical.right, physical.bottom,
        gameMode_ ? kGameClientWidth : kPetWidth,
        gameMode_ ? kGameClientHeight : kPetHeight);
  }

  void ChangeDpi(UINT dpi, const RECT *suggested = nullptr) {
    dpi_ = plane_pet_dpi::Normalize(dpi);
    OnLeftButtonUp();
    RECT current{};
    GetWindowRect(window_, &current);
    if (suggested) current = *suggested;
    // The pet and playfield intentionally retain their original physical-pixel
    // footprint at every monitor scale. DPI awareness keeps text crisp; only
    // native window chrome uses the monitor DPI, not our compact content.
    RECT size{0, 0, gameMode_ ? kGameClientWidth : kPetWidth,
                  gameMode_ ? kGameClientHeight : kPetHeight};
    if (gameMode_) plane_pet_dpi::Adjust(size, static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)),
        static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)),
        plane_pet_dpi::ForWindow(window_));
    current.right = current.left + size.right - size.left;
    current.bottom = current.top + size.bottom - size.top;
    current = plane_pet_dpi::FitMonitor(current);
    SetWindowPos(window_, nullptr, current.left, current.top,
        current.right - current.left, current.bottom - current.top,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
    if (!gameMode_) { petX_ = current.left; petY_ = current.top; }
    InvalidateRect(window_, nullptr, FALSE);
  }

  bool IsInteractivePetPoint(int x, int y) const {
    if (gameMode_) return true;
    const POINT point{x, y};
    if (HasDndNotice() && PtInRect(&plane_pet_ui::kDndNoticeLayout.panel, point)) return true;
    for (const auto &badge : StatusBadges()) {
      const RECT bounds = badge.Bounds();
      if (badge.visible && PtInRect(&bounds, point)) return true;
    }
    const uint32_t elapsedMs = MillisSince(start_);
    const PetAnimationPose pose = OpponentOnline()
        ? PetFormationAt(elapsedMs).blue : PetPoseAt(elapsedMs);
    const auto hitsPlane = [&](const PetAnimationPose &candidate) {
      const int planeX = static_cast<int>(std::lround(candidate.x));
      const int planeY = static_cast<int>(std::lround(candidate.y));
      RECT plane{planeX - 24, planeY - 24, planeX + 25, planeY + 25};
      return PtInRect(&plane, point) != FALSE;
    };
    if (hitsPlane(pose)) return true;
    if (OpponentOnline() &&
        hitsPlane(PetFormationAt(elapsedMs).red)) return true;
    const auto hitsBubble = [&](const PetAnimationPose &candidate, bool own) {
      const RECT bubble = QuickEmoteBubbleRect(static_cast<int>(std::lround(candidate.x)),
          static_cast<int>(std::lround(candidate.y)), own);
      return PtInRect(&bubble, point) != FALSE;
    };
    if (ownEmote_ && Clock::now() < ownEmoteUntil_ && hitsBubble(pose, true)) return true;
    if (OpponentOnline() && peerEmote_ && Clock::now() < peerEmoteUntil_ &&
        hitsBubble(PetFormationAt(elapsedMs).red, false)) return true;
    if ((awaitingPairing_ || pairingAttempted_) && pairingPanelVisible_) {
      RECT pairingCard{76, 5, kPetWidth - 5, kPetHeight - 5};
      return PtInRect(&pairingCard, point) != FALSE;
    }
    if (IsPetToolbarSurface(x, y)) {
      return true;
    }
    if (Phase() == plink::GamePhase::Waiting) {
      const auto &layout = IsIncomingInvite() ? plane_pet_ui::kIncomingInviteLayout
                                            : plane_pet_ui::kOutgoingInviteLayout;
      return PtInRect(&layout.panel, point) != FALSE;
    }
    if (!inviteFeedback_.empty() && Clock::now() < inviteFeedbackUntil_) {
      return PtInRect(&plane_pet_ui::kInviteFeedbackPanel, point) != FALSE;
    }
    return false;
  }

  void OnKeyDown(WPARAM key) {
    if (key == VK_ESCAPE && HasDndNotice()) { ClearDndNotice(); return; }
    if (pairingAttempted_) {
      if (key == VK_ESCAPE) StopPairing();
      return;
    }
    if (awaitingPairing_) return;
    if (gameMode_ && GameFinishedVisible()) {
      ReturnToPetFromFinished();
      return;
    }
    if (key == VK_ESCAPE && gameMode_) {
      EmergencyHide();
      return;
    }
  }

  void OnChar(wchar_t character) {
    if (!awaitingPairing_ || pairingAttempted_ || !pairingPanelVisible_)
      return;
    if (character >= L'0' && character <= L'9') {
      if (pairingInput_.size() < 6) {
        pairingInput_.push_back(character);
        pairingError_.clear();
      }
    } else if (character == L'\b') {
      if (!pairingInput_.empty()) pairingInput_.pop_back();
      pairingError_.clear();
    } else if (character == L'\r') {
      SubmitPairingCode();
    }
    InvalidateRect(window_, nullptr, FALSE);
  }

  void HideByUser() {
    ResetPetToolbar();
    ClearDndNotice();
    dndAttemptOperation_ = 0;
    CloseInfoWindow();
    CloseUpdateWindow();
    updateManager_.Dismiss();
    if (gameMode_) {
      EmergencyHide();
      return;
    }
    hiddenByUser_ = true;
    LogEvent("pet_hidden");
    ShowWindow(window_, SW_HIDE);
  }

  void ShowPet() {
    hiddenByUser_ = false;
    if (returnToPetRequested_) {
      SetPetMode(true);
      LogEvent("pet_shown");
      return;
    }
    if (gameMode_ && (Phase() != plink::GamePhase::Menu || fxDeferredMenu_)) {
      emergencyHidden_ = false;
      ShowWindow(window_, SW_SHOW);
      SetForegroundWindow(window_);
      LogEvent("game_restored");
      return;
    }
    emergencyHidden_ = false;
    SetPetMode(true);
    LogEvent("pet_shown");
  }

  void EmergencyHide() {
    ResetPetToolbar();
    ClearDndNotice();
    dndAttemptOperation_ = 0;
    CloseInfoWindow();
    CloseUpdateWindow();
    updateManager_.Dismiss();
    const bool hidingGame = gameMode_;
    mouseDragging_ = false;
    currentInput_ = 0;
    if (GetCapture() == window_) ReleaseCapture();
    hiddenByUser_ = true;
    emergencyHidden_ = hidingGame;
    ShowWindow(window_, SW_HIDE);
    LogEvent(hidingGame ? "game_hidden" : "pet_hidden");
  }

  void CloseRequested() {
    if (gameMode_ && GameFinishedVisible()) {
      ReturnToPetFromFinished();
    } else if (gameMode_) {
      EmergencyHide();
    } else {
      HideByUser();
    }
  }

  bool IsGameMode() const { return gameMode_; }
  bool IsStartupHealthy() const { return statePersistenceOk_ && gGdiPlusSession.ready(); }
  bool IsPairingSearch() const { return pairingAttempted_; }
  bool NeedsPairing() const { return awaitingPairing_ || pairingAttempted_; }
  bool IsPairingPanelVisible() const { return pairingPanelVisible_; }
  void TogglePairingPanel() {
    if (!NeedsPairing()) return;
    pairingPanelVisible_ = !pairingPanelVisible_;
    SaveSettingsOrNotify();
    InvalidateRect(window_, nullptr, FALSE);
  }
  bool HasBinding() const { return bindingId_ != 0; }
  bool IsUnbinding() const { return unbindPending_; }
  bool IsPeerOnline() const { return OpponentOnline(); }
  bool IsDoNotDisturb() const { return doNotDisturb_; }
  bool IsTelemetryEnabled() const {
    return telemetryEnabled_ &&
           (!telemetryUploadCapable_ || telemetryUploadChoice_ == 1);
  }
  bool UpdatesEnabled() const { return updateManager_.Enabled(); }
  bool IsPetHidden() const { return hiddenByUser_; }
  bool IsUpdateUiAvailable() const {
    return gModalDepth == 0 && !gameMode_ && !pairingAttempted_ &&
           Phase() == plink::GamePhase::Menu && pendingAction_ == 0 && !HasDndNotice();
  }
  plane_pet_update::Snapshot UpdateSnapshot() const {
    return updateManager_.GetSnapshot();
  }
#ifdef PLANE_PET_UPDATE_WINDOW_SELF_TEST
  plane_pet_update::Manager &UpdateManagerForTest() { return updateManager_; }
  void SetUpdateTestGamePhase(plink::GamePhase phase) {
    haveSnapshot_ = true;
    snapshot_.phase = phase;
    gameMode_ = phase != plink::GamePhase::Menu;
  }
#endif
  void ManualUpdateCheck() {
    if (!updateManager_.Enabled()) return;
    updateManager_.CheckNow(majorVersionMismatch_ && localUpdateRequired_);
    if (IsUpdateUiAvailable()) {
      if (hiddenByUser_) ShowPet();
      ShowUpdateWindow(window_, true);
    }
  }
  void AcceptUpdate() {
    const auto state = updateManager_.GetSnapshot();
    if (IsUpdateUiAvailable() && updateManager_.AcceptAndDownload())
      LogEvent("update_accepted", state.required ? 1 : 0);
  }
  void DeclineOrDismissUpdate() {
    const auto state = updateManager_.GetSnapshot();
    if (state.state == plane_pet_update::State::Available && !state.required) {
      updateSnoozeUntilMs_ =
          UnixTimeMillis() + 7ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
      updateManager_.SetOptionalSnoozeUntil(updateSnoozeUntilMs_);
      SaveSettingsOrNotify();
      LogEvent("update_declined", 7);
    } else {
      updateManager_.Dismiss();
    }
    CloseUpdateWindow();
  }
  void BeginUpdateInstall() {
    if (!IsUpdateUiAvailable() || !updateManager_.HasInstallRequest()) return;
    LogEvent("update_download_completed");
    LogEvent("update_install_started");
    gProcessExitCode = kUpdateInstallExitCode;
    DestroyWindow(window_);
  }
  void ToggleTelemetry() {
    if (IsTelemetryEnabled()) {
      EndUsageSegment();
      const bool saved = PersistTelemetryPreference(false);
      PetMessageBoxW(window_, saved
          ? L"已停止后续匿名统计与本地实验事件记录。"
          : L"本次运行已停止统计，但无法保存关闭设置。\n请解除文件占用或检查磁盘后再次关闭；重启前请确认保存成功。",
          L"匿名统计", MB_OK | (saved ? MB_ICONINFORMATION : MB_ICONWARNING));
      return;
    }
    if (telemetryUploadCapable_) {
      const int consent = PetMessageBoxW(
          window_,
          L"开启后通过加密连接发送启动/使用时长、匹配/在线变化、邀请、显示/隐藏/勿扰、固定表情类型、对局过程/结果及升级检查/选择/安装结果。\n\n不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题。原始匿名事件最长保留 90 天。",
          L"开启匿名统计", MB_YESNO | MB_ICONINFORMATION);
      if (consent != IDYES) return;
    }
    if (!PersistTelemetryPreference(true)) {
      PetMessageBoxW(window_, L"无法保存统计设置，本次未开启统计。请检查存档权限或文件占用。",
                  L"匿名统计", MB_OK | MB_ICONWARNING);
      return;
    }
    BeginUsageSegment();
    nextTelemetryHeartbeat_ = Clock::now() + std::chrono::seconds(60);
    LogEvent("session_heartbeat", gameMode_ ? 2 : (hiddenByUser_ ? 1 : 0));
  }
  bool CanInvite() const {
    return !gameMode_ && !awaitingPairing_ && !pairingAttempted_ &&
           !pairingCancelPending_ && pendingAction_ == 0 &&
           (!scopedActionsSupported_ || (haveRoundMeta_ && roundMetaPhase_ == Phase())) &&
           session_ != 0 && Phase() == plink::GamePhase::Menu &&
           OpponentOnline() && !majorVersionMismatch_;
  }
  bool CanSendQuickEmote() const { return CanInvite(); }
  bool IsPetToolbarAvailable() const {
    return !gameMode_ && !hiddenByUser_ && !awaitingPairing_ && !pairingAttempted_ &&
        !pairingCancelPending_ && !unbindPending_ && pendingAction_ == 0 &&
        Phase() == plink::GamePhase::Menu && gModalDepth == 0 &&
        (connectionNotice_.empty() || Clock::now() >= connectionNoticeUntil_);
  }
  bool IsPetToolbarVisible() const { return IsPetToolbarAvailable() && toolbar_.visible; }
  bool IsPetToolbarSurface(int x, int y) const {
    if (!IsPetToolbarVisible()) return false;
    const POINT point{x, y};
    if (PtInRect(&plane_pet_ui::kToolbarToggle, point)) return true;
    if (!toolbar_.expanded || !toolbar_.Settled(GetTickCount64()))
      return PtInRect(&plane_pet_ui::kToolbarPlay, point) != FALSE;
    for (int index = 0; index < 4; ++index) {
      const RECT button = QuickEmoteRect(index);
      if (PtInRect(&button, point)) return true;
    }
    return false;
  }
  bool IsQuickEmoteBarVisible() const {
    return IsPetToolbarVisible() && toolbar_.expanded && toolbar_.Settled(GetTickCount64());
  }
  void ResetPetToolbar() {
    toolbar_.Hide();
    if (petWindowDragging_) {
      petWindowDragging_ = false;
      if (GetCapture() == window_) ReleaseCapture();
    }
    if (toolbarPressed_) {
      toolbarPressed_ = 0;
      if (GetCapture() == window_) ReleaseCapture();
    }
  }
  void PollPetToolbar() {
    POINT cursor{};
    RECT rect{};
    const bool available = IsPetToolbarAvailable() && IsWindowVisible(window_);
    const bool inside = available && GetCursorPos(&cursor) &&
        ScreenToClient(window_, &cursor) && GetClientRect(window_, &rect) &&
        PtInRect(&rect, cursor);
    toolbar_.Observe(available, inside,
        (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, GetTickCount64());
    if (!toolbar_.visible && toolbarPressed_) {
      toolbarPressed_ = 0;
      if (GetCapture() == window_) ReleaseCapture();
    }
  }
  int ToolbarTargetAt(int x, int y) const {
    if (!IsPetToolbarVisible()) return 0;
    const POINT point{x, y};
    if (PtInRect(&plane_pet_ui::kToolbarToggle, point)) return 2;
    if (!toolbar_.Settled(GetTickCount64())) return 0;
    if (!toolbar_.expanded) return PtInRect(&plane_pet_ui::kToolbarPlay, point) ? 1 : 0;
    for (int index = 0; index < 4; ++index) {
      const RECT button = QuickEmoteRect(index);
      if (PtInRect(&button, point)) return index + 3;
    }
    return 0;
  }
  void InviteFromMenu() {
    if (!CanInvite() || (HasDndNotice() && dndNoticeCancelable_)) return;
    if (serverCapabilitiesKnown_ && (!motionSupported_ || !battleSupported_)) {
      ShowDndNotice(L"服务器需更新，请稍后再试", false);
      return;
    }
    ClearDndNotice();
    if (PeerDndKnown() && peerDnd_) {
      ShowDndNotice(L"对方已开启勿扰...", true);
      LogEvent("invite_blocked_dnd");
      return;
    }
    inviteFeedback_.clear();
    inviteFeedbackUntil_ = Clock::time_point{};
    outgoingInviteAt_ = Clock::now();
    LogEvent("invite_sent");
    QueueAction(plink::PlayerAction::Invite);
    dndAttemptOperation_ = pendingOperationId_;
    dndAttemptUntil_ = Clock::now() + std::chrono::seconds(6);
  }
  void StopPairingFromMenu() { StopPairing(); }
  void ToggleDoNotDisturb() {
    EndDndUsage();
    doNotDisturb_ = !doNotDisturb_;
    if (++dndRevision_ == 0) dndRevision_ = 1;
    nextDndSync_ = Clock::time_point{};
    const bool saved = SaveSettings();
    ShowDndNotice(saved ? (session_ != 0 && dndSupported_ ? L"状态同步中..." :
                          (doNotDisturb_ ? L"已开启勿扰" : L"已恢复接收邀请"))
                        : L"设置仅本次生效，保存失败", false);
    LogEvent(doNotDisturb_ ? "dnd_enabled" : "dnd_disabled");
    BeginDndUsage();
    if (session_ != 0 && dndSupported_) SendDndPreference();
    else if (doNotDisturb_ && IsIncomingInvite()) QueueAction(plink::PlayerAction::ReturnToMenu);
    InvalidateRect(window_, nullptr, FALSE);
  }

  bool PeerDndKnown() const {
    return dndSupported_ && peerDndKnown_ && OpponentOnline() &&
        Clock::now() - dndPresenceAt_ < std::chrono::seconds(5);
  }
  bool HasDndNotice() const {
    return !hiddenByUser_ && !gameMode_ && !((awaitingPairing_ || pairingAttempted_) && pairingPanelVisible_) &&
        (connectionNotice_.empty() || Clock::now() >= connectionNoticeUntil_) &&
        Phase() == plink::GamePhase::Menu && !dndNotice_.empty() && Clock::now() < dndNoticeUntil_;
  }
  void ClearDndNotice() {
    dndNotice_.clear();
    dndNoticeUntil_ = Clock::time_point{};
    dndNoticeCancelable_ = false;
  }
  void ShowDndNotice(const std::wstring &text, bool cancelable) {
    if (hiddenByUser_ || gameMode_) return;
    dndNotice_ = text;
    dndNoticeCancelable_ = cancelable;
    dndNoticeUntil_ = Clock::now() + std::chrono::seconds(3);
    inviteFeedback_.clear();
  }
  void ResetDndConnection() {
    motionSupported_ = serverCapabilitiesKnown_ = false;
    motionPredictor_ = pcmotion::Predictor{};
    motionTimeline_ = pcmotion::Timeline{};
    battlePredictor_ = pcbattle::Predictor{};
    battleEvents_ = pcbattle::Events{};
    battleSupported_ = false;
    battleStateSequence_ = 0;
    motionRound_ = 0;
    motionFault_ = syncFailed_ = false;
    dndSupported_ = false;
    dndAcknowledged_ = 0;
    peerDndKnown_ = peerDndCapable_ = peerDnd_ = false;
    dndPresenceAt_ = nextDndSync_ = Clock::time_point{};
    dndAttemptOperation_ = 0;
    lastDndInterruptedInvite_ = 0;
    ClearDndNotice();
  }
  void SendDndPreference() {
    if (!dndSupported_ || session_ == 0) return;
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), pcpair::kDndPreferenceType,
        session_, ++sequence_, lastServerSequence_, lastServerTick_);
    if (pcpair::WriteDndPreference(writer, {dndRevision_, doNotDisturb_})) SendRaw(bytes, writer.Finish());
  }
  void BeginDndUsage() {
    if (!telemetryEnabled_ || !telemetrySegmentOpen_ || !doNotDisturb_ || dndUsageOpen_) return;
    dndUsageOpen_ = true;
    dndUsageSince_ = Clock::now();
    LogEvent("dnd_usage_started", dndUsageMillis_);
  }
  int64_t DndUsageMillis() const { return dndUsageMillis_ + (dndUsageOpen_ ? ElapsedMillis(dndUsageSince_) : 0); }
  void EndDndUsage() {
    if (!dndUsageOpen_) return;
    dndUsageMillis_ = DndUsageMillis();
    dndUsageOpen_ = false;
    LogEvent("dnd_usage_ended", dndUsageMillis_);
  }

  std::array<plane_pet_ui::StatusBadge, 2> StatusBadges() const {
    const bool online = OpponentOnline();
    const auto elapsed = MillisSince(start_);
    const auto formation = PetFormationAt(elapsed);
    const auto blue = online ? formation.blue : PetPoseAt(elapsed);
    std::array<plane_pet_ui::StatusBadge, 2> result{};
    const auto place = [&](const PetAnimationPose &plane, bool own, bool moon, bool offline) {
      plane_pet_ui::StatusBadge badge;
      badge.visible = true;
      badge.moon = moon;
      badge.offline = offline;
      const int x = static_cast<int>(std::lround(plane.x));
      const int y = static_cast<int>(std::lround(plane.y));
      badge.center = {x - 19, y - 19};
      const bool emote = own ? (ownEmote_ && Clock::now() < ownEmoteUntil_)
                             : (peerEmote_ && Clock::now() < peerEmoteUntil_);
      if (emote) {
        const RECT bubble = QuickEmoteBubbleRect(x, y, own);
        badge.center = {bubble.right + (moon ? 2 : -3), bubble.bottom - (moon ? 9 : 11)};
      }
      badge.center.x = std::clamp<LONG>(badge.center.x, moon ? 9 : 5, kPetWidth - (moon ? 12 : 5));
      badge.center.y = std::clamp<LONG>(badge.center.y, moon ? 9 : 5, kPetHeight - (moon ? 12 : 5));
      return badge;
    };
    if (doNotDisturb_ || !online) result[0] = place(blue, true, doNotDisturb_, !online);
    if (online) result[1] = place(formation.red, false, PeerDndKnown() && peerDnd_, false);
    return result;
  }

  std::wstring StatusTooltip(int x, int y) const {
    const auto badges = StatusBadges();
    const POINT point{x, y};
    for (size_t index = 0; index < badges.size(); ++index) {
      const RECT bounds = badges[index].Bounds();
      if (!badges[index].visible || !PtInRect(&bounds, point)) continue;
      if (index == 0) {
        std::wstring text = doNotDisturb_ ? L"我已暂停接收邀请" : L"";
        if (!OpponentOnline()) text += text.empty() ? L"好友离线或尚未连接" : L" · 好友离线";
        if (doNotDisturb_ && session_ != 0) {
          if (!dndSupported_) text += L" · 服务器不支持状态同步";
          else if (dndAcknowledged_ != dndRevision_) text += L" · 状态同步中";
        }
        return text;
      }
      if (PeerDndKnown()) return peerDnd_ ? L"好友在线 · 勿扰中" : L"好友在线";
      return peerDndCapable_ ? L"好友在线 · 勿扰状态同步中" : L"好友在线 · 勿扰状态未知（旧版）";
    }
    return L"";
  }
  void ClearLocalData() {
    const int choice = PetMessageBoxW(
        window_,
        L"确定清除本机的历史战绩和匿名实验事件吗？\n\n绑定关系、设置和服务器已收到的匿名统计不会被删除。",
        L"清除本地战绩与统计", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES) return;
    const bool cleared = ClearLocalDataFiles();
    RefreshHistoryInfoWindow();
    PetMessageBoxW(window_, cleared ? L"本机战绩与实验事件已清除。"
        : L"部分本地数据未能清除。请解除文件占用或检查存档权限后重试。\n未能保存的战绩已在界面中保留。",
        L"Plane Pet", MB_OK | (cleared ? MB_ICONINFORMATION : MB_ICONWARNING));
  }

  bool ClearLocalDataFiles() {
    const auto recent = recentHistory_;
    const auto total = historyTotal_, wins = historyWins_, losses = historyLosses_, draws = historyDraws_;
    const auto lastRound = lastRecordedRoundId_;
    ResetHistory();
    historyPersistenceOk_ = SaveHistory();
    if (!historyPersistenceOk_) {
      recentHistory_ = recent;
      historyTotal_ = total; historyWins_ = wins; historyLosses_ = losses; historyDraws_ = draws;
      lastRecordedRoundId_ = lastRound;
    }
    bool removed = true;
    if (!eventsPath_.empty()) {
      for (const auto &path : {eventsPath_, std::filesystem::path(eventsPath_.wstring() + L".legacy")}) {
        std::error_code error;
        std::filesystem::remove(path, error);
        removed = removed && !error;
      }
    }
    return historyPersistenceOk_ && removed;
  }
  void ExportLocalData() {
    if (eventsPath_.empty() || !std::filesystem::exists(eventsPath_)) {
      PetMessageBoxW(window_,
                  L"当前没有可导出的匿名实验事件。\n如需参与统计，可在右键“设置与隐私 → 匿名使用统计”中开启。",
                  L"导出统计记录", MB_OK | MB_ICONINFORMATION);
      return;
    }
    wchar_t target[MAX_PATH] = L"PlanePet-events.csv";
    constexpr wchar_t filter[] =
        L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = target;
    dialog.nMaxFile = static_cast<DWORD>(std::size(target));
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                   OFN_NOCHANGEDIR;
    if (!PetGetSaveFileNameW(&dialog)) return;
    if (CopyFileW(eventsPath_.c_str(), target, FALSE) == FALSE) {
      PetMessageBoxW(window_, L"导出失败，请检查目标目录权限。",
                  L"导出统计记录", MB_OK | MB_ICONERROR);
      return;
    }
    PetMessageBoxW(window_, L"匿名实验事件已导出。", L"导出统计记录",
                MB_OK | MB_ICONINFORMATION);
  }
  void RequestUnbind() {
    if (bindingId_ == 0 || unbindPending_) return;
    const int choice = PetMessageBoxW(
        window_,
        L"确定解除当前电脑绑定吗？\n\n双方都会回到匹配码输入界面；本机历史战绩会保留。",
        L"解除绑定", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES) return;
    unbindPending_ = true;
    nextUnbind_ = Clock::now();
    LogEvent("unbind_requested");
  }
  const std::wstring &OwnName() const { return ownName_; }

#if defined(PLANE_PET_INFO_SELF_TEST) || defined(PLANE_PET_RELEASE_SELF_TEST)
  bool RunColoredHeartHudSelfTest() {
    const auto previous = snapshot_;
    const bool hadSnapshot = haveSnapshot_;
    haveSnapshot_ = true;
    snapshot_.phase = plink::GamePhase::Playing;
    snapshot_.players[0].health = plink::kInitialHealth;
    snapshot_.players[1].health = plink::kInitialHealth;
    HDC target = GetDC(window_);
    if (target == nullptr) {
      snapshot_ = previous;
      haveSnapshot_ = hadSnapshot;
      return false;
    }
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(
        target, plink::kWorldWidth, plink::kWorldHeight);
    if (memory == nullptr || bitmap == nullptr) {
      if (bitmap != nullptr) DeleteObject(bitmap);
      if (memory != nullptr) DeleteDC(memory);
      ReleaseDC(window_, target);
      snapshot_ = previous;
      haveSnapshot_ = hadSnapshot;
      return false;
    }
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    DrawGameHud(memory, 1, 0, 0, 0);
    GdiFlush();
    unsigned bluePixels = 0;
    unsigned redPixels = 0;
    for (int y = 12; y < 38; ++y) {
      for (int x = 22; x < 76; ++x) {
        const COLORREF pixel = GetPixel(memory, x, y);
        if (GetBValue(pixel) > 200 && GetGValue(pixel) > 100 && GetRValue(pixel) < 50) ++bluePixels;
      }
      for (int x = 184; x < 225; ++x) {
        const COLORREF pixel = GetPixel(memory, x, y);
        if (GetRValue(pixel) > 200 && GetGValue(pixel) < 110 && GetBValue(pixel) < 130) ++redPixels;
      }
    }
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window_, target);
    snapshot_ = previous;
    haveSnapshot_ = hadSnapshot;
    return bluePixels >= 20U && redPixels >= 20U;
  }

  bool RunFinishedCloseSelfTest() {
    haveSnapshot_ = true;
    snapshot_.phase = plink::GamePhase::Finished;
    pendingAction_ = 0;
    returnToPetRequested_ = false;
    hiddenByUser_ = false;
    SetGameMode();
    CloseRequested();
    RECT windowRect{};
    GetWindowRect(window_, &windowRect);
    const bool result = !gameMode_ && returnToPetRequested_ &&
        pendingAction_ ==
            static_cast<uint8_t>(plink::PlayerAction::ReturnToMenu) &&
        IsWindowVisible(window_) &&
        windowRect.right - windowRect.left == kPetWidth &&
        windowRect.bottom - windowRect.top == kPetHeight;
    snapshot_.phase = plink::GamePhase::Menu;
    pendingAction_ = 0;
    returnToPetRequested_ = false;
    SetPetMode(true);
    return result;
  }

  bool RunHistoryPersistenceSelfTest() {
    ResetHistory();
    haveSnapshot_ = true;
    snapshot_.players[0].health = 2;
    snapshot_.players[1].health = 1;
    snapshot_.winnerSlot = 1;
    slot_ = 1;
    historyRecordedForRound_ = false;
    currentRoundId_ = 1;
    RecordMatchHistory();
    slot_ = 2;
    historyRecordedForRound_ = false;
    currentRoundId_ = 2;
    RecordMatchHistory();
    for (uint8_t index = 0; index < 10; ++index) {
      slot_ = 1;
      snapshot_.players[0].health = static_cast<uint8_t>(index % 4U);
      snapshot_.players[1].health =
          static_cast<uint8_t>(plink::kInitialHealth - index % 4U);
      snapshot_.winnerSlot = 0;
      historyRecordedForRound_ = false;
      currentRoundId_ = static_cast<uint64_t>(index) + 3U;
      RecordMatchHistory();
    }
    if (!historyPersistenceOk_ || historyTotal_ != 12 ||
        historyWins_ != 1 || historyLosses_ != 1 || historyDraws_ != 10 ||
        recentHistory_.size() != 10 || recentHistory_.front().ownHealth != 1 ||
        recentHistory_.front().peerHealth != 2 ||
        recentHistory_.back().ownHealth != 0 ||
        recentHistory_.back().peerHealth != 3) {
      return false;
    }
    ResetHistory();
    LoadHistory();
    return historyTotal_ == 12 && historyWins_ == 1 &&
           historyLosses_ == 1 && historyDraws_ == 10 &&
           recentHistory_.size() == 10 &&
           recentHistory_.front().outcome == 0 &&
           recentHistory_.front().ownHealth == 1 &&
           recentHistory_.front().peerHealth == 2 &&
           recentHistory_.back().ownHealth == 0 &&
           recentHistory_.back().peerHealth == 3;
  }
#endif

#ifdef PLANE_PET_RENDER_SELF_TEST
  bool RenderGameBoundaryFixture(const std::filesystem::path &path) {
    slot_ = 2;
    haveSnapshot_ = true;
    predictionReady_ = false;
    snapshot_ = plink::SnapshotPayload{};
    snapshot_.phase = plink::GamePhase::Playing;
    snapshot_.onlineMask = 0x03U;
    snapshot_.phaseRemainingMs = 178000;
    snapshot_.players[0] = {
        plink::kWorldWidth / 2, plink::kPlayerOneMaxY,
        plink::kInitialHealth, 1};
    snapshot_.players[1] = {
        static_cast<int16_t>(plink::kWorldWidth - 1 -
                             plink::kWorldWidth / 2),
        plink::kPlayerTwoMinY, plink::kInitialHealth, 1};
    // Include one projectile from each side so the render fixture verifies
    // both pixel models and their local-view directions.
    snapshot_.bulletCount = 2;
    snapshot_.bullets[0] = {1, 2, 159, 99};
    snapshot_.bullets[1] = {2, 1, 79, 219};
    rttMs_ = 42;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = kGameClientWidth;
    bitmapInfo.bmiHeader.biHeight = -kGameClientHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HDC memory = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(
        memory, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (memory == nullptr || bitmap == nullptr || pixels == nullptr) {
      if (bitmap != nullptr) DeleteObject(bitmap);
      if (memory != nullptr) DeleteDC(memory);
      return false;
    }
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    DrawGameWindow(memory, kGameClientWidth, kGameClientHeight);
    GdiFlush();

    const DWORD imageBytes = static_cast<DWORD>(
        kGameClientWidth * kGameClientHeight * 4);
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + imageBytes;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    const bool ok = file != INVALID_HANDLE_VALUE &&
        WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) &&
        written == sizeof(fileHeader) &&
        WriteFile(file, &bitmapInfo.bmiHeader, sizeof(BITMAPINFOHEADER),
                  &written, nullptr) &&
        written == sizeof(BITMAPINFOHEADER) &&
        WriteFile(file, pixels, imageBytes, &written, nullptr) &&
        written == imageBytes && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    if (!ok) return false;
    return RenderPetOnlineFixture(path.parent_path() /
                                  L"pet_online_fixture.bmp");
  }

  bool ValidatePetFormationMotion(std::string *diagnostics = nullptr) {
    const uint32_t savedClientId = clientId_;
    constexpr uint32_t sampleClients[] = {1, 137, 499, 996};
    bool valid = true;
    for (const uint32_t sampleClient : sampleClients) {
      clientId_ = sampleClient;
      PetFormation previous = PetFormationAt(0);
      double minimumDistance = 10000.0;
      double maximumDistance = 0.0;
      double minimumX = 10000.0;
      double maximumX = -10000.0;
      double minimumY = 10000.0;
      double maximumY = -10000.0;
      double maximumFrameTravel = 0.0;
      double minimumForwardDirection = 1.0;
      double minimumRedTurn = 1.0;
      double maximumRedTurn = -1.0;
      double minimumBlueTurn = 1.0;
      double maximumBlueTurn = -1.0;
      int previousLateralSign = 0;
      int lateralCrossings = 0;
      bool sampleValid = true;
      bool stayedInside = true;
      for (uint32_t elapsed = 16; elapsed <= 120000; elapsed += 16) {
        const PetFormation current = PetFormationAt(elapsed);
        const auto inside = [](const PetAnimationPose &pose) {
          return pose.x >= 24.0 && pose.x <= 256.0 &&
                 pose.y >= 20.0 && pose.y <= 108.0;
        };
        if (!inside(current.red) || !inside(current.blue)) {
          sampleValid = false;
          stayedInside = false;
        }
        const auto frameTravel = [](const PetAnimationPose &a,
                                    const PetAnimationPose &b) {
          const double dx = a.x - b.x;
          const double dy = a.y - b.y;
          return std::sqrt(dx * dx + dy * dy);
        };
        maximumFrameTravel = std::max(
            maximumFrameTravel,
            std::max(frameTravel(current.red, previous.red),
                     frameTravel(current.blue, previous.blue)));
        minimumForwardDirection = std::min(
            minimumForwardDirection,
            std::min(current.red.directionX, current.blue.directionX));
        minimumRedTurn = std::min(minimumRedTurn, current.red.directionY);
        maximumRedTurn = std::max(maximumRedTurn, current.red.directionY);
        minimumBlueTurn = std::min(minimumBlueTurn, current.blue.directionY);
        maximumBlueTurn = std::max(maximumBlueTurn, current.blue.directionY);
        if (maximumFrameTravel > 3.0) {
          sampleValid = false;
        }
        const double separationX = current.blue.x - current.red.x;
        const double separationY = current.blue.y - current.red.y;
        const double distance = std::sqrt(
            separationX * separationX + separationY * separationY);
        minimumDistance = std::min(minimumDistance, distance);
        maximumDistance = std::max(maximumDistance, distance);
        minimumX = std::min(minimumX,
                            std::min(current.red.x, current.blue.x));
        maximumX = std::max(maximumX,
                            std::max(current.red.x, current.blue.x));
        minimumY = std::min(minimumY,
                            std::min(current.red.y, current.blue.y));
        maximumY = std::max(maximumY,
                            std::max(current.red.y, current.blue.y));
        const double lateral = separationX * (-current.red.directionY) +
                               separationY * current.red.directionX;
        const int lateralSign = lateral > 2.0 ? 1 : (lateral < -2.0 ? -1 : 0);
        if (lateralSign != 0 && previousLateralSign != 0 &&
            lateralSign != previousLateralSign) {
          ++lateralCrossings;
        }
        if (lateralSign != 0) previousLateralSign = lateralSign;
        previous = current;
      }
      if (minimumDistance < 96.0 || maximumDistance > 132.0 ||
          maximumDistance - minimumDistance < 12.0 || lateralCrossings < 3 ||
          minimumForwardDirection < 0.85 ||
          maximumRedTurn - minimumRedTurn < 0.45 ||
          maximumBlueTurn - minimumBlueTurn < 0.20) {
        sampleValid = false;
      }
      valid = valid && sampleValid;
      if (diagnostics != nullptr) {
        char line[320]{};
        std::snprintf(line, sizeof(line),
                      "client=%u ok=%u x=%.2f..%.2f y=%.2f..%.2f "
                      "distance=%.2f..%.2f step_max=%.2f inside=%u "
                      "forward_min=%.2f red_turn=%.2f..%.2f "
                      "blue_turn=%.2f..%.2f lateral_crossings=%d\n",
                      sampleClient, sampleValid ? 1U : 0U,
                      minimumX, maximumX, minimumY, maximumY,
                      minimumDistance, maximumDistance, maximumFrameTravel,
                      stayedInside ? 1U : 0U, minimumForwardDirection,
                      minimumRedTurn, maximumRedTurn,
                      minimumBlueTurn, maximumBlueTurn, lateralCrossings);
        diagnostics->append(line);
      }
    }
    clientId_ = savedClientId;
    return valid;
  }

  bool RenderPetOnlineFixture(const std::filesystem::path &path) {
    std::string motionDiagnostics;
    const bool motionValid = ValidatePetFormationMotion(&motionDiagnostics);
    std::ofstream(path.parent_path() / L"pet_formation_metrics.txt",
                  std::ios::trunc) << motionDiagnostics;
    if (!motionValid) return false;
    // Render a fixed patrol phase with a separated automatic projectile.
    start_ = Clock::now() - std::chrono::milliseconds(350);
    bindingId_ = 1;
    session_ = 1;
    haveSnapshot_ = true;
    snapshot_ = plink::SnapshotPayload{};
    snapshot_.phase = plink::GamePhase::Menu;
    snapshot_.onlineMask = 0x03U;
    ownEmote_ = 1;
    peerEmote_ = 4;
    ownEmoteStartedAt_ = peerEmoteStartedAt_ = Clock::now();
    ownEmoteUntil_ = peerEmoteUntil_ =
        Clock::now() + std::chrono::seconds(10);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = kPetWidth;
    bitmapInfo.bmiHeader.biHeight = -kPetHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HDC memory = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(
        memory, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (memory == nullptr || bitmap == nullptr || pixels == nullptr) {
      if (bitmap != nullptr) DeleteObject(bitmap);
      if (memory != nullptr) DeleteDC(memory);
      return false;
    }
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    RECT canvas{0, 0, kPetWidth, kPetHeight};
    Fill(memory, canvas, RGB(215, 223, 233));
    DrawPet(memory, kPetWidth, kPetHeight);
    GdiFlush();

    const DWORD imageBytes = static_cast<DWORD>(kPetWidth * kPetHeight * 4);
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + imageBytes;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    const bool ok = file != INVALID_HANDLE_VALUE &&
        WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) &&
        written == sizeof(fileHeader) &&
        WriteFile(file, &bitmapInfo.bmiHeader, sizeof(BITMAPINFOHEADER),
                  &written, nullptr) &&
        written == sizeof(BITMAPINFOHEADER) &&
        WriteFile(file, pixels, imageBytes, &written, nullptr) &&
        written == imageBytes && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    return ok;
  }
#endif

 private:
  struct EmbeddedImageResource {
    IStream *stream = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    bool attempted = false;

    ~EmbeddedImageResource() {
      bitmap.reset();
      if (stream != nullptr) stream->Release();
    }
    EmbeddedImageResource() = default;
    EmbeddedImageResource(const EmbeddedImageResource &) = delete;
    EmbeddedImageResource &operator=(const EmbeddedImageResource &) = delete;
  };

  static uint64_t UnixTimeMillis() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
  }

  static int64_t ElapsedMillis(const Clock::time_point &since) {
    if (since.time_since_epoch().count() == 0) return 0;
    return static_cast<int64_t>(std::chrono::duration_cast<
        std::chrono::milliseconds>(Clock::now() - since).count());
  }

  void SendTelemetryEvent(const char *event, int64_t value) {
    if (!telemetryUploadCapable_ || telemetryUploadChoice_ != 1 ||
        socket_ == INVALID_SOCKET || event == nullptr) {
      return;
    }
    char packet[768]{};
    const unsigned long long sequence = ++telemetryEventSequence_;
    const int length = std::snprintf(
        packet, sizeof(packet),
        "%s{\"v\":1,\"s\":\"%016llx\",\"q\":%llu,\"t\":%llu,"
        "\"a\":\"%s\",\"g\":%u,\"i\":\"%016llx\",\"r\":\"%016llx\","
        "\"e\":\"%s\",\"x\":%lld}",
        kTelemetryMagic,
        static_cast<unsigned long long>(telemetrySessionId_), sequence,
        static_cast<unsigned long long>(UnixTimeMillis()), kAppVersion,
        static_cast<unsigned>(plane_pet_version::kReleaseEpoch),
        static_cast<unsigned long long>(currentInviteId_),
        static_cast<unsigned long long>(currentRoundId_), event,
        static_cast<long long>(value));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(packet)) return;
    sendto(socket_, packet, length, 0,
           reinterpret_cast<const sockaddr *>(&server_), sizeof(server_));
  }

  void LogEvent(const char *event, int64_t value = 0) {
    if (!telemetryEnabled_ || event == nullptr) return;
    if (!eventsPath_.empty()) {
      std::error_code error;
      const auto parent = eventsPath_.parent_path();
      if (!parent.empty()) std::filesystem::create_directories(parent, error);
      constexpr const char *header =
          "timestamp_ms,client_id,app_version,invite_id,round_id,event,value";
      bool writeHeader = !std::filesystem::exists(eventsPath_, error) ||
                         std::filesystem::file_size(eventsPath_, error) == 0;
      if (!writeHeader) {
        std::ifstream existing(eventsPath_);
        std::string existingHeader;
        std::getline(existing, existingHeader);
        if (existingHeader != header) {
          existing.close();
          const std::filesystem::path legacy =
              eventsPath_.wstring() + L".legacy";
          std::filesystem::remove(legacy, error);
          error.clear();
          std::filesystem::rename(eventsPath_, legacy, error);
          if (!error) writeHeader = true;
        }
      }
      if (!error) {
        std::ofstream output(eventsPath_, std::ios::app);
        if (output) {
          if (writeHeader) output << header << '\n';
          output << UnixTimeMillis() << ',' << clientId_ << ',' << kAppVersion
                 << ',' << currentInviteId_ << ',' << currentRoundId_ << ','
                 << event << ',' << value << '\n';
        }
      }
    }
    SendTelemetryEvent(event, value);
  }

  int64_t EnabledUsageMillis() const {
    return telemetryActiveMillis_ + (telemetrySegmentOpen_ ? ElapsedMillis(telemetryActiveSince_) : 0);
  }

  void BeginUsageSegment() {
    if (!telemetryEnabled_ || telemetrySegmentOpen_) return;
    telemetryActiveSince_ = Clock::now();
    telemetrySegmentOpen_ = true;
    LogEvent("usage_started", telemetryActiveMillis_);
    BeginDndUsage();
  }

  void EndUsageSegment() {
    EndDndUsage();
    if (!telemetrySegmentOpen_) return;
    telemetryActiveMillis_ = EnabledUsageMillis();
    telemetrySegmentOpen_ = false;
    LogEvent("usage_ended", telemetryActiveMillis_);
  }

  bool WriteTelemetryOffMarker() const {
    const std::filesystem::path path = settingsPath_.wstring() + L".telemetry-off";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const char bytes[] = "disabled\n";
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes, sizeof(bytes) - 1, &written, nullptr) &&
                    written == sizeof(bytes) - 1 && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
  }

  bool PersistTelemetryPreference(bool enabled) {
    telemetryEnabled_ = enabled;
    telemetryChoiceKnown_ = true;
    if (telemetryUploadCapable_) telemetryUploadChoice_ = enabled ? 1 : 0;
    if (!enabled) {
      const bool marked = WriteTelemetryOffMarker();
      return SaveSettings() || marked;
    }
    if (SaveSettings()) {
      const auto marker = settingsPath_.wstring() + L".telemetry-off";
      if (DeleteFileW(marker.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    }
    // Never enable recording when its persistent consent state is uncertain.
    telemetryEnabled_ = false;
    if (telemetryUploadCapable_) telemetryUploadChoice_ = 0;
    WriteTelemetryOffMarker();
    SaveSettings();
    connectionNotice_ = L"统计未开启\n无法保存设置";
    connectionNoticeUntil_ = Clock::now() + std::chrono::seconds(15);
    return false;
  }

  void SaveSettingsOrNotify() {
    if (SaveSettings()) return;
    connectionNotice_ = L"设置仅本次生效\n无法保存，请重试";
    connectionNoticeUntil_ = Clock::now() + std::chrono::seconds(15);
  }

  void LoadSettings() {
    ReadSettingsFile();
    const auto marker = settingsPath_.wstring() + L".telemetry-off";
    const DWORD attributes = GetFileAttributesW(marker.c_str());
    const DWORD error = GetLastError();
    if (attributes != INVALID_FILE_ATTRIBUTES ||
        (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)) {
      telemetryEnabled_ = false;
      telemetryUploadChoice_ = 0;
      telemetryChoiceKnown_ = true;
    }
  }

  void ReadSettingsFile() {
    std::ifstream input(settingsPath_);
    std::string magic;
    uint32_t version = 0;
    int telemetry = -1;
    int upload = -1;
    int dnd = 0;
    int pairingPanel = 1;
    unsigned long long updateSnooze = 0;
    if (!(input >> magic >> version) || magic != "PLANE_PET_SETTINGS") return;
    if (version == 1 && input >> telemetry >> dnd &&
        telemetry >= 0 && telemetry <= 1 && dnd >= 0 && dnd <= 1) {
      telemetryChoiceKnown_ = true;
      telemetryEnabled_ = telemetry != 0;
      telemetryUploadChoice_ = -1;
      doNotDisturb_ = dnd != 0;
    } else if (version == 2 && input >> telemetry >> upload >> dnd &&
               telemetry >= 0 && telemetry <= 1 && upload >= -1 &&
               upload <= 1 && dnd >= 0 && dnd <= 1) {
      telemetryChoiceKnown_ = true;
      telemetryEnabled_ = telemetry != 0;
      telemetryUploadChoice_ = upload;
      doNotDisturb_ = dnd != 0;
      if (input >> pairingPanel && pairingPanel >= 0 && pairingPanel <= 1)
        pairingPanelVisible_ = pairingPanel != 0;
    } else if (version == 3 && input >> telemetry >> upload >> dnd >>
                   pairingPanel >> updateSnooze &&
               telemetry >= 0 && telemetry <= 1 && upload >= -1 &&
               upload <= 1 && dnd >= 0 && dnd <= 1 &&
               pairingPanel >= 0 && pairingPanel <= 1) {
      telemetryChoiceKnown_ = true;
      telemetryEnabled_ = telemetry != 0;
      telemetryUploadChoice_ = upload;
      doNotDisturb_ = dnd != 0;
      pairingPanelVisible_ = pairingPanel != 0;
      updateSnoozeUntilMs_ = updateSnooze;
    }
  }

  bool SaveSettings() const {
    std::error_code error;
    const auto parent = settingsPath_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    const std::filesystem::path temporary = settingsPath_.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "PLANE_PET_SETTINGS 3 " << (telemetryEnabled_ ? 1 : 0)
           << ' ' << telemetryUploadChoice_
           << ' ' << (doNotDisturb_ ? 1 : 0)
           << ' ' << (pairingPanelVisible_ ? 1 : 0)
           << ' ' << updateSnoozeUntilMs_ << '\n';
    output.close();
    if (!output) return false;
    return MoveFileExW(temporary.c_str(), settingsPath_.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
           FALSE;
  }

  static std::filesystem::path DefaultStatePath(bool bob) {
    wchar_t localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData,
        static_cast<DWORD>(std::size(localAppData)));
    std::filesystem::path root = length > 0 && length < std::size(localAppData)
                                     ? std::filesystem::path(localAppData)
                                     : std::filesystem::current_path();
    return root / L"PlanePet" / (bob ? L"bob.binding" : L"alice.binding");
  }

  void ResetHistory() {
    historyTotal_ = 0;
    historyWins_ = 0;
    historyLosses_ = 0;
    historyDraws_ = 0;
    lastRecordedRoundId_ = 0;
    recentHistory_.clear();
  }

  void LoadHistory() {
    std::ifstream input(historyPath_);
    if (!input) return;
    std::string magic;
    uint32_t version = 0;
    uint32_t total = 0;
    uint32_t wins = 0;
    uint32_t losses = 0;
    uint32_t draws = 0;
    if (!(input >> magic >> version >> total >> wins >> losses >> draws) ||
        magic != "PLANE_PET_HISTORY" || (version != 1 && version != 2) ||
        static_cast<uint64_t>(wins) + losses + draws != total) {
      ResetHistory();
      return;
    }
    uint64_t lastRecordedRoundId = 0;
    if (version == 2 && !(input >> lastRecordedRoundId)) {
      ResetHistory();
      return;
    }
    std::vector<HistoryEntry> entries;
    uint64_t timestamp = 0;
    uint32_t ownHealth = 0;
    uint32_t peerHealth = 0;
    int outcome = 0;
    while (input >> timestamp >> ownHealth >> peerHealth >> outcome) {
      if (ownHealth > plink::kInitialHealth ||
          peerHealth > plink::kInitialHealth || outcome < -1 || outcome > 1) {
        ResetHistory();
        return;
      }
      if (entries.size() < 10) {
        entries.push_back({timestamp, static_cast<uint8_t>(ownHealth),
                           static_cast<uint8_t>(peerHealth),
                           static_cast<int8_t>(outcome)});
      }
    }
    if (!input.eof()) {
      ResetHistory();
      return;
    }
    historyTotal_ = total;
    historyWins_ = wins;
    historyLosses_ = losses;
    historyDraws_ = draws;
    lastRecordedRoundId_ = lastRecordedRoundId;
    recentHistory_ = std::move(entries);
  }

  bool SaveHistory() const {
    std::error_code error;
    const auto parent = historyPath_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    const std::filesystem::path temporary =
        historyPath_.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "PLANE_PET_HISTORY 2 " << historyTotal_ << ' '
           << historyWins_ << ' ' << historyLosses_ << ' '
           << historyDraws_ << ' ' << lastRecordedRoundId_ << '\n';
    for (const HistoryEntry &entry : recentHistory_) {
      output << entry.timestampMs << ' '
             << static_cast<uint32_t>(entry.ownHealth) << ' '
             << static_cast<uint32_t>(entry.peerHealth) << ' '
             << static_cast<int>(entry.outcome) << '\n';
    }
    output.close();
    if (!output) return false;
    return MoveFileExW(temporary.c_str(), historyPath_.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
           FALSE;
  }

  void RecordMatchHistory() {
    if (historyRecordedForRound_ || currentRoundId_ == 0 ||
        currentRoundId_ == lastRecordedRoundId_ || !haveSnapshot_ ||
        slot_ < 1 || slot_ > 2)
      return;
    if (syncFailed_) {
      // An invalidated simulation is not a competitive draw/loss. Remember the
      // round for deduplication without changing existing history's schema.
      historyRecordedForRound_ = true;
      lastRecordedRoundId_ = currentRoundId_;
      historyPersistenceOk_ = SaveHistory();
      return;
    }
    historyRecordedForRound_ = true;
    const uint8_t viewer = static_cast<uint8_t>(slot_ - 1);
    const uint8_t ownHealth = std::min(
        snapshot_.players[viewer].health, plink::kInitialHealth);
    const uint8_t peerHealth = std::min(
        snapshot_.players[1U - viewer].health, plink::kInitialHealth);
    const int8_t outcome = snapshot_.winnerSlot == 0
        ? 0 : (snapshot_.winnerSlot == slot_ ? 1 : -1);
    AppendHistoryEntry(outcome, ownHealth, peerHealth);
  }

  void RecordAbandonedMatch() {
    if (!haveSnapshot_ ||
        (snapshot_.phase != plink::GamePhase::Countdown &&
         snapshot_.phase != plink::GamePhase::Playing) ||
        historyRecordedForRound_ || currentRoundId_ == 0 ||
        currentRoundId_ == lastRecordedRoundId_ || slot_ < 1 || slot_ > 2 ||
        syncFailed_) {
      return;
    }
    historyRecordedForRound_ = true;
    const uint8_t viewer = static_cast<uint8_t>(slot_ - 1);
    const uint8_t ownHealth = std::min(
        snapshot_.players[viewer].health, plink::kInitialHealth);
    const uint8_t peerHealth = std::min(
        snapshot_.players[1U - viewer].health, plink::kInitialHealth);
    AppendHistoryEntry(-1, ownHealth, peerHealth);
    LogEvent("game_abandoned");
  }

  void AppendHistoryEntry(int8_t outcome, uint8_t ownHealth,
                          uint8_t peerHealth) {
    lastRecordedRoundId_ = currentRoundId_;
    ++historyTotal_;
    if (outcome > 0) ++historyWins_;
    else if (outcome < 0) ++historyLosses_;
    else ++historyDraws_;
    recentHistory_.insert(recentHistory_.begin(),
                          {UnixTimeMillis(), ownHealth, peerHealth, outcome});
    if (recentHistory_.size() > 10) recentHistory_.resize(10);
    historyPersistenceOk_ = SaveHistory();
    LogEvent("history_recorded", outcome);
  }

  static std::wstring FormatHistoryTime(uint64_t unixMs) {
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = unixMs * 10000ULL + 116444736000000000ULL;
    FILETIME fileTime{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&fileTime, &utc) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
      return L"--";
    }
    wchar_t text[24]{};
    swprintf(text, std::size(text), L"%02u-%02u %02u : %02u",
             local.wMonth, local.wDay, local.wHour, local.wMinute);
    return text;
  }

  bool LoadStateFrom(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) return false;
    std::string magic;
    uint32_t version = 0;
    uint32_t loadedClientId = 0;
    uint32_t loadedBindingId = 0;
    uint32_t loadedTokenLow = 0;
    uint32_t loadedTokenHigh = 0;
    uint32_t loadedPeerDeviceId = 0;
    uint32_t storedSlot = 0;
    if (!(input >> magic >> version >> loadedClientId >> loadedBindingId >>
          loadedTokenLow >> loadedTokenHigh >> loadedPeerDeviceId >>
          storedSlot) || magic != "PLANE_PET_CLIENT" || version != 1 ||
        loadedClientId == 0 || storedSlot < 1 || storedSlot > 2) {
      return false;
    }
    input >> std::ws;
    if (!input.eof()) return false;
    const bool unbound = loadedBindingId == 0 && loadedTokenLow == 0 &&
                         loadedTokenHigh == 0 && loadedPeerDeviceId == 0;
    const bool bound = loadedBindingId != 0 && loadedTokenLow != 0 &&
                       loadedTokenHigh != 0 && loadedPeerDeviceId != 0 &&
                       loadedPeerDeviceId != loadedClientId;
    if (!unbound && !bound) return false;
    clientId_ = loadedClientId;
    bindingId_ = loadedBindingId;
    tokenLow_ = loadedTokenLow;
    tokenHigh_ = loadedTokenHigh;
    peerDeviceId_ = loadedPeerDeviceId;
    slot_ = static_cast<uint8_t>(storedSlot);
    return true;
  }

  void LoadState() {
    std::error_code error;
    const bool primaryExists = std::filesystem::exists(statePath_, error);
    if (primaryExists && LoadStateFrom(statePath_)) return;
    if (primaryExists) {
      const std::filesystem::path corrupt = statePath_.wstring() + L".corrupt";
      MoveFileExW(statePath_.c_str(), corrupt.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    const std::filesystem::path backup = statePath_.wstring() + L".bak";
    if (LoadStateFrom(backup)) {
      stateRecoveredFromBackup_ = true;
      SaveState();
    }
  }

  bool SaveState() const {
    std::error_code error;
    const auto parent = statePath_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    const std::filesystem::path temporary = statePath_.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "PLANE_PET_CLIENT 1 " << clientId_ << ' ' << bindingId_ << ' '
           << tokenLow_ << ' ' << tokenHigh_ << ' ' << peerDeviceId_ << ' '
           << static_cast<uint32_t>(slot_) << '\n';
    output.close();
    if (!output) return false;
    if (!MoveFileExW(temporary.c_str(), statePath_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      return false;
    }
    const std::filesystem::path backup = statePath_.wstring() + L".bak";
    const std::filesystem::path backupTemporary =
        statePath_.wstring() + L".bak.tmp";
    if (!CopyFileW(statePath_.c_str(), backupTemporary.c_str(), FALSE) ||
        !MoveFileExW(backupTemporary.c_str(), backup.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      MoveFileExW(backup.c_str(),
                  (statePath_.wstring() + L".bak.stale").c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    return true;
  }

  void ClearBinding() {
    ResetDndConnection();
    bindingId_ = 0;
    tokenLow_ = 0;
    tokenHigh_ = 0;
    peerDeviceId_ = 0;
    session_ = 0;
    lastServerSequence_ = 0;
    lastActionSequence_ = 0;
    lastServerTick_ = 0;
    haveSnapshot_ = false;
    currentRoundId_ = 0;
    currentInviteId_ = 0;
    historyRecordedForRound_ = false;
    unbindPending_ = false;
    unbindRequestId_ = 0;
    resumeRequestId_ = SecureRandomU32();
    scopedActionsSupported_ = false;
    haveRoundMeta_ = false;
    pendingAction_ = 0;
  }

  void BeginPairing(uint32_t code) {
    if (pairingCancelPending_) {
      pairingError_ = L"正在确认停止上次匹配，请稍候";
      return;
    }
    pairingCode_ = code;
    pairingRequestId_ = SecureRandomU32();
    const std::filesystem::path journal = statePath_.wstring() + L".pairing";
    const std::filesystem::path temporary = journal.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    output << pairingRequestId_ << '\n';
    output.close();
    if (!output || !MoveFileExW(temporary.c_str(), journal.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      pairingError_ = L"无法保存匹配请求，请检查目录权限";
      pairingRequestId_ = 0;
      pairingCode_ = 0;
      awaitingPairing_ = true;
      return;
    }
    pairingAttempted_ = true;
    awaitingPairing_ = false;
    serverConfirmedWaiting_ = false;
    pairingSubmittedAt_ = Clock::now();
    nextPairing_ = pairingSubmittedAt_;
    pairingError_.clear();
    session_ = 0;
    haveSnapshot_ = false;
    LogEvent("pairing_started");
    if (window_ != nullptr) SetPetMode(true);
  }

  void StopPairing() {
    if (!pairingAttempted_) return;
    for (int i = 0; i < 3; ++i) SendPairCancel();
    pairingAttempted_ = false;
    pairingCancelPending_ = true;
    lastCancelledRequest_ = pairingRequestId_;
    awaitingPairing_ = true;
    serverConfirmedWaiting_ = false;
    pairingCode_ = 0;
    pairingInput_.clear();
    pairingError_ = L"正在确认停止匹配，请稍候";
    LogEvent("pairing_stopped", ElapsedMillis(pairingSubmittedAt_));
    SetPetMode(true);
    InvalidateRect(window_, nullptr, FALSE);
  }

  bool ResolveServer() {
    const size_t colon = serverText_.rfind(':');
    const std::string host = colon == std::string::npos
                                 ? serverText_
                                 : serverText_.substr(0, colon);
    const std::string port = colon == std::string::npos
                                 ? "32110"
                                 : serverText_.substr(colon + 1);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) return false;
    memcpy(&server_, result->ai_addr, sizeof(server_));
    freeaddrinfo(result);
    return true;
  }

  bool SendRaw(const uint8_t *bytes, size_t length) {
    uint8_t datagram[pcpair::kMaxDatagramSize]{};
    if (bytes == nullptr || length > plink::kMaxPacketSize) return false;
    memcpy(datagram, bytes, length);
    if (!pcpair::AppendPacketAuth(datagram, length, sizeof(datagram),
                                  networkKey_)) {
      return false;
    }
    return sendto(socket_, reinterpret_cast<const char *>(datagram),
                  static_cast<int>(length), 0,
                  reinterpret_cast<const sockaddr *>(&server_), sizeof(server_)) ==
           static_cast<int>(length);
  }

  void SendControl(pcpair::Message message) {
    uint8_t bytes[pcpair::kMessageSize]{};
    message.deviceId = clientId_;
    message.appVersion = pcpair::CurrentAppVersion();
    message.compatibilityFlags |= pcpair::kDndSupported | pcmotion::kSupported;
    if (pcpair::Serialize(message, bytes, sizeof(bytes), networkKey_)) {
      sendto(socket_, reinterpret_cast<const char *>(bytes), sizeof(bytes), 0,
             reinterpret_cast<const sockaddr *>(&server_), sizeof(server_));
    }
  }

  void SendPairStart() {
    pcpair::Message message;
    message.type = pcpair::MessageType::Start;
    message.requestId = pairingRequestId_;
    message.pairingCode = pairingCode_;
    SendControl(message);
  }

  void SendPairCancel() {
    pcpair::Message message;
    message.type = pcpair::MessageType::Cancel;
    message.requestId = pairingRequestId_;
    message.pairingCode = pairingCode_;
    SendControl(message);
  }

  void SendResume() {
    pcpair::Message message;
    message.type = pcpair::MessageType::Resume;
    message.requestId = resumeRequestId_;
    message.bindingId = bindingId_;
    message.tokenLow = tokenLow_;
    message.tokenHigh = tokenHigh_;
    SendControl(message);
  }

  void SendGoodbye() {
    pcpair::Message message;
    message.type = pcpair::MessageType::Goodbye;
    message.bindingId = bindingId_;
    message.tokenLow = tokenLow_;
    message.tokenHigh = tokenHigh_;
    SendControl(message);
  }

  void SendUnbind() {
    if (bindingId_ == 0) return;
    pcpair::Message message;
    message.type = pcpair::MessageType::Unbind;
    if (unbindRequestId_ == 0) unbindRequestId_ = SecureRandomU32();
    message.requestId = unbindRequestId_;
    message.bindingId = bindingId_;
    message.tokenLow = tokenLow_;
    message.tokenHigh = tokenHigh_;
    SendControl(message);
  }

  void SendInput(uint32_t nowMs) {
    if (battleSupported_) {
      if (Phase() != plink::GamePhase::Playing || !battlePredictor_.ready) return;
      uint8_t bytes[plink::kMaxPacketSize]{};
      if (motionFault_) {
        plink::PacketWriter writer(bytes, sizeof(bytes), pcmotion::kAbortType,
            session_, ++sequence_, lastServerSequence_, lastServerTick_);
        if (writer.U32(static_cast<uint32_t>(motionRound_)) &&
            writer.U32(static_cast<uint32_t>(motionRound_ >> 32))) SendRaw(bytes, writer.Finish());
        return;
      }
      pcbattle::Batch batch; batch.round = motionRound_;
      batch.first = battlePredictor_.confirmed.tick + 1;
      batch.count = static_cast<uint8_t>(std::min<uint32_t>(pcmotion::kBatchLimit,
          battlePredictor_.tick - battlePredictor_.confirmed.tick));
      if (!batch.count) return;
      for (unsigned i = 0; i < batch.count; ++i)
        if (!battlePredictor_.inputs[slot_ - 1].Get(batch.first + i, batch.commands[i])) { motionFault_ = true; return; }
      plink::PacketWriter writer(bytes, sizeof(bytes), pcbattle::kInputType,
          session_, ++sequence_, lastServerSequence_, lastServerTick_);
      if (pcbattle::WriteBatch(writer, batch)) SendRaw(bytes, writer.Finish());
      return;
    }
    if (motionSupported_) {
      if (Phase() != plink::GamePhase::Playing || !motionPredictor_.ready) return;
      uint8_t bytes[plink::kMaxPacketSize]{};
      if (motionFault_) {
        plink::PacketWriter writer(bytes, sizeof(bytes), pcmotion::kAbortType,
            session_, ++sequence_, lastServerSequence_, lastServerTick_);
        if (writer.U32(static_cast<uint32_t>(motionRound_)) &&
            writer.U32(static_cast<uint32_t>(motionRound_ >> 32))) SendRaw(bytes, writer.Finish());
        return;
      }
      pcmotion::Batch batch;
      batch.round = motionRound_;
      batch.first = motionPredictor_.acknowledged + 1;
      batch.count = static_cast<uint8_t>(std::min<size_t>(pcmotion::kBatchLimit,
          motionPredictor_.sequence - motionPredictor_.acknowledged));
      if (!batch.count) return;
      for (unsigned i = 0; i < batch.count; ++i)
        batch.commands[i] = motionPredictor_.pending[(batch.first + i) % pcmotion::kPendingLimit];
      plink::PacketWriter writer(bytes, sizeof(bytes), pcmotion::kInputType,
          session_, ++sequence_, lastServerSequence_, lastServerTick_);
      if (pcmotion::WriteBatch(writer, batch)) SendRaw(bytes, writer.Finish());
      return;
    }
    inputHistory_[2] = inputHistory_[1];
    inputHistory_[1] = inputHistory_[0];
    inputHistory_[0] = currentInput_;
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Input,
                               session_, ++sequence_, lastServerSequence_,
                               lastServerTick_);
    plink::InputPayload input;
    input.clientTimeMs = nowMs;
    input.current = inputHistory_[0];
    input.previous1 = inputHistory_[1];
    input.previous2 = inputHistory_[2];
    if (plink::WriteInput(writer, input)) SendRaw(bytes, writer.Finish());
  }

  void SendPing(uint32_t nowMs) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Ping,
                               session_, ++sequence_, lastServerSequence_,
                               lastServerTick_);
    if (plink::WriteTimestamp(writer, nowMs)) SendRaw(bytes, writer.Finish());
  }

  void SendAction(plink::PlayerAction action) {
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), plink::PacketType::Action,
                               session_, ++sequence_, lastServerSequence_,
                               lastServerTick_);
    plink::ActionPayload payload;
    payload.action = action;
    if (plink::WriteAction(writer, payload)) SendRaw(bytes, writer.Finish());
  }

  static RECT QuickEmoteRect(int index) {
    return plane_pet_ui::ToolbarEmoteRect(index);
  }

  static plink::PlayerAction QuickEmoteAction(int index) {
    return static_cast<plink::PlayerAction>(
        static_cast<uint8_t>(plink::PlayerAction::EmoteLaugh) +
        static_cast<uint8_t>(std::clamp(index, 0, 3)));
  }

  void SendQuickEmote(int index) {
    if (!CanSendQuickEmote()) return;
    const auto now = Clock::now();
    if (lastEmoteSentAt_.time_since_epoch().count() != 0 &&
        now - lastEmoteSentAt_ < std::chrono::milliseconds(700)) {
      return;
    }
    const plink::PlayerAction action = QuickEmoteAction(index);
    SendAction(action);
    lastEmoteSentAt_ = now;
    toolbar_.EmoteSent(GetTickCount64());
    ownEmote_ = plink::QuickEmoteValue(action);
    ownEmoteStartedAt_ = now;
    ownEmoteUntil_ = now +
        std::chrono::milliseconds(kEmoteDisplayMilliseconds);
    LogEvent("quick_emote_sent", ownEmote_);
    InvalidateRect(window_, nullptr, FALSE);
  }

  void QueueAction(plink::PlayerAction action) {
    if ((action == plink::PlayerAction::Invite || action == plink::PlayerAction::Accept) &&
        serverCapabilitiesKnown_ && !battleSupported_) {
      ShowDndNotice(L"服务器需更新，请稍后再试", false);
      return;
    }
    if (scopedActionsSupported_ &&
        (!haveRoundMeta_ || roundMetaPhase_ != Phase())) return;
    pendingAction_ = static_cast<uint8_t>(action);
    pendingOperationId_ = SecureRandomU32();
    pendingActionContext_ = {currentRoundId_, currentInviteId_, Phase()};
    pendingActionStarted_ = Clock::now();
    nextAction_ = Clock::now();
  }

  void SendPendingAction() {
    if (!scopedActionsSupported_) {
      SendAction(static_cast<plink::PlayerAction>(pendingAction_));
      return;
    }
    uint8_t bytes[plink::kMaxPacketSize]{};
    plink::PacketWriter writer(bytes, sizeof(bytes), pcpair::kScopedActionPacketType,
                               session_, ++sequence_, lastServerSequence_, lastServerTick_);
    pcpair::ScopedAction action;
    action.operationId = pendingOperationId_;
    action.action = static_cast<plink::PlayerAction>(pendingAction_);
    action.context = pendingActionContext_;
    if (pcpair::WriteScopedAction(writer, action)) SendRaw(bytes, writer.Finish());
  }

  void FinishPairCancellation() {
    lastCancelledRequest_ = pairingRequestId_;
    pairingCancelPending_ = false;
    pairingAttempted_ = false;
    awaitingPairing_ = bindingId_ == 0;
    serverConfirmedWaiting_ = false;
    pairingCode_ = pairingRequestId_ = 0;
    DeleteFileW((statePath_.wstring() + L".pairing").c_str());
    pairingError_ = L"已停止匹配，可以重新输入";
  }

  void QueueReturn() {
    QueueAction(plink::PlayerAction::ReturnToMenu);
  }

  void ReturnToPetFromFinished() {
    if (!GameFinishedVisible()) return;
    mouseDragging_ = false;
    currentInput_ = 0;
    if (GetCapture() == window_) ReleaseCapture();
    if (gameMode_ && gameEffects_.Finishing(FxSeconds(Clock::now()))) {
      fxReturnPending_ = true;
      return;
    }
    fxReturnPending_ = false;
    if (fxDeferredMenu_) { CompleteServerMenuReturn(); return; }
    hiddenByUser_ = false;
    emergencyHidden_ = false;
    returnToPetRequested_ = true;
    QueueReturn();
    SetPetMode(true);
  }

  void SubmitPairingCode() {
    if (!statePersistenceOk_) {
      statePersistenceOk_ = SaveState();
      if (!statePersistenceOk_) {
        pairingError_ = L"无法写入身份存档，请检查存档目录权限";
        return;
      }
    }
    if (pairingInput_.size() != 6) {
      pairingError_ = L"请输入完整的 6 位匹配码";
      return;
    }
    uint32_t code = 0;
    for (wchar_t digit : pairingInput_)
      code = code * 10U + static_cast<uint32_t>(digit - L'0');
    if (code == 0) {
      pairingError_ = L"匹配码不能全部为 0";
      return;
    }
    lastPacketAt_ = Clock::time_point{};
    BeginPairing(code);
  }

  void HandleControl(const pcpair::Message &message) {
    if (message.type != pcpair::MessageType::Status ||
        message.deviceId != clientId_) return;
    lastPacketAt_ = Clock::now();
    const auto applyPeerVersion = [&]() {
      serverCapabilitiesKnown_ = true;
      motionSupported_ = (message.compatibilityFlags & pcmotion::kSupported) != 0;
      scopedActionsSupported_ =
          (message.compatibilityFlags & pcpair::ScopedActionsSupported) != 0;
      dndSupported_ = (message.compatibilityFlags & pcpair::kDndSupported) != 0;
      const bool wasForcedUpdate =
          majorVersionMismatch_ && localUpdateRequired_;
      peerAppVersion_ = message.appVersion;
      peerVersionKnown_ =
          (message.compatibilityFlags & pcpair::PeerVersionKnown) != 0 &&
          message.appVersion.Known();
      peerVersionDiffers_ = peerVersionKnown_ &&
          (message.appVersion.releaseEpoch != plane_pet_version::kReleaseEpoch ||
           message.appVersion.major != plane_pet_version::kMajor ||
           message.appVersion.minor != plane_pet_version::kMinor ||
           message.appVersion.patch != plane_pet_version::kPatch);
      const bool mismatch =
          (message.compatibilityFlags & pcpair::MajorMismatch) != 0;
      if (mismatch != majorVersionMismatch_) {
        majorVersionMismatch_ = mismatch;
        LogEvent(mismatch ? "peer_version_mismatch"
                          : "peer_version_compatible",
                 peerVersionKnown_ ? message.appVersion.major : 0);
      }
      localUpdateRequired_ =
          (message.compatibilityFlags & pcpair::LocalUpdateRequired) != 0;
      if (mismatch && localUpdateRequired_ && !wasForcedUpdate)
        LogEvent("forced_update_required", message.appVersion.major);
    };
    if (message.status == pcpair::Status::Matched && message.bindingId != 0 &&
        ((pairingCancelPending_ && message.requestId == pairingRequestId_) ||
         (lastCancelledRequest_ != 0 && message.requestId == lastCancelledRequest_)) &&
        message.tokenLow != 0 && message.tokenHigh != 0) {
      // Also cleans up a late Matched reply from a legacy server.
      pcpair::Message revoke = message;
      revoke.type = pcpair::MessageType::Unbind;
      for (int attempt = 0; attempt < 3; ++attempt) SendControl(revoke);
      return;
    }
    if (pairingCancelPending_ && message.requestId == pairingRequestId_ &&
        (message.status == pcpair::Status::Cancelled ||
         message.status == pcpair::Status::Unbound)) {
      FinishPairCancellation();
      return;
    }
    if (message.status == pcpair::Status::Waiting && pairingAttempted_ &&
        message.requestId == pairingRequestId_) {
      serverConfirmedWaiting_ = true;
    } else if (message.status == pcpair::Status::Matched &&
               pairingAttempted_ && message.requestId == pairingRequestId_ &&
               message.bindingId != 0 && message.assignedSlot >= 1 &&
               message.assignedSlot <= 2) {
      bindingId_ = message.bindingId;
      resumeRequestId_ = SecureRandomU32();
      applyPeerVersion();
      tokenLow_ = message.tokenLow;
      tokenHigh_ = message.tokenHigh;
      peerDeviceId_ = message.peerDeviceId;
      slot_ = message.assignedSlot;
      pairingAttempted_ = false;
      awaitingPairing_ = false;
      serverConfirmedWaiting_ = false;
      pairingCode_ = 0;
      pairingInput_.clear();
      pairingError_.clear();
      nextPairing_ = Clock::now();
      statePersistenceOk_ = SaveState();
      if (!statePersistenceOk_) {
        for (int attempt = 0; attempt < 3; ++attempt) SendUnbind();
        ClearBinding();
        awaitingPairing_ = true;
        pairingInput_.clear();
        pairingError_ = L"无法保存绑定，请检查存档目录权限";
        SetPetMode(true);
        return;
      }
      LogEvent("pairing_matched", slot_);
      DeleteFileW((statePath_.wstring() + L".pairing").c_str());
      SetPetMode(true);
      SendResume();
    } else if (message.status == pcpair::Status::Matched &&
               bindingId_ != 0 && message.bindingId == bindingId_) {
      applyPeerVersion();
      if (majorVersionMismatch_) {
        connectionNotice_ = peerAppVersion_.releaseEpoch != plane_pet_version::kReleaseEpoch
            ? L"好友仍在使用测试版\n请手动安装正式版"
            : localUpdateRequired_
            ? L"双方大版本不同\n请检查软件更新"
            : L"好友版本较旧\n暂不能邀请对战";
        connectionNoticeUntil_ = Clock::now() + std::chrono::seconds(12);
      }
    } else if (message.status == pcpair::Status::Cancelled &&
                pairingAttempted_ && message.requestId == pairingRequestId_) {
      pairingAttempted_ = false;
      awaitingPairing_ = true;
      serverConfirmedWaiting_ = false;
      pairingCode_ = 0;
      pairingRequestId_ = 0;
      DeleteFileW((statePath_.wstring() + L".pairing").c_str());
    } else if ((message.status == pcpair::Status::Invalid ||
                message.status == pcpair::Status::AlreadyBound) &&
               pairingAttempted_ && message.requestId == pairingRequestId_) {
      pairingAttempted_ = false;
      awaitingPairing_ = true;
      serverConfirmedWaiting_ = false;
      pairingCode_ = 0;
      pairingRequestId_ = 0;
      pairingInput_.clear();
      DeleteFileW((statePath_.wstring() + L".pairing").c_str());
      pairingError_ = message.status == pcpair::Status::AlreadyBound
                          ? L"该设备已经绑定，请保留身份存档"
                          : L"匹配请求无效，请重新输入";
    } else if (message.status == pcpair::Status::StorageError &&
               ((pairingAttempted_ && message.requestId == pairingRequestId_) ||
                (unbindPending_ && message.requestId == unbindRequestId_))) {
      const bool wasUnbinding = unbindPending_;
      pairingAttempted_ = false;
      unbindPending_ = false;
      serverConfirmedWaiting_ = false;
      if (!wasUnbinding) {
        awaitingPairing_ = true;
        pairingCode_ = 0;
        pairingRequestId_ = 0;
        pairingInput_.clear();
      }
      pairingError_ = L"服务器无法保存绑定，请稍后重试";
      if (wasUnbinding) {
        connectionNotice_ = L"解除绑定失败\n服务器存档不可写";
        connectionNoticeUntil_ = Clock::now() + std::chrono::seconds(10);
      }
      LogEvent(wasUnbinding ? "unbind_storage_failed"
                            : "pairing_storage_failed");
      SetPetMode(true);
    } else if (message.status == pcpair::Status::Unbound &&
                bindingId_ != 0 &&
                (message.bindingId == bindingId_ ||
                 (message.bindingId == 0 && unbindPending_ &&
                  unbindRequestId_ != 0 && message.requestId == unbindRequestId_))) {
      LogEvent("binding_unbound");
      ClearBinding();
      statePersistenceOk_ = SaveState();
      awaitingPairing_ = true;
      pairingAttempted_ = false;
      pairingInput_.clear();
      pairingError_ = statePersistenceOk_
          ? L"当前绑定已解除，请输入新的匹配码"
          : L"绑定已解除，但本机存档写入失败";
      hiddenByUser_ = false;
      SetPetMode(true);
      FlashWindow(window_, TRUE);
    } else if (message.status == pcpair::Status::BindingMissing &&
                bindingId_ != 0 &&
                (message.bindingId == bindingId_ || message.bindingId == 0) &&
                (message.requestId == resumeRequestId_ ||
                 (unbindPending_ && unbindRequestId_ != 0 &&
                  message.requestId == unbindRequestId_))) {
      LogEvent("binding_missing");
      ClearBinding();
      statePersistenceOk_ = SaveState();
      awaitingPairing_ = true;
      pairingInput_.clear();
      pairingError_ = statePersistenceOk_
          ? L"绑定存档失效，请重新匹配"
          : L"绑定失效，且本机存档写入失败";
      SetPetMode(true);
    }
  }

  void ReceiveAll() {
    for (unsigned processed = 0; processed < 512; ++processed) {
      uint8_t bytes[pcpair::kMaxDatagramSize]{};
      sockaddr_in from{};
      int fromLength = sizeof(from);
      const int received = recvfrom(socket_, reinterpret_cast<char *>(bytes),
                                    sizeof(bytes), 0,
                                    reinterpret_cast<sockaddr *>(&from),
                                    &fromLength);
      if (received < 0) return;
      if (!SameEndpoint(from, server_)) continue;
      pcpair::Message control;
      if (pcpair::Parse(bytes, static_cast<size_t>(received), control,
                        networkKey_)) {
        HandleControl(control);
        continue;
      }
      size_t packetLength = static_cast<size_t>(received);
      if (!pcpair::VerifyAndStripPacketAuth(bytes, packetLength,
                                            networkKey_)) {
        continue;
      }
      plink::PacketHeader header;
      const uint8_t *payload = nullptr;
      if (!plink::ParsePacket(bytes, packetLength, header, payload)) continue;
      if (header.type == pcpair::kActionAckPacketType) {
        uint32_t operationId = 0;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (session_ != 0 && header.session == session_ &&
            reader.U32(operationId) && reader.Done() &&
            operationId == pendingOperationId_) pendingAction_ = 0;
      } else if (header.type == pcpair::kRoundMetaPacketType) {
        if (session_ == 0 || header.session != session_ ||
            header.sequence <= lastServerSequence_) {
          continue;
        }
        pcpair::RoundMeta meta;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (!pcpair::ReadRoundMeta(reader, meta)) continue;
        haveRoundMeta_ = true;
        roundMetaPhase_ = meta.phase;
        lastPacketAt_ = Clock::now();
        lastServerSequence_ = header.sequence;
        lastServerTick_ = header.tick;
        if (currentRoundId_ != meta.roundId) {
          currentRoundId_ = meta.roundId;
          historyRecordedForRound_ =
              currentRoundId_ != 0 &&
              currentRoundId_ == lastRecordedRoundId_;
        }
        currentInviteId_ = meta.inviteId;
        if (meta.phase == plink::GamePhase::Finished && haveSnapshot_ &&
            snapshot_.phase == plink::GamePhase::Finished) {
          RecordMatchHistory();
        }
      } else if (header.type == plink::PacketType::Welcome) {
        if (header.session == 0 ||
            (session_ != 0 && header.session != session_) ||
            header.sequence <= lastServerSequence_) {
          continue;
        }
        plink::WelcomePayload welcome;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (plink::ReadWelcome(reader, welcome) && welcome.assignedSlot >= 1 &&
            welcome.assignedSlot <= 2) {
          lastPacketAt_ = Clock::now();
          lastServerSequence_ = header.sequence;
          lastServerTick_ = header.tick;
          slot_ = welcome.assignedSlot;
          session_ = header.session;
          nextDndSync_ = Clock::time_point{};
          pairingAttempted_ = false;
          pairingError_.clear();
          statePersistenceOk_ = SaveState();
        }
      } else if (header.type == plink::PacketType::Action) {
        if (session_ == 0 || header.session != session_ ||
            header.sequence <= lastActionSequence_) {
          continue;
        }
        plink::ActionPayload action;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (!plink::ReadAction(reader, action) ||
            !plink::IsQuickEmote(action.action)) {
          continue;
        }
        lastPacketAt_ = Clock::now();
        lastActionSequence_ = header.sequence;
        lastServerSequence_ = std::max(lastServerSequence_, header.sequence);
        lastServerTick_ = header.tick;
        const auto receivedAt = Clock::now();
        peerEmote_ = plink::QuickEmoteValue(action.action);
        peerEmoteStartedAt_ = receivedAt;
        peerEmoteUntil_ = receivedAt +
            std::chrono::milliseconds(kEmoteDisplayMilliseconds);
        LogEvent("quick_emote_received", peerEmote_);
        InvalidateRect(window_, nullptr, FALSE);
      } else if (header.type == pcbattle::kRelayType) {
        if (!session_ || header.session != session_ || !battleSupported_ ||
            !battlePredictor_.ready || Phase() != plink::GamePhase::Playing) continue;
        pcbattle::Batch batch; plink::PayloadReader reader(payload, header.payloadLength);
        if (!pcbattle::ReadBatch(reader, batch) || batch.round != motionRound_) continue;
        auto proposed = battlePredictor_.inputs[slot_ == 1 ? 1 : 0];
        bool valid = true;
        for (unsigned i = 0; i < batch.count; ++i)
          valid = proposed.Put(batch.first + i, batch.commands[i], battlePredictor_.confirmed.tick,
              std::min<uint32_t>(pcbattle::kMaxTicks, battlePredictor_.tick + pcbattle::kWindow)) && valid;
        if (!valid) { motionFault_ = true; continue; }
        battlePredictor_.inputs[slot_ == 1 ? 1 : 0] = proposed;
        if (!battlePredictor_.Replay()) motionFault_ = true;
      } else if (header.type == pcbattle::kImpactType) {
        if (!session_ || header.session != session_ || !battleSupported_) continue;
        pcbattle::Events events; plink::PayloadReader reader(payload, header.payloadLength);
        if (!pcbattle::ReadEvents(reader, events) || events.round != motionRound_ || !pcbattle::Extends(events, battleEvents_)) continue;
        battleEvents_ = events; // complete immutable event list; duplicates do not apply damage
        if (battlePredictor_.ready && !motionFault_) {
          pcbattle::AttachEvents(battlePredictor_.confirmed, battleEvents_);
          if (!battlePredictor_.Replay()) motionFault_ = true;
        }
      } else if (header.type == plink::PacketType::Snapshot || header.type == pcpair::kPresenceSnapshotType ||
                 header.type == pcmotion::kSnapshotType || header.type == pcbattle::kStateType) {
        const bool battle = header.type == pcbattle::kStateType;
        if (session_ == 0 || header.session != session_ ||
            header.sequence <= (battle ? battleStateSequence_ : lastServerSequence_)) {
          continue;
        }
        plink::SnapshotPayload next;
        plink::PayloadReader reader(payload, header.payloadLength);
        pcpair::PresenceSnapshot presence;
        pcmotion::Snapshot motion;
        pcbattle::State battleState;
        const bool modern = header.type == pcmotion::kSnapshotType;
        const bool extended = header.type == pcpair::kPresenceSnapshotType || modern || battle;
        const bool firstPresence = dndPresenceAt_ == Clock::time_point{};
        if (extended) {
          if (battle) {
            if (!pcbattle::ReadState(reader, battleState)) continue;
            presence = battleState.presence;
            motionSupported_ = battleSupported_ = true;
            battleStateSequence_ = header.sequence;
          } else if (modern) {
            if (!pcmotion::ReadSnapshot(reader, motion)) continue;
            presence = motion.presence;
            motionSupported_ = true;
          } else if (!pcpair::ReadPresenceSnapshot(reader, presence)) continue;
          next = presence.game;
          dndSupported_ = true;
          dndAcknowledged_ = presence.acknowledgedRevision;
          if (dndAcknowledged_ == dndRevision_ && dndNotice_ == L"状态同步中...")
            dndNotice_ = doNotDisturb_ ? L"已开启勿扰" : L"已恢复接收邀请";
          const auto peerBit = static_cast<uint8_t>(1U << (slot_ == 1 ? 1 : 0));
          peerDndKnown_ = (presence.knownMask & peerBit) != 0;
          peerDnd_ = (presence.enabledMask & peerBit) != 0;
          peerDndCapable_ = (presence.capableMask & peerBit) != 0;
          dndPresenceAt_ = Clock::now();
          currentInviteId_ = presence.inviteId;
          if (currentRoundId_ != presence.roundId) {
            currentRoundId_ = presence.roundId;
            historyRecordedForRound_ = currentRoundId_ != 0 && currentRoundId_ == lastRecordedRoundId_;
          }
          haveRoundMeta_ = true;
          roundMetaPhase_ = next.phase;
        } else if (!plink::ReadSnapshot(reader, next)) continue;
        lastPacketAt_ = Clock::now();
        lastServerSequence_ = std::max(lastServerSequence_, header.sequence);
        lastServerTick_ = header.tick;
        const plink::GamePhase previous = Phase();
        const uint8_t previousInviterSlot = snapshot_.inviterSlot;
        snapshot_ = next;
        haveSnapshot_ = true;
        if (battle) ApplyBattleState(battleState, previous);
        else if (modern) ApplyMotionSnapshot(motion, previous);
        if (next.phase != plink::GamePhase::Menu) {
          ClearDndNotice();
          dndAttemptOperation_ = 0;
        } else if (extended && dndAttemptOperation_ != 0 &&
                   presence.blockedOperation == dndAttemptOperation_) {
          dndAttemptOperation_ = 0;
          pendingAction_ = 0;
          if (Clock::now() < dndAttemptUntil_ && presence.blockedReason != pcpair::InviteBlock::None) {
            const bool blockedDnd = presence.blockedReason == pcpair::InviteBlock::DoNotDisturb;
            ShowDndNotice(blockedDnd ? L"对方已开启勿扰..." : L"好友状态同步中...", true);
            if (blockedDnd) LogEvent("invite_blocked_dnd");
          }
        }
        if (extended && presence.interruptedByDnd && presence.inviteId != lastDndInterruptedInvite_) {
          lastDndInterruptedInvite_ = presence.inviteId;
          // A reconnect/startup snapshot may retain an old Menu outcome. It
          // initializes the baseline, never replays a past notification/event.
          if (!firstPresence) {
            LogEvent("invite_interrupted_dnd");
            if (presence.interruptedInviter == slot_) ShowDndNotice(L"对方已开启勿扰...", true);
          }
        }
        if (pendingAction_ != 0 && snapshot_.phase != pendingActionContext_.phase)
          pendingAction_ = 0;
        const bool peerOnline = OpponentOnline();
        if (!peerOnlineKnown_ || peerOnline != lastPeerOnline_) {
          peerOnlineKnown_ = true;
          lastPeerOnline_ = peerOnline;
          LogEvent(peerOnline ? "peer_online" : "peer_offline");
        }
        if (!peerOnline) {
          peerDndKnown_ = peerDnd_ = peerDndCapable_ = false;
          if (dndNoticeCancelable_) {
            ClearDndNotice();
            connectionNotice_ = L"好友已离线";
            connectionNoticeUntil_ = Clock::now() + std::chrono::seconds(3);
          }
          peerEmote_ = 0;
          peerEmoteStartedAt_ = Clock::time_point{};
          peerEmoteUntil_ = Clock::time_point{};
        }
        if (pendingAction_ == static_cast<uint8_t>(plink::PlayerAction::Invite) &&
            snapshot_.phase == plink::GamePhase::Waiting)
          pendingAction_ = 0;
        if (pendingAction_ == static_cast<uint8_t>(plink::PlayerAction::Accept) &&
            snapshot_.phase == plink::GamePhase::Countdown)
          pendingAction_ = 0;
        if (pendingAction_ ==
                static_cast<uint8_t>(plink::PlayerAction::ReturnToMenu) &&
            snapshot_.phase == plink::GamePhase::Menu)
          pendingAction_ = 0;
        if (!predictionReady_ || snapshot_.phase != plink::GamePhase::Playing) {
          predicted_ = snapshot_.players[slot_ - 1];
          predictionReady_ = true;
        }
        if (snapshot_.phase == plink::GamePhase::Waiting &&
            snapshot_.inviterSlot != slot_ &&
            previous != plink::GamePhase::Waiting) {
          incomingInviteAt_ = Clock::now();
          LogEvent("invite_received");
          if (doNotDisturb_) {
            LogEvent("invite_auto_rejected_dnd");
            if (dndSupported_) SendDndPreference();
            else QueueAction(plink::PlayerAction::ReturnToMenu);
            continue;
          }
          hiddenBeforeInvite_ = hiddenByUser_;
          hiddenByUser_ = false;
          SetPetMode(true);
          SetForegroundWindow(window_);
          FlashWindow(window_, TRUE);
        }
        if (snapshot_.phase == plink::GamePhase::Waiting &&
            previous != plink::GamePhase::Waiting &&
            snapshot_.inviterSlot == slot_) {
          inviteFeedback_.clear();
          inviteFeedbackUntil_ = Clock::time_point{};
          if (outgoingInviteAt_.time_since_epoch().count() == 0)
            outgoingInviteAt_ = Clock::now();
          LogEvent("invite_waiting");
          SetPetMode(!hiddenByUser_);
        }
        if (snapshot_.phase == plink::GamePhase::Countdown &&
            previous != plink::GamePhase::Countdown) {
          returnToPetRequested_ = false;
          historyRecordedForRound_ = false;
          if (snapshot_.inviterSlot == slot_)
            LogEvent("invite_accepted_by_peer",
                     ElapsedMillis(outgoingInviteAt_));
          LogEvent("game_countdown");
        }
        if (snapshot_.phase == plink::GamePhase::Playing &&
            previous != plink::GamePhase::Playing)
          LogEvent("game_started");
        if (snapshot_.phase == plink::GamePhase::Finished &&
            previous != plink::GamePhase::Finished) {
          // Start before processing the next packet: a fast peer may already
          // return the shared server room to Menu in this same receive batch.
          AdvanceGameEffects(Clock::now());
          RecordMatchHistory();
          LogEvent("game_finished", snapshot_.matchElapsedMs);
          LogEvent("game_end_reason",
                   static_cast<int64_t>(snapshot_.endReason));
          const int64_t outcome = snapshot_.winnerSlot == 0
              ? 0 : (snapshot_.winnerSlot == slot_ ? 1 : -1);
          if (!syncFailed_) LogEvent("game_outcome", outcome);
          if (emergencyHidden_ || abandonedMatch_) QueueReturn();
        }
        if ((snapshot_.phase == plink::GamePhase::Countdown ||
             snapshot_.phase == plink::GamePhase::Playing ||
             snapshot_.phase == plink::GamePhase::Finished) &&
             !gameMode_ && !returnToPetRequested_ && !abandonedMatch_) {
          SetGameMode();
        }
        if (snapshot_.phase == plink::GamePhase::Menu &&
            (previous != plink::GamePhase::Menu || gameMode_ ||
             returnToPetRequested_ || abandonedMatch_)) {
          if (previous == plink::GamePhase::Waiting) {
            const Clock::time_point started =
                outgoingInviteAt_.time_since_epoch().count() != 0
                    ? outgoingInviteAt_ : incomingInviteAt_;
            LogEvent("invite_ended_without_game", ElapsedMillis(started));
            if (extended && presence.interruptedByDnd) {
              // Explicit outcome handled once above, including lost Waiting snapshots.
            } else if (previousInviterSlot == slot_ &&
                snapshot_.endReason ==
                    plink::MatchEndReason::InviteRejected) {
              inviteFeedback_ = L"对方拒绝邀请";
              inviteFeedbackUntil_ =
                  Clock::now() + std::chrono::seconds(kInviteFeedbackSeconds);
              LogEvent("invite_rejected_by_peer", ElapsedMillis(started));
            } else if (previousInviterSlot == slot_ &&
                       snapshot_.endReason ==
                           plink::MatchEndReason::InviteTimedOut) {
              inviteFeedback_ = L"邀请已超时";
              inviteFeedbackUntil_ =
                  Clock::now() + std::chrono::seconds(kInviteFeedbackSeconds);
              LogEvent("invite_timed_out", ElapsedMillis(started));
            }
          } else if (previous == plink::GamePhase::Finished) {
            LogEvent("game_returned_to_pet");
          }
          ApplyServerMenuReturn(Clock::now());
        }
      } else if (header.type == plink::PacketType::Pong) {
        if (session_ == 0 || header.session != session_ ||
            header.sequence <= lastServerSequence_) {
          continue;
        }
        uint32_t sentAt = 0;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (plink::ReadTimestamp(reader, sentAt)) {
          lastPacketAt_ = Clock::now();
          lastServerSequence_ = header.sequence;
          lastServerTick_ = header.tick;
          const uint32_t sample = MillisSince(start_) - sentAt;
          rttMs_ = rttMs_ == 0 ? sample : (rttMs_ * 3U + sample) / 4U;
#ifdef PLANE_PET_LATENCY_SELF_TEST
          // Test-only observation of the real UI/network pump; absent from
          // release builds. No identities, binding tokens or coordinates.
          const auto report = Wide(Option("rtt-report", ""));
          if (!report.empty()) {
            RECT client{};
            GetClientRect(window_, &client);
            std::ofstream output(std::filesystem::path(report), std::ios::app);
            output << static_cast<unsigned>(Phase()) << ',' << sample << ','
                   << rttMs_ << ',' << client.right << ',' << client.bottom << '\n';
          }
#endif
        }
      }
    }
  }

  uint8_t ReadInput(bool includeMouse = true) const {
#ifdef PLANE_PET_LATENCY_SELF_TEST
    // Move both test peers to their own left edge, apart in world space, so
    // the RTT sample window is not cut short by stationary spawn-point fire.
    if (Option("latency-move", "0") == "1") return plink::InputLeft;
#endif
    uint8_t input = 0;
    if (GetForegroundWindow() == window_) {
      if ((GetAsyncKeyState('W') & 0x8000) ||
          (GetAsyncKeyState(VK_UP) & 0x8000))
        input |= plink::InputUp;
      if ((GetAsyncKeyState('S') & 0x8000) ||
          (GetAsyncKeyState(VK_DOWN) & 0x8000))
        input |= plink::InputDown;
      if ((GetAsyncKeyState('A') & 0x8000) ||
          (GetAsyncKeyState(VK_LEFT) & 0x8000))
        input |= plink::InputLeft;
      if ((GetAsyncKeyState('D') & 0x8000) ||
          (GetAsyncKeyState(VK_RIGHT) & 0x8000))
        input |= plink::InputRight;
    }
    if (includeMouse && mouseDragging_ && gameMode_ && (predictionReady_ || haveSnapshot_)) {
      const GameLayout layout = MakeGameLayout(kGameClientWidth, kGameClientHeight);
      const int targetX = std::clamp(
          (mouseTargetX_ - layout.left) / layout.scale, 0,
                                     static_cast<int>(plink::kWorldWidth - 1));
      const int targetY = layout.WorldYFromScreen(mouseTargetY_);
      const uint8_t viewer = slot_ - 1;
      const plink::PlayerState canonical =
          predictionReady_ ? predicted_ : snapshot_.players[viewer];
      const plink::PlayerState local = plink::ToLocalView(canonical, viewer);
      constexpr int kMouseDeadZone = 2;
      if ((input & (plink::InputLeft | plink::InputRight)) == 0U) {
        if (targetX < local.x - kMouseDeadZone) input |= plink::InputLeft;
        else if (targetX > local.x + kMouseDeadZone)
          input |= plink::InputRight;
      }
      if ((input & (plink::InputUp | plink::InputDown)) == 0U) {
        if (targetY < local.y - kMouseDeadZone) input |= plink::InputUp;
        else if (targetY > local.y + kMouseDeadZone)
          input |= plink::InputDown;
      }
    }
    return input;
  }

  void SetMouseTarget(int x, int y) {
    mouseTargetX_ = std::clamp(x, 0, kGameClientWidth - 1);
    mouseTargetY_ = std::clamp(y, 0, kGameClientHeight - 1);
  }

  void ResetPrediction() {
    predicted_.x = slot_ == 1 ? plink::kPlayerOneSpawnX
                              : plink::kPlayerTwoSpawnX;
    predicted_.y = slot_ == 1 ? plink::kPlayerOneSpawnY
                              : plink::kPlayerTwoSpawnY;
    predicted_.health = plink::kInitialHealth;
    predicted_.flags = 1;
    predictionReady_ = true;
  }

  pcmotion::Command ReadMotionCommand() const {
    pcmotion::Command command;
    command.keys = ReadInput(false);
    // Any pressed movement key takes precedence over the mouse as a whole.
    if (!command.keys && mouseDragging_ && gameMode_) {
      const auto layout = MakeGameLayout(kGameClientWidth, kGameClientHeight);
      command.mouse = true;
      command.x = static_cast<uint8_t>(std::clamp((mouseTargetX_ - layout.left) / layout.scale,
          0, static_cast<int>(plink::kWorldWidth - 1)));
      command.y = static_cast<uint16_t>(layout.WorldYFromScreen(mouseTargetY_));
    }
    return command;
  }

  void ApplyMotionSnapshot(const pcmotion::Snapshot &state, plink::GamePhase previous) {
    const auto now = Clock::now();
    motionTimeline_.Add(state, std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count());
    syncFailed_ = state.syncFailed;
    if (motionRound_ != state.presence.roundId || !motionPredictor_.ready ||
        state.presence.game.phase != plink::GamePhase::Playing) {
      motionRound_ = state.presence.roundId;
      motionPredictor_.Reset(state.Position(slot_ - 1), slot_ - 1);
      motionClock_ = now;
      motionRemainder_ = 0;
      motionFault_ = false;
    } else if (!motionFault_ &&
               !motionPredictor_.Reconcile(state.presence.game.lastProcessedInput, state.Position(slot_ - 1))) {
      motionFault_ = true;
    }
    if (previous != plink::GamePhase::Playing && state.presence.game.phase == plink::GamePhase::Playing) {
      motionClock_ = now;
      motionRemainder_ = 0;
    }
    predicted_ = state.presence.game.players[slot_ - 1];
    predicted_.x = static_cast<int16_t>((motionPredictor_.position.x + 128) / pcmotion::kUnit);
    predicted_.y = static_cast<int16_t>((motionPredictor_.position.y + 128) / pcmotion::kUnit);
    predictionReady_ = true;
  }

  void AdvanceMotion(Clock::time_point now) {
    if (battleSupported_) { AdvanceBattle(now); return; }
    if (!motionSupported_ || !motionPredictor_.ready || Phase() != plink::GamePhase::Playing || motionFault_) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - motionClock_).count();
    motionClock_ = now;
    // Do not burst old movement after suspend/a blocked UI. The server informs
    // both clients that synchronization failed; no artificial catch-up speed.
    if (elapsed < 0 || elapsed > 250000) { motionFault_ = true; return; }
    motionRemainder_ += elapsed * pcmotion::kHz;
    const auto command = ReadMotionCommand();
    while (motionRemainder_ >= 1000000) {
      if (!motionPredictor_.Push(command)) { motionFault_ = true; break; }
      motionRemainder_ -= 1000000;
    }
    predicted_.x = static_cast<int16_t>((motionPredictor_.position.x + 128) / pcmotion::kUnit);
    predicted_.y = static_cast<int16_t>((motionPredictor_.position.y + 128) / pcmotion::kUnit);
  }

  void ApplyBattleState(const pcbattle::State &state, plink::GamePhase previous) {
    const auto now = Clock::now();
    syncFailed_ = state.failed;
    const auto phase = state.presence.game.phase;
    auto base = state.world;
    if (state.presence.roundId == motionRound_) pcbattle::AttachEvents(base, battleEvents_);
    if (motionRound_ != state.presence.roundId || !battlePredictor_.ready ||
        phase != plink::GamePhase::Playing) {
      // Finished discards unconfirmed ticks, including predictions made after
      // the server's lethal hit. No mid-game movement reconciliation changes.
      motionRound_ = state.presence.roundId;
      battlePredictor_.Reset(base, slot_ - 1);
      battleEvents_ = pcbattle::Events{};
      motionClock_ = now; motionRemainder_ = 0; motionFault_ = false;
    } else if (!motionFault_ && !state.failed && !battlePredictor_.Confirm(base)) {
      motionFault_ = true;
    }
    if (previous != plink::GamePhase::Playing && phase == plink::GamePhase::Playing) {
      motionClock_ = now; motionRemainder_ = 0;
    }
    UpdateBattleOwn();
  }

  void UpdateBattleOwn() {
    if (!battlePredictor_.ready) return;
    predicted_ = snapshot_.players[slot_ - 1];
    const auto p = battlePredictor_.current.players[slot_ - 1];
    predicted_.x = static_cast<int16_t>((p.x + 128) / pcmotion::kUnit);
    predicted_.y = static_cast<int16_t>((p.y + 128) / pcmotion::kUnit);
    predictionReady_ = true;
  }

  void AdvanceBattle(Clock::time_point now) {
    if (!battlePredictor_.ready || Phase() != plink::GamePhase::Playing || motionFault_) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - motionClock_).count();
    motionClock_ = now;
    if (elapsed < 0 || elapsed > 250000) { motionFault_ = true; return; }
    motionRemainder_ += elapsed * pcbattle::kHz;
    const auto command = ReadMotionCommand();
    while (motionRemainder_ >= 1000000) {
      if (battlePredictor_.tick >= pcbattle::kMaxTicks) { motionRemainder_ = 0; break; }
      if (!battlePredictor_.Push(command)) { motionFault_ = true; break; }
      motionRemainder_ -= 1000000;
    }
    UpdateBattleOwn();
  }

  static double FxSeconds(Clock::time_point now) {
    return std::chrono::duration<double>(now.time_since_epoch()).count();
  }

  bool GameFinishedVisible() const {
    return Phase() == plink::GamePhase::Finished || fxDeferredMenu_;
  }

  double EffectNow() const {
#ifdef PLANE_PET_EFFECTS_SELF_TEST
    if (fxTestNow_ >= 0) return fxTestNow_;
#endif
    return FxSeconds(Clock::now());
  }

  void ApplyServerMenuReturn(Clock::time_point now) {
    if (gameMode_ && !emergencyHidden_ && !abandonedMatch_ &&
        gameEffects_.Finishing(FxSeconds(now))) {
      // Network state continues normally; retain only the visual result.
      fxDeferredMenu_ = true;
    } else {
      CompleteServerMenuReturn();
    }
  }

  void CompleteServerMenuReturn() {
    fxDeferredMenu_ = fxReturnPending_ = false;
    const bool keepHidden = hiddenBeforeInvite_ || hiddenByUser_;
    SetPetMode(!keepHidden);
    hiddenByUser_ = keepHidden;
    emergencyHidden_ = false;
    returnToPetRequested_ = false;
    abandonedMatch_ = false;
    if (keepHidden) ShowWindow(window_, SW_HIDE);
    hiddenBeforeInvite_ = false;
    incomingInviteAt_ = Clock::time_point{};
    outgoingInviteAt_ = Clock::time_point{};
  }

  plink::SnapshotPayload GamePresentation() const {
    if (fxDeferredMenu_) return fxResult_;
    const bool battle = battleSupported_ && battlePredictor_.ready && !syncFailed_ &&
        (Phase() == plink::GamePhase::Playing || Phase() == plink::GamePhase::Finished);
    const bool modern = !battle && motionSupported_ && motionPredictor_.ready && Phase() == plink::GamePhase::Playing;
    auto visual = battle ? pcbattle::Presentation(battlePredictor_,
        Phase() == plink::GamePhase::Finished ? 1.0 : motionRemainder_ / 1000000.0, snapshot_) :
        (modern && motionTimeline_.size ? motionTimeline_.Sample(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count()) : snapshot_);
    if (!haveSnapshot_) {
      plink::WorldState preview; plink::InitializeWorld(preview);
      visual.players[0] = preview.players[0]; visual.players[1] = preview.players[1];
    } else {
      const unsigned viewer = slot_ - 1;
      if (!battle && predictionReady_ && Phase() == plink::GamePhase::Playing)
        visual.players[viewer] = predicted_;
      if (modern && !motionFault_) {
        const double t = std::clamp(motionRemainder_ / 1000000.0, 0.0, 1.0);
        const auto a = motionPredictor_.previous, b = motionPredictor_.position;
        visual.players[viewer].x = static_cast<int16_t>(std::lround((a.x + (b.x - a.x) * t) / pcmotion::kUnit));
        visual.players[viewer].y = static_cast<int16_t>(std::lround((a.y + (b.y - a.y) * t) / pcmotion::kUnit));
      }
    }
    return visual;
  }

  void AdvanceGameEffects(Clock::time_point clock) {
    const double now = FxSeconds(clock);
    const auto phase = Phase();
    if ((phase == plink::GamePhase::Countdown && fxLastPhase_ != phase) ||
        (phase == plink::GamePhase::Playing && (gameEffects_.round_ != currentRoundId_ ||
         (fxLastPhase_ != phase && fxLastPhase_ != plink::GamePhase::Countdown)))) {
      gameEffects_.Reset(currentRoundId_);
      combatFeedback_.Reset();
      resultTimeline_.Reset();
      fxDeferredMenu_ = fxReturnPending_ = false;
      fxHaveHealth_ = false;
    }
    if (phase != plink::GamePhase::Menu && phase != plink::GamePhase::Finished)
      fxDeferredMenu_ = false;
    const auto visual = GamePresentation();
    const std::array<plane_pet_fx::Point, 2> positions{{
        {double(visual.players[0].x), double(visual.players[0].y)},
        {double(visual.players[1].x), double(visual.players[1].y)}}};
    resultTimeline_.Observe(visual.phase==plink::GamePhase::Finished,now,currentRoundId_);
    const std::array<uint8_t, 2> health{{visual.players[0].health, visual.players[1].health}};
    combatFeedback_.Observe(now, health, haveSnapshot_ && !syncFailed_ &&
        (phase == plink::GamePhase::Playing ||
         (phase == plink::GamePhase::Finished && visual.endReason == plink::MatchEndReason::Destroyed)));
    gameEffects_.Advance(now, positions, health, haveSnapshot_ && phase == plink::GamePhase::Playing);
    if (battleSupported_ && battlePredictor_.ready && !syncFailed_ && !fxDeferredMenu_ &&
        (phase == plink::GamePhase::Playing || phase == plink::GamePhase::Finished)) {
      const double alpha = phase == plink::GamePhase::Finished ? 1.0 : std::clamp(motionRemainder_ / 1000000.0, 0.0, 1.0);
      const double tick = battlePredictor_.current.tick - 1.0 + alpha;
      const auto impact = [&](const pcbattle::Impact &event) {
        const double age = (tick - (event.tick - 1.0 + event.fraction / 65535.0)) / pcbattle::kHz;
        const uint64_t key = (uint64_t(event.tick) << 24U) | (uint64_t(event.bullet) << 8U) | (event.target + 1U);
        gameEffects_.Impact(key, event.target,
            {(event.contact.x - event.plane.x) / double(pcmotion::kUnit),
             (event.contact.y - event.plane.y) / double(pcmotion::kUnit)}, now, age);
      };
      for (unsigned i = 0; i < battlePredictor_.current.impactCount; ++i) impact(battlePredictor_.current.impacts[i]);
      for (unsigned i = 0; i < battleEvents_.count; ++i) impact(battleEvents_.hits[i]);
    } else if (!battleSupported_ && fxHaveHealth_ && phase == plink::GamePhase::Playing) {
      // Legacy PC view has no contact stream. Do not invent bullet collision
      // events; use the authoritative HP transition as a centered damage cue.
      for (unsigned side = 0; side < 2; ++side) if (health[side] < fxPreviousHealth_[side])
        gameEffects_.Impact(0xf0000000ULL | (side << 8U) | (3 - health[side]), side, {}, now);
    }
    if (phase == plink::GamePhase::Finished && !gameEffects_.finished_) {
      fxResult_ = snapshot_; fxResultSyncFailed_ = syncFailed_;
      gameEffects_.Finish(now, !syncFailed_ && snapshot_.endReason == plink::MatchEndReason::Destroyed, positions, health);
    }
    fxPreviousHealth_ = health;
    fxHaveHealth_ = phase == plink::GamePhase::Playing;
    fxLastPhase_ = phase;
    if (fxDeferredMenu_ && !gameEffects_.Finishing(now)) CompleteServerMenuReturn();
    else if (fxReturnPending_ && phase == plink::GamePhase::Finished && !gameEffects_.Finishing(now))
      ReturnToPetFromFinished();
  }

  plink::GamePhase Phase() const {
    return haveSnapshot_ ? snapshot_.phase : plink::GamePhase::Menu;
  }

  bool OpponentOnline() const {
    if (!haveSnapshot_) return false;
    const uint8_t bit = static_cast<uint8_t>(1U << (slot_ == 1 ? 1U : 0U));
    return (snapshot_.onlineMask & bit) != 0;
  }

  bool IsIncomingInvite() const {
    return Phase() == plink::GamePhase::Waiting &&
           snapshot_.inviterSlot != 0 && snapshot_.inviterSlot != slot_;
  }

  void SetPetMode(bool show) {
    ResetPetToolbar();
    if (mouseDragging_) {
      mouseDragging_ = false;
      if (GetCapture() == window_) ReleaseCapture();
    }
    gameMode_ = false;
    std::wstring title = L"Plane Pet - " + ownName_;
    if (pairingAttempted_) title = L"Plane Pet Pairing - " + ownName_;
    else if (IsIncomingInvite()) title = L"Plane Pet Invited - " + ownName_;
    else if (Phase() == plink::GamePhase::Waiting)
      title = L"Plane Pet Waiting - " + ownName_;
    SetWindowTextW(window_, title.c_str());
    SetWindowLongPtrW(window_, GWL_STYLE, WS_POPUP);
    SetWindowLongPtrW(window_, GWL_EXSTYLE,
                      WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST);
    SetLayeredWindowAttributes(window_, kTransparent, 0, LWA_COLORKEY);
    RECT pet{petX_, petY_, petX_ + kPetWidth, petY_ + kPetHeight};
    pet = plane_pet_dpi::FitMonitor(pet);
    petX_ = pet.left; petY_ = pet.top;
    SetWindowPos(window_, HWND_TOPMOST, petX_, petY_, pet.right - pet.left, pet.bottom - pet.top,
                 SWP_FRAMECHANGED | (show ? SWP_SHOWWINDOW : SWP_NOACTIVATE));
    if (!show) ShowWindow(window_, SW_HIDE);
  }

  void SetGameMode() {
    ResetPetToolbar();
    CloseInfoWindow();
    CloseUpdateWindow();
    if (!gameMode_) {
      RECT pet{};
      GetWindowRect(window_, &pet);
      petX_ = pet.left;
      petY_ = pet.top;
    }
    mouseDragging_ = false;
    gameMode_ = true;
    const std::wstring title = L"Plane Pet Game - " + ownName_;
    SetWindowTextW(window_, title.c_str());
    const DWORD gameStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD gameExStyle = WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(window_, GWL_STYLE, static_cast<LONG_PTR>(gameStyle));
    SetWindowLongPtrW(window_, GWL_EXSTYLE,
                      static_cast<LONG_PTR>(gameExStyle));
    RECT gameRect{0, 0, kGameClientWidth, kGameClientHeight};
    // Native caption/border metrics must match the HWND's current monitor.
    plane_pet_dpi::Adjust(gameRect, gameStyle, gameExStyle,
                          plane_pet_dpi::ForWindow(window_));
    const int gameWidth = gameRect.right - gameRect.left;
    const int gameHeight = gameRect.bottom - gameRect.top;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    RECT anchor{petX_, petY_, petX_ + kPetWidth, petY_ + kPetHeight};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT work = monitor.rcWork;
    const int gameX = std::clamp(
        petX_, static_cast<int>(work.left),
        std::max(static_cast<int>(work.left),
                 static_cast<int>(work.right) - gameWidth));
    const int gameY = std::clamp(
        petY_, static_cast<int>(work.top),
        std::max(static_cast<int>(work.top),
                 static_cast<int>(work.bottom) - gameHeight));
    SetWindowPos(window_, HWND_NOTOPMOST, gameX, gameY, gameWidth, gameHeight,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
  }

  static void Text(HDC dc, const std::wstring &text, RECT rect, int height,
                   COLORREF color, int weight = FW_NORMAL,
                   UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    TextWithFace(dc, text, rect, height, color, L"Microsoft YaHei UI",
                 weight, format);
  }

  static void TextWithFace(
      HDC dc, const std::wstring &text, RECT rect, int height,
      COLORREF color, const wchar_t *face, int weight = FW_NORMAL,
      UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             VARIABLE_PITCH | FF_SWISS,
                             face);
    HGDIOBJ old = SelectObject(dc, font);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
              format);
    SelectObject(dc, old);
    DeleteObject(font);
  }

  static void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
  }

  static void RoundPanel(HDC dc, const RECT &rect, int radius,
                         COLORREF fill, COLORREF edge, int edgeWidth = 1) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, edgeWidth), edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
  }

  void DrawHistoryHealth(HDC dc, int x, int y, uint8_t health,
                         COLORREF color) const {
    using namespace plane_pet_history_ui;
    for (uint8_t index = 0; index < plink::kInitialHealth; ++index) {
      const bool filled = index < health;
      const int center = x + index * kHeartPitch;
      if (DrawHeartImage(dc, center, y, kHeartWidth, kHeartHeight,
                         filled ? (color == kBlue ? 0 : 1) : 2))
        continue;
      const COLORREF edge = color == kBlue ? RGB(160, 235, 255) : RGB(255, 150, 160);
      // Resource/GDI+ failure must not hide HP. The old geometry's origin
      // is three pixels above its visible center; combat HUD stays unchanged.
      DrawHeart(dc, center, y - 3, kHeartScale,
                filled ? color : RGB(42, 49, 64),
                filled ? edge : RGB(75, 84, 103), true);
    }
  }

  static void DrawLine(HDC dc, int x1, int y1, int x2, int y2,
                       COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, width), color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
  }

  PetAnimationPose PetPoseAt(uint32_t elapsedMs) const {
    const double time = static_cast<double>(elapsedMs) / 1000.0;
    const double seed = static_cast<double>(clientId_ % 997U) * 0.006302;

    // Two incommensurate waves create a smooth, non-repeating patrol path.
    // Their summed amplitudes keep the game-sized aircraft inside the pet.
    const double phaseX1 = time * 0.38 + seed;
    const double phaseX2 = time * 0.73 + seed * 1.71 + 1.2;
    const double phaseY1 = time * 0.43 + seed * 0.83 + 0.7;
    const double phaseY2 = time * 0.89 + seed * 1.37 + 2.4;
    const double x = 140.0 + 78.0 * std::sin(phaseX1) +
                     34.0 * std::sin(phaseX2);
    const double y = 70.0 + 30.0 * std::sin(phaseY1) +
                     18.0 * std::sin(phaseY2);
    const double velocityX = 78.0 * 0.38 * std::cos(phaseX1) +
                             34.0 * 0.73 * std::cos(phaseX2);
    const double velocityY = 30.0 * 0.43 * std::cos(phaseY1) +
                             18.0 * 0.89 * std::cos(phaseY2);
    const double speed = std::max(0.001,
                                  std::sqrt(velocityX * velocityX +
                                            velocityY * velocityY));
    const double directionX = velocityX / speed;
    const double directionY = velocityY / speed;
    return {x, y, std::atan2(directionX, -directionY),
            directionX, directionY};
  }

  PetFormation PetFormationAt(uint32_t elapsedMs) const {
    const double time = static_cast<double>(elapsedMs) / 1000.0;
    const double seed = static_cast<double>(clientId_ % 997U) * 0.006302;
    // Treat the pet as a side-scrolling camera following an airborne chase.
    // Screen-space x may breathe as the gap changes, but both aircraft keep
    // positive world-space forward speed and therefore never turn backwards
    // merely because the animation must remain inside the compact window.
    const auto evasionAt = [&](double sampleTime) {
      const double phase = sampleTime * 0.55 + seed;
      const double secondPhase = phase * 2.0 + seed * 0.24 + 0.72;
      const double y = 62.0 + 22.0 * std::sin(phase) +
                       6.0 * std::sin(secondPhase);
      const double velocityY = 22.0 * 0.55 * std::cos(phase) +
                               6.0 * 1.10 * std::cos(secondPhase);
      return std::pair<double, double>{y, velocityY};
    };

    const auto redRoute = evasionAt(time);
    const double delayPhase = time * 0.21 + seed * 0.63 + 0.40;
    const double routeDelay = 0.80 + 0.15 *
        (0.5 + 0.5 * std::sin(delayPhase));
    const auto blueRoute = evasionAt(time - routeDelay);
    const double leadPhase = time * 0.29 + seed * 1.31 + 0.65;
    const double horizontalLead = 113.0 + 9.0 * std::sin(leadPhase);
    const double redX = 194.0 +
        4.0 * std::sin(time * 0.25 + seed * 0.47 + 0.90);

    PetFormation formation;
    formation.red.x = redX;
    formation.red.y = redRoute.first;
    formation.blue.x = redX - horizontalLead;
    formation.blue.y = blueRoute.first;

    // The red jet banks along a continuous S-shaped escape route. A virtual
    // forward speed supplies the missing camera motion, keeping its nose
    // generally rightward even at the top and bottom of a screen-space turn.
    constexpr double virtualForwardSpeed = 38.0;
    const double redSlope = redRoute.second / virtualForwardSpeed;
    const double redDirectionLength = std::sqrt(1.0 + redSlope * redSlope);
    formation.red.directionX = 1.0 / redDirectionLength;
    formation.red.directionY = redSlope / redDirectionLength;
    formation.red.angle = std::atan2(formation.red.directionX,
                                     -formation.red.directionY);

    // The follower aims slightly ahead of the red position. Since its y
    // coordinate is a delayed sample of the same route, this creates a clear
    // follow-through at every turn instead of two unrelated aircraft shakes.
    const double interceptY = formation.red.y +
                              formation.red.directionY * 18.0;
    const double pursuitSlope =
        (interceptY - formation.blue.y) / (horizontalLead * 0.62);
    const double blueDirectionLength =
        std::sqrt(1.0 + pursuitSlope * pursuitSlope);
    formation.blue.directionX = 1.0 / blueDirectionLength;
    formation.blue.directionY = pursuitSlope / blueDirectionLength;
    formation.blue.angle = std::atan2(formation.blue.directionX,
                                      -formation.blue.directionY);
    return formation;
  }

  static const wchar_t *QuickEmoteGlyph(uint8_t value) {
    switch (value) {
      case 1: return L"😂";
      case 2: return L"😭";
      case 3: return L"😡";
      case 4: return L"☝";
      default: return L"";
    }
  }

  Gdiplus::Bitmap *EmbeddedBitmap(int resourceId,
                                  EmbeddedImageResource &image) const {
    if (!gGdiPlusSession.ready()) return nullptr;
    if (image.attempted) return image.bitmap.get();
    image.attempted = true;
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(
        module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (resource == nullptr) return nullptr;
    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    const void *bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
    if (bytes == nullptr || size == 0) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (memory == nullptr) return nullptr;
    void *destination = GlobalLock(memory);
    if (destination == nullptr) {
      GlobalFree(memory);
      return nullptr;
    }
    std::memcpy(destination, bytes, size);
    GlobalUnlock(memory);
    IStream *stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK ||
        stream == nullptr) {
      GlobalFree(memory);
      return nullptr;
    }
    std::unique_ptr<Gdiplus::Bitmap> bitmap(
        Gdiplus::Bitmap::FromStream(stream, FALSE));
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok ||
        bitmap->GetWidth() == 0 || bitmap->GetHeight() == 0) {
      bitmap.reset();
      stream->Release();
      return nullptr;
    }
    image.stream = stream;
    image.bitmap = std::move(bitmap);
    return image.bitmap.get();
  }

  Gdiplus::Bitmap *QuickEmoteBitmap(uint8_t value) const {
    if (value < 1 || value > 4) return nullptr;
    return EmbeddedBitmap(100 + value, emojiImages_[value - 1]);
  }

  Gdiplus::Bitmap *PetPlaneBitmap(bool red) const {
    const size_t index = red ? 0U : 1U;
    return EmbeddedBitmap(red ? 151 : 152, petPlaneImages_[index]);
  }

  Gdiplus::Bitmap *GamePlaneBitmap(bool red, uint8_t health) const {
    if (health >= 3) return PetPlaneBitmap(red);
    const unsigned index = (red ? 2U : 0U) + (health <= 1 ? 1U : 0U);
    auto *damaged = EmbeddedBitmap(175 + index, damagedPlaneImages_[index]);
    if (!damaged || damaged->GetWidth() != 512 || damaged->GetHeight() != 512)
      return PetPlaneBitmap(red);
    return damaged;
  }

  Gdiplus::Bitmap *PetCloudBitmap(int variant) const {
    const size_t index = static_cast<size_t>(std::clamp(variant, 0, 2));
    return EmbeddedBitmap(153 + static_cast<int>(index),
                          petCloudImages_[index]);
  }

  Gdiplus::Bitmap *EffectBitmap(unsigned kind, unsigned frame) const {
    if (kind > 2 || frame >= (kind == 2 ? 6U : 12U)) return nullptr;
    auto &cached = effectFrames_[kind][frame];
    if (cached) return cached.get();
    auto *atlas = EmbeddedBitmap(171 + kind, effectImages_[kind]);
    if (!atlas) return nullptr;
    const unsigned columns = kind == 2 ? 3 : 4, rows = kind == 2 ? 2 : 3;
    if (atlas->GetWidth() % columns || atlas->GetHeight() % rows) return nullptr;
    const int w = atlas->GetWidth() / columns, h = atlas->GetHeight() / rows;
    cached.reset(atlas->Clone(Gdiplus::Rect((frame % columns) * w, (frame / columns) * h, w, h), PixelFormat32bppPARGB));
    if (cached && cached->GetLastStatus() != Gdiplus::Ok) cached.reset();
    return cached.get();
  }

  void DrawGameEffects(HDC dc, const GameLayout &layout,
                       const plink::SnapshotPayload &visual, bool foreground) const {
    using namespace plane_pet_fx;
    const double now = EffectNow();
    const unsigned viewer = slot_ - 1;
    const auto screen = [&](Point p) {
      if (viewer) p = {239.0 - p.x, 319.0 - p.y};
      return Gdiplus::PointF(static_cast<float>(layout.left + p.x * layout.scale),
          static_cast<float>(layout.playTop + p.y * layout.scale));
    };
    plane_pet_dpi::ImageCanvas canvas(dc);
    auto &g = canvas.graphics();
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    const auto sprite = [&](unsigned kind, unsigned frame, Point p, double size, double alpha, double angle) {
      auto *bitmap = EffectBitmap(kind, frame);
      if (!bitmap || alpha <= 0) return;
      const auto center = screen(p);
      const auto saved = g.Save();
      g.TranslateTransform(center.X, center.Y);
      g.RotateTransform(static_cast<float>(angle + (viewer ? 180.0 : 0.0)));
      const float width = static_cast<float>(size * layout.scale);
      Gdiplus::ColorMatrix colors{};
      colors.m[0][0] = colors.m[1][1] = colors.m[2][2] = colors.m[4][4] = 1;
      colors.m[3][3] = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
      Gdiplus::ImageAttributes attributes; attributes.SetColorMatrix(&colors);
      g.DrawImage(bitmap, Gdiplus::RectF(-width / 2, -width / 2, width, width),
          0, 0, static_cast<float>(bitmap->GetWidth()), static_cast<float>(bitmap->GetHeight()), Gdiplus::UnitPixel, &attributes);
      g.Restore(saved);
    };
    if (!foreground) {
      for (unsigned side = 0; side < 2; ++side) for (unsigned wing = 0; wing < 2; ++wing) {
        const auto &trail = gameEffects_.trails_[side][wing];
        const bool own = side == viewer;
        // Draw the entire restrained halo before the brighter narrow core. Both
        // layers use the same world history; no static beam or blurred sprite.
        for (unsigned layer = 0; layer < 2; ++layer) {
          const bool glow = layer == 0;
          Gdiplus::Pen pen(Gdiplus::Color(0, 0, 0, 0), static_cast<float>(
              (glow ? kTrailGlowWidth : kTrailCoreWidth) * layout.scale));
          const auto line = [&](Point a, Point b, double alpha) {
            const auto opacity = static_cast<BYTE>(255 * alpha * (glow ? kTrailGlowOpacity : 1.0));
            if (!opacity) return;
            pen.SetColor(own ? (glow ? Gdiplus::Color(opacity, 0, 180, 255) : Gdiplus::Color(opacity, 95, 218, 255))
                : (glow ? Gdiplus::Color(opacity, 255, 55, 75) : Gdiplus::Color(opacity, 255, 136, 148)));
            g.DrawLine(&pen, screen(a), screen(b));
          };
          for (size_t i = 0; i + 1 < trail.size(); ++i) {
            if (trail[i + 1].breakBefore) continue;
            line(gameEffects_.TrailAt(trail[i], side, now),
                gameEffects_.TrailAt(trail[i + 1], side, now),
                gameEffects_.TrailAlpha(trail[i], side, now));
          }
          if (!trail.empty() && visual.phase == plink::GamePhase::Playing && visual.players[side].health) {
            const auto p = visual.players[side];
            const TrailPoint head{Wing({double(p.x), double(p.y)}, side, wing), now, false};
            line(gameEffects_.TrailAt(trail.back(), side, now), head.p,
                gameEffects_.TrailAlpha(head, side, now));
          }
        }
      }
      for (const auto &p : gameEffects_.smoke_)
        sprite(2, p.variant, p.At(now), p.Size(now), p.Alpha(now), p.angle);
    } else {
      for (const auto &hit : gameEffects_.hits_) {
        const double age = now - hit.born;
        if (age < 0 || age >= kHitLife) continue;
        const auto p = visual.players[hit.side];
        sprite(0, Frame(age, kHitLife, kHitFrames), Point{double(p.x), double(p.y)} + hit.offset, 20, 1, 0);
      }
      for (const auto &e : gameEffects_.explosions_) if (e.started) {
        const double age = now - e.born;
        if (age >= 0 && age < kExplosionLife)
          sprite(1, Frame(age, kExplosionLife, kExplosionFrames), e.p, 48, 1, 0);
      }
    }
  }

  Gdiplus::Bitmap *HeartAtlasBitmap() const {
    using namespace plane_pet_hearts;
    auto *bitmap = EmbeddedBitmap(kResourceId, heartAtlasImage_);
    if (bitmap == nullptr ||
        bitmap->GetWidth() != kHeartAtlasWidth || bitmap->GetHeight() != kHeartAtlasHeight)
      return nullptr;
    return bitmap;
  }

  Gdiplus::Bitmap *HudHeartBitmap(int state) const {
    using namespace plane_pet_hearts;
    using namespace plane_pet_ui;
    if (state < 0 || state > 2) return nullptr;
    auto &cached = hudHeartImages_[state];
    if (cached) return cached.get();
    auto *atlas = HeartAtlasBitmap();
    if (!atlas) return nullptr;
    auto candidate = std::make_unique<Gdiplus::Bitmap>(kGameHeartWidth, kGameHeartHeight, PixelFormat32bppPARGB);
    Gdiplus::Graphics raster(candidate.get());
    raster.Clear(Gdiplus::Color(0, 0, 0, 0));
    raster.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    raster.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    if (raster.DrawImage(atlas, Gdiplus::Rect(0, 0, kGameHeartWidth, kGameHeartHeight),
        kHeartSourceX[state], kHeartSourceY, kHeartSourceWidth, kHeartSourceHeight,
        Gdiplus::UnitPixel) == Gdiplus::Ok) cached = std::move(candidate);
    return cached.get();
  }

  Gdiplus::Bitmap *HeartFeedbackBitmap(unsigned color, unsigned frame) const {
    using namespace plane_pet_feedback;
    using namespace plane_pet_ui;
    if (color > 1 || frame >= kHeartFrames) return nullptr;
    auto &cached = heartFeedbackFrames_[color][frame];
    if (cached) return cached.get();
    auto *source = HudHeartBitmap(color);
    if (!source) return nullptr;
    auto bitmap = std::make_unique<Gdiplus::Bitmap>(kHeartCellWidth, kHeartCellHeight, PixelFormat32bppPARGB);
    { Gdiplus::Graphics g(bitmap.get()); g.Clear(Gdiplus::Color(0,0,0,0)); }
    const auto pose = Heart(frame / double(kHz));
    for (int y=0; y<kGameHeartHeight; ++y) for (int x=0; x<kGameHeartWidth; ++x) {
      Gdiplus::Color c; source->GetPixel(x,y,&c);
      if (!c.GetA()) continue;
      const bool left = x < HeartCut(y);
      int dx=x, dy=y;
      if (pose.split) {
        dx += static_cast<int>(std::lround(left ? -pose.separation : pose.separation));
        dy += static_cast<int>(std::lround(pose.drop));
        c.SetValue(Gdiplus::Color(static_cast<BYTE>(std::lround(c.GetA()*pose.alpha)),c.GetR(),c.GetG(),c.GetB()).GetValue());
      } else if (y < pose.crack*kGameHeartHeight && x == HeartCut(y)) {
        c.SetValue(Gdiplus::Color(c.GetA(),8,17,30).GetValue());
      }
      bitmap->SetPixel(kHeartOriginX-kGameHeartWidth/2+dx,
          kHeartOriginY-kGameHeartHeight/2+dy,c);
    }
    if (bitmap->GetLastStatus() == Gdiplus::Ok) cached=std::move(bitmap);
    return cached.get();
  }

  Gdiplus::Bitmap *HitFeedbackBitmap(unsigned frame) const {
    using namespace plane_pet_feedback;
    if (frame >= kHitFrames) return nullptr;
    auto &cached = hitFeedbackFrames_[frame];
    if (cached) return cached.get();
    auto *source = EmbeddedBitmap(174,hitWordImage_);
    if (!source) return nullptr;
    auto bitmap=std::make_unique<Gdiplus::Bitmap>(kHitCellWidth,kHitCellHeight,PixelFormat32bppPARGB);
    const auto pose=Hit(frame/double(kHz));
    const int w=static_cast<int>(std::lround(kHitBaseWidth*pose.scale));
    const int h=std::max(1,static_cast<int>(std::lround(w*double(source->GetHeight())/source->GetWidth())));
    {
      Gdiplus::Graphics g(bitmap.get()); g.Clear(Gdiplus::Color(0,0,0,0));
      g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
      g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
      g.DrawImage(source,Gdiplus::Rect((kHitCellWidth-w)/2,(kHitCellHeight-h)/2,w,h));
    }
    for (int y=0;y<kHitCellHeight;++y) for(int x=0;x<kHitCellWidth;++x) {
      Gdiplus::Color c; bitmap->GetPixel(x,y,&c); if (!c.GetA()) continue;
      double blend=0;
      if (pose.sweep>=0 && c.GetR()>100 && c.GetG()>65) {
        const double scan=-8+(kHitCellWidth+kHitCellHeight*.44+16)*pose.sweep;
        const double d=x+y*.44-scan;
        blend=std::abs(d)<.8 ? .96 : (std::abs(d+2.2)<.45 ? .78 : 0);
      }
      bitmap->SetPixel(x,y,Gdiplus::Color(static_cast<BYTE>(std::lround(c.GetA()*pose.alpha)),
          static_cast<BYTE>(c.GetR()+(255-c.GetR())*blend),
          static_cast<BYTE>(c.GetG()+(255-c.GetG())*blend),
          static_cast<BYTE>(c.GetB()+(233-c.GetB())*blend)));
    }
    if (bitmap->GetLastStatus()==Gdiplus::Ok) cached=std::move(bitmap);
    return cached.get();
  }

  static void DrawFeedbackBitmap(HDC dc, Gdiplus::Bitmap *bitmap, int x, int y, int scale) {
    if (!bitmap) return;
    plane_pet_dpi::ImageCanvas canvas(dc);
    auto &g=canvas.graphics();
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.DrawImage(bitmap,Gdiplus::Rect(x,y,bitmap->GetWidth()*scale,bitmap->GetHeight()*scale));
  }

  bool DrawHeartImage(HDC dc, int cx, int cy, int width, int height, int state) const {
    using namespace plane_pet_hearts;
    using namespace plane_pet_ui;
    auto *bitmap = HeartAtlasBitmap();
    if (bitmap == nullptr || state < 0 || state > 2 || width <= 0 || height <= 0) return false;
    const bool hudSize = width % kGameHeartWidth == 0 && height % kGameHeartHeight == 0 &&
        width / kGameHeartWidth == height / kGameHeartHeight;
    if (hudSize) if (auto *cached=HudHeartBitmap(state)) bitmap=cached;
    plane_pet_dpi::ImageCanvas imageCanvas(dc);
    auto &graphics = imageCanvas.graphics();
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const int left = cx - (hudSize ? (kGameHeartWidth/2)*(width/kGameHeartWidth) : width/2);
    const Gdiplus::Rect destination(left, cy - height / 2, width, height);
    if (bitmap == hudHeartImages_[state].get())
      return graphics.DrawImage(bitmap, destination, 0, 0, kGameHeartWidth, kGameHeartHeight, Gdiplus::UnitPixel) == Gdiplus::Ok;
    return graphics.DrawImage(bitmap, destination, kHeartSourceX[state], kHeartSourceY,
        kHeartSourceWidth, kHeartSourceHeight, Gdiplus::UnitPixel) == Gdiplus::Ok;
  }

  bool DrawQuickEmoteImage(HDC dc, uint8_t value, const RECT &rect,
                           bool enabled = true,
                           uint32_t animationElapsedMs = 0,
                           bool animate = false) const {
    Gdiplus::Bitmap *bitmap = QuickEmoteBitmap(value);
    if (bitmap == nullptr) return false;
    if (value == 4) {
      const UINT frameCount = bitmap->GetFrameCount(
          &Gdiplus::FrameDimensionTime);
      if (frameCount != 0) {
        constexpr uint32_t kBeckonFrameMilliseconds = 50;
        const UINT frame = animate
            ? static_cast<UINT>((animationElapsedMs /
                kBeckonFrameMilliseconds) % frameCount)
            : 0U;
        if (bitmap->SelectActiveFrame(&Gdiplus::FrameDimensionTime, frame) !=
            Gdiplus::Ok) {
          return false;
        }
      }
    }
    plane_pet_dpi::ImageCanvas imageCanvas(dc);
    auto &graphics = imageCanvas.graphics();
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const Gdiplus::Rect destination(
        rect.left, rect.top, std::max<LONG>(1, rect.right - rect.left),
        std::max<LONG>(1, rect.bottom - rect.top));
    if (enabled) {
      return graphics.DrawImage(bitmap, destination) == Gdiplus::Ok;
    }
    Gdiplus::ColorMatrix dimmed = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.38f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    Gdiplus::ImageAttributes attributes;
    attributes.SetColorMatrix(&dimmed, Gdiplus::ColorMatrixFlagsDefault,
                              Gdiplus::ColorAdjustTypeBitmap);
    return graphics.DrawImage(
        bitmap, destination, 0, 0, bitmap->GetWidth(), bitmap->GetHeight(),
        Gdiplus::UnitPixel, &attributes) == Gdiplus::Ok;
  }

  static RECT QuickEmoteBubbleRect(int planeX, int planeY, bool toLeft) {
    const int left = std::clamp(planeX + (toLeft ? -54 : 12), 2, 230);
    const int top = std::clamp(planeY - 49, 2, 70);
    return RECT{left, top, left + 48, top + 45};
  }

  void DrawQuickEmoteBubble(HDC dc, int planeX, int planeY,
                            uint8_t value, bool toLeft,
                            uint32_t elapsedMs) const {
    if (value < 1 || value > 4) return;
    const RECT bubble = QuickEmoteBubbleRect(planeX, planeY, toLeft);
    HBRUSH brush = CreateSolidBrush(RGB(248, 250, 255));
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(85, 105, 135));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, bubble.left, bubble.top, bubble.right, bubble.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    const double phase = static_cast<double>(elapsedMs % 760U) /
                         760.0 * 6.283185307179586;
    // The beckoning gesture already contains its own finger animation. Keep
    // its speech bubble and image canvas stationary while those frames play.
    const int bounce = value == 4 ? 0 :
        static_cast<int>(std::lround(std::sin(phase) * 5.0));
    RECT glyph{bubble.left + 7, bubble.top + 5 + bounce,
               bubble.left + 42, bubble.top + 40 + bounce};
    if (!DrawQuickEmoteImage(dc, value, glyph, true, elapsedMs,
                             value == 4)) {
      TextWithFace(dc, QuickEmoteGlyph(value), glyph, 32, RGB(20, 24, 34),
                   L"Segoe UI Emoji", FW_NORMAL);
    }
  }

  static void Plane(HDC dc, int cx, int cy, int scale, bool up,
                    COLORREF color) {
    const int d = up ? -1 : 1;
    const int s = std::max(1, scale);
    const COLORREF wingColor = color == kBlue ? RGB(0, 112, 184)
                                               : RGB(170, 36, 62);

    // Engine glow behind the tail.
    HBRUSH glow = CreateSolidBrush(kYellow);
    HGDIOBJ oldGlow = SelectObject(dc, glow);
    const int glowY1 = cy - d * 29 * s;
    const int glowY2 = cy - d * 18 * s;
    Ellipse(dc, cx - 4 * s, std::min(glowY1, glowY2), cx + 4 * s,
            std::max(glowY1, glowY2));
    SelectObject(dc, oldGlow);
    DeleteObject(glow);

    // Swept main wings and rear stabilizers are drawn beneath the fuselage.
    POINT wings[8] = {
        {cx - 4 * s, cy + d * 7 * s},
        {cx - 29 * s, cy - d * 8 * s},
        {cx - 27 * s, cy - d * 15 * s},
        {cx - 5 * s, cy - d * 8 * s},
        {cx + 5 * s, cy - d * 8 * s},
        {cx + 27 * s, cy - d * 15 * s},
        {cx + 29 * s, cy - d * 8 * s},
        {cx + 4 * s, cy + d * 7 * s},
    };
    HBRUSH wingBrush = CreateSolidBrush(wingColor);
    HPEN outline = CreatePen(PS_SOLID, std::max(1, s), kWhite);
    HGDIOBJ oldBrush = SelectObject(dc, wingBrush);
    HGDIOBJ oldPen = SelectObject(dc, outline);
    Polygon(dc, wings, 8);

    POINT tail[6] = {
        {cx - 4 * s, cy - d * 14 * s},
        {cx - 14 * s, cy - d * 23 * s},
        {cx - 12 * s, cy - d * 27 * s},
        {cx + 12 * s, cy - d * 27 * s},
        {cx + 14 * s, cy - d * 23 * s},
        {cx + 4 * s, cy - d * 14 * s},
    };
    Polygon(dc, tail, 6);

    // Long fuselage with a pointed nose.
    POINT body[7] = {
        {cx, cy + d * 31 * s},
        {cx + 5 * s, cy + d * 17 * s},
        {cx + 7 * s, cy - d * 15 * s},
        {cx + 4 * s, cy - d * 27 * s},
        {cx - 4 * s, cy - d * 27 * s},
        {cx - 7 * s, cy - d * 15 * s},
        {cx - 5 * s, cy + d * 17 * s},
    };
    HBRUSH bodyBrush = CreateSolidBrush(color);
    SelectObject(dc, bodyBrush);
    Polygon(dc, body, 7);

    // Canopy and center highlight make the tiny model readable at a glance.
    HBRUSH canopy = CreateSolidBrush(color == kBlue ? kBlueLight
                                                    : RGB(255, 168, 180));
    SelectObject(dc, canopy);
    const int canopyY1 = cy + d * 7 * s;
    const int canopyY2 = cy + d * 18 * s;
    Ellipse(dc, cx - 4 * s, std::min(canopyY1, canopyY2), cx + 4 * s,
            std::max(canopyY1, canopyY2));
    DrawLine(dc, cx, cy + d * 27 * s, cx, cy - d * 18 * s,
             RGB(220, 247, 255), std::max(1, s));

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(wingBrush);
    DeleteObject(bodyBrush);
    DeleteObject(canopy);
    DeleteObject(outline);
  }

  static void GamePlaneFallback(HDC dc, int cx, int cy, bool up,
                                COLORREF color) {
    const int saved = SaveDC(dc);
    SetGraphicsMode(dc, GM_ADVANCED);
    constexpr FLOAT factor = 1.0f / 3.0f;
    XFORM transform{factor, 0.0f, 0.0f, factor,
                    cx * (1.0f - factor), cy * (1.0f - factor)};
    if (SetWorldTransform(dc, &transform)) {
      Plane(dc, cx, cy, 1, up, color);
    } else {
      HBRUSH fallback = CreateSolidBrush(color);
      HGDIOBJ old = SelectObject(dc, fallback);
      POINT triangle[3] = {{cx, cy + (up ? -10 : 10)},
                           {cx - 9, cy + (up ? 9 : -9)},
                           {cx + 9, cy + (up ? 9 : -9)}};
      Polygon(dc, triangle, 3);
      SelectObject(dc, old);
      DeleteObject(fallback);
    }
    RestoreDC(dc, saved);
  }

  static void PetPlaneFallback(HDC dc, int cx, int cy, double angle,
                               COLORREF color) {
    const int saved = SaveDC(dc);
    SetGraphicsMode(dc, GM_ADVANCED);
    constexpr FLOAT factor = 1.0f / 3.0f;
    const FLOAT cosine = static_cast<FLOAT>(std::cos(angle));
    const FLOAT sine = static_cast<FLOAT>(std::sin(angle));
    XFORM transform{
        factor * cosine,
        factor * sine,
        -factor * sine,
        factor * cosine,
        static_cast<FLOAT>(cx) - factor * cosine * cx + factor * sine * cy,
        static_cast<FLOAT>(cy) - factor * sine * cx - factor * cosine * cy,
    };
    if (SetWorldTransform(dc, &transform)) {
      Plane(dc, cx, cy, 1, true, color);
    } else {
      const double forwardX = std::sin(angle);
      const double forwardY = -std::cos(angle);
      const double sideX = -forwardY;
      const double sideY = forwardX;
      POINT triangle[3] = {
          {static_cast<LONG>(std::lround(cx + forwardX * 11.0)),
           static_cast<LONG>(std::lround(cy + forwardY * 11.0))},
          {static_cast<LONG>(std::lround(cx - forwardX * 8.0 + sideX * 8.0)),
           static_cast<LONG>(std::lround(cy - forwardY * 8.0 + sideY * 8.0))},
          {static_cast<LONG>(std::lround(cx - forwardX * 8.0 - sideX * 8.0)),
           static_cast<LONG>(std::lround(cy - forwardY * 8.0 - sideY * 8.0))},
      };
      PolygonFill(dc, triangle, 3, color, kWhite);
    }
    RestoreDC(dc, saved);
  }

  static bool DrawPixelSprite(HDC dc, Gdiplus::Bitmap *bitmap,
                              int cx, int cy, double angle,
                              int spriteSize) {
    if (bitmap == nullptr || spriteSize <= 0) return false;
    plane_pet_dpi::ImageCanvas imageCanvas(dc);
    auto &graphics = imageCanvas.graphics();
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const Gdiplus::GraphicsState state = graphics.Save();
    graphics.TranslateTransform(static_cast<Gdiplus::REAL>(cx),
                                static_cast<Gdiplus::REAL>(cy));
    graphics.RotateTransform(static_cast<Gdiplus::REAL>(
        angle * 180.0 / 3.14159265358979323846));
    const Gdiplus::Rect destination(-spriteSize / 2, -spriteSize / 2,
                                    spriteSize, spriteSize);
    const bool drawn = graphics.DrawImage(bitmap, destination) == Gdiplus::Ok;
    graphics.Restore(state);
    return drawn;
  }

  void GamePlane(HDC dc, int cx, int cy, bool up, COLORREF color, uint8_t health=3) const {
    constexpr int kGameSpriteSize = 36;
    const double angle = up ? 0.0 : 3.14159265358979323846;
    if (!DrawPixelSprite(dc, GamePlaneBitmap(color == kRed,health), cx, cy,
                         angle, kGameSpriteSize)) {
      GamePlaneFallback(dc, cx, cy, up, color);
    }
  }

  void PetPlane(HDC dc, int cx, int cy, double angle,
                COLORREF color) const {
    constexpr int kPetSpriteSize = 48;
    if (!DrawPixelSprite(dc, PetPlaneBitmap(color == kRed), cx, cy,
                         angle, kPetSpriteSize)) {
      PetPlaneFallback(dc, cx, cy, angle, color);
    }
  }

  static void DrawCloudFallback(HDC dc, int cx, int cy, int size) {
    const int s = std::max(1, size);
    const COLORREF fill = RGB(91, 111, 136);
    const COLORREF edge = RGB(137, 157, 181);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, cx - 13 * s, cy - 4 * s, cx + 13 * s, cy + 6 * s);
    Ellipse(dc, cx - 8 * s, cy - 9 * s, cx + 3 * s, cy + 5 * s);
    Ellipse(dc, cx, cy - 7 * s, cx + 10 * s, cy + 5 * s);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
  }

  void DrawCloud(HDC dc, int cx, int cy, int variant,
                 double angle) const {
    constexpr int spriteSizes[3] = {52, 64, 44};
    const int index = std::clamp(variant, 0, 2);
    if (!DrawPixelSprite(dc, PetCloudBitmap(index), cx, cy, angle,
                         spriteSizes[index])) {
      DrawCloudFallback(dc, cx, cy, index == 1 ? 2 : 1);
    }
  }

  static void DrawPixelProjectile(HDC dc, int cx, int cy,
                                  double directionX, double directionY,
                                  int scale, COLORREF edge, COLORREF body,
                                  COLORREF core, COLORREF exhaust) {
    const int s = std::max(1, scale);
    const double length = std::max(
        0.001, std::sqrt(directionX * directionX +
                         directionY * directionY));
    const double forwardX = directionX / length;
    const double forwardY = directionY / length;
    const double sideX = -forwardY;
    const double sideY = forwardX;
    const auto point = [&](double along, double side) {
      return POINT{
          static_cast<LONG>(std::lround(
              cx + (forwardX * along + sideX * side) * s)),
          static_cast<LONG>(std::lround(
              cy + (forwardY * along + sideY * side) * s))};
    };

    // A separated exhaust pixel and a short flame make direction readable
    // without turning the projectile into a blurred line.
    const POINT sparkStart = point(-12.0, 0.0);
    const POINT sparkEnd = point(-10.0, 0.0);
    DrawLine(dc, sparkStart.x, sparkStart.y, sparkEnd.x, sparkEnd.y,
             exhaust, std::max(1, s));
    POINT flame[4] = {point(-4.0, 0.0), point(-7.0, 1.5),
                      point(-10.0, 0.0), point(-7.0, -1.5)};
    PolygonFill(dc, flame, 4, exhaust, edge);

    // The dark six-point shell gives the bullet a crisp pixel silhouette;
    // the inset colored body and white center remain visible on any field.
    POINT shell[6] = {point(7.0, 0.0), point(3.0, 2.5),
                      point(-4.0, 2.5), point(-6.0, 0.0),
                      point(-4.0, -2.5), point(3.0, -2.5)};
    PolygonFill(dc, shell, 6, edge, edge);
    POINT inset[6] = {point(5.0, 0.0), point(2.0, 1.5),
                      point(-3.0, 1.5), point(-4.0, 0.0),
                      point(-3.0, -1.5), point(2.0, -1.5)};
    PolygonFill(dc, inset, 6, body, body);
    const POINT coreStart = point(-1.5, 0.0);
    const POINT coreEnd = point(3.5, 0.0);
    DrawLine(dc, coreStart.x, coreStart.y, coreEnd.x, coreEnd.y,
             core, std::max(1, s));
  }

  void DrawPetAnimation(HDC dc, int width, int height) const {
    const uint32_t elapsedMs = MillisSince(start_);
    const bool peerOnline = OpponentOnline();
    const PetFormation formation = PetFormationAt(elapsedMs);
    const PetAnimationPose pose = peerOnline ? formation.blue
                                             : PetPoseAt(elapsedMs);
    const double sideX = -pose.directionY;
    const double sideY = pose.directionX;
    constexpr double cloudLanes[4] = {-54.0, 44.0, -18.0, 67.0};
    constexpr double cloudOffsets[4] = {0.05, 0.34, 0.61, 0.82};

    // Clouds travel from the nose towards the tail to suggest forward speed.
    for (int index = 0; index < 4; ++index) {
      const double progress = std::fmod(
          static_cast<double>(elapsedMs) / 4400.0 + cloudOffsets[index], 1.0);
      const double along = 190.0 - progress * 380.0;
      const int cloudX = static_cast<int>(std::lround(
          pose.x + pose.directionX * along + sideX * cloudLanes[index]));
      const int cloudY = static_cast<int>(std::lround(
          pose.y + pose.directionY * along + sideY * cloudLanes[index]));
      if (cloudX > -35 && cloudX < width + 35 &&
          cloudY > -24 && cloudY < height + 24) {
        // D2 cloud streaks point to the right in their source pose, so rotate
        // their default leftward travel to the current nose-to-tail direction.
        DrawCloud(dc, cloudX, cloudY, index % 3,
                  pose.angle - 1.57079632679489661923);
      }
    }

    plane_pet_ui::ForEachIdleAutoShot(elapsedMs, idleShotSeed_, [&](uint32_t fired) {
      return peerOnline ? PetFormationAt(fired).blue : PetPoseAt(fired);
    }, [&](const plane_pet_ui::IdleShot &shot) {
      DrawPixelProjectile(dc, static_cast<int>(std::lround(shot.x)),
                          static_cast<int>(std::lround(shot.y)),
                          shot.dx, shot.dy, 1,
                          RGB(96, 43, 12), RGB(255, 190, 32),
                          RGB(255, 251, 194), RGB(255, 82, 24));
    });

    const int planeX = static_cast<int>(std::lround(pose.x));
    const int planeY = static_cast<int>(std::lround(pose.y));
    int peerX = 0;
    int peerY = 0;
    if (peerOnline) {
      peerX = static_cast<int>(std::lround(formation.red.x));
      peerY = static_cast<int>(std::lround(formation.red.y));
      PetPlane(dc, peerX, peerY, formation.red.angle, kRed);
    }
    PetPlane(dc, planeX, planeY, pose.angle, kBlue);

    const auto now = Clock::now();
    const bool ownEmoteVisible = ownEmote_ != 0 && now < ownEmoteUntil_;
    if (ownEmoteVisible)
      DrawQuickEmoteBubble(dc, planeX, planeY, ownEmote_, true,
                           MillisSince(ownEmoteStartedAt_));
    if (peerOnline && peerEmote_ != 0 && now < peerEmoteUntil_)
      DrawQuickEmoteBubble(dc, peerX, peerY, peerEmote_, false,
                           MillisSince(peerEmoteStartedAt_));

    for (const auto &badge : StatusBadges()) plane_pet_ui::DrawStatusBadge(dc, badge);
  }

  static void PolygonFill(HDC dc, POINT *points, int count, COLORREF fill,
                          COLORREF edge) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, count);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
  }

  static void DrawHeart(HDC dc, int centerX, int centerY, int scale,
                        COLORREF fill, COLORREF edge, bool continuousOutline = false) {
    const int radius = std::max(2, 3 * scale);
    POINT point[3] = {{centerX - radius - radius / 2, centerY},
                      {centerX + radius + radius / 2, centerY},
                      {centerX, centerY + 2 * radius}};
    if (continuousOutline) {
      // Fallback for both history and HUD: outline the UNION, not each
      // component (which would expose a horizontal seam). Regions and
      // FrameRgn use logical units and retain the info-window DPI mapping.
      HRGN shape = CreateEllipticRgn(centerX - radius - radius / 2, centerY - radius,
                                    centerX + radius / 2, centerY + radius);
      HRGN right = CreateEllipticRgn(centerX - radius / 2, centerY - radius,
                                    centerX + radius + radius / 2, centerY + radius);
      HRGN tip = CreatePolygonRgn(point, 3, WINDING);
      HBRUSH fillBrush = CreateSolidBrush(fill), edgeBrush = CreateSolidBrush(edge);
      const bool drawn = shape && right && tip && fillBrush && edgeBrush &&
          CombineRgn(shape, shape, right, RGN_OR) != ERROR &&
          CombineRgn(shape, shape, tip, RGN_OR) != ERROR &&
          FillRgn(dc, shape, fillBrush) && FrameRgn(dc, shape, edgeBrush,
                                                 std::max(1, scale), std::max(1, scale));
      if (shape) DeleteObject(shape);
      if (right) DeleteObject(right);
      if (tip) DeleteObject(tip);
      if (fillBrush) DeleteObject(fillBrush);
      if (edgeBrush) DeleteObject(edgeBrush);
      if (drawn) return;
    }
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, scale), edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, centerX - radius - radius / 2, centerY - radius,
            centerX + radius / 2, centerY + radius);
    Ellipse(dc, centerX - radius / 2, centerY - radius,
            centerX + radius + radius / 2, centerY + radius);
    Polygon(dc, point, 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
  }

  static void DrawMiniPlaneFallback(HDC dc, int cx, int cy, int scale,
                                    COLORREF color) {
    POINT plane[5] = {{cx, cy - 6 * scale},
                      {cx + 3 * scale, cy + scale},
                      {cx + 6 * scale, cy + 5 * scale},
                      {cx, cy + 3 * scale},
                      {cx - 6 * scale, cy + 5 * scale}};
    PolygonFill(dc, plane, 5, color, kWhite);
  }

  void DrawMiniPlane(HDC dc, int cx, int cy, int scale,
                     COLORREF color, uint8_t health=3) const {
    if (!DrawPixelSprite(dc, GamePlaneBitmap(color == kRed,health), cx, cy, 0.0,
                         24 * std::max(1, scale))) {
      DrawMiniPlaneFallback(dc, cx, cy, scale, color);
    }
  }

  void DrawHistoryPlane(HDC dc, int cx, int cy, COLORREF color) const {
    if (!DrawPixelSprite(dc, PetPlaneBitmap(color == kRed), cx, cy,
                         plane_pet_history_ui::kPlaneAngle,
                         plane_pet_history_ui::kPlaneSize)) {
      PetPlaneFallback(dc, cx, cy, plane_pet_history_ui::kPlaneAngle, color);
    }
  }

  void DrawGameHud(HDC dc, int scale, int ox, int oy,
                   uint8_t viewer, const plink::SnapshotPayload *presentation = nullptr) const {
    using namespace plane_pet_ui;
    RECT hud{ox, oy, ox + plink::kWorldWidth * scale,
             oy + kGameHudHeight * scale};
    Fill(dc, hud, RGB(27, 31, 43));
    DrawLine(dc, hud.left, hud.bottom - scale, hud.right,
             hud.bottom - scale, RGB(54, 67, 88), scale);

    const int savedHud = SaveDC(dc);
    IntersectClipRect(dc,hud.left,hud.top,hud.right,hud.bottom-scale);
    const int centerY = oy + kGameHeartY * scale;
    const auto &shown = presentation ? *presentation : snapshot_;
    const uint8_t ownHealth = haveSnapshot_
        ? std::min(shown.players[viewer].health, plink::kInitialHealth)
        : plink::kInitialHealth;
    const uint8_t peerHealth = haveSnapshot_
        ? std::min(shown.players[1U - viewer].health,
                   plink::kInitialHealth)
        : plink::kInitialHealth;
    DrawMiniPlane(dc,ox+kGameOwnPlaneX*scale,oy+kGamePlaneY*scale,scale,kBlue,ownHealth);
    DrawMiniPlane(dc,ox+kGamePeerPlaneX*scale,oy+kGamePlaneY*scale,scale,kRed,peerHealth);
    const double now=EffectNow();
    for (uint8_t i = 0; i < plink::kInitialHealth; ++i) {
      // Preserve the old mirrored depletion order, but retain grey slots.
      const bool ownFilled = i < ownHealth;
      const bool peerFilled = i >= plink::kInitialHealth - peerHealth;
      const int ownX = ox + (kGameOwnHeartX + i * kGameHeartPitch) * scale;
      const int peerX = ox + (kGamePeerHeartX + i * kGameHeartPitch) * scale;
      const int heartY = oy + kGameHeartY * scale;
      if (!DrawHeartImage(dc, ownX, heartY, kGameHeartWidth * scale,
                           kGameHeartHeight * scale, ownFilled ? 0 : 2))
        DrawHeart(dc, ownX, centerY, scale, ownFilled ? kBlue : RGB(42, 49, 64),
                  ownFilled ? RGB(160, 235, 255) : RGB(75, 84, 103), true);
      if (!DrawHeartImage(dc, peerX, heartY, kGameHeartWidth * scale,
                           kGameHeartHeight * scale, peerFilled ? 1 : 2))
        DrawHeart(dc, peerX, centerY, scale, peerFilled ? kRed : RGB(42, 49, 64),
                  peerFilled ? RGB(255, 150, 160) : RGB(75, 84, 103), true);
      const double ownAge = ownFilled ? -1 : combatFeedback_.HeartAge(viewer,i,now);
      const double peerAge = peerFilled ? -1 : combatFeedback_.HeartAge(1U-viewer,2U-i,now);
      const auto drawBreak = [&](double age,unsigned color,int x) {
        if(age<0) return;
        using namespace plane_pet_feedback;
        const unsigned frame=std::min(kHeartFrames-1,static_cast<unsigned>(age*kHz));
        DrawFeedbackBitmap(dc,HeartFeedbackBitmap(color,frame),x-kHeartOriginX*scale,
            heartY-kHeartOriginY*scale,scale);
      };
      drawBreak(ownAge,0,ownX); drawBreak(peerAge,1,peerX);
    }

    for(unsigned side=0;side<2;++side) {
      const double age=combatFeedback_.HitAge(side?1U-viewer:viewer,now);
      if(age<0) continue;
      using namespace plane_pet_feedback;
      const unsigned frame=std::min(kHitFrames-1,static_cast<unsigned>(age*kHz));
      const int x=side?kGamePeerHitX:kGameOwnHitX;
      DrawFeedbackBitmap(dc,HitFeedbackBitmap(frame),ox+(x-kHitCellWidth/2)*scale,
          oy+(kGameHitY-kHitCellHeight/2)*scale,scale);
    }

    wchar_t timer[20]{};
    if (shown.phase == plink::GamePhase::Playing &&
        shown.phaseRemainingMs > 0) {
      const uint32_t seconds = (shown.phaseRemainingMs + 999U) / 1000U;
      swprintf(timer, std::size(timer), L"%us", seconds);
    } else if (shown.phase == plink::GamePhase::Playing ||
               shown.phase == plink::GamePhase::Finished) {
      const uint32_t elapsed = shown.matchElapsedMs / 1000U;
      if (elapsed < 3600U) {
        if (shown.phase == plink::GamePhase::Playing)
          swprintf(timer, std::size(timer), L"0s");
        else
          swprintf(timer, std::size(timer), L"%02u:%02u",
                   elapsed / 60U, elapsed % 60U);
      } else {
        swprintf(timer, std::size(timer), L"%u:%02u:%02u",
                 elapsed / 3600U, (elapsed / 60U) % 60U, elapsed % 60U);
      }
    } else {
      swprintf(timer, std::size(timer), L"--:--");
    }
    RECT timerRect{ox + kGameTimerLeft * scale, oy + 2 * scale, ox + kGameTimerRight * scale,
                   oy + 27 * scale};
    Text(dc, timer, timerRect, std::max(18, 10 * scale), kWhite, FW_BOLD);
    wchar_t latency[16]{};
    swprintf(latency, 16, L"%ums", rttMs_);
    RECT latencyRect{ox + 76 * scale, oy + 27 * scale,
                     ox + 164 * scale, oy + 49 * scale};
    Text(dc, latency, latencyRect, std::max(16, 8 * scale), kMuted, FW_BOLD);
    RestoreDC(dc,savedHud);
  }

  void DrawToolbarEmotes(HDC dc, bool enabled, int offset = 0) const {
    for (int index = 0; index < 4; ++index) {
      RECT button = QuickEmoteRect(index);
      OffsetRect(&button, offset, 0);
      RoundPanel(dc, button, 15,
                 enabled ? RGB(28, 37, 53) : RGB(25, 29, 38),
                 enabled ? RGB(91, 146, 190) : RGB(60, 66, 78), 1);
      const RECT icon = plane_pet_ui::ToolbarEmoteIcon(button);
      if (!DrawQuickEmoteImage(dc, static_cast<uint8_t>(index + 1), icon, enabled))
        TextWithFace(dc, QuickEmoteGlyph(static_cast<uint8_t>(index + 1)),
                     button, 24, enabled ? kWhite : RGB(110, 116, 128),
                     L"Segoe UI Emoji", FW_NORMAL);
    }
  }

  void DrawPetToolbar(HDC dc) const {
    if (!IsPetToolbarVisible()) return;
    using namespace plane_pet_ui;
    const bool enabled = CanInvite();
    const double reveal = toolbar_.Reveal(GetTickCount64());
    if (reveal < 1.0) {
      RoundPanel(dc, kToolbarPlay, 15,
                 toolbarPressed_ == 1 ? RGB(28, 57, 77) : RGB(19, 27, 41),
                 enabled ? RGB(56, 156, 222) : RGB(60, 66, 78), 1);
      auto *fist = EmbeddedBitmap(kToolbarFistResource, challengeFistImage_);
      if (fist) {
        plane_pet_dpi::ImageCanvas canvas(dc);
        auto &graphics = canvas.graphics();
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::ColorMatrix matrix = {
          1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0,
          0,0,0,enabled ? 1.0f : 0.38f,0, 0,0,0,0,1};
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&matrix);
        graphics.DrawImage(fist, Gdiplus::Rect(kToolbarFist.left, kToolbarFist.top,
            kToolbarFist.right - kToolbarFist.left, kToolbarFist.bottom - kToolbarFist.top),
            0, 0, fist->GetWidth(), fist->GetHeight(), Gdiplus::UnitPixel, &attributes);
      } else {
        TextWithFace(dc, L"👊", kToolbarFist, 23,
                     enabled ? kYellow : kMuted, L"Segoe UI Emoji");
      }
      plane_pet_text::PetText(dc, L"开一局", kToolbarPlayText, 16,
          enabled ? kWhite : RGB(110, 116, 128), FW_BOLD,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (reveal >= 1.0) {
      // Final expanded pixels match the original four buttons, including gaps.
      DrawToolbarEmotes(dc, CanSendQuickEmote());
    } else if (reveal > 0.0) {
      const int width = kToolbarPlay.right - kToolbarPlay.left;
      const int offset = width - static_cast<int>(std::lround(width * reveal));
      const int saved = SaveDC(dc);
      if (saved) {
        IntersectClipRect(dc, kToolbarPlay.left + offset, kToolbarPlay.top,
                         kToolbarPlay.right, kToolbarPlay.bottom);
        Fill(dc, kToolbarPlay, RGB(19, 27, 41));
        DrawToolbarEmotes(dc, CanSendQuickEmote(), offset);
        RestoreDC(dc, saved);
      }
    }
    RoundPanel(dc, kToolbarToggle, 15,
        toolbarPressed_ == 2 ? RGB(28, 57, 77) : RGB(19, 27, 41), RGB(56, 156, 222), 1);
    const int cx = (kToolbarToggle.left + kToolbarToggle.right) / 2;
    const int cy = (kToolbarToggle.top + kToolbarToggle.bottom) / 2;
    const int direction = toolbar_.expanded ? 1 : -1;
    POINT triangle[3]{{cx + direction * 4, cy},
                      {cx - direction * 3, cy - 5}, {cx - direction * 3, cy + 5}};
    HBRUSH brush = CreateSolidBrush(kBlueLight);
    const auto oldBrush = SelectObject(dc, brush);
    const auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, triangle, 3);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(brush);
  }

  void DrawPet(HDC dc, int width, int height) const {
    // Card text has its own explicit em-pixel sizing. Do not change the game,
    // help/history, sprite rendering, or window geometry along with it.
    using plane_pet_text::PetText;
    if ((awaitingPairing_ || pairingAttempted_) && pairingPanelVisible_) {
      DrawPetAnimation(dc, width, height);
      RECT card{84, 6, width - 6, height - 6};
      RoundPanel(dc, card, 16, kCard, kCardEdge, 2);
      POINT pointer[3] = {{84, 36}, {76, 48}, {84, 60}};
      HBRUSH pointerBrush = CreateSolidBrush(kCard);
      HPEN pointerPen = CreatePen(PS_SOLID, 1, kCardEdge);
      HGDIOBJ oldPointerBrush = SelectObject(dc, pointerBrush);
      HGDIOBJ oldPointerPen = SelectObject(dc, pointerPen);
      Polygon(dc, pointer, 3);
      SelectObject(dc, oldPointerBrush);
      SelectObject(dc, oldPointerPen);
      DeleteObject(pointerBrush);
      DeleteObject(pointerPen);

      HBRUSH pairingDot = CreateSolidBrush(pairingAttempted_ ? kYellow : kBlue);
      HGDIOBJ oldPairingBrush = SelectObject(dc, pairingDot);
      Ellipse(dc, 96, 17, 108, 29);
      SelectObject(dc, oldPairingBrush);
      DeleteObject(pairingDot);

      RECT title{114, 7, width - 12, 38};
      PetText(dc, pairingAttempted_ ? L"搜索电脑中" : L"绑定电脑",
           title, 18, pairingAttempted_ ? kYellow : kBlueLight, FW_BOLD,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      if (pairingAttempted_) {
        wchar_t code[32]{};
        swprintf(code, 32, L"匹配码  %06u", pairingCode_);
        RECT codeRect{96, 39, width - 12, 70};
        PetText(dc, code, codeRect, 19, kWhite, FW_BOLD,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT status{96, 71, width - 12, 113};
        PetText(dc,
             serverConfirmedWaiting_
                 ? L"等待同码电脑…\n右键或 Esc 停止"
                 : L"连接匹配服务…\n右键或 Esc 停止",
             status, 12, kMuted, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
        RECT back{96, 122, width - 14, 135};
        RoundPanel(dc, back, 8, RGB(45, 51, 66), RGB(69, 77, 96));
        const int barWidth = static_cast<int>(back.right - back.left);
        const int span = std::max(18, barWidth / 3);
        const int travel = std::max(1, barWidth - span);
        const int offset = static_cast<int>(MillisSince(start_) / 8U) % travel;
        RECT progress{back.left + offset, back.top,
                      back.left + offset + span, back.bottom};
        RoundPanel(dc, progress, 8, kYellow, kYellow);
      } else {
        const bool dndFeedback = !dndNotice_.empty() && Clock::now() < dndNoticeUntil_;
        const bool errorPrompt = !dndFeedback && !pairingError_.empty();
        // A two-line error needs two full font cells. Reuse the existing gap
        // above the input, without moving the input or its hit rectangle.
        RECT prompt{96, errorPrompt ? 30 : 34, width - 12, errorPrompt ? 66 : 64};
        PetText(dc, dndFeedback ? dndNotice_ : (pairingError_.empty() ? L"请输入六位匹配码" : pairingError_),
             prompt, dndFeedback ? 13 : (pairingError_.empty() ? 15 : 12),
             dndFeedback ? kWhite : (pairingError_.empty() ? kMuted : kRed), FW_NORMAL,
             DT_LEFT | DT_VCENTER | DT_WORDBREAK);

        std::wstring digits;
        for (size_t index = 0; index < 6; ++index) {
          if (index != 0) digits += L"  ";
          digits += index < pairingInput_.size() ? pairingInput_[index] : L'_';
        }
        RECT input{96, 66, width - 14, 99};
        RoundPanel(dc, input, 10, RGB(10, 14, 24), RGB(82, 108, 148), 2);
        PetText(dc, digits, input, 20, kWhite, FW_BOLD,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT hint{96, 103, width - 12, 138};
        PetText(dc, L"点击输入 · Enter", hint, 15, kBlueLight,
             FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      }
      return;
    }

    DrawPetAnimation(dc, width, height);
    DrawPetToolbar(dc);
    if (!connectionNotice_.empty() &&
        Clock::now() < connectionNoticeUntil_) {
      RECT notice{58, 76, width - 58, 121};
      RoundPanel(dc, notice, 12, kCard, kYellow, 2);
      RECT message{64, 79, width - 64, 118};
      PetText(dc, connectionNotice_, message, 14, kWhite, FW_BOLD,
           DT_CENTER | DT_VCENTER | DT_WORDBREAK);
      return;
    }
    if (HasDndNotice()) {
      const auto &layout = plane_pet_ui::kDndNoticeLayout;
      RoundPanel(dc, layout.panel, 14, kCard, kCardEdge, 2);
      if (dndNoticeCancelable_) {
        PetText(dc, dndNotice_, layout.message, 14, kWhite, FW_BOLD,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(dndNoticeUntil_ - Clock::now()).count();
        PetText(dc, std::to_wstring(std::clamp<int64_t>((remaining + 999) / 1000, 1, 3)) + L"s",
             layout.countdown, 14, kYellow, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RoundPanel(dc, layout.primary, 10, RGB(74, 38, 44), kRed, 2);
        PetText(dc, L"取消", layout.primary, 14, kWhite, FW_BOLD,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      } else {
        PetText(dc, dndNotice_, plane_pet_ui::kDndStatusTextRect,
             plane_pet_ui::kDndStatusFontEmHeight, kWhite, FW_BOLD,
             plane_pet_ui::kDndStatusTextFormat);
      }
      return;
    }
    if (Phase() != plink::GamePhase::Waiting && !inviteFeedback_.empty() &&
        Clock::now() < inviteFeedbackUntil_) {
      const RECT &panel = plane_pet_ui::kInviteFeedbackPanel;
      const COLORREF edge = inviteFeedback_ == L"对方拒绝邀请"
          ? kRed : kYellow;
      RoundPanel(dc, panel, 14, kCard, edge, 2);
      const RECT &message = plane_pet_ui::kInviteFeedbackMessage;
      PetText(dc, inviteFeedback_, message, 15, edge, FW_BOLD,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return;
    }
    if (Phase() != plink::GamePhase::Waiting) return;

    if (IsIncomingInvite()) {
      if (doNotDisturb_) return;
      const auto &layout = plane_pet_ui::kIncomingInviteLayout;
      RoundPanel(dc, layout.panel, 14, kCard, kCardEdge, 2);
      PetText(dc, L"对方邀请你开一局...", layout.message, 14, kWhite, FW_BOLD,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      const RECT &accept = layout.primary;
      const RECT &reject = layout.secondary;
      RoundPanel(dc, accept, 10, RGB(24, 116, 70), kGreen, 2);
      RoundPanel(dc, reject, 10, RGB(126, 35, 48), kRed, 2);
      PetText(dc, L"接受", accept, 14, kWhite, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      PetText(dc, L"拒绝", reject, 14, kWhite, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return;
    }

    const auto &layout = plane_pet_ui::kOutgoingInviteLayout;
    RoundPanel(dc, layout.panel, 14, kCard, kCardEdge, 2);
    const size_t dotCount = static_cast<size_t>((MillisSince(start_) / 380U) % 4U);
    std::wstring waiting = L"等待对方响应";
    waiting.append(dotCount, L'.');
    PetText(dc, waiting, layout.message, 14, kWhite, FW_BOLD,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const uint32_t remainingSeconds =
        (snapshot_.phaseRemainingMs + 999U) / 1000U;
    wchar_t countdown[16]{};
    swprintf(countdown, std::size(countdown), L"%us", remainingSeconds);
    PetText(dc, countdown, layout.countdown, 14, kYellow, FW_BOLD,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const RECT &cancel = layout.primary;
    RoundPanel(dc, cancel, 10, RGB(74, 38, 44), kRed, 2);
    PetText(dc, L"取消", cancel, 14, kWhite, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  void DrawDamageWarning(HDC dc, const GameLayout &layout,
                         const plink::SnapshotPayload &visual, unsigned viewer) const {
    using namespace plane_pet_feedback;
    const double alpha=combatFeedback_.WarningAlpha(viewer,EffectNow(),
        haveSnapshot_ && !syncFailed_ && visual.phase==plink::GamePhase::Playing);
    if (alpha<=0) return;
    const int width=plink::kWorldWidth*layout.scale;
    const int edgeWidth=kWarningWidth*layout.scale;
    plane_pet_dpi::ImageCanvas canvas(dc);
    auto &g=canvas.graphics();
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.SetClip(Gdiplus::Rect(layout.left,layout.playTop,width,kGamePlayHeight*layout.scale),
              Gdiplus::CombineModeIntersect);
    for(int x=0;x<edgeWidth;++x) {
      const auto opacity=static_cast<BYTE>(std::lround(255*alpha*SideEdgeFalloff(x,edgeWidth)));
      if(!opacity) continue;
      Gdiplus::SolidBrush brush(Gdiplus::Color(opacity,238,25,45));
      g.FillRectangle(&brush,layout.left+x,layout.playTop,1,kGamePlayHeight*layout.scale);
      g.FillRectangle(&brush,layout.left+width-1-x,layout.playTop,1,kGamePlayHeight*layout.scale);
    }
  }

  Gdiplus::Bitmap *ResultTitleBitmap(bool win) const {
    using namespace plane_pet_result;
    auto *bitmap=EmbeddedBitmap(win?kWinResource:kLostResource,resultTitleImages_[win?0:1]);
    if(!bitmap || bitmap->GetWidth()!=unsigned(kWidth)*(win?kColumns:1U) ||
       bitmap->GetHeight()!=unsigned(kHeight)*(win?kRows:1U)) return nullptr;
    return bitmap;
  }

  bool DrawResultTitle(HDC dc,const GameLayout &layout,const plink::SnapshotPayload &visual) const {
    using namespace plane_pet_result;
    const auto art=Select(visual.phase==plink::GamePhase::Finished,
        fxDeferredMenu_?fxResultSyncFailed_:syncFailed_,slot_,visual.winnerSlot);
    if(art==Art::None) return false;
    const bool win=art==Art::Win;
    auto *bitmap=ResultTitleBitmap(win); if(!bitmap) return false;
    const unsigned frame=win?resultTimeline_.Frame(EffectNow()):0;
    plane_pet_dpi::ImageCanvas canvas(dc);
    auto &g=canvas.graphics();
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetClip(Gdiplus::Rect(layout.left,layout.playTop,240*layout.scale,320*layout.scale),Gdiplus::CombineModeIntersect);
    return g.DrawImage(bitmap,Gdiplus::Rect(layout.left+kLeft*layout.scale,
        layout.ScreenY(kWorldTop),kWidth*layout.scale,kHeight*layout.scale),
        (frame%kColumns)*kWidth,(frame/kColumns)*kHeight,kWidth,kHeight,Gdiplus::UnitPixel)==Gdiplus::Ok;
  }

  void DrawGameWindow(HDC dc, int width, int height) const {
    const GameLayout layout = MakeGameLayout(width, height);
    const int scale = layout.scale;
    const int ox = layout.left;
    RECT playRect{ox, layout.playTop, ox + plink::kWorldWidth * scale,
                  layout.PlayBottom()};
    const int savedPlayfield = SaveDC(dc);
    IntersectClipRect(dc, playRect.left, playRect.top,
                     playRect.right, playRect.bottom);
    Fill(dc, playRect, RGB(7, 10, 19));
    DrawLine(dc, playRect.left, playRect.top, playRect.right, playRect.top,
             RGB(76, 94, 124), std::max(1, scale));
    for (int i = 0; i < 28; ++i) {
      const int x = (i * 53 + 17) % plink::kWorldWidth;
      const int y = (i * 97 + 7) % kGamePlayHeight;
      const int screenY = layout.ScreenY(y);
      RECT star{ox + x * scale, screenY,
                ox + x * scale + std::max(1, scale / 2),
                screenY + std::max(1, scale / 2)};
      Fill(dc, star, RGB(90, 96, 112));
    }
    const uint8_t viewer = slot_ - 1;
    const auto visual = GamePresentation();
    const auto own = plink::ToLocalView(visual.players[viewer], viewer);
    const auto peer = plink::ToLocalView(visual.players[1 - viewer], viewer);
    // Background warning never tints the HUD, top/bottom strips, aircraft or
    // bullets. Collision geometry and the full playfield remain unchanged.
    DrawDamageWarning(dc,layout,visual,viewer);
    DrawGameEffects(dc, layout, visual, false);
    const double fxNow = EffectNow();
    if (!gameEffects_.HidePlane(viewer, fxNow))
      GamePlane(dc, ox + own.x * scale, layout.ScreenY(own.y), true, kBlue,own.health);
    if (!gameEffects_.HidePlane(1 - viewer, fxNow))
      GamePlane(dc, ox + peer.x * scale, layout.ScreenY(peer.y), false, kRed,peer.health);
    if (haveSnapshot_) {
      for (uint8_t i = 0; i < visual.bulletCount; ++i) {
        const plink::BulletState bullet =
            plink::ToLocalView(visual.bullets[i], viewer);
        const int bx = ox + bullet.x * scale;
        const int by = layout.ScreenY(bullet.y);
        const bool ownBullet = bullet.owner == slot_;
        DrawPixelProjectile(
            dc, bx, by, 0.0, ownBullet ? -1.0 : 1.0, scale,
            ownBullet ? RGB(0, 72, 110) : RGB(105, 12, 76),
            ownBullet ? RGB(0, 235, 255) : RGB(255, 48, 193),
            ownBullet ? RGB(220, 255, 255) : RGB(255, 224, 246),
            ownBullet ? RGB(68, 137, 255) : RGB(255, 112, 55));
      }
    }
    DrawGameEffects(dc, layout, visual, true);
    RestoreDC(dc, savedPlayfield);
    DrawGameHud(dc, scale, ox, layout.top, viewer, &visual);
    if (visual.phase == plink::GamePhase::Countdown) {
      const uint32_t count = std::max(1U,
          (snapshot_.phaseRemainingMs + 999U) / 1000U);
      wchar_t value[4]{};
      swprintf(value, 4, L"%u", count);
      RECT center{ox, layout.ScreenY(100),
                  ox + plink::kWorldWidth * scale, layout.ScreenY(205)};
      Text(dc, value, center, std::max(76, 39 * scale), kWhite, FW_BOLD);
    } else if (visual.phase == plink::GamePhase::Finished) {
      const bool draw = visual.winnerSlot == 0;
      const bool won = visual.winnerSlot == slot_;
      const bool failed = fxDeferredMenu_ ? fxResultSyncFailed_ : syncFailed_;
      const wchar_t *result = failed ? L"本局中止" : (draw ? L"平局" : (won ? L"WIN!" : L"LOST!"));
      RECT center{ox, layout.ScreenY(100),
                  ox + plink::kWorldWidth * scale, layout.ScreenY(180)};
      if(!DrawResultTitle(dc,layout,visual))
        Text(dc, result, center, std::max(48, 23 * scale),
             draw ? kYellow : (won ? kGreen : kRed), FW_BOLD);
      const wchar_t *reason = L"";
      if (visual.endReason == plink::MatchEndReason::TimeLimitDraw) {
        reason = L"达到 3 分钟，本局平局";
      } else if (visual.endReason ==
                 plink::MatchEndReason::PlayerDisconnected) {
        reason = visual.winnerSlot == slot_
            ? L"对方断线，本局结束" : L"连接中断，本局结束";
      } else if (visual.endReason ==
                 plink::MatchEndReason::ServerUnavailable) {
        reason = failed ? L"网络或运行卡顿，同步中断" : L"服务器连接中断，本局结束";
      }
      RECT reasonRect{ox, layout.ScreenY(174),
                      ox + plink::kWorldWidth * scale, layout.ScreenY(205)};
      Text(dc, reason, reasonRect, std::max(18, 9 * scale), kWhite, FW_BOLD);
      RECT hint{ox, layout.ScreenY(207),
                ox + plink::kWorldWidth * scale, layout.ScreenY(242)};
      Text(dc, L"单击或按任意键返回", hint, std::max(19, 10 * scale),
           kWhite, FW_BOLD);
    }
  }

  HWND window_ = nullptr;
  SOCKET socket_ = INVALID_SOCKET;
  sockaddr_in server_{};
  std::string serverText_;
  pcpair::AuthKey networkKey_{};
  std::filesystem::path statePath_;
  std::filesystem::path eventsPath_;
  std::filesystem::path historyPath_;
  std::filesystem::path settingsPath_;
  std::filesystem::path updateRequestPath_;
  std::wstring ownName_;
  std::wstring peerName_;
  Clock::time_point start_{};
  Clock::time_point nextPairing_{};
  Clock::time_point nextInput_{};
  Clock::time_point nextPing_{};
  Clock::time_point nextPrediction_{};
  Clock::time_point nextAction_{};
  Clock::time_point nextUnbind_{};
  Clock::time_point lastPacketAt_{};
  Clock::time_point pairingSubmittedAt_{};
  Clock::time_point incomingInviteAt_{};
  Clock::time_point outgoingInviteAt_{};
  Clock::time_point connectionNoticeUntil_{};
  Clock::time_point inviteFeedbackUntil_{};
  Clock::time_point lastEmoteSentAt_{};
  Clock::time_point ownEmoteStartedAt_{};
  Clock::time_point peerEmoteStartedAt_{};
  Clock::time_point ownEmoteUntil_{};
  Clock::time_point peerEmoteUntil_{};
  Clock::time_point nextTelemetryHeartbeat_{};
  std::wstring pairingInput_;
  std::wstring pairingError_;
  std::wstring connectionNotice_;
  std::wstring inviteFeedback_;
  uint32_t clientId_ = 0;
  uint32_t pairingCode_ = 123456;
  uint32_t pairingRequestId_ = 0;
  uint32_t lastCancelledRequest_ = 0;
  uint32_t unbindRequestId_ = 0;
  uint32_t pendingOperationId_ = 0;
  pcpair::RoundMeta pendingActionContext_{};
  Clock::time_point pendingActionStarted_{};
  plink::GamePhase roundMetaPhase_ = plink::GamePhase::Menu;
  bool haveRoundMeta_ = false;
  bool scopedActionsSupported_ = false;
  bool pairingCancelPending_ = false;
  bool updating_ = false;
  UINT dpi_ = 96;
  uint32_t resumeRequestId_ = 0;
  uint32_t bindingId_ = 0;
  uint32_t tokenLow_ = 0;
  uint32_t tokenHigh_ = 0;
  uint32_t peerDeviceId_ = 0;
  uint32_t session_ = 0;
  uint32_t sequence_ = 0;
  uint32_t lastServerSequence_ = 0;
  uint32_t lastActionSequence_ = 0;
  uint32_t lastServerTick_ = 0;
  uint32_t rttMs_ = 0;
  uint32_t historyTotal_ = 0;
  uint32_t historyWins_ = 0;
  uint32_t historyLosses_ = 0;
  uint32_t historyDraws_ = 0;
  uint64_t currentRoundId_ = 0;
  uint64_t currentInviteId_ = 0;
  uint64_t lastRecordedRoundId_ = 0;
  uint64_t telemetrySessionId_ = 0;
  int64_t telemetryActiveMillis_ = 0;
  Clock::time_point telemetryActiveSince_{};
  bool telemetrySegmentOpen_ = false;
  uint64_t telemetryEventSequence_ = 0;
  uint64_t updateSnoozeUntilMs_ = 0;
  uint64_t lastUpdateGeneration_ = 0;
  uint64_t lastUpdatePromptGeneration_ = 0;
  plane_pet_update::State lastUpdateState_ = plane_pet_update::State::Disabled;
  uint8_t slot_ = 1;
  uint8_t currentInput_ = 0;
  uint8_t inputHistory_[3]{};
  uint8_t pendingAction_ = 0;
  uint8_t ownEmote_ = 0;
  uint8_t peerEmote_ = 0;
  int petX_ = 100;
  int petY_ = 100;
  int mouseTargetX_ = plink::kWorldWidth / 2;
  int mouseTargetY_ = plink::kWorldHeight / 2;
  bool hiddenByUser_ = false;
  bool hiddenBeforeInvite_ = false;
  bool emergencyHidden_ = false;
  bool returnToPetRequested_ = false;
  bool gameMode_ = false;
  bool mouseDragging_ = false;
  bool petWindowDragging_ = false;
  POINT petDragStartCursor_{};
  POINT petDragStartClient_{};
  bool petDragHasScreenCursor_ = false;
  int petDragStartX_ = 0;
  int petDragStartY_ = 0;
  bool autoInvite_ = false;
  bool autoAccept_ = false;
  bool testAutoUnbind_ = false;
  bool autoInviteSent_ = false;
  bool autoAcceptSent_ = false;
  bool testAutoUnbindSent_ = false;
  bool awaitingPairing_ = false;
  bool pairingAttempted_ = false;
  bool pairingPanelVisible_ = true;
  bool serverConfirmedWaiting_ = false;
  bool unbindPending_ = false;
  bool haveSnapshot_ = false;
  bool predictionReady_ = false;
  bool telemetryEnabled_ = true;
  bool telemetryChoiceKnown_ = false;
  bool telemetryUploadCapable_ = false;
  int telemetryUploadChoice_ = -1;
  bool doNotDisturb_ = false;
  bool dndSupported_ = false;
  bool peerDndKnown_ = false;
  bool peerDndCapable_ = false;
  bool peerDnd_ = false;
  uint32_t dndRevision_ = 1;
  uint32_t dndAcknowledged_ = 0;
  uint32_t dndAttemptOperation_ = 0;
  uint64_t lastDndInterruptedInvite_ = 0;
  Clock::time_point dndAttemptUntil_{};
  Clock::time_point nextDndSync_{};
  Clock::time_point dndPresenceAt_{};
  Clock::time_point dndNoticeUntil_{};
  std::wstring dndNotice_;
  bool dndNoticeCancelable_ = false;
  Clock::time_point dndUsageSince_{};
  int64_t dndUsageMillis_ = 0;
  bool dndUsageOpen_ = false;
  bool historyRecordedForRound_ = false;
  bool historyPersistenceOk_ = true;
  bool statePersistenceOk_ = true;
  bool stateRecoveredFromBackup_ = false;
  bool abandonedMatch_ = false;
  pcpair::AppVersion peerAppVersion_{};
  bool peerVersionKnown_ = false;
  bool peerVersionDiffers_ = false;
  bool majorVersionMismatch_ = false;
  bool localUpdateRequired_ = false;
  bool peerOnlineKnown_ = false;
  bool lastPeerOnline_ = false;
  plane_pet_update::Manager updateManager_{};
  mutable EmbeddedImageResource emojiImages_[4];
  mutable EmbeddedImageResource petPlaneImages_[2];
  mutable EmbeddedImageResource damagedPlaneImages_[4];
  mutable EmbeddedImageResource petCloudImages_[3];
  mutable EmbeddedImageResource heartAtlasImage_;
  mutable std::array<std::unique_ptr<Gdiplus::Bitmap>, 3> hudHeartImages_;
  mutable EmbeddedImageResource hitWordImage_;
  mutable EmbeddedImageResource resultTitleImages_[2];
  mutable std::array<std::unique_ptr<Gdiplus::Bitmap>,plane_pet_feedback::kHitFrames> hitFeedbackFrames_;
  mutable std::array<std::array<std::unique_ptr<Gdiplus::Bitmap>,plane_pet_feedback::kHeartFrames>,2> heartFeedbackFrames_;
  mutable EmbeddedImageResource challengeFistImage_;
  mutable EmbeddedImageResource effectImages_[3];
  mutable std::array<std::array<std::unique_ptr<Gdiplus::Bitmap>, 12>, 3> effectFrames_;
  plane_pet_ui::PetToolbarState toolbar_;
  uint32_t idleShotSeed_ = 1;
  int toolbarPressed_ = 0;
  POINT toolbarPressPoint_{};
  std::vector<HistoryEntry> recentHistory_;
  std::string pendingUpdateToken_;
  Clock::time_point nextUpdateHealthRead_{};
  plink::SnapshotPayload snapshot_{};
  plink::PlayerState predicted_{};
  pcmotion::Predictor motionPredictor_;
  pcmotion::Timeline motionTimeline_;
  pcbattle::Predictor battlePredictor_;
  pcbattle::Events battleEvents_;
  plane_pet_fx::State gameEffects_;
  plane_pet_feedback::State combatFeedback_;
  plane_pet_result::Timeline resultTimeline_;
  plink::SnapshotPayload fxResult_{};
  plink::GamePhase fxLastPhase_ = plink::GamePhase::Menu;
  std::array<uint8_t, 2> fxPreviousHealth_{{3, 3}};
  bool fxDeferredMenu_ = false, fxReturnPending_ = false, fxHaveHealth_ = false;
  bool fxResultSyncFailed_ = false;
#ifdef PLANE_PET_EFFECTS_SELF_TEST
  double fxTestNow_ = -1;
#endif
  bool battleSupported_ = false;
  uint32_t battleStateSequence_ = 0;
  bool motionSupported_ = false, serverCapabilitiesKnown_ = false;
  bool motionFault_ = false, syncFailed_ = false;
  uint64_t motionRound_ = 0;
  int64_t motionRemainder_ = 0;
  Clock::time_point motionClock_{};
};

PetClient gClient;
HWND gStatusTooltip = nullptr;
std::wstring gStatusTooltipText;

void CreateStatusTooltip(HWND owner) {
  // Load the Windows tooltip class without adding a new redistributable or
  // link dependency to every existing isolated regression executable.
  static HMODULE controls = LoadLibraryW(L"comctl32.dll");
  if (!controls) return;
  using InitializeControls = BOOL (WINAPI *)(const INITCOMMONCONTROLSEX *);
  const auto address = GetProcAddress(controls, "InitCommonControlsEx");
  InitializeControls initialize = nullptr;
  static_assert(sizeof(address) == sizeof(initialize));
  std::memcpy(&initialize, &address, sizeof(initialize));
  INITCOMMONCONTROLSEX config{sizeof(config), ICC_WIN95_CLASSES};
  if (!initialize || !initialize(&config)) return;
  gStatusTooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW,
      nullptr, WS_POPUP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!gStatusTooltip) return;
  TOOLINFOW tool{};
  tool.cbSize = sizeof(tool);
  tool.uFlags = TTF_SUBCLASS;
  tool.hwnd = owner;
  tool.lpszText = LPSTR_TEXTCALLBACKW;
  for (UINT_PTR id : {1U, 2U}) {
    tool.uId = id;
    SendMessageW(gStatusTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
  }
  SendMessageW(gStatusTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
  SendMessageW(gStatusTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
  SendMessageW(gStatusTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 3000);
}
void RefreshStatusTooltip(HWND owner) {
  if (!gStatusTooltip) return;
  const auto badges = gClient.StatusBadges();
  for (size_t index = 0; index < badges.size(); ++index) {
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool); tool.hwnd = owner; tool.uId = index + 1;
    if (!gClient.IsGameMode() && IsWindowVisible(owner) && badges[index].visible && !gClient.HasDndNotice())
      tool.rect = badges[index].Bounds();
    SendMessageW(gStatusTooltip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}
NOTIFYICONDATAW gTray{};
bool gTrayAdded = false;
HWND gInfoWindow = nullptr;
HWND gUpdateWindow = nullptr;
UINT gUpdateDpi = 96;
uint64_t gUpdateContentGeneration = UINT64_MAX;
uint32_t gUpdateContentProgress = UINT32_MAX;

void UpdateCardText(HDC dc, const std::wstring &text, RECT rect, int height,
                    COLORREF color, int weight = FW_NORMAL,
                    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE,
                           L"Microsoft YaHei UI");
  HGDIOBJ previous = SelectObject(dc, font);
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
  SelectObject(dc, previous);
  DeleteObject(font);
}

void UpdateCardPanel(HDC dc, RECT rect, COLORREF fill, COLORREF edge) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, edge);
  HGDIOBJ oldBrush = SelectObject(dc, brush);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(pen);
  DeleteObject(brush);
}

constexpr int kUpdateCardWidth = 360;
constexpr int kUpdateCardHeight = 244;
constexpr RECT kUpdatePrimaryButton{24, 194, 171, 229};
constexpr RECT kUpdateSecondaryButton{189, 194, 336, 229};
constexpr RECT kUpdateCloseButton{326, 12, 348, 34};
constexpr RECT kUpdateCloseHitRect{318, 4, 356, 42};
enum class UpdateCardButton { None, Close, Download, Retry, Secondary };
UpdateCardButton gUpdatePressedButton = UpdateCardButton::None;

UpdateCardButton UpdateCardButtonAt(POINT point,
                                    const plane_pet_update::Snapshot &state) {
  if (PtInRect(&kUpdateCloseHitRect, point)) return UpdateCardButton::Close;
  if (PtInRect(&kUpdatePrimaryButton, point)) {
    if (state.canDownload) return UpdateCardButton::Download;
    if (state.state == plane_pet_update::State::Error ||
        state.state == plane_pet_update::State::Required)
      return UpdateCardButton::Retry;
  }
  if (PtInRect(&kUpdateSecondaryButton, point))
    return UpdateCardButton::Secondary;
  return UpdateCardButton::None;
}

bool RepositionUpdateWindow(HWND owner) {
  if (gUpdateWindow == nullptr || !IsWindow(gUpdateWindow) ||
      owner == nullptr || !IsWindow(owner)) return false;
  RECT pet{};
  if (!GetWindowRect(owner, &pet)) return false;
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (!GetMonitorInfoW(
          MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor))
    return false;
  gUpdateDpi = gClient.UiDpi();
  const int width = kUpdateCardWidth;
  const int height = kUpdateCardHeight;
  const int gap = 8;
  int x = pet.right + gap;
  if (x + width > monitor.rcWork.right)
    x = pet.left - width - gap;
  x = std::clamp(x, static_cast<int>(monitor.rcWork.left),
                 std::max(static_cast<int>(monitor.rcWork.left),
                          static_cast<int>(monitor.rcWork.right) -
                              width));
  const int y = std::clamp(
      static_cast<int>(pet.top), static_cast<int>(monitor.rcWork.top),
      std::max(static_cast<int>(monitor.rcWork.top),
               static_cast<int>(monitor.rcWork.bottom) - height));
  RECT current{};
  if (GetWindowRect(gUpdateWindow, &current) && current.left == x &&
      current.top == y && current.right - current.left == width &&
      current.bottom - current.top == height) {
    return false;
  }
  SetWindowPos(gUpdateWindow, HWND_TOPMOST, x, y, width,
               height, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  return true;
}

bool RefreshUpdateWindowContent(uint64_t generation, uint32_t progress) {
  if (gUpdateWindow == nullptr || !IsWindow(gUpdateWindow) ||
      !IsWindowVisible(gUpdateWindow)) return false;
  if (gUpdateContentGeneration == generation &&
      gUpdateContentProgress == progress) return false;
  gUpdateContentGeneration = generation;
  gUpdateContentProgress = progress;
  InvalidateRect(gUpdateWindow, nullptr, FALSE);
  return true;
}

void CloseUpdateWindow() {
  if (gUpdateWindow != nullptr && IsWindow(gUpdateWindow))
    DestroyWindow(gUpdateWindow);
  gUpdateWindow = nullptr;
  gUpdateContentGeneration = UINT64_MAX;
  gUpdateContentProgress = UINT32_MAX;
}

LRESULT CALLBACK UpdateWindowProcedure(HWND window, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
  const auto logicalPoint = [&] {
    RECT physical{};
    GetClientRect(window, &physical);
    return plane_pet_dpi::LogicalPoint(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)},
        physical.right, physical.bottom, kUpdateCardWidth, kUpdateCardHeight);
  };
  switch (message) {
    case WM_DPICHANGED: {
      gUpdateDpi = plane_pet_dpi::Normalize(HIWORD(wParam));
      const auto *suggested = reinterpret_cast<const RECT *>(lParam);
      if (suggested) {
        RECT rect{suggested->left, suggested->top,
            suggested->left + kUpdateCardWidth,
            suggested->top + kUpdateCardHeight};
        rect = plane_pet_dpi::FitMonitor(rect);
        SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left,
            rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOZORDER);
      }
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC target = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      const int width = std::max<LONG>(1, client.right - client.left);
      const int height = std::max<LONG>(1, client.bottom - client.top);
      HDC memory = CreateCompatibleDC(target);
      HBITMAP bitmap = memory == nullptr
          ? nullptr : CreateCompatibleBitmap(target, width, height);
      HGDIOBJ previousBitmap = nullptr;
      HDC dc = target;
      if (memory != nullptr && bitmap != nullptr) {
        previousBitmap = SelectObject(memory, bitmap);
        dc = memory;
      }
      {
      plane_pet_dpi::LogicalCanvas canvas(dc, kUpdateCardWidth, kUpdateCardHeight, width, height);
      client = RECT{0, 0, kUpdateCardWidth, kUpdateCardHeight};
      HBRUSH background = CreateSolidBrush(RGB(11, 16, 27));
      FillRect(dc, &client, background);
      DeleteObject(background);
      const auto state = gClient.UpdateSnapshot();
      RECT card{1, 1, client.right - 1, client.bottom - 1};
      UpdateCardPanel(dc, card, RGB(19, 25, 39), RGB(74, 102, 145));
      RECT title{22, 13, 321, 42};
      UpdateCardText(dc,
                     state.state == plane_pet_update::State::Required
                         ? L"需要更新 Plane Pet"
                         : L"Plane Pet 软件更新",
                     title, 23,
                     state.required ? RGB(255, 215, 70) : kBlueLight,
                     FW_BOLD);
      RECT close = kUpdateCloseButton;
      UpdateCardText(dc, L"×", close, 24, RGB(180, 194, 218), FW_BOLD,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      const bool errorDetails = state.state == plane_pet_update::State::Error;
      RECT messageRect{23, 49, 337, errorDetails ? 184L : 78L};
      UpdateCardText(dc, state.message, messageRect, 18, kWhite, FW_BOLD,
          errorDetails ? (DT_LEFT | DT_TOP | DT_WORDBREAK) :
                         (DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS));
      if (!state.latestVersion.empty() && !errorDetails) {
        const std::wstring versions = L"当前 " + state.currentVersion +
            L"   ·   最新 " + state.latestVersion;
        RECT versionRect{23, 78, 337, 104};
        UpdateCardText(dc, versions, versionRect, 16, kMuted);
      }
      int summaryY = 105;
      for (const std::wstring &line : state.summary) {
        if (errorDetails) break;
        if (line.empty()) continue;
        if (state.state == plane_pet_update::State::Downloading && summaryY >= 150)
          break;
        RECT summaryRect{27, summaryY, 337, summaryY + 25};
        UpdateCardText(dc, L"• " + line, summaryRect, 16,
                       RGB(215, 226, 244), FW_NORMAL,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        summaryY += 24;
      }
      if (state.state == plane_pet_update::State::Downloading) {
        RECT track{24, 151, 336, 169};
        UpdateCardPanel(dc, track, RGB(41, 49, 66), RGB(66, 80, 105));
        RECT bar = track;
        bar.right = bar.left + static_cast<int>(
            (bar.right - bar.left) *
            std::min(100U, state.progressPercent) / 100U);
        if (bar.right > bar.left)
          UpdateCardPanel(dc, bar, RGB(35, 199, 132), RGB(35, 199, 132));
      } else if (state.canDownload) {
        UpdateCardPanel(dc, kUpdatePrimaryButton, RGB(23, 150, 105),
                        RGB(74, 236, 169));
        UpdateCardText(dc, L"下载并升级", kUpdatePrimaryButton, 18, kWhite,
                       FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        UpdateCardPanel(dc, kUpdateSecondaryButton, RGB(57, 46, 52),
                        state.required ? RGB(240, 163, 83) : kCardEdge);
        UpdateCardText(dc,
                       state.required ? L"暂时离线使用" : L"7 天后提醒",
                       kUpdateSecondaryButton, 17, kWhite, FW_BOLD,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      } else if (state.state == plane_pet_update::State::Error ||
                 state.state == plane_pet_update::State::Required) {
        UpdateCardPanel(dc, kUpdatePrimaryButton, RGB(38, 81, 120), kBlue);
        UpdateCardText(dc, L"重新检查", kUpdatePrimaryButton, 18, kWhite,
                       FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        UpdateCardPanel(dc, kUpdateSecondaryButton, RGB(43, 49, 63),
                        kCardEdge);
        UpdateCardText(dc, L"关闭", kUpdateSecondaryButton, 18, kWhite,
                       FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      if (state.state == plane_pet_update::State::Checking ||
          state.state == plane_pet_update::State::Current ||
          state.state == plane_pet_update::State::Downloading) {
        UpdateCardPanel(dc, kUpdateSecondaryButton, RGB(43, 49, 63), kCardEdge);
        UpdateCardText(dc,
                       state.state == plane_pet_update::State::Downloading
                           ? L"取消下载" : L"关闭",
                       kUpdateSecondaryButton, 18, kWhite, FW_BOLD,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      }
      if (dc == memory)
        BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
      if (previousBitmap != nullptr) SelectObject(memory, previousBitmap);
      if (bitmap != nullptr) DeleteObject(bitmap);
      if (memory != nullptr) DeleteDC(memory);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const POINT point = logicalPoint();
      gUpdatePressedButton = UpdateCardButtonAt(point, gClient.UpdateSnapshot());
      if (gUpdatePressedButton != UpdateCardButton::None) SetCapture(window);
      return 0;
    }
    case WM_LBUTTONUP: {
      const POINT point = logicalPoint();
      const auto state = gClient.UpdateSnapshot();
      const auto pressed = gUpdatePressedButton;
      gUpdatePressedButton = UpdateCardButton::None;
      if (GetCapture() == window) ReleaseCapture();
      if (pressed == UpdateCardButton::None ||
          pressed != UpdateCardButtonAt(point, state)) return 0;
      if (pressed == UpdateCardButton::Close ||
          pressed == UpdateCardButton::Secondary) {
        gClient.DeclineOrDismissUpdate();
      } else if (state.canDownload) {
        gClient.AcceptUpdate();
      } else if (state.state == plane_pet_update::State::Error ||
                 state.state == plane_pet_update::State::Required) {
        gClient.ManualUpdateCheck();
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
      gUpdatePressedButton = UpdateCardButton::None;
      return 0;
    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) gClient.DeclineOrDismissUpdate();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_CLOSE:
    case kEmergencyHideMessage:
      gClient.DeclineOrDismissUpdate();
      return 0;
    case WM_DESTROY:
      gUpdatePressedButton = UpdateCardButton::None;
      if (window == gUpdateWindow) {
        gUpdateWindow = nullptr;
        gUpdateContentGeneration = UINT64_MAX;
        gUpdateContentProgress = UINT32_MAX;
      }
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

void ShowUpdateWindow(HWND owner, bool activate) {
  bool created = false;
  if (gUpdateWindow == nullptr || !IsWindow(gUpdateWindow)) {
    gUpdateWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kUpdateWindowClassName,
        L"Plane Pet 软件更新", WS_POPUP, 0, 0, kUpdateCardWidth,
        kUpdateCardHeight, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    created = gUpdateWindow != nullptr;
  }
  if (gUpdateWindow == nullptr) return;
  if (created) {
    gUpdateContentGeneration = UINT64_MAX;
    gUpdateContentProgress = UINT32_MAX;
  }
  RepositionUpdateWindow(owner);
  const bool wasVisible = IsWindowVisible(gUpdateWindow) != FALSE;
  if (!wasVisible)
    ShowWindow(gUpdateWindow, activate ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
  const auto state = gClient.UpdateSnapshot();
  RefreshUpdateWindowContent(state.generation, state.progressPercent);
  if (activate && !wasVisible) SetForegroundWindow(gUpdateWindow);
}

HICON PlanePetIcon(int width, int height) {
  HICON icon = static_cast<HICON>(LoadImageW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
      width, height, LR_DEFAULTCOLOR | LR_SHARED));
  return icon != nullptr
      ? icon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
}

enum class InfoWindowMode {
  Diagnostics,
  History,
  Help,
};

InfoWindowMode gInfoWindowMode = InfoWindowMode::History;
bool gHelpDetailed = false;
HWND gHelpNavigation = nullptr;
constexpr int kHelpNavigationId = 6101;
int gInfoDpi = 96;
int gInfoScrollOffset = 0;
constexpr int kHistoryWidth = 560;
constexpr int kHistoryContentHeight = 600;
constexpr int kHelpWidth = plane_pet_help::kWidth;
constexpr int kHelpViewportHeight = 740;
constexpr int kHelpContentHeight = plane_pet_help::kContentHeight;

void RefreshHistoryInfoWindow() {
  if (gInfoWindow != nullptr && IsWindow(gInfoWindow) &&
      gInfoWindowMode == InfoWindowMode::History) {
    InvalidateRect(gInfoWindow, nullptr, FALSE);
  }
}

int InfoBaseWidth() {
  return gInfoWindowMode == InfoWindowMode::History
      ? kHistoryWidth : kHelpWidth;
}

int InfoContentHeight() {
  if (gInfoWindowMode == InfoWindowMode::Diagnostics) return 720;
  return gInfoWindowMode == InfoWindowMode::History
      ? kHistoryContentHeight : (gHelpDetailed ? kHelpContentHeight : plane_pet_help::kBriefContentHeight);
}

UINT WindowDpi(HWND window) {
  return plane_pet_dpi::ForWindow(window);
}

int HelpFooterPixels(HWND window) {
  if (gInfoWindowMode != InfoWindowMode::Help) return 0;
  RECT client{}; GetClientRect(window, &client);
  return std::min<int>(std::max(0L, client.bottom - 1),
                       plane_pet_dpi::Scale(plane_pet_help::kFooterHeight, gInfoDpi));
}

RECT HelpNavigationRect(HWND window) {
  RECT client{}; GetClientRect(window, &client);
  const int margin = std::min<int>(plane_pet_dpi::Scale(plane_pet_help::kNavigationMargin, gInfoDpi),
                                    std::max(0L, client.right / 8));
  const int width = std::min<int>(plane_pet_dpi::Scale(plane_pet_help::kNavigationWidth, gInfoDpi),
                                   std::max(1L, client.right - 2 * margin));
  const int footer = HelpFooterPixels(window);
  const int height = std::min(plane_pet_dpi::Scale(plane_pet_help::kNavigationHeight, gInfoDpi),
                              std::max(1, footer - 8));
  const int left = gHelpDetailed ? margin : client.right - margin - width;
  const int top = client.bottom - footer + (footer - height) / 2;
  return RECT{left, top, left + width, top + height};
}

void LayoutHelpNavigation(HWND window) {
  if (gInfoWindowMode != InfoWindowMode::Help || !gHelpNavigation) return;
  const RECT rect = HelpNavigationRect(window);
  SetWindowPos(gHelpNavigation, nullptr, rect.left, rect.top, rect.right - rect.left,
               rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

int InfoPageHeight(HWND window) {
  RECT client{};
  GetClientRect(window, &client);
  return std::max(1, MulDiv(client.bottom - client.top - HelpFooterPixels(window), 96, gInfoDpi));
}

void UpdateInfoScrollBar(HWND window) {
  const int page = InfoPageHeight(window);
  gInfoScrollOffset = std::clamp(
      gInfoScrollOffset, 0, std::max(0, InfoContentHeight() - page));
  SCROLLINFO info{};
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  // Keep the scroll gutter stable between the short and long page; the brief
  // page's bar is disabled when it fits, rather than resizing the client area.
  if (gInfoWindowMode == InfoWindowMode::Help) info.fMask |= SIF_DISABLENOSCROLL;
  info.nMin = 0;
  info.nMax = InfoContentHeight() - 1;
  info.nPage = static_cast<UINT>(page);
  info.nPos = gInfoScrollOffset;
  SetScrollInfo(window, SB_VERT, &info, TRUE);
}

void SetHelpPage(HWND window, bool detailed) {
  if (gInfoWindowMode != InfoWindowMode::Help || !IsWindow(window)) return;
  gHelpDetailed = detailed;
  gInfoScrollOffset = 0;
  SetWindowTextW(window, detailed ? L"Plane Pet - 详细帮助" : L"Plane Pet - 简略帮助");
  if (gHelpNavigation) SetWindowTextW(gHelpNavigation,
      detailed ? plane_pet_help::kBackLink : plane_pet_help::kDetailLink);
  UpdateInfoScrollBar(window);
  LayoutHelpNavigation(window);
  RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

bool HandleHelpDialogMessage(MSG *message) {
  if (!gInfoWindow || gInfoWindowMode != InfoWindowMode::Help ||
      (message->hwnd != gInfoWindow && !IsChild(gInfoWindow, message->hwnd))) return false;
  return IsDialogMessageW(gInfoWindow, message) != FALSE;
}

void CloseInfoWindow() {
  plane_pet_about::Close();
  if (gInfoWindow != nullptr && IsWindow(gInfoWindow)) {
    DestroyWindow(gInfoWindow);
  }
  gInfoWindow = nullptr;
}

LRESULT CALLBACK InfoWindowProcedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  switch (message) {
    case WM_DPICHANGED: {
      gInfoDpi = std::max(96, static_cast<int>(HIWORD(wParam)));
      const auto *suggested = reinterpret_cast<const RECT *>(lParam);
      if (suggested) SetWindowPos(window, nullptr, suggested->left, suggested->top,
          suggested->right - suggested->left, suggested->bottom - suggested->top,
          SWP_NOACTIVATE | SWP_NOZORDER);
      UpdateInfoScrollBar(window);
      LayoutHelpNavigation(window);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_TIMER: {
      if (gInfoWindowMode == InfoWindowMode::Diagnostics) {
        static std::wstring previous;
        const auto current = gClient.DiagnosticText();
        if (current != previous) {
          previous = current;
          InvalidateRect(window, nullptr, FALSE);
        }
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC target = BeginPaint(window, &paint);
      RECT physical{};
      GetClientRect(window, &physical);
      const int physicalWidth =
          std::max<LONG>(1, physical.right - physical.left);
      const int physicalHeight =
          std::max<LONG>(1, physical.bottom - physical.top);
      HDC memory = CreateCompatibleDC(target);
      HBITMAP bitmap = memory == nullptr
          ? nullptr
          : CreateCompatibleBitmap(target, physicalWidth, physicalHeight);
      HGDIOBJ previousBitmap = nullptr;
      HDC dc = target;
      if (memory != nullptr && bitmap != nullptr) {
        previousBitmap = SelectObject(memory, bitmap);
        dc = memory;
      }
      const int page = InfoPageHeight(window);
      const int bodyPixels = std::max(1, physicalHeight - HelpFooterPixels(window));
      const int saved = SaveDC(dc);
      SetMapMode(dc, MM_ANISOTROPIC);
      SetWindowExtEx(dc, InfoBaseWidth(), page, nullptr);
      SetViewportExtEx(dc, std::max(1L, physical.right),
                       bodyPixels, nullptr);
      SetWindowOrgEx(dc, 0, gInfoScrollOffset, nullptr);
      RECT rect{0, gInfoScrollOffset, InfoBaseWidth(),
                gInfoScrollOffset + page};
      if (gInfoWindowMode == InfoWindowMode::Help)
        IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
      if (gInfoWindowMode == InfoWindowMode::History) {
        gClient.DrawHistoryWindow(dc, rect);
      } else if (gInfoWindowMode == InfoWindowMode::Diagnostics) {
        gClient.DrawDiagnosticsWindow(dc, rect);
      } else {
        gClient.DrawHelpWindow(dc, rect, gHelpDetailed);
      }
      RestoreDC(dc, saved);
      if (gInfoWindowMode == InfoWindowMode::Help)
        gClient.DrawHelpFooter(dc, RECT{0, bodyPixels, physicalWidth, physicalHeight});
      if (dc == memory)
        BitBlt(target, 0, 0, physicalWidth, physicalHeight, memory, 0, 0,
               SRCCOPY);
      if (previousBitmap != nullptr) SelectObject(memory, previousBitmap);
      if (bitmap != nullptr) DeleteObject(bitmap);
      if (memory != nullptr) DeleteDC(memory);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_SIZE:
      UpdateInfoScrollBar(window);
      LayoutHelpNavigation(window);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_DRAWITEM: {
      const auto *item = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
      if (gInfoWindowMode == InfoWindowMode::Help && item && item->hwndItem == gHelpNavigation) {
        gClient.DrawHelpNavigation(item->hDC, item->rcItem, gInfoDpi, gHelpDetailed,
                                  (item->itemState & ODS_SELECTED) != 0,
                                  (item->itemState & ODS_FOCUS) != 0);
        return TRUE;
      }
      break;
    }
    case DM_GETDEFID:
      if (gInfoWindowMode == InfoWindowMode::Help)
        return MAKELRESULT(kHelpNavigationId, DC_HASDEFID);
      break;
    case WM_COMMAND:
      if (gInfoWindowMode == InfoWindowMode::Help) {
        if (LOWORD(wParam) == kHelpNavigationId && HIWORD(wParam) == BN_CLICKED) {
          SetHelpPage(window, !gHelpDetailed);
          return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
          if (gHelpDetailed) SetHelpPage(window, false);
          else DestroyWindow(window);
          return 0;
        }
      }
      break;
    case WM_MOUSEWHEEL: {
      const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      gInfoScrollOffset -= (delta / WHEEL_DELTA) * 54;
      UpdateInfoScrollBar(window);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_VSCROLL: {
      SCROLLINFO info{};
      info.cbSize = sizeof(info);
      info.fMask = SIF_ALL;
      GetScrollInfo(window, SB_VERT, &info);
      int next = gInfoScrollOffset;
      switch (LOWORD(wParam)) {
        case SB_LINEUP: next -= 36; break;
        case SB_LINEDOWN: next += 36; break;
        case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: next = info.nTrackPos; break;
        case SB_TOP: next = 0; break;
        case SB_BOTTOM: next = InfoContentHeight(); break;
        default: break;
      }
      gInfoScrollOffset = next;
      UpdateInfoScrollBar(window);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_CLOSE:
    case kEmergencyHideMessage:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      if (window == gInfoWindow) {
        gInfoWindow = nullptr;
        gHelpNavigation = nullptr;
        gHelpDetailed = false;
      }
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

void ShowInfoWindow(HWND owner, InfoWindowMode mode) {
  if (gInfoWindow != nullptr && IsWindow(gInfoWindow) &&
      gInfoWindowMode == mode &&
      gInfoDpi == static_cast<int>(WindowDpi(owner))) {
    if (mode == InfoWindowMode::Help) SetHelpPage(gInfoWindow, false);
    if (!IsWindowVisible(gInfoWindow)) ShowWindow(gInfoWindow, SW_SHOWNORMAL);
    InvalidateRect(gInfoWindow, nullptr, FALSE);
    SetForegroundWindow(gInfoWindow);
    return;
  }
  CloseInfoWindow();
  gInfoWindowMode = mode;
  gHelpDetailed = false;
  gInfoScrollOffset = 0;
  gInfoDpi = static_cast<int>(WindowDpi(owner));
  const int baseClientWidth = mode == InfoWindowMode::History
      ? kHistoryWidth : kHelpWidth;
  const int baseClientHeight = mode == InfoWindowMode::History
      ? kHistoryContentHeight : kHelpViewportHeight;
  const int desiredClientWidth = MulDiv(baseClientWidth, gInfoDpi, 96);
  const int desiredClientHeight = MulDiv(baseClientHeight, gInfoDpi, 96);
  const DWORD style = WS_CAPTION | WS_SYSMENU | WS_VSCROLL |
      (mode == InfoWindowMode::Help ? WS_CLIPCHILDREN : 0);
  const DWORD extendedStyle = WS_EX_TOOLWINDOW;

  RECT ownerRect{};
  GetWindowRect(owner, &ownerRect);
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST),
                  &monitorInfo);
  const RECT work = monitorInfo.rcWork;
  RECT chrome{0, 0, 0, 0};
  plane_pet_dpi::Adjust(chrome, style, extendedStyle, gInfoDpi);
  const int nonClientWidth = chrome.right - chrome.left;
  const int nonClientHeight = chrome.bottom - chrome.top;
  const int clientWidth = std::max(
      260, std::min(desiredClientWidth,
                    static_cast<int>(work.right - work.left) -
                        nonClientWidth - 16));
  const int clientHeight = std::max(
      220, std::min(desiredClientHeight,
                    static_cast<int>(work.bottom - work.top) -
                        nonClientHeight - 16));
  const int activeScrollWidth = mode == InfoWindowMode::Help
      ? GetSystemMetrics(SM_CXVSCROLL) : 0;
  RECT frame{0, 0, clientWidth + activeScrollWidth, clientHeight};
  plane_pet_dpi::Adjust(frame, style, extendedStyle, gInfoDpi);
  const int width = frame.right - frame.left;
  const int height = frame.bottom - frame.top;
  int x = ownerRect.right + 8;
  if (x + width > work.right) x = ownerRect.left - width - 8;
  const int workLeft = static_cast<int>(work.left);
  const int workTop = static_cast<int>(work.top);
  const int maxX = std::max(workLeft, static_cast<int>(work.right) - width);
  const int maxY = std::max(workTop, static_cast<int>(work.bottom) - height);
  x = std::clamp(x, workLeft, maxX);
  const int y = std::clamp(static_cast<int>(ownerRect.top), workTop, maxY);

  const wchar_t *title = mode == InfoWindowMode::History
      ? L"Plane Pet - 历史战绩" : (mode == InfoWindowMode::Diagnostics
          ? L"Plane Pet - 连接与诊断" : L"Plane Pet - 简略帮助");
  gInfoWindow = CreateWindowExW(
      extendedStyle, kInfoWindowClassName, title, style, x, y, width, height,
      owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (gInfoWindow == nullptr) return;
  if (mode == InfoWindowMode::Help) {
    gHelpNavigation = CreateWindowW(L"BUTTON", plane_pet_help::kDetailLink,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1,
        gInfoWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHelpNavigationId)),
        GetModuleHandleW(nullptr), nullptr);
    if (!gHelpNavigation) { CloseInfoWindow(); return; }
    LayoutHelpNavigation(gInfoWindow);
  }
  if (mode == InfoWindowMode::Diagnostics) SetTimer(gInfoWindow, 1, 1000, nullptr);
  UpdateInfoScrollBar(gInfoWindow);
  LayoutHelpNavigation(gInfoWindow);
  ShowWindow(gInfoWindow, SW_SHOWNORMAL);
  UpdateWindow(gInfoWindow);
  SetForegroundWindow(gInfoWindow);
}

BOOL CALLBACK HidePlanePetWindow(HWND candidate, LPARAM) {
  wchar_t className[64]{};
  if (GetClassNameW(candidate, className,
                    static_cast<int>(std::size(className))) > 0) {
    if (lstrcmpW(className, kWindowClassName) == 0 ||
        lstrcmpW(className, kInfoWindowClassName) == 0) {
      PostMessageW(candidate, kEmergencyHideMessage, 0, 0);
    }
  }
  return TRUE;
}

void BroadcastEmergencyHide() {
  EnumWindows(HidePlanePetWindow, 0);
}

void AddTray(HWND window) {
  gTray = NOTIFYICONDATAW{};
  gTray.cbSize = sizeof(gTray);
  gTray.hWnd = window;
  gTray.uID = 1;
  gTray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  gTray.uCallbackMessage = kTrayMessage;
  gTray.hIcon = PlanePetIcon(GetSystemMetrics(SM_CXSMICON),
                             GetSystemMetrics(SM_CYSMICON));
  swprintf(gTray.szTip, 128, L"Plane Pet - %.48ls%ls", gClient.OwnName().c_str(),
      gBossHotkeyAvailable ? L"" : L"（隐藏快捷键被占用，请右键隐藏）");
  // Also tolerate a duplicated TaskbarCreated notification in tests/Windows.
  gTrayAdded = Shell_NotifyIconW(NIM_ADD, &gTray) != FALSE ||
              Shell_NotifyIconW(NIM_MODIFY, &gTray) != FALSE;
}

void RemoveTray() {
  if (gTrayAdded) Shell_NotifyIconW(NIM_DELETE, &gTray);
  gTrayAdded = false;
}

struct PetMenuState {
  bool pairing = false;
  bool needsPairing = false;
  bool pairingPanelVisible = true;
  bool bound = false;
  bool unbinding = false;
  bool peerOnline = false;
  bool canInvite = false;
  bool doNotDisturb = false;
  bool telemetry = false;
  bool updatesEnabled = false;
  bool hidden = false;
  bool game = false;
};

PetMenuState CurrentPetMenuState(HWND window) {
  return {gClient.IsPairingSearch(), gClient.NeedsPairing(),
          gClient.IsPairingPanelVisible(), gClient.HasBinding(),
          gClient.IsUnbinding(), gClient.IsPeerOnline(), gClient.CanInvite(),
          gClient.IsDoNotDisturb(), gClient.IsTelemetryEnabled(),
          gClient.UpdatesEnabled(), IsWindowVisible(window) == FALSE,
          gClient.IsGameMode()};
}

// Shared by the pet and tray. Build a stable snapshot for the duration of the
// native menu; the command handlers still validate live game/binding state.
HMENU BuildPetContextMenu(const PetMenuState &state) {
  HMENU menu = CreatePopupMenu();
  HMENU settings = CreatePopupMenu();
  HMENU help = CreatePopupMenu();
  if (!menu || !settings || !help) {
    if (menu) DestroyMenu(menu);
    if (settings) DestroyMenu(settings);
    if (help) DestroyMenu(help);
    return nullptr;
  }
  bool ok = true;
  const auto item = [&](HMENU parent, UINT_PTR id, const wchar_t *label,
                        UINT flags = MF_STRING) {
    if (!AppendMenuW(parent, flags, id, label)) ok = false;
  };
  const auto separator = [&](HMENU parent) { item(parent, 0, nullptr, MF_SEPARATOR); };
  if (state.pairing) item(menu, kMenuStopPairing, L"停止匹配");
  if (state.needsPairing) {
    item(menu, kMenuPairingPanel, L"显示绑定电脑框",
         MF_STRING | (state.pairingPanelVisible ? MF_CHECKED : 0));
  } else if (!state.pairing) {
    const wchar_t *label = state.bound && !state.peerOnline
        ? L"邀请对战（对方离线）" : L"邀请对战";
    item(menu, kMenuInvite, label,
         MF_STRING | (state.canInvite && !state.unbinding ? MF_ENABLED : MF_GRAYED));
  }
  item(menu, kMenuHistory, L"历史战绩");
  item(menu, kMenuDoNotDisturb, L"暂停接收邀请",
       MF_STRING | (state.doNotDisturb ? MF_CHECKED : 0));
  item(menu, state.hidden ? kMenuShow : kMenuHide,
       state.game ? (state.hidden ? L"显示游戏窗口" : L"隐藏游戏窗口")
                  : (state.hidden ? L"显示悬浮窗" : L"隐藏悬浮窗"));
  separator(menu);

  item(settings, kMenuTelemetry, L"匿名使用统计",
       MF_STRING | (state.telemetry ? MF_CHECKED : 0));
  item(settings, kMenuExportData, L"导出统计记录…");
  item(settings, kMenuClearLocalData, L"清除本地战绩与统计…");
  separator(settings);
  item(settings, kMenuUnbind, state.unbinding ? L"正在解除绑定…" : L"解除绑定…",
       MF_STRING | (state.bound && !state.unbinding ? MF_ENABLED : MF_GRAYED));
  item(help, kMenuHelp, L"操作帮助");
  item(help, kMenuCheckUpdate, L"检查软件更新…",
       MF_STRING | (state.updatesEnabled ? MF_ENABLED : MF_GRAYED));
  separator(help);
  item(help, kMenuDiagnostics, L"连接与诊断…");
  item(help, kMenuExportDiagnostics, L"导出诊断信息…");

  const bool settingsAttached = AppendMenuW(menu, MF_POPUP,
      reinterpret_cast<UINT_PTR>(settings), L"设置与隐私") != FALSE;
  const bool helpAttached = AppendMenuW(menu, MF_POPUP,
      reinterpret_cast<UINT_PTR>(help), L"帮助与更新") != FALSE;
  item(menu, kMenuAbout, L"关于…");
  separator(menu);
  item(menu, kMenuExit, L"退出");
  if (!ok || !settingsAttached || !helpAttached) {
    // DestroyMenu recursively releases attached submenus, exactly once.
    if (!settingsAttached) DestroyMenu(settings);
    if (!helpAttached) DestroyMenu(help);
    DestroyMenu(menu);
    return nullptr;
  }
  return menu;
}

void DispatchPetMenuCommand(HWND window, UINT command) {
  if (command != 0 && IsWindow(window))
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(command, 0), 0);
}

void ShowPetContextMenu(HWND window, bool fromTray = false) {
  if (!fromTray && gClient.IsGameMode()) return;
  static bool showing = false;
  if (showing) return;
  showing = true;
  struct MenuGuard { bool &active; ~MenuGuard() { active = false; } } guard{showing};
  HMENU menu = BuildPetContextMenu(CurrentPetMenuState(window));
  if (!menu) return;
  POINT point{};
  GetCursorPos(&point);
  SetForegroundWindow(window);
  // Dispatch after the entire submenu loop and ModalScope have ended, so
  // update/help windows aren't opened inside a still-active native menu.
  const UINT command = static_cast<UINT>(PetTrackPopupMenu(menu,
      TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y,
      0, window, nullptr));
  DestroyMenu(menu);
  PostMessageW(window, WM_NULL, 0, 0);
  DispatchPetMenuCommand(window, command);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
  if (gTaskbarCreatedMessage && message == gTaskbarCreatedMessage) {
    gTrayAdded = false;
    AddTray(window);
    return 0;
  }
  if (message == plane_pet_windows::ActivateMessage() &&
      GetPropW(window, plane_pet_windows::kPublicWindowProperty)) {
    gClient.ShowPet();
    SetForegroundWindow(window);
    return 0;
  }
  switch (message) {
    case WM_TIMER:
      if (wParam == kNetworkPumpTimer) {
        // Native menus/dialogs run nested message loops. Keep the same guarded
        // client pump alive there; otherwise a menu alone causes a disconnect.
        gClient.Update();
#ifdef PLANE_PET_MOTION_SELF_TEST
        gClient.MotionTestFrame();
#endif
#ifdef PLANE_PET_LATENCY_SELF_TEST
        gClient.WriteDndPublicTestState();
#endif
        if (gReadyEvent && GetTickCount64() >= gReadyAt && gModalDepth == 0 &&
            gClient.IsStartupHealthy()) {
          SetEvent(gReadyEvent);
          CloseHandle(gReadyEvent);
          gReadyEvent = nullptr;
        }
        static ULONGLONG nextPaint = 0;
        const auto tick = GetTickCount64();
        if (tick >= nextPaint) {
          RefreshStatusTooltip(window);
          if (IsWindowVisible(window)) InvalidateRect(window, nullptr, FALSE);
          nextPaint = tick + (gClient.IsGameMode() ? 16 : 33);
        }
        return 0;
      }
      break;
    case WM_NOTIFY: {
      const auto *notification = reinterpret_cast<const NMHDR *>(lParam);
      if (notification && notification->hwndFrom == gStatusTooltip && notification->code == TTN_GETDISPINFOW) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        point = gClient.LogicalClientPoint(point);
        gStatusTooltipText = gClient.IsGameMode() ? L"" : gClient.StatusTooltip(point.x, point.y);
        reinterpret_cast<NMTTDISPINFOW *>(lParam)->lpszText = gStatusTooltipText.data();
        return 0;
      }
      break;
    }
    case WM_DPICHANGED:
      gClient.ChangeDpi(HIWORD(wParam), reinterpret_cast<const RECT *>(lParam));
      RepositionUpdateWindow(window);
      return 0;
    case WM_DISPLAYCHANGE:
      gClient.ChangeDpi(plane_pet_dpi::ForWindow(window));
      RepositionUpdateWindow(window);
      return 0;
    case WM_SETTINGCHANGE:
      if (wParam == SPI_SETWORKAREA) {
        gClient.ChangeDpi(plane_pet_dpi::ForWindow(window));
        RepositionUpdateWindow(window);
      }
      break;
    case WM_NCHITTEST: {
      if (gClient.IsGameMode())
        return DefWindowProcW(window, message, wParam, lParam);
      POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(window, &point);
      point = gClient.LogicalClientPoint(point);
      return gClient.IsInteractivePetPoint(point.x, point.y)
                 ? HTCLIENT : HTTRANSPARENT;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      RECT rect{};
      GetClientRect(window, &rect);
      gClient.Draw(dc, rect);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONDOWN: {
      const POINT point = gClient.LogicalClientPoint(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
      gClient.OnLeftButtonDown(point.x, point.y);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const POINT point = gClient.LogicalClientPoint(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
      gClient.OnMouseMove(point.x, point.y);
      return 0;
    }
    case WM_LBUTTONUP: {
      const POINT point = gClient.LogicalClientPoint(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
      gClient.OnLeftButtonUp(point.x, point.y);
      return 0;
    }
    case WM_CAPTURECHANGED:
      gClient.OnCaptureChanged();
      return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      ShowPetContextMenu(window);
      return 0;
    case WM_KEYDOWN:
      gClient.OnKeyDown(wParam);
      return 0;
    case WM_CHAR:
      gClient.OnChar(static_cast<wchar_t>(wParam));
      return 0;
    case WM_CLOSE:
      gClient.CloseRequested();
      return 0;
    case WM_HOTKEY:
      if (static_cast<int>(wParam) == kBossHotkeyId)
        BroadcastEmergencyHide();
      return 0;
    case kEmergencyHideMessage:
      gClient.EmergencyHide();
      return 0;
    case kTrayMessage:
      if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
        gClient.ShowPet();
      } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
        ShowPetContextMenu(window, true);
      }
      return 0;
    case WM_COMMAND:
      if (LOWORD(wParam) == kMenuShow) gClient.ShowPet();
      else if (LOWORD(wParam) == kMenuHide) gClient.HideByUser();
      else if (LOWORD(wParam) == kMenuExit) DestroyWindow(window);
      else if (LOWORD(wParam) == kMenuInvite) gClient.InviteFromMenu();
      else if (LOWORD(wParam) == kMenuStopPairing)
        gClient.StopPairingFromMenu();
      else if (LOWORD(wParam) == kMenuPairingPanel)
        gClient.TogglePairingPanel();
      else if (LOWORD(wParam) == kMenuHistory)
        ShowInfoWindow(window, InfoWindowMode::History);
      else if (LOWORD(wParam) == kMenuHelp)
        ShowInfoWindow(window, InfoWindowMode::Help);
      else if (LOWORD(wParam) == kMenuAbout) {
        // Reuse the About window if it already exists; keep unrelated readers
        // mutually exclusive, without touching pairing/game/update state.
        if (gInfoWindow && IsWindow(gInfoWindow)) DestroyWindow(gInfoWindow);
        plane_pet_about::Show(window);
      }
      else if (LOWORD(wParam) == kMenuDiagnostics)
        ShowInfoWindow(window, InfoWindowMode::Diagnostics);
      else if (LOWORD(wParam) == kMenuExportDiagnostics)
        gClient.ExportDiagnostics();
      else if (LOWORD(wParam) == kMenuCheckUpdate)
        gClient.ManualUpdateCheck();
      else if (LOWORD(wParam) == kMenuDoNotDisturb)
        gClient.ToggleDoNotDisturb();
      else if (LOWORD(wParam) == kMenuTelemetry)
        gClient.ToggleTelemetry();
      else if (LOWORD(wParam) == kMenuExportData)
        gClient.ExportLocalData();
      else if (LOWORD(wParam) == kMenuClearLocalData)
        gClient.ClearLocalData();
      else if (LOWORD(wParam) == kMenuUnbind)
        gClient.RequestUnbind();
      return 0;
    case plane_pet_update::kChangedMessage:
      if (gUpdateWindow != nullptr) {
        const auto state = gClient.UpdateSnapshot();
        RefreshUpdateWindowContent(state.generation, state.progressPercent);
      }
      return 0;
    case plane_pet_update::kInstallMessage:
      gClient.BeginUpdateInstall();
      return 0;
    case WM_DESTROY:
      KillTimer(window, kNetworkPumpTimer);
      if (gStatusTooltip) { DestroyWindow(gStatusTooltip); gStatusTooltip = nullptr; }
      RemovePropW(window, plane_pet_windows::kPublicWindowProperty);
      if (gReadyEvent) { CloseHandle(gReadyEvent); gReadyEvent = nullptr; }
      CloseInfoWindow();
      CloseUpdateWindow();
      UnregisterHotKey(window, kBossHotkeyId);
      RemoveTray();
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

#ifdef PLANE_PET_UPDATE_WINDOW_SELF_TEST
#include "../tests/update_window_cases.h"
#endif
#ifdef PLANE_PET_RELEASE_SELF_TEST
#include "../tests/menu_window_cases.h"
#include "../tests/release_window_cases.h"
#endif

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  EnableCrispDpiAwareness();
#ifdef PLANE_PET_RENDER_SELF_TEST
  const std::filesystem::path fixture =
      Wide(Option("render-fixture", "game_boundary_fixture.bmp"));
  return gClient.RenderGameBoundaryFixture(fixture) ? 0 : 4;
#endif
  WNDCLASSW windowClass{};
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = WindowProcedure;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  windowClass.hIcon = PlanePetIcon(GetSystemMetrics(SM_CXICON),
                                   GetSystemMetrics(SM_CYICON));
  windowClass.hbrBackground = CreateSolidBrush(kTransparent);
  windowClass.lpszClassName = kWindowClassName;
  if (!RegisterClassW(&windowClass)) return 1;
  WNDCLASSW infoClass{};
  infoClass.style = 0;
  infoClass.lpfnWndProc = InfoWindowProcedure;
  infoClass.hInstance = instance;
  infoClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  infoClass.hIcon = PlanePetIcon(GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON));
  infoClass.hbrBackground = nullptr;
  infoClass.lpszClassName = kInfoWindowClassName;
  if (!RegisterClassW(&infoClass)) return 1;
  WNDCLASSW updateClass{};
  updateClass.style = 0;
  updateClass.lpfnWndProc = UpdateWindowProcedure;
  updateClass.hInstance = instance;
  updateClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  updateClass.hIcon = PlanePetIcon(GetSystemMetrics(SM_CXICON),
                                   GetSystemMetrics(SM_CYICON));
  updateClass.hbrBackground = nullptr;
  updateClass.lpszClassName = kUpdateWindowClassName;
  if (!RegisterClassW(&updateClass)) return 1;
  HWND window = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST, kWindowClassName,
      L"Plane Pet PC-PC Test", WS_POPUP, 100, 100, kPetWidth, kPetHeight,
      nullptr, nullptr, instance, nullptr);
  if (window == nullptr) return 1;
  if (!gClient.Initialize(window)) return 2;
  CreateStatusTooltip(window);
  if (Option("public-instance", "0") == "1")
    SetPropW(window, plane_pet_windows::kPublicWindowProperty,
             reinterpret_cast<HANDLE>(1));
  const auto readyName = plane_pet_windows::Option(L"ready-event");
  if (!readyName.empty()) {
    gReadyEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName.c_str());
    if (!gReadyEvent) return 2;
    gReadyAt = GetTickCount64() + 2000;
  }
  gTaskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
  if (!SetTimer(window, kNetworkPumpTimer, 16, nullptr)) {
    gClient.Shutdown();
    DestroyWindow(window);
    return 2;
  }
#ifdef PLANE_PET_RELEASE_SELF_TEST
  const bool releaseCasesOk = RunReleaseWindowCases(window);
  gClient.Shutdown();
  DestroyWindow(window);
  return releaseCasesOk ? 0 : 10;
#endif
#ifdef PLANE_PET_UPDATE_WINDOW_SELF_TEST
  const bool updateWindowsOk = RunUpdateWindowSelfTest(window);
  gClient.Shutdown();
  DestroyWindow(window);
  return updateWindowsOk ? 0 : 9;
#endif
#ifdef PLANE_PET_INFO_SELF_TEST
  const auto validInfoWindow = [&](InfoWindowMode mode, int baseWidth,
                                   int baseHeight) {
    ShowInfoWindow(window, mode);
    if (gInfoWindow == nullptr || !IsWindow(gInfoWindow) ||
        !IsWindowVisible(gInfoWindow) ||
        GetWindow(gInfoWindow, GW_OWNER) != window) {
      return false;
    }
    RECT client{};
    if (!GetClientRect(gInfoWindow, &client) ||
        client.right - client.left != MulDiv(baseWidth, gInfoDpi, 96) ||
        client.bottom - client.top != MulDiv(baseHeight, gInfoDpi, 96)) {
      return false;
    }
    const LONG_PTR extendedStyle = GetWindowLongPtrW(gInfoWindow, GWL_EXSTYLE);
    return (extendedStyle & WS_EX_TOOLWINDOW) != 0 &&
           (extendedStyle & WS_EX_APPWINDOW) == 0;
  };
  const bool historyOk = validInfoWindow(InfoWindowMode::History, 560, 600);
  gClient.HideByUser();
  const bool historyClosedWithPet = gInfoWindow == nullptr;
  gClient.ShowPet();
  const bool helpOk = validInfoWindow(InfoWindowMode::Help, 700, 740);
  CloseInfoWindow();
  ShowUpdateWindow(window, false);
  RECT updateClient{};
  const bool updatePaintOk =
      gUpdateWindow != nullptr &&
      RedrawWindow(gUpdateWindow, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_UPDATENOW) != FALSE;
  const bool updateWindowOk =
      updatePaintOk && gUpdateWindow != nullptr && IsWindow(gUpdateWindow) &&
      IsWindowVisible(gUpdateWindow) &&
      GetWindow(gUpdateWindow, GW_OWNER) == window &&
      GetClientRect(gUpdateWindow, &updateClient) &&
      updateClient.right - updateClient.left == kUpdateCardWidth &&
      updateClient.bottom - updateClient.top == kUpdateCardHeight &&
      (GetWindowLongPtrW(gUpdateWindow, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 &&
      (GetWindowLongPtrW(gUpdateWindow, GWL_EXSTYLE) & WS_EX_APPWINDOW) == 0;
  const auto updateSnapshot = gClient.UpdateSnapshot();
  const bool steadyUpdateWindowOk =
      !RepositionUpdateWindow(window) &&
      !RefreshUpdateWindowContent(updateSnapshot.generation,
                                  updateSnapshot.progressPercent) &&
      (GetClassLongPtrW(gUpdateWindow, GCL_STYLE) &
       (CS_HREDRAW | CS_VREDRAW)) == 0 &&
      GetClassLongPtrW(gUpdateWindow, GCLP_HBRBACKGROUND) == 0;
  CloseUpdateWindow();
  const bool coloredHeartOk = gClient.RunColoredHeartHudSelfTest();
  const bool finishedCloseOk = gClient.RunFinishedCloseSelfTest();
  const bool historyPersistenceOk = gClient.RunHistoryPersistenceSelfTest();
  gClient.Shutdown();
  DestroyWindow(window);
  return (historyOk ? 0 : 1) |
         (historyClosedWithPet ? 0 : 2) |
         (helpOk ? 0 : 4) |
         (coloredHeartOk ? 0 : 8) |
         (finishedCloseOk ? 0 : 16) |
         (historyPersistenceOk ? 0 : 32) |
         (updateWindowOk ? 0 : 64) |
         (steadyUpdateWindowOk ? 0 : 128);
#endif
  gBossHotkeyAvailable = RegisterHotKey(window, kBossHotkeyId,
                 MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'H') != FALSE;
  AddTray(window);
  MSG message{};
  // One timer drives simulation/network even inside modal loops. Do not also
  // poll every millisecond while idle: that duplicated all client work.
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (plane_pet_about::HandleMessage(&message) || HandleHelpDialogMessage(&message)) continue;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  gClient.Shutdown();
  return gProcessExitCode;
}
