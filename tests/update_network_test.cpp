#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef PLANE_PET_UPDATE_SELF_TEST
#define PLANE_PET_UPDATE_SELF_TEST
#endif
#define PLANE_PET_UPDATE_NETWORK_TEST
#include "../desktop/update_manager.cpp"
#include <cstdio>
#include <chrono>

namespace plane_pet_update {
struct ManagerTestAccess {
  static void StartRequest(Manager &m, const std::string &url) {
    m.cancel_.store(false); m.workerRunning_.store(true);
    m.worker_ = std::thread([&m, url] {
      WorkerCompletion finished{m.workerRunning_};
      std::vector<uint8_t> bytes; HttpGet(url, 1024, bytes, &m.cancel_);
    });
  }
};
}
using namespace plane_pet_update;
struct LocalServer {
  SOCKET listener = INVALID_SOCKET;
  unsigned port = 0;
  std::atomic<bool> stop{false}, ready{false};
  std::thread worker;
  explicit LocalServer(int mode) {
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(listener, 1)) return;
    int length = sizeof(address); getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length);
    port = ntohs(address.sin_port);
    worker = std::thread([this, mode] {
      SOCKET peer = INVALID_SOCKET;
      for (;;) {
        if (stop.load()) break;
        SOCKET target = peer == INVALID_SOCKET ? listener : peer;
        fd_set read; FD_ZERO(&read); FD_SET(target, &read); timeval wait{0, 20000};
        if (select(0, &read, nullptr, nullptr, &wait) <= 0) continue;
        if (peer == INVALID_SOCKET) { peer = accept(listener, nullptr, nullptr); continue; }
        char request[8192]{}; const int received = recv(peer, request, sizeof(request), 0);
        if (received <= 0) break;
        if (!ready.load()) {
          const char *reply = mode == 1 ? "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nx" :
              mode == 3 ? "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello" :
              mode == 4 ? "HTTP/1.1 200 OK\r\nContent-Length: 99999999\r\nConnection: close\r\n\r\n" :
              mode == 5 ? "HTTP/1.1 302 Found\r\nLocation: https://example.org/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" :
              mode == 6 ? "HTTP/1.1 200 OK\r\nContent-Length: 9\r\nConnection: close\r\n\r\nshort" : nullptr;
          if (reply) send(peer, reply, static_cast<int>(strlen(reply)), 0);
          ready.store(true);
          if (mode >= 3) break;
        }
      }
      if (peer != INVALID_SOCKET) { shutdown(peer, SD_BOTH); closesocket(peer); }
    });
  }
  ~LocalServer() { stop.store(true); if (worker.joinable()) worker.join(); if (listener != INVALID_SOCKET) closesocket(listener); }
  std::string Url(bool tls = false) const { return std::string(tls ? "https://" : "http://") + "127.0.0.1:" + std::to_string(port) + "/test"; }
  bool WaitReady() const {
    for (unsigned i = 0; i < 300 && !ready.load(); ++i) Sleep(10);
    return ready.load();
  }
};
int main() {
  WSADATA ws{}; if (WSAStartup(MAKEWORD(2, 2), &ws)) return 2;
  unsigned count = 0; uint64_t maxMs = 0;
  // Stall before response headers, in the response body, and during TLS handshake.
  // TLS certificates are never bypassed: the TLS server deliberately sends none.
  for (int iteration = 0; iteration < 6; ++iteration) for (int mode = 0; mode < 3; ++mode) {
    LocalServer server(mode); if (!server.port) return 3;
    auto manager = std::make_unique<Manager>();
    ManagerTestAccess::StartRequest(*manager, server.Url(mode == 2));
    if (!server.WaitReady()) { std::printf("NETWORK_TEST_SERVER_NOT_READY mode=%d error=%lu contexts=%u\n", mode, testLastHttpError.load(), testLiveHttpContexts.load()); return 4; }
    Sleep(60);
    const auto start = GetTickCount64();
    if (iteration % 2 == 0) manager->CancelPending();
    manager.reset(); // half cancel explicitly; half rely on destructor cancellation
    const auto elapsed = GetTickCount64() - start; maxMs = std::max<uint64_t>(maxMs, elapsed);
    if (elapsed > 750) { std::printf("CANCEL_TOO_SLOW mode=%d ms=%llu\n", mode, elapsed); return 5; }
    ++count;
  }
  for (int mode = 3; mode <= 6; ++mode) {
    LocalServer server(mode); std::vector<uint8_t> bytes; std::atomic<bool> cancel{false};
    const bool ok = HttpGet(server.Url(), 1024, bytes, &cancel);
    if (ok != (mode == 3) || (ok && std::string(bytes.begin(), bytes.end()) != "hello")) return 6;
    ++count;
  }
  // Final callback owns pending buffers; all contexts must eventually disappear.
  for (unsigned i = 0; i < 300 && testLiveHttpContexts.load(); ++i) Sleep(10);
  if (testLiveHttpContexts.load()) { std::printf("HTTP_CONTEXT_LEAK %u\n", testLiveHttpContexts.load()); return 7; }
  WSACleanup();
  std::printf("UPDATE_NETWORK_REGRESSIONS_OK cases=%u max_cancel_ms=%llu live_contexts=0\n", count,
      static_cast<unsigned long long>(maxMs));
}
