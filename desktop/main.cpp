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
#include <string>
#include <utility>
#include <vector>

#include <bcrypt.h>

#include "../common/pairing_protocol.h"
#include "game_layout.h"
#include "../../shared/plane_protocol.h"
#include "../../shared/plane_sim.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr UINT kTrayMessage = WM_APP + 1;
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
constexpr char kAppVersion[] = "0.6.7";
constexpr char kTelemetryMagic[] = "PPTELEM1\n";

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
  const std::string command = GetCommandLineA();
  const std::string prefix = std::string("--") + name + "=";
  const size_t begin = command.find(prefix);
  if (begin == std::string::npos) return fallback;
  size_t valueBegin = begin + prefix.size();
  const bool quoted = valueBegin < command.size() && command[valueBegin] == '"';
  if (quoted) ++valueBegin;
  size_t end = quoted ? command.find('"', valueBegin)
                      : command.find(' ', valueBegin);
  if (end == std::string::npos) end = command.size();
  return command.substr(valueBegin, end - valueBegin);
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
    start_ = Clock::now();
    const bool defaultBob = DefaultBobInstance();
    serverText_ = Option("server", "127.0.0.1:32110");
    if (!pcpair::ParseAuthKey(Option("network-key", ""), networkKey_)) {
      MessageBoxW(window_,
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
      SaveSettings();
    } else if (telemetryUploadCapable_ && telemetryUploadChoice_ < 0) {
      const int consent = MessageBoxW(
          window_,
          L"是否允许记录并发送匿名使用统计？\n\n"
          L"记录启动与使用时长、匹配和好友在线状态变化、邀请响应、桌宠显示/隐藏与勿扰状态、四种固定表情的发送/接收类型，以及对局过程和结果；不会记录聊天文字、匹配码、姓名、按键、鼠标轨迹、屏幕内容或窗口标题。\n\n"
          L"数据通过加密连接发送，原始事件最多保留 90 天；之后可在右键菜单随时停止。",
          L"Plane Pet - 匿名测试统计", MB_YESNO | MB_ICONINFORMATION);
      telemetryUploadChoice_ = consent == IDYES ? 1 : 0;
      telemetryEnabled_ = consent == IDYES;
      telemetryChoiceKnown_ = true;
      SaveSettings();
    } else if (!telemetryChoiceKnown_) {
      const int consent = MessageBoxW(
          window_,
          L"是否允许在本机记录匿名实验事件？\n\n"
          L"仅在本机记录启动与使用时长、匹配和好友在线状态变化、邀请响应、桌宠显示/隐藏与勿扰状态、四种固定表情的发送/接收类型，以及对局过程和结果；不会记录聊天文字、匹配码、姓名、按键、鼠标轨迹、屏幕内容或窗口标题，也不会自动上传。\n\n"
          L"之后可在右键菜单中清除或停止记录。",
          L"Plane Pet - 本地实验数据", MB_YESNO | MB_ICONINFORMATION);
      telemetryEnabled_ = consent == IDYES;
      telemetryChoiceKnown_ = true;
      SaveSettings();
    }
    const std::wstring configuredEvents = Wide(Option("events", ""));
    if (configuredEvents.empty()) {
      eventsPath_ = statePath_.parent_path() /
                    (statePath_.stem().wstring() + L".events.csv");
    } else {
      eventsPath_ = std::filesystem::path(configuredEvents);
    }
    LoadState();
    historyPath_ = statePath_.parent_path() /
                   (statePath_.stem().wstring() + L".history");
    LoadHistory();
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
    } else if (startupPairingCode == 0 || !statePersistenceOk_) {
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
    LogEvent("app_started", bindingId_ != 0 ? 1 : 0);
    ResetPrediction();
    nextPairing_ = nextInput_ = nextPing_ = nextPrediction_ = Clock::now();
    nextTelemetryHeartbeat_ = Clock::now() + std::chrono::seconds(60);
    SetPetMode(!hiddenByUser_);
    return true;
  }

  void Shutdown() {
    RecordAbandonedMatch();
    LogEvent("app_exited", MillisSince(start_));
    if (socket_ != INVALID_SOCKET) {
      if (pairingAttempted_) {
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
    ReceiveAll();
    const auto now = Clock::now();
    if (!inviteFeedback_.empty() && now >= inviteFeedbackUntil_) {
      inviteFeedback_.clear();
      inviteFeedbackUntil_ = Clock::time_point{};
    }
    const uint32_t nowMs = MillisSince(start_);
    if (telemetryEnabled_ && now >= nextTelemetryHeartbeat_) {
      const int64_t state = gameMode_ ? 2 : (hiddenByUser_ ? 1 : 0);
      LogEvent("session_heartbeat", state);
      nextTelemetryHeartbeat_ = now + std::chrono::seconds(60);
    }
    if (unbindPending_ && now >= nextUnbind_) {
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
    if (session_ != 0 && now >= nextInput_) {
      SendInput(nowMs);
      nextInput_ = now + std::chrono::milliseconds(50);
    }
    if (session_ != 0 && now >= nextPing_) {
      SendPing(nowMs);
      nextPing_ = now + std::chrono::seconds(1);
    }
    if (pendingAction_ != 0 && session_ != 0 && now >= nextAction_) {
      SendAction(static_cast<plink::PlayerAction>(pendingAction_));
      nextAction_ = now + std::chrono::milliseconds(250);
    }
    if (autoInvite_ && !autoInviteSent_ && session_ != 0 &&
        Phase() == plink::GamePhase::Menu && OpponentOnline()) {
      autoInviteSent_ = true;
      InviteFromMenu();
    }
    if (autoAccept_ && !autoAcceptSent_ && session_ != 0 &&
        IsIncomingInvite()) {
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
    if (Phase() == plink::GamePhase::Playing && now >= nextPrediction_) {
      UpdatePrediction();
      nextPrediction_ = now + std::chrono::milliseconds(33);
    }
    if (lastPacketAt_.time_since_epoch().count() != 0 &&
        now - lastPacketAt_ > std::chrono::seconds(5)) {
      const bool matchWasActive = haveSnapshot_ &&
          (snapshot_.phase == plink::GamePhase::Countdown ||
           snapshot_.phase == plink::GamePhase::Playing);
      if (matchWasActive) RecordAbandonedMatch();
      session_ = 0;
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

  void Draw(HDC target, const RECT &rect) const {
    const int width = std::max<LONG>(1, rect.right - rect.left);
    const int height = std::max<LONG>(1, rect.bottom - rect.top);
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    RECT full{0, 0, width, height};
    HBRUSH background = CreateSolidBrush(gameMode_ ? RGB(0, 0, 0) : kTransparent);
    FillRect(memory, &full, background);
    DeleteObject(background);
    if (gameMode_) DrawGameWindow(memory, width, height);
    else DrawPet(memory, width, height);
    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
  }

  void DrawHistoryWindow(HDC dc, const RECT &rect) const {
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
      const int top = 178 + static_cast<int>(index) * 38;
      RECT row{20, top, width - 20, top + 34};
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
      RECT ownLabel{112, top, 141, top + 34};
      Text(dc, L"我", ownLabel, 18, kBlueLight, FW_BOLD);
      DrawHealthPips(dc, 143, top + 12, entry.ownHealth, kBlue);
      RECT peerLabel{205, top, 237, top + 34};
      Text(dc, L"对", peerLabel, 18, RGB(255, 150, 165), FW_BOLD);
      DrawHealthPips(dc, 237, top + 12, entry.peerHealth, kRed);
      const std::wstring time = FormatHistoryTime(entry.timestampMs);
      RECT timeRect{310, top, width - 30, top + 34};
      Text(dc, time, timeRect, 17, kMuted, FW_NORMAL,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (!historyPersistenceOk_) {
      RECT warning{24, rect.bottom - 34, width - 24, rect.bottom - 7};
      Text(dc, L"战绩暂未写入磁盘，请检查存档目录权限", warning, 17,
           kYellow, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
  }

  void DrawHelpWindow(HDC dc, const RECT &rect) const {
    const int width = std::max<LONG>(1, rect.right - rect.left);
    Fill(dc, rect, RGB(9, 13, 22));
    RECT title{26, 12, width - 26, 54};
    Text(dc, L"Plane Pet 操作帮助", title, 32, kBlueLight, FW_BOLD,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitle{26, 52, width - 26, 84};
    Text(dc, L"随时邀请朋友，轻量开始一局桌面小游戏", subtitle,
         20, kMuted, FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int y = 92;
    const auto section = [&](const wchar_t *heading, const wchar_t *body,
                             int bodyHeight = 50) {
      RECT headingRect{26, y, width - 26, y + 34};
      Text(dc, heading, headingRect, 23, kWhite, FW_BOLD,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      RECT bodyRect{38, y + 36, width - 28, y + 36 + bodyHeight};
      Text(dc, body, bodyRect, 20, kMuted, FW_NORMAL,
           DT_LEFT | DT_TOP | DT_WORDBREAK);
      y += 38 + bodyHeight;
    };
    section(L"1 · 初次绑定",
            L"绑定电脑框默认显示；想只看飞机动画，可在右键菜单取消勾选“显示绑定电脑框”，再次勾选即可恢复。输入框和信息卡可按住左键拖动；搜索中可按 Esc 或右键停止。两端输入相同六位码并按 Enter，成功后下次会自动上线。", 92);
    section(L"2 · 桌宠与在线状态",
            L"飞机左上角绿点表示对方在线，红点表示离线。像素云向机尾掠过；对方在线时红机会变向逃跑，蓝机切弯追击射击（仅为动画，无伤害）。按住飞机可拖动。", 58);
    section(L"3 · 快捷表情",
            L"双方在线且待机时，点击大笑、哭泣、生气或勾手挑衅即可发送。自己的表情跟随蓝机，对方表情跟随红机约 3 秒；勾手仅播放手指弯曲，气泡不跳动。", 58);
    section(L"4 · 邀请对战",
            L"发出邀请后最多等待 5 分钟，也可点“取消”。对方拒绝或邀请超时后，发起方会在原状态栏看到结果。收到邀请时可选择“接受”或“拒绝”。", 66);
    section(L"5 · 游戏操作",
            L"接受后经过三秒倒计时。使用 WASD、方向键，或按住鼠标左键拖动飞机。最长 3 分钟，届时未分胜负则平局并计入战绩。");
    section(L"6 · 隐藏、恢复与退出",
            L"对局进行中按 Esc、关闭窗口或 Ctrl+Shift+H 可紧急隐藏。单击托盘图标恢复；要彻底关闭程序，请在右键菜单或托盘菜单选择“退出”。", 66);
    section(L"7 · 历史战绩",
            L"右键选择“历史战绩”查看总胜负和最近十局。完整战绩保存在本机；匿名统计只上传第 10 项列出的事件，不会上传完整战绩文件。", 50);
    section(L"8 · 解除绑定",
            L"右键选择“解除绑定”并确认，双方都会回到匹配码输入界面；本机历史战绩仍会保留。", 42);
    section(L"9 · 暂停接收邀请",
            L"需要专注时，可在右键菜单开启“暂停接收邀请”。对方的邀请会被自动拒绝，桌宠不会突然弹出；再次点击即可恢复。", 54);
    section(L"10 · 隐私与本地数据",
            L"同意后会记录启动和使用时长、匹配及在线状态变化、邀请响应、显示/隐藏与勿扰状态、四种表情类型和对局过程/结果。不记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题；服务器原始事件最长保留 90 天。即使关闭统计，联网必需的匿名绑定和鉴权凭据仍会保存。", 114);
  }

  void OnLeftButtonDown(int x, int y) {
    const auto dragPetWindow = [&]() {
      SetForegroundWindow(window_);
      ReleaseCapture();
      POINT cursor{};
      GetCursorPos(&cursor);
      SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION,
                   MAKELPARAM(cursor.x, cursor.y));
      RECT rect{};
      GetWindowRect(window_, &rect);
      petX_ = rect.left;
      petY_ = rect.top;
    };

    if (awaitingPairing_ || pairingAttempted_) {
      SetFocus(window_);
      dragPetWindow();
      return;
    }
    if (gameMode_) {
      if (Phase() == plink::GamePhase::Finished) {
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
    for (int index = 0; index < 4; ++index) {
      const RECT button = QuickEmoteRect(index);
      if (PtInRect(&button, point)) {
        if (Phase() == plink::GamePhase::Menu) SendQuickEmote(index);
        return;
      }
    }
    if (Phase() == plink::GamePhase::Waiting) {
      if (IsIncomingInvite()) {
        RECT accept{65, 95, 137, 122};
        RECT reject{143, 95, 215, 122};
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
        RECT cancel{205, 86, 247, 121};
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
    if (gameMode_ && mouseDragging_) SetMouseTarget(x, y);
  }

  void OnLeftButtonUp() {
    if (!mouseDragging_) return;
    mouseDragging_ = false;
    if (GetCapture() == window_) ReleaseCapture();
  }

  void OnCaptureChanged() { mouseDragging_ = false; }

  bool IsInteractivePetPoint(int x, int y) const {
    if (gameMode_) return true;
    const POINT point{x, y};
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
    if ((awaitingPairing_ || pairingAttempted_) && pairingPanelVisible_) {
      RECT pairingCard{76, 5, kPetWidth - 5, kPetHeight - 5};
      return PtInRect(&pairingCard, point) != FALSE;
    }
    if (!awaitingPairing_ && !pairingAttempted_ &&
        Phase() == plink::GamePhase::Menu) {
      for (int index = 0; index < 4; ++index) {
        const RECT button = QuickEmoteRect(index);
        if (PtInRect(&button, point)) return true;
      }
    }
    if (Phase() == plink::GamePhase::Waiting ||
        (!inviteFeedback_.empty() && Clock::now() < inviteFeedbackUntil_)) {
      RECT inviteCard{26, 68, 252, 149};
      return PtInRect(&inviteCard, point) != FALSE;
    }
    return false;
  }

  void OnKeyDown(WPARAM key) {
    if (pairingAttempted_) {
      if (key == VK_ESCAPE) StopPairing();
      return;
    }
    if (awaitingPairing_) return;
    if (gameMode_ && Phase() == plink::GamePhase::Finished) {
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
    CloseInfoWindow();
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
    if (gameMode_ && Phase() != plink::GamePhase::Menu) {
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
    CloseInfoWindow();
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
    if (gameMode_ && Phase() == plink::GamePhase::Finished) {
      ReturnToPetFromFinished();
    } else if (gameMode_) {
      EmergencyHide();
    } else {
      HideByUser();
    }
  }

  bool IsGameMode() const { return gameMode_; }
  bool IsPairingSearch() const { return pairingAttempted_; }
  bool NeedsPairing() const { return awaitingPairing_ || pairingAttempted_; }
  bool IsPairingPanelVisible() const { return pairingPanelVisible_; }
  void TogglePairingPanel() {
    if (!NeedsPairing()) return;
    pairingPanelVisible_ = !pairingPanelVisible_;
    SaveSettings();
    InvalidateRect(window_, nullptr, FALSE);
  }
  bool HasBinding() const { return bindingId_ != 0; }
  bool IsUnbinding() const { return unbindPending_; }
  bool IsDoNotDisturb() const { return doNotDisturb_; }
  bool IsTelemetryEnabled() const {
    return telemetryEnabled_ &&
           (!telemetryUploadCapable_ || telemetryUploadChoice_ == 1);
  }
  void ToggleTelemetry() {
    if (IsTelemetryEnabled()) {
      telemetryEnabled_ = false;
      if (telemetryUploadCapable_) telemetryUploadChoice_ = 0;
      telemetryChoiceKnown_ = true;
      SaveSettings();
      MessageBoxW(window_, L"已停止后续匿名统计与本地实验事件记录。",
                  L"匿名统计", MB_OK | MB_ICONINFORMATION);
      return;
    }
    if (telemetryUploadCapable_) {
      const int consent = MessageBoxW(
          window_,
          L"开启后会通过加密连接发送启动与使用时长、匹配和好友在线状态变化、邀请响应、桌宠显示/隐藏与勿扰状态、四种固定表情的发送/接收类型，以及对局过程和结果。\n\n不会记录聊天文字、匹配码、姓名、键鼠轨迹、屏幕内容或窗口标题。原始匿名事件最长保留 90 天。",
          L"开启匿名统计", MB_YESNO | MB_ICONINFORMATION);
      if (consent != IDYES) return;
      telemetryUploadChoice_ = 1;
    }
    telemetryEnabled_ = true;
    telemetryChoiceKnown_ = true;
    nextTelemetryHeartbeat_ = Clock::now() + std::chrono::seconds(60);
    SaveSettings();
    LogEvent("session_heartbeat", gameMode_ ? 2 : (hiddenByUser_ ? 1 : 0));
  }
  bool CanInvite() const {
    return !gameMode_ && !awaitingPairing_ && !pairingAttempted_ &&
           session_ != 0 && Phase() == plink::GamePhase::Menu &&
           OpponentOnline();
  }
  bool CanSendQuickEmote() const { return CanInvite(); }
  void InviteFromMenu() {
    if (!CanInvite()) return;
    currentInviteId_ = 0;
    inviteFeedback_.clear();
    inviteFeedbackUntil_ = Clock::time_point{};
    outgoingInviteAt_ = Clock::now();
    LogEvent("invite_sent");
    QueueAction(plink::PlayerAction::Invite);
  }
  void StopPairingFromMenu() { StopPairing(); }
  void ToggleDoNotDisturb() {
    doNotDisturb_ = !doNotDisturb_;
    SaveSettings();
    LogEvent(doNotDisturb_ ? "dnd_enabled" : "dnd_disabled");
    InvalidateRect(window_, nullptr, FALSE);
  }
  void ClearLocalData() {
    const int choice = MessageBoxW(
        window_,
        L"确定清除本机的历史战绩和匿名实验事件吗？\n\n绑定关系、设置和服务器已收到的匿名统计不会被删除。",
        L"清除本地数据", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES) return;
    ResetHistory();
    historyPersistenceOk_ = SaveHistory();
    std::error_code error;
    std::filesystem::remove(eventsPath_, error);
    std::filesystem::remove(eventsPath_.wstring() + L".legacy", error);
    MessageBoxW(window_, L"本机战绩与实验事件已清除。", L"Plane Pet",
                MB_OK | MB_ICONINFORMATION);
  }
  void ExportLocalData() {
    if (eventsPath_.empty() || !std::filesystem::exists(eventsPath_)) {
      MessageBoxW(window_,
                  L"当前没有可导出的匿名实验事件。\n如已关闭实验记录，可在下次启动时通过 --telemetry=1 启用。",
                  L"导出实验数据", MB_OK | MB_ICONINFORMATION);
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
    if (!GetSaveFileNameW(&dialog)) return;
    if (CopyFileW(eventsPath_.c_str(), target, FALSE) == FALSE) {
      MessageBoxW(window_, L"导出失败，请检查目标目录权限。",
                  L"导出实验数据", MB_OK | MB_ICONERROR);
      return;
    }
    MessageBoxW(window_, L"匿名实验事件已导出。", L"导出实验数据",
                MB_OK | MB_ICONINFORMATION);
  }
  void RequestUnbind() {
    if (bindingId_ == 0 || unbindPending_) return;
    const int choice = MessageBoxW(
        window_,
        L"确定解除当前电脑绑定吗？\n\n双方都会回到匹配码输入界面；本机历史战绩会保留。",
        L"解除绑定", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES) return;
    unbindPending_ = true;
    nextUnbind_ = Clock::now();
    LogEvent("unbind_requested");
  }
  const std::wstring &OwnName() const { return ownName_; }

#ifdef PLANE_PET_INFO_SELF_TEST
  bool RunColoredHeartHudSelfTest() {
    haveSnapshot_ = true;
    snapshot_.phase = plink::GamePhase::Playing;
    snapshot_.players[0].health = plink::kInitialHealth;
    snapshot_.players[1].health = plink::kInitialHealth;
    HDC target = GetDC(window_);
    if (target == nullptr) return false;
    HDC memory = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(
        target, plink::kWorldWidth, plink::kWorldHeight);
    if (memory == nullptr || bitmap == nullptr) {
      if (bitmap != nullptr) DeleteObject(bitmap);
      if (memory != nullptr) DeleteDC(memory);
      ReleaseDC(window_, target);
      return false;
    }
    HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
    DrawGameHud(memory, 1, 0, 0, 0);
    const COLORREF ownHeart = GetPixel(memory, 29, 21);
    const COLORREF peerHeart = GetPixel(memory, 190, 21);
    SelectObject(memory, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window_, target);
    snapshot_.phase = plink::GamePhase::Menu;
    return ownHeart == kBlue && peerHeart == kRed;
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
    // Render partway through the firing interval so the fixture contains a
    // fully separated idle projectile rather than one hidden under the jet.
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
        "\"a\":\"%s\",\"i\":\"%016llx\",\"r\":\"%016llx\","
        "\"e\":\"%s\",\"x\":%lld}",
        kTelemetryMagic,
        static_cast<unsigned long long>(telemetrySessionId_), sequence,
        static_cast<unsigned long long>(UnixTimeMillis()), kAppVersion,
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

  void LoadSettings() {
    std::ifstream input(settingsPath_);
    std::string magic;
    uint32_t version = 0;
    int telemetry = -1;
    int upload = -1;
    int dnd = 0;
    int pairingPanel = 1;
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
    }
  }

  bool SaveSettings() const {
    std::error_code error;
    const auto parent = settingsPath_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    const std::filesystem::path temporary = settingsPath_.wstring() + L".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "PLANE_PET_SETTINGS 2 " << (telemetryEnabled_ ? 1 : 0)
           << ' ' << telemetryUploadChoice_
           << ' ' << (doNotDisturb_ ? 1 : 0)
           << ' ' << (pairingPanelVisible_ ? 1 : 0) << '\n';
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
        currentRoundId_ == lastRecordedRoundId_ || slot_ < 1 || slot_ > 2) {
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
    swprintf(text, std::size(text), L"%02u-%02u %02u:%02u",
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
  }

  void BeginPairing(uint32_t code) {
    pairingCode_ = code;
    pairingRequestId_ = SecureRandomU32();
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
    awaitingPairing_ = true;
    serverConfirmedWaiting_ = false;
    pairingCode_ = 0;
    pairingRequestId_ = 0;
    pairingInput_.clear();
    pairingError_ = L"已停止匹配，可以重新输入";
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
    message.bindingId = bindingId_;
    message.tokenLow = tokenLow_;
    message.tokenHigh = tokenHigh_;
    SendControl(message);
  }

  void SendInput(uint32_t nowMs) {
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
    const int left = 64 + std::clamp(index, 0, 3) * 38;
    return RECT{left, 116, left + 35, 149};
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
    ownEmote_ = plink::QuickEmoteValue(action);
    ownEmoteStartedAt_ = now;
    ownEmoteUntil_ = now +
        std::chrono::milliseconds(kEmoteDisplayMilliseconds);
    LogEvent("quick_emote_sent", ownEmote_);
    InvalidateRect(window_, nullptr, FALSE);
  }

  void QueueAction(plink::PlayerAction action) {
    pendingAction_ = static_cast<uint8_t>(action);
    nextAction_ = Clock::now();
  }

  void QueueReturn() {
    QueueAction(plink::PlayerAction::ReturnToMenu);
  }

  void ReturnToPetFromFinished() {
    if (Phase() != plink::GamePhase::Finished) return;
    mouseDragging_ = false;
    currentInput_ = 0;
    if (GetCapture() == window_) ReleaseCapture();
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
    if (message.status == pcpair::Status::Waiting && pairingAttempted_ &&
        message.requestId == pairingRequestId_) {
      serverConfirmedWaiting_ = true;
    } else if (message.status == pcpair::Status::Matched &&
               pairingAttempted_ && message.requestId == pairingRequestId_ &&
               message.bindingId != 0 && message.assignedSlot >= 1 &&
               message.assignedSlot <= 2) {
      bindingId_ = message.bindingId;
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
      SetPetMode(true);
      SendResume();
    } else if (message.status == pcpair::Status::Cancelled &&
               message.requestId == pairingRequestId_) {
      pairingAttempted_ = false;
      awaitingPairing_ = true;
      serverConfirmedWaiting_ = false;
      pairingCode_ = 0;
      pairingRequestId_ = 0;
    } else if (message.status == pcpair::Status::Invalid ||
               message.status == pcpair::Status::AlreadyBound) {
      pairingAttempted_ = false;
      awaitingPairing_ = true;
      serverConfirmedWaiting_ = false;
      pairingCode_ = 0;
      pairingRequestId_ = 0;
      pairingInput_.clear();
      pairingError_ = message.status == pcpair::Status::AlreadyBound
                          ? L"该设备已经绑定，请保留身份存档"
                          : L"匹配请求无效，请重新输入";
    } else if (message.status == pcpair::Status::StorageError) {
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
               (bindingId_ == 0 || message.bindingId == 0 ||
                message.bindingId == bindingId_)) {
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
               bindingId_ != 0) {
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
    for (;;) {
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
      if (header.type == pcpair::kRoundMetaPacketType) {
        if (session_ == 0 || header.session != session_ ||
            header.sequence <= lastServerSequence_) {
          continue;
        }
        pcpair::RoundMeta meta;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (!pcpair::ReadRoundMeta(reader, meta)) continue;
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
      } else if (header.type == plink::PacketType::Snapshot) {
        if (session_ == 0 || header.session != session_ ||
            header.sequence <= lastServerSequence_) {
          continue;
        }
        plink::SnapshotPayload next;
        plink::PayloadReader reader(payload, header.payloadLength);
        if (!plink::ReadSnapshot(reader, next)) continue;
        lastPacketAt_ = Clock::now();
        lastServerSequence_ = header.sequence;
        lastServerTick_ = header.tick;
        const plink::GamePhase previous = Phase();
        const uint8_t previousInviterSlot = snapshot_.inviterSlot;
        snapshot_ = next;
        haveSnapshot_ = true;
        const bool peerOnline = OpponentOnline();
        if (!peerOnlineKnown_ || peerOnline != lastPeerOnline_) {
          peerOnlineKnown_ = true;
          lastPeerOnline_ = peerOnline;
          LogEvent(peerOnline ? "peer_online" : "peer_offline");
        }
        if (!peerOnline) {
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
            QueueAction(plink::PlayerAction::ReturnToMenu);
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
          RecordMatchHistory();
          LogEvent("game_finished", snapshot_.matchElapsedMs);
          LogEvent("game_end_reason",
                   static_cast<int64_t>(snapshot_.endReason));
          const int64_t outcome = snapshot_.winnerSlot == 0
              ? 0 : (snapshot_.winnerSlot == slot_ ? 1 : -1);
          LogEvent("game_outcome", outcome);
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
            if (previousInviterSlot == slot_ &&
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
        }
      }
    }
  }

  uint8_t ReadInput() const {
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
    if (mouseDragging_ && gameMode_ && (predictionReady_ || haveSnapshot_)) {
      RECT client{};
      GetClientRect(window_, &client);
      const int width = std::max<LONG>(1, client.right - client.left);
      const int height = std::max<LONG>(1, client.bottom - client.top);
      const GameLayout layout = MakeGameLayout(width, height);
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
    RECT client{};
    GetClientRect(window_, &client);
    mouseTargetX_ = std::clamp(
        x, 0, std::max(0, static_cast<int>(client.right) - 1));
    mouseTargetY_ = std::clamp(
        y, 0, std::max(0, static_cast<int>(client.bottom) - 1));
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

  void UpdatePrediction() {
    if (!haveSnapshot_) return;
    plink::MovePlayer(predicted_, currentInput_, slot_ - 1);
    const plink::PlayerState &server = snapshot_.players[slot_ - 1];
    const int errorX = server.x - predicted_.x;
    const int errorY = server.y - predicted_.y;
    if (std::abs(errorX) > 24 || std::abs(errorY) > 24) predicted_ = server;
    else {
      predicted_.x = static_cast<int16_t>(
          predicted_.x + std::clamp(errorX, -2, 2));
      predicted_.y = static_cast<int16_t>(
          predicted_.y + std::clamp(errorY, -2, 2));
    }
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
    SetWindowPos(window_, HWND_TOPMOST, petX_, petY_, kPetWidth, kPetHeight,
                 SWP_FRAMECHANGED | (show ? SWP_SHOWWINDOW : SWP_NOACTIVATE));
    if (!show) ShowWindow(window_, SW_HIDE);
  }

  void SetGameMode() {
    CloseInfoWindow();
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
    AdjustWindowRectEx(&gameRect, gameStyle, FALSE, gameExStyle);
    const int gameWidth = gameRect.right - gameRect.left;
    const int gameHeight = gameRect.bottom - gameRect.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
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

  static void DrawHealthPips(HDC dc, int x, int y, uint8_t health,
                             COLORREF color) {
    for (uint8_t index = 0; index < plink::kInitialHealth; ++index) {
      RECT pip{x + static_cast<int>(index) * 17, y,
               x + static_cast<int>(index) * 17 + 13, y + 10};
      const bool filled = index < health;
      RoundPanel(dc, pip, 5,
                 filled ? color : RGB(42, 49, 64),
                 filled ? RGB(225, 242, 255) : RGB(75, 84, 103));
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

  Gdiplus::Bitmap *PetCloudBitmap(int variant) const {
    const size_t index = static_cast<size_t>(std::clamp(variant, 0, 2));
    return EmbeddedBitmap(153 + static_cast<int>(index),
                          petCloudImages_[index]);
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
    Gdiplus::Graphics graphics(dc);
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
    Gdiplus::Graphics graphics(dc);
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

  void GamePlane(HDC dc, int cx, int cy, bool up, COLORREF color) const {
    constexpr int kGameSpriteSize = 36;
    const double angle = up ? 0.0 : 3.14159265358979323846;
    if (!DrawPixelSprite(dc, PetPlaneBitmap(color == kRed), cx, cy,
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

    // Each projectile remembers the aircraft position and direction at firing.
    constexpr uint32_t fireIntervalMs = 620;
    constexpr uint32_t bulletLifetimeMs = 1120;
    const uint32_t newestShot = elapsedMs / fireIntervalMs;
    for (uint32_t back = 0; back < 3 && back <= newestShot; ++back) {
      const uint32_t shotMs = (newestShot - back) * fireIntervalMs;
      const uint32_t ageMs = elapsedMs - shotMs;
      if (ageMs > bulletLifetimeMs) continue;
      const PetAnimationPose shot = peerOnline
          ? PetFormationAt(shotMs).blue : PetPoseAt(shotMs);
      const double distance = 13.0 + static_cast<double>(ageMs) * 0.105;
      const int bulletX = static_cast<int>(std::lround(
          shot.x + shot.directionX * distance));
      const int bulletY = static_cast<int>(std::lround(
          shot.y + shot.directionY * distance));
      DrawPixelProjectile(dc, bulletX, bulletY,
                          shot.directionX, shot.directionY, 1,
                          RGB(96, 43, 12), RGB(255, 190, 32),
                          RGB(255, 251, 194), RGB(255, 82, 24));
    }

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

    // Keep presence information attached to the moving pet.
    int dotX = planeX - 19;
    int dotY = planeY - 19;
    if (ownEmoteVisible) {
      const RECT ownBubble = QuickEmoteBubbleRect(planeX, planeY, true);
      // Sit on the outside of the speech-bubble outline. The glyph ends six
      // pixels before this edge, so the presence dot never covers the emoji.
      // This point lies on the lower-right arc of the 48x45 ellipse, so the
      // status dot straddles the outline instead of floating beside it.
      dotX = ownBubble.right - 3;
      dotY = ownBubble.bottom - 11;
    }
    HBRUSH dot = CreateSolidBrush(peerOnline ? kGreen : kRed);
    HGDIOBJ oldDot = SelectObject(dc, dot);
    Ellipse(dc, dotX - 4, dotY - 4, dotX + 5, dotY + 5);
    SelectObject(dc, oldDot);
    DeleteObject(dot);
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
                        COLORREF fill, COLORREF edge) {
    const int radius = std::max(2, 3 * scale);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, scale), edge);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, centerX - radius - radius / 2, centerY - radius,
            centerX + radius / 2, centerY + radius);
    Ellipse(dc, centerX - radius / 2, centerY - radius,
            centerX + radius + radius / 2, centerY + radius);
    POINT point[3] = {{centerX - radius - radius / 2, centerY},
                      {centerX + radius + radius / 2, centerY},
                      {centerX, centerY + 2 * radius}};
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
                     COLORREF color) const {
    if (!DrawPixelSprite(dc, PetPlaneBitmap(color == kRed), cx, cy, 0.0,
                         24 * std::max(1, scale))) {
      DrawMiniPlaneFallback(dc, cx, cy, scale, color);
    }
  }

  void DrawGameHud(HDC dc, int scale, int ox, int oy,
                   uint8_t viewer) const {
    RECT hud{ox, oy, ox + plink::kWorldWidth * scale,
             oy + kGameHudHeight * scale};
    Fill(dc, hud, RGB(27, 31, 43));
    DrawLine(dc, hud.left, hud.bottom - scale, hud.right,
             hud.bottom - scale, RGB(54, 67, 88), scale);

    const int centerY = oy + 21 * scale;
    DrawMiniPlane(dc, ox + 10 * scale, centerY, scale, kBlue);
    DrawMiniPlane(dc, ox + 230 * scale, centerY, scale, kRed);
    const uint8_t ownHealth = haveSnapshot_
        ? std::min(snapshot_.players[viewer].health, plink::kInitialHealth)
        : plink::kInitialHealth;
    const uint8_t peerHealth = haveSnapshot_
        ? std::min(snapshot_.players[1U - viewer].health,
                   plink::kInitialHealth)
        : plink::kInitialHealth;
    for (uint8_t i = 0; i < ownHealth; ++i)
      DrawHeart(dc, ox + (29 + i * 14) * scale, centerY, scale,
                kBlue, RGB(160, 235, 255));
    for (uint8_t i = static_cast<uint8_t>(plink::kInitialHealth - peerHealth);
         i < plink::kInitialHealth; ++i)
      DrawHeart(dc, ox + (190 + i * 14) * scale, centerY, scale,
                kRed, RGB(255, 150, 160));

    wchar_t timer[20]{};
    if (Phase() == plink::GamePhase::Playing &&
        snapshot_.phaseRemainingMs > 0) {
      const uint32_t seconds = (snapshot_.phaseRemainingMs + 999U) / 1000U;
      swprintf(timer, std::size(timer), L"%us", seconds);
    } else if (Phase() == plink::GamePhase::Playing ||
               Phase() == plink::GamePhase::Finished) {
      const uint32_t elapsed = snapshot_.matchElapsedMs / 1000U;
      if (elapsed < 3600U) {
        if (Phase() == plink::GamePhase::Playing)
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
    RECT timerRect{ox + 76 * scale, oy + 2 * scale, ox + 164 * scale,
                   oy + 27 * scale};
    Text(dc, timer, timerRect, std::max(18, 10 * scale), kWhite, FW_BOLD);
    wchar_t latency[16]{};
    swprintf(latency, 16, L"%ums", rttMs_);
    RECT latencyRect{ox + 76 * scale, oy + 27 * scale,
                     ox + 164 * scale, oy + 49 * scale};
    Text(dc, latency, latencyRect, std::max(16, 8 * scale), kMuted, FW_BOLD);
  }

  void DrawPet(HDC dc, int width, int height) const {
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
      Text(dc, pairingAttempted_ ? L"搜索电脑中" : L"绑定电脑",
           title, 24, pairingAttempted_ ? kYellow : kBlueLight, FW_BOLD,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

      if (pairingAttempted_) {
        wchar_t code[32]{};
        swprintf(code, 32, L"匹配码  %06u", pairingCode_);
        RECT codeRect{96, 39, width - 12, 70};
        Text(dc, code, codeRect, 25, kWhite, FW_BOLD,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT status{96, 71, width - 12, 113};
        Text(dc,
             serverConfirmedWaiting_
                 ? L"等待同码电脑…\n右键或 Esc 停止"
                 : L"连接匹配服务…\n右键或 Esc 停止",
             status, 18, kMuted, FW_NORMAL, DT_LEFT | DT_WORDBREAK);
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
        RECT prompt{96, 34, width - 12, 64};
        Text(dc, pairingError_.empty() ? L"请输入六位匹配码" : pairingError_,
             prompt, pairingError_.empty() ? 20 : 17,
             pairingError_.empty() ? kMuted : kRed, FW_NORMAL,
             DT_LEFT | DT_VCENTER | DT_WORDBREAK);

        std::wstring digits;
        for (size_t index = 0; index < 6; ++index) {
          if (index != 0) digits += L"  ";
          digits += index < pairingInput_.size() ? pairingInput_[index] : L'_';
        }
        RECT input{96, 66, width - 14, 99};
        RoundPanel(dc, input, 10, RGB(10, 14, 24), RGB(82, 108, 148), 2);
        Text(dc, digits, input, 27, kWhite, FW_BOLD,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT hint{96, 103, width - 12, 138};
        Text(dc, L"点击输入 · Enter", hint, 20, kBlueLight,
             FW_NORMAL, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      }
      return;
    }

    DrawPetAnimation(dc, width, height);
    if (!awaitingPairing_ && !pairingAttempted_) {
      const bool emotesEnabled = CanSendQuickEmote();
      for (int index = 0; index < 4; ++index) {
        const RECT button = QuickEmoteRect(index);
        RoundPanel(dc, button, 15,
                   emotesEnabled ? RGB(28, 37, 53) : RGB(25, 29, 38),
                   emotesEnabled ? RGB(91, 146, 190) : RGB(60, 66, 78), 1);
        // 23 x 24 px: 15% smaller than the previous 27 x 28 px artwork,
        // centered without changing the button hit target or bubble display.
        RECT icon{button.left + 6, button.top + 4,
                  button.right - 6, button.bottom - 5};
        if (!DrawQuickEmoteImage(
                dc, static_cast<uint8_t>(index + 1), icon, emotesEnabled)) {
          TextWithFace(dc, QuickEmoteGlyph(static_cast<uint8_t>(index + 1)),
                       button, 24,
                       emotesEnabled ? kWhite : RGB(110, 116, 128),
                       L"Segoe UI Emoji", FW_NORMAL);
        }
      }
    }
    if (!connectionNotice_.empty() &&
        Clock::now() < connectionNoticeUntil_) {
      RECT notice{58, 76, width - 58, 121};
      RoundPanel(dc, notice, 12, kCard, kYellow, 2);
      RECT message{64, 79, width - 64, 118};
      Text(dc, connectionNotice_, message, 19, kWhite, FW_BOLD,
           DT_CENTER | DT_VCENTER | DT_WORDBREAK);
      return;
    }
    if (!inviteFeedback_.empty() && Clock::now() < inviteFeedbackUntil_) {
      RECT panel{28, 74, width - 28, 121};
      const COLORREF edge = inviteFeedback_ == L"对方拒绝邀请"
          ? kRed : kYellow;
      RoundPanel(dc, panel, 14, kCard, edge, 2);
      RECT message{34, 77, width - 34, 118};
      Text(dc, inviteFeedback_, message, 20, edge, FW_BOLD,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return;
    }
    if (Phase() != plink::GamePhase::Waiting) return;

    if (IsIncomingInvite()) {
      RECT panel{55, 68, width - 55, 122};
      RoundPanel(dc, panel, 14, kCard, kCardEdge, 2);
      RECT message{62, 70, width - 62, 95};
      Text(dc, L"对方邀请你开一局...", message, 18, kWhite, FW_BOLD,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      RECT accept{65, 95, 137, 122};
      RECT reject{143, 95, 215, 122};
      RoundPanel(dc, accept, 10, RGB(24, 116, 70), kGreen, 2);
      RoundPanel(dc, reject, 10, RGB(126, 35, 48), kRed, 2);
      Text(dc, L"接受", accept, 19, kWhite, FW_BOLD);
      Text(dc, L"拒绝", reject, 19, kWhite, FW_BOLD);
      return;
    }

    RECT panel{28, 76, width - 28, 122};
    RoundPanel(dc, panel, 14, kCard, kCardEdge, 2);
    const size_t dotCount = static_cast<size_t>((MillisSince(start_) / 380U) % 4U);
    std::wstring waiting = L"等待对方响应";
    waiting.append(dotCount, L'.');
    RECT message{34, 79, 157, 119};
    Text(dc, waiting, message, 18, kWhite, FW_BOLD,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const uint32_t remainingSeconds =
        (snapshot_.phaseRemainingMs + 999U) / 1000U;
    wchar_t countdown[16]{};
    swprintf(countdown, std::size(countdown), L"%us", remainingSeconds);
    RECT countdownRect{160, 79, 202, 119};
    Text(dc, countdown, countdownRect, 18, kYellow, FW_BOLD);
    RECT cancel{205, 86, 247, 121};
    RoundPanel(dc, cancel, 10, RGB(74, 38, 44), kRed, 2);
    Text(dc, L"取消", cancel, 18, kWhite, FW_BOLD);
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
    plink::PlayerState players[2]{};
    if (haveSnapshot_) {
      players[0] = snapshot_.players[0];
      players[1] = snapshot_.players[1];
      if (predictionReady_ && Phase() == plink::GamePhase::Playing)
        players[viewer] = predicted_;
    } else {
      plink::WorldState preview;
      plink::InitializeWorld(preview);
      players[0] = preview.players[0];
      players[1] = preview.players[1];
    }
    const plink::PlayerState own = plink::ToLocalView(players[viewer], viewer);
    const plink::PlayerState peer = plink::ToLocalView(players[1 - viewer], viewer);
    GamePlane(dc, ox + own.x * scale, layout.ScreenY(own.y), true, kBlue);
    GamePlane(dc, ox + peer.x * scale, layout.ScreenY(peer.y), false, kRed);
    if (haveSnapshot_) {
      for (uint8_t i = 0; i < snapshot_.bulletCount; ++i) {
        const plink::BulletState bullet =
            plink::ToLocalView(snapshot_.bullets[i], viewer);
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
    RestoreDC(dc, savedPlayfield);
    DrawGameHud(dc, scale, ox, layout.top, viewer);
    if (Phase() == plink::GamePhase::Countdown) {
      const uint32_t count = std::max(1U,
          (snapshot_.phaseRemainingMs + 999U) / 1000U);
      wchar_t value[4]{};
      swprintf(value, 4, L"%u", count);
      RECT center{ox, layout.ScreenY(100),
                  ox + plink::kWorldWidth * scale, layout.ScreenY(205)};
      Text(dc, value, center, std::max(76, 39 * scale), kWhite, FW_BOLD);
    } else if (Phase() == plink::GamePhase::Finished) {
      const bool draw = snapshot_.winnerSlot == 0;
      const bool won = snapshot_.winnerSlot == slot_;
      const wchar_t *result = draw ? L"平局" : (won ? L"WIN!" : L"LOST!");
      RECT center{ox, layout.ScreenY(100),
                  ox + plink::kWorldWidth * scale, layout.ScreenY(180)};
      Text(dc, result, center, std::max(48, 23 * scale),
           draw ? kYellow : (won ? kGreen : kRed), FW_BOLD);
      const wchar_t *reason = L"";
      if (snapshot_.endReason == plink::MatchEndReason::TimeLimitDraw) {
        reason = L"达到 3 分钟，本局平局";
      } else if (snapshot_.endReason ==
                 plink::MatchEndReason::PlayerDisconnected) {
        reason = snapshot_.winnerSlot == slot_
            ? L"对方断线，本局结束" : L"连接中断，本局结束";
      } else if (snapshot_.endReason ==
                 plink::MatchEndReason::ServerUnavailable) {
        reason = L"服务器连接中断，本局结束";
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
  uint64_t telemetryEventSequence_ = 0;
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
  bool historyRecordedForRound_ = false;
  bool historyPersistenceOk_ = true;
  bool statePersistenceOk_ = true;
  bool stateRecoveredFromBackup_ = false;
  bool abandonedMatch_ = false;
  bool peerOnlineKnown_ = false;
  bool lastPeerOnline_ = false;
  mutable EmbeddedImageResource emojiImages_[4];
  mutable EmbeddedImageResource petPlaneImages_[2];
  mutable EmbeddedImageResource petCloudImages_[3];
  std::vector<HistoryEntry> recentHistory_;
  plink::SnapshotPayload snapshot_{};
  plink::PlayerState predicted_{};
};

PetClient gClient;
NOTIFYICONDATAW gTray{};
bool gTrayAdded = false;
HWND gInfoWindow = nullptr;

HICON PlanePetIcon(int width, int height) {
  HICON icon = static_cast<HICON>(LoadImageW(
      GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
      width, height, LR_DEFAULTCOLOR | LR_SHARED));
  return icon != nullptr
      ? icon : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
}

enum class InfoWindowMode {
  History,
  Help,
};

InfoWindowMode gInfoWindowMode = InfoWindowMode::History;
int gInfoDpi = 96;
int gInfoScrollOffset = 0;
constexpr int kHistoryWidth = 560;
constexpr int kHistoryContentHeight = 600;
constexpr int kHelpWidth = 700;
constexpr int kHelpViewportHeight = 740;
constexpr int kHelpContentHeight = 1140;

int InfoBaseWidth() {
  return gInfoWindowMode == InfoWindowMode::History
      ? kHistoryWidth : kHelpWidth;
}

int InfoContentHeight() {
  return gInfoWindowMode == InfoWindowMode::History
      ? kHistoryContentHeight : kHelpContentHeight;
}

UINT WindowDpi(HWND window) {
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  if (user32 == nullptr) return 96;
  using GetDpi = UINT(WINAPI *)(HWND);
  const FARPROC address = GetProcAddress(user32, "GetDpiForWindow");
  GetDpi getDpi = nullptr;
  static_assert(sizeof(getDpi) == sizeof(address));
  std::memcpy(&getDpi, &address, sizeof(getDpi));
  return getDpi == nullptr ? 96 : std::max<UINT>(96, getDpi(window));
}

int InfoPageHeight(HWND window) {
  RECT client{};
  GetClientRect(window, &client);
  return std::max(1, MulDiv(client.bottom - client.top, 96, gInfoDpi));
}

void UpdateInfoScrollBar(HWND window) {
  const int page = InfoPageHeight(window);
  gInfoScrollOffset = std::clamp(
      gInfoScrollOffset, 0, std::max(0, InfoContentHeight() - page));
  SCROLLINFO info{};
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  info.nMin = 0;
  info.nMax = InfoContentHeight() - 1;
  info.nPage = static_cast<UINT>(page);
  info.nPos = gInfoScrollOffset;
  SetScrollInfo(window, SB_VERT, &info, TRUE);
}

void CloseInfoWindow() {
  if (gInfoWindow != nullptr && IsWindow(gInfoWindow)) {
    DestroyWindow(gInfoWindow);
  }
  gInfoWindow = nullptr;
}

LRESULT CALLBACK InfoWindowProcedure(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      RECT physical{};
      GetClientRect(window, &physical);
      const int page = InfoPageHeight(window);
      const int saved = SaveDC(dc);
      SetMapMode(dc, MM_ANISOTROPIC);
      SetWindowExtEx(dc, InfoBaseWidth(), page, nullptr);
      SetViewportExtEx(dc, std::max(1L, physical.right),
                       std::max(1L, physical.bottom), nullptr);
      SetWindowOrgEx(dc, 0, gInfoScrollOffset, nullptr);
      RECT rect{0, gInfoScrollOffset, InfoBaseWidth(),
                gInfoScrollOffset + page};
      if (gInfoWindowMode == InfoWindowMode::History) {
        gClient.DrawHistoryWindow(dc, rect);
      } else {
        gClient.DrawHelpWindow(dc, rect);
      }
      RestoreDC(dc, saved);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_SIZE:
      UpdateInfoScrollBar(window);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
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
      if (window == gInfoWindow) gInfoWindow = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

void ShowInfoWindow(HWND owner, InfoWindowMode mode) {
  CloseInfoWindow();
  gInfoWindowMode = mode;
  gInfoScrollOffset = 0;
  gInfoDpi = static_cast<int>(WindowDpi(owner));
  const int baseClientWidth = mode == InfoWindowMode::History
      ? kHistoryWidth : kHelpWidth;
  const int baseClientHeight = mode == InfoWindowMode::History
      ? kHistoryContentHeight : kHelpViewportHeight;
  const int desiredClientWidth = MulDiv(baseClientWidth, gInfoDpi, 96);
  const int desiredClientHeight = MulDiv(baseClientHeight, gInfoDpi, 96);
  const DWORD style = WS_CAPTION | WS_SYSMENU | WS_VSCROLL;
  const DWORD extendedStyle = WS_EX_TOOLWINDOW;

  RECT ownerRect{};
  GetWindowRect(owner, &ownerRect);
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST),
                  &monitorInfo);
  const RECT work = monitorInfo.rcWork;
  RECT chrome{0, 0, 0, 0};
  AdjustWindowRectEx(&chrome, style, FALSE, extendedStyle);
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
  AdjustWindowRectEx(&frame, style, FALSE, extendedStyle);
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
      ? L"Plane Pet - 历史战绩" : L"Plane Pet - 操作帮助";
  gInfoWindow = CreateWindowExW(
      extendedStyle, kInfoWindowClassName, title, style, x, y, width, height,
      owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (gInfoWindow == nullptr) return;
  UpdateInfoScrollBar(gInfoWindow);
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
  swprintf(gTray.szTip, 128, L"Plane Pet - %s", gClient.OwnName().c_str());
  gTrayAdded = Shell_NotifyIconW(NIM_ADD, &gTray) != FALSE;
}

void RemoveTray() {
  if (gTrayAdded) Shell_NotifyIconW(NIM_DELETE, &gTray);
  gTrayAdded = false;
}

void ShowPetContextMenu(HWND window) {
  if (gClient.IsGameMode()) return;
  POINT point{};
  GetCursorPos(&point);
  HMENU menu = CreatePopupMenu();
  if (gClient.IsPairingSearch()) {
    AppendMenuW(menu, MF_STRING, kMenuStopPairing, L"停止匹配");
  } else {
    AppendMenuW(menu,
                MF_STRING | (gClient.CanInvite() ? MF_ENABLED : MF_GRAYED),
                kMenuInvite, L"邀请对战");
  }
  if (gClient.NeedsPairing()) {
    AppendMenuW(menu,
                MF_STRING | (gClient.IsPairingPanelVisible() ? MF_CHECKED : 0),
                kMenuPairingPanel, L"显示绑定电脑框");
  }
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuHistory, L"历史战绩");
  AppendMenuW(menu, MF_STRING, kMenuHelp, L"操作帮助");
  AppendMenuW(menu, MF_STRING | (gClient.IsDoNotDisturb() ? MF_CHECKED : 0),
              kMenuDoNotDisturb, L"暂停接收邀请");
  AppendMenuW(menu, MF_STRING | (gClient.IsTelemetryEnabled() ? MF_CHECKED : 0),
              kMenuTelemetry, L"匿名测试统计");
  AppendMenuW(menu, MF_STRING, kMenuExportData, L"导出实验数据…");
  AppendMenuW(menu, MF_STRING, kMenuClearLocalData, L"清除本地数据…");
  AppendMenuW(menu,
              MF_STRING | (gClient.HasBinding() && !gClient.IsUnbinding()
                               ? MF_ENABLED : MF_GRAYED),
              kMenuUnbind,
              gClient.IsUnbinding() ? L"正在解除绑定…" : L"解除绑定");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuHide, L"隐藏悬浮窗");
  AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");
  SetForegroundWindow(window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
  DestroyMenu(menu);
  PostMessageW(window, WM_NULL, 0, 0);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
  switch (message) {
    case WM_NCHITTEST: {
      if (gClient.IsGameMode())
        return DefWindowProcW(window, message, wParam, lParam);
      POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ScreenToClient(window, &point);
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
    case WM_LBUTTONDOWN:
      gClient.OnLeftButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      return 0;
    case WM_MOUSEMOVE:
      gClient.OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      return 0;
    case WM_LBUTTONUP:
      gClient.OnLeftButtonUp();
      return 0;
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
        POINT point{};
        GetCursorPos(&point);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuShow, L"显示桌宠");
        AppendMenuW(menu, MF_STRING, kMenuHide, L"隐藏桌宠");
        if (gClient.NeedsPairing()) {
          AppendMenuW(
              menu,
              MF_STRING |
                  (gClient.IsPairingPanelVisible() ? MF_CHECKED : 0),
              kMenuPairingPanel, L"显示绑定电脑框");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuHistory, L"历史战绩");
        AppendMenuW(menu, MF_STRING, kMenuHelp, L"操作帮助");
        AppendMenuW(menu,
                    MF_STRING | (gClient.IsDoNotDisturb() ? MF_CHECKED : 0),
                    kMenuDoNotDisturb, L"暂停接收邀请");
        AppendMenuW(menu,
                    MF_STRING | (gClient.IsTelemetryEnabled() ? MF_CHECKED : 0),
                    kMenuTelemetry, L"匿名测试统计");
        AppendMenuW(menu, MF_STRING, kMenuExportData, L"导出实验数据…");
        AppendMenuW(menu, MF_STRING, kMenuClearLocalData,
                    L"清除本地数据…");
        AppendMenuW(menu,
                    MF_STRING | (gClient.HasBinding() &&
                                         !gClient.IsUnbinding()
                                     ? MF_ENABLED : MF_GRAYED),
                    kMenuUnbind,
                    gClient.IsUnbinding() ? L"正在解除绑定…" : L"解除绑定");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");
        SetForegroundWindow(window);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window,
                       nullptr);
        DestroyMenu(menu);
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
    case WM_DESTROY:
      CloseInfoWindow();
      UnregisterHotKey(window, kBossHotkeyId);
      RemoveTray();
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

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
  infoClass.style = CS_HREDRAW | CS_VREDRAW;
  infoClass.lpfnWndProc = InfoWindowProcedure;
  infoClass.hInstance = instance;
  infoClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
  infoClass.hIcon = PlanePetIcon(GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON));
  infoClass.hbrBackground =
      static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  infoClass.lpszClassName = kInfoWindowClassName;
  if (!RegisterClassW(&infoClass)) return 1;
  HWND window = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST, kWindowClassName,
      L"Plane Pet PC-PC Test", WS_POPUP, 100, 100, kPetWidth, kPetHeight,
      nullptr, nullptr, instance, nullptr);
  if (window == nullptr) return 1;
  if (!gClient.Initialize(window)) return 2;
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
  const bool coloredHeartOk = gClient.RunColoredHeartHudSelfTest();
  const bool finishedCloseOk = gClient.RunFinishedCloseSelfTest();
  const bool historyPersistenceOk = gClient.RunHistoryPersistenceSelfTest();
  gClient.Shutdown();
  DestroyWindow(window);
  return historyOk && historyClosedWithPet && helpOk && coloredHeartOk &&
      finishedCloseOk && historyPersistenceOk ? 0 : 3;
#endif
  RegisterHotKey(window, kBossHotkeyId,
                 MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'H');
  AddTray(window);
  MSG message{};
  auto nextFrame = Clock::now();
  bool running = true;
  while (running) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    gClient.Update();
    const auto now = Clock::now();
    if (now >= nextFrame) {
      if (IsWindowVisible(window)) InvalidateRect(window, nullptr, FALSE);
      nextFrame = now + std::chrono::milliseconds(16);
    }
    Sleep(1);
  }
  gClient.Shutdown();
  return 0;
}
