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

// The included server translation unit defines the POSIX socket aliases.
// Keep only header imports here so INVALID_SOCKET is not defined twice.
#endif

#include "../common/pairing_protocol.h"
#include "../shared/plane_protocol.h"
#include "../shared/plane_sim.h"

#define private public
#define main OriginalServerMainForRegression
#include "../server/main.cpp"
#undef main
#undef private

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  Config config;
  config.storePath = std::filesystem::u8path(argv[1]) / "expiry.db";
  PetServer server(config);
  Binding provisional;
  provisional.id = 42;
  provisional.devices[0] = 11;
  provisional.devices[1] = 12;
  provisional.tokenLow[0] = provisional.tokenHigh[0] = 21;
  provisional.tokenLow[1] = provisional.tokenHigh[1] = 22;
  provisional.pendingRequest[1] = 31; // only the other client saved its credentials
  provisional.provisionalDeadline = Clock::now() - std::chrono::seconds(1);
  server.bindings_.push_back(provisional);
  Binding confirmed = provisional;
  confirmed.id = 43;
  confirmed.devices[0] = 13;
  confirmed.devices[1] = 14;
  confirmed.pendingRequest[1] = 0;
  confirmed.provisionalDeadline = {};
  server.bindings_.push_back(confirmed);
  if (!server.SaveBindings()) return 3;
  server.Tick();
  if (server.FindBinding(42) != nullptr || server.FindBinding(43) == nullptr) return 4;
  std::puts("UNCONFIRMED_PAIR_EXPIRES_CONFIRMED_PAIR_PRESERVED_OK");
  return 0;
}
