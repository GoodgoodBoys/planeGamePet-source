#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
#include "../common/pc_motion_view.h"

using namespace pcmotion;
static uint64_t checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL line=%d %s\n", __LINE__, #x); return false; } } while (0)
double Distance(Point a, Point b) { return std::hypot(double(a.x - b.x), double(a.y - b.y)) / kUnit; }
bool Movement() {
  for (unsigned slot = 0; slot < 2; ++slot) {
    for (unsigned bits = 0; bits < 16; ++bits) {
      Point p = Spawn(slot);
      for (int tick = 0; tick < 500; ++tick) {
        auto q = Move(p, {static_cast<uint8_t>(bits)}, slot);
        CHECK(Distance(p, q) <= 1.5); CHECK(q == Bound(q, slot)); p = q;
      }
    }
    for (int x = 0; x < 240; x += 7) for (int y = 0; y < 320; y += 13) {
      Command c{0, true, static_cast<uint8_t>(x), static_cast<uint16_t>(y)};
      Point p = Spawn(slot);
      for (int n = 0; n < 300; ++n) {
        Point q = Move(p, c, slot); CHECK(Distance(p, q) <= 1.5); p = q;
      }
      CHECK(Move(p, c, slot) == p);
      Point target{x * kUnit, y * kUnit};
      if (slot) target = {(239 - x) * kUnit, (319 - y) * kUnit};
      CHECK(p == Bound(target, slot));
    }
  }
  Point one = Spawn(0), two = Spawn(1);
  for (unsigned n = 0; n < 1000; ++n) {
    Command c{0, true, static_cast<uint8_t>((n * 37) % 240), static_cast<uint16_t>((n * 71) % 320)};
    one = Move(one, c, 0); two = Move(two, c, 1);
    CHECK(one.x + two.x == 239 * kUnit); CHECK(one.y + two.y == 319 * kUnit);
  }
  std::puts("MOTION_SPEED_TARGET_BOUNDARY_MIRROR_OK"); return true;
}
bool Collision() {
  CHECK(SweptHit({0, 0}, {0, 100 * kUnit}, {0, 50 * kUnit}, {0, 50 * kUnit}));
  CHECK(!SweptHit({11 * kUnit, 0}, {11 * kUnit, 100 * kUnit}, {0, 50 * kUnit}, {0, 50 * kUnit}));
  CHECK(SweptHit({0, 0}, {0, 0}, {-20 * kUnit, 0}, {20 * kUnit, 0}));
  // Overlapping swept bounding boxes alone would be a false positive here.
  CHECK(!SweptHit({0, 0}, {0, 100 * kUnit}, {0, 90 * kUnit}, {100 * kUnit, 90 * kUnit}));
  plink::WorldState legacy; plink::InitializeWorld(legacy); World world;
  const Point spawn = world.players[0];
  world.bullets[0] = {true, 1, 2, {spawn.x, spawn.y - 14 * kUnit}, 0};
  Command idle[2]{}; world.Step(legacy, idle);
  CHECK(legacy.players[0].health == 2); CHECK(world.players[0] == spawn);
  CHECK(legacy.hitCount[1] == 1); world.Step(legacy, idle);
  CHECK(legacy.players[0].health == 2); // no duplicate hit
  legacy.players[0].health = legacy.players[1].health = 1;
  world.bullets[0] = {true, 2, 2, world.players[0], 0};
  world.bullets[1] = {true, 3, 1, world.players[1], 0};
  world.Step(legacy, idle);
  CHECK(!legacy.players[0].health && !legacy.players[1].health);
  World firing; plink::InitializeWorld(legacy);
  for (int n = 0; n < 47; ++n) firing.Step(legacy, idle);
  CHECK(firing.nextBullet == 1); firing.Step(legacy, idle); CHECK(firing.nextBullet == 3);
  CHECK(firing.bullets[0].p.y == Spawn(0).y - 16 * kUnit);
  World movingShot; plink::InitializeWorld(legacy); movingShot.tick = 47;
  const Command moving[2]{{plink::InputRight}, {}};
  movingShot.Step(legacy, moving);
  CHECK(movingShot.players[0].x == Spawn(0).x + kStep);
  CHECK(movingShot.bullets[0].p.x == Spawn(0).x); // same interval as target movement
  std::puts("MOTION_CONTINUOUS_HITS_NO_RESPAWN_FIRE_CADENCE_OK"); return true;
}
bool Wire() {
  Batch b; b.round = 123456789; b.first = 1; b.count = kBatchLimit;
  for (unsigned i = 0; i < b.count; ++i) b.commands[i] = {0, true, static_cast<uint8_t>(i), uint16_t(200 + i)};
  uint8_t bytes[plink::kMaxPacketSize]{};
  plink::PacketWriter writer(bytes, sizeof(bytes), kInputType, 1, 1, 0, 0);
  CHECK(WriteBatch(writer, b)); auto length = writer.Finish(); CHECK(length <= 192);
  plink::PacketHeader header; const uint8_t *payload = nullptr;
  CHECK(plink::ParsePacket(bytes, length, header, payload));
  Batch read; plink::PayloadReader reader(payload, header.payloadLength); CHECK(ReadBatch(reader, read));
  CHECK(read.round == b.round && read.commands[47].y == 247);
  for (unsigned cut = 0; cut < header.payloadLength; ++cut) {
    plink::PayloadReader truncated(payload, cut); CHECK(!ReadBatch(truncated, read));
  }
  Snapshot s; s.presence.game.players[0] = {120, 264, 3, 1}; s.presence.game.players[1] = {119, 55, 3, 1};
  s.fractions[0] = 249; s.presence.game.bulletCount = 12;
  plink::PacketWriter sw(bytes, sizeof(bytes), kSnapshotType, 1, 2, 0, 0);
  CHECK(WriteSnapshot(sw, s)); length = sw.Finish(); CHECK(length <= 192);
  CHECK(plink::ParsePacket(bytes, length, header, payload));
  Snapshot r; plink::PayloadReader sr(payload, header.payloadLength); CHECK(ReadSnapshot(sr, r));
  CHECK(r.Position(0).x == 120 * kUnit + 249);
  InputQueue queue; CHECK(!queue.Put(10000, {})); CHECK(queue.Put(1, {plink::InputRight}));
  CHECK(queue.Put(1, {plink::InputLeft}));
  CHECK(queue.Put(2, {})); CHECK(queue.Put(3, {})); CHECK(queue.Put(4, {}));
  Command c; CHECK(queue.Pop(c)); CHECK(c.keys == plink::InputRight); CHECK(!queue.Put(1, {}));
  std::puts("MOTION_WIRE_LIMIT_MALFORMED_DUPLICATE_WINDOW_OK"); return true;
}
bool Presentation() {
  Timeline timeline;
  Snapshot a, b;
  a.presence.roundId = b.presence.roundId = 1;
  a.presence.game.phase = b.presence.game.phase = plink::GamePhase::Playing;
  a.presence.game.serverTimeMs = 1000; b.presence.game.serverTimeMs = 1033;
  a.presence.game.players[0].x = 120; b.presence.game.players[0].x = 123;
  a.presence.game.bulletCount = b.presence.game.bulletCount = 1;
  a.presence.game.bullets[0] = {10, 1, 100, 200}; b.presence.game.bullets[0] = {10, 1, 100, 194};
  timeline.Add(a, 1050); timeline.Add(b, 1083);
  auto sampled = timeline.Sample(1133); // server time 1016
  CHECK(sampled.players[0].x == 121 && sampled.bullets[0].y == 197);
  CHECK(timeline.Sample(1400).bullets[0].y == 194); // no unlimited extrapolation
  b.presence.game.bullets[0].id = 11; timeline = Timeline{};
  timeline.Add(a, 1050); timeline.Add(b, 1083);
  CHECK(timeline.Sample(1133).bullets[0].y == 200); // distinct shots never blend
  b.presence.roundId = 2; timeline.Add(b, 1100); CHECK(timeline.size == 1);
  std::puts("MOTION_PRESENTATION_COMMON_TIMELINE_IDS_NEW_ROUND_OK"); return true;
}
struct Flight { int due; Batch batch; };
struct Ack { int due; uint32_t seq; Point p; };
bool Network(int rtt, int fps, int jitter, int loss, unsigned seed) {
  std::mt19937 random(seed); Predictor p; p.Reset(Spawn(0), 0); InputQueue server;
  Point authoritative = Spawn(0);
  std::vector<Flight> uplink; std::vector<Ack> downlink;
  int64_t elapsed = 0; int frame = 0; uint32_t ackLatest = 0;
  const auto delay = [&]() { return rtt / 2 + int(random() % (jitter + 1)); };
  unsigned peak = 0;
  for (int ms = 0; ms < 120000; ++ms) {
    if (ms >= frame * 1000 / fps) {
      const int prevMs = frame ? (frame - 1) * 1000 / fps : 0;
      elapsed += (ms - prevMs) * kHz;
      Command command{0, true, static_cast<uint8_t>((ms / 170) % 2 ? 220 : 20), static_cast<uint16_t>(200 + (ms / 230) % 90)};
      while (elapsed >= 1000) { CHECK(p.Push(command)); elapsed -= 1000; }
      ++frame;
    }
    if (ms % 33 == 0 && p.sequence > p.acknowledged && int(random() % 100) >= loss) {
      Batch batch; batch.round = 1; batch.first = p.acknowledged + 1;
      batch.count = static_cast<uint8_t>(std::min<size_t>(kBatchLimit, p.sequence - p.acknowledged));
      for (unsigned i = 0; i < batch.count; ++i) batch.commands[i] = p.pending[(batch.first + i) % kPendingLimit];
      uplink.push_back({ms + delay(), batch});
    }
    for (auto it = uplink.begin(); it != uplink.end();) {
      if (it->due > ms) { ++it; continue; }
      for (unsigned i = 0; i < it->batch.count; ++i) server.Put(it->batch.first + i, it->batch.commands[i]);
      it = uplink.erase(it);
    }
    if ((ms * kHz / 1000) != ((ms - 1) * kHz / 1000)) {
      Command command{}; if (server.Pop(command)) authoritative = Move(authoritative, command, 0);
    }
    if (ms % 33 == 0 && int(random() % 100) >= loss)
      downlink.push_back({ms + delay(), server.acknowledged, authoritative});
    for (auto it = downlink.begin(); it != downlink.end();) {
      if (it->due > ms) { ++it; continue; }
      if (it->seq >= ackLatest) {
        const auto before = p.position;
        CHECK(p.Reconcile(it->seq, it->p)); CHECK(p.position == before); ackLatest = it->seq;
      }
      it = downlink.erase(it);
    }
    peak = std::max(peak, p.sequence - p.acknowledged);
  }
  std::printf("MOTION_NETWORK_OK rtt=%d fps=%d jitter=%d loss=%d peak_pending=%u correction=0\n", rtt, fps, jitter, loss, peak);
  return true;
}
int main() {
  if (!Movement() || !Collision() || !Wire() || !Presentation()) return 1;
  for (int rtt : {0, 50, 100, 150, 200}) for (int fps : {30, 60, 120})
    if (!Network(rtt, fps, 20, 2, unsigned(rtt + fps))) return 2;
  std::printf("PC_MOTION_ACCEPTANCE_OK checks=%llu\n", static_cast<unsigned long long>(checks));
}
