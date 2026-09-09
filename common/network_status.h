#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace plane_pet_network {
enum class State : unsigned { Starting, Connecting, Connected, Disconnected,
  CredentialError, PortBusy, RateLimited, CapacityFull, ProxyError, TlsError,
  NetworkError, ServiceUnavailable };
struct Status {
  uint64_t tick = 0;
  State state = State::Starting;
  unsigned error = 0;
};
inline bool Write(const std::filesystem::path &path, State state, unsigned error) {
  const std::filesystem::path temporary = path.wstring() + L".tmp";
  std::ofstream output(temporary, std::ios::trunc);
  output << "PPNET1 " << GetTickCount64() << ' ' << static_cast<unsigned>(state)
         << ' ' << error << '\n';
  output.close();
  return output && MoveFileExW(temporary.c_str(), path.c_str(),
      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
inline Status Read(const std::filesystem::path &path) {
  Status result;
  if (path.empty()) return result;
  std::error_code error;
  if (std::filesystem::file_size(path, error) > 160 || error) return result;
  std::ifstream input(path);
  std::string magic;
  unsigned state = 0;
  if (!(input >> magic >> result.tick >> state >> result.error) ||
      magic != "PPNET1" || state > static_cast<unsigned>(State::ServiceUnavailable))
    return {};
  result.state = static_cast<State>(state);
  return result;
}
inline std::wstring Describe(const Status &status, uint64_t now = GetTickCount64()) {
  if (!status.tick || now < status.tick || now - status.tick > 45000)
    return L"联网组件暂无新状态，请检查程序是否仍在运行";
  switch (status.state) {
    case State::Starting: return L"联网组件正在启动";
    case State::Connecting: return L"正在直连游戏服务器";
    case State::Connected: return L"公网加密隧道已连接";
    case State::CredentialError: return L"联网凭据不可用，请联系测试发起者；不要直接删除绑定";
    case State::PortBusy: return L"本地端口已被占用，请退出其他 Plane Pet 测试实例";
    case State::RateLimited: return L"当前网络登记过于频繁，请稍后重试";
    case State::CapacityFull: return L"服务器测试名额已满，请联系测试发起者";
    case State::ProxyError: return L"代理连接或认证失败，请检查系统代理设置";
    case State::TlsError: return L"安全连接校验失败，请检查系统时间；不要关闭证书校验";
    case State::ServiceUnavailable: return L"服务器繁忙或暂不可用，正在等待重试";
    case State::Disconnected: return L"公网连接已断开，正在重新连接";
    default: return L"暂时无法连接服务器，请检查网络或所在网络的访问限制";
  }
}
inline unsigned RetryMilliseconds(unsigned failures, unsigned entropy) {
  const unsigned capped = failures > 5 ? 5 : failures;
  const unsigned base = (1000U << capped);
  return base + entropy % 1000U;  // bounded 1–33 s, never a reconnect busy loop
}
}  // namespace plane_pet_network
