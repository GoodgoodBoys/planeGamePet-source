#include <cstdio>
#include <random>
#include <vector>
#include "../common/pc_battle_protocol.h"
#include "../common/pc_battle_view.h"
using namespace pcbattle;
static uint64_t checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return false; } } while (0)

bool Wire() {
  State s; s.world.tick = 600; s.presence.roundId = 987654;
  s.presence.game.phase = plink::GamePhase::Playing;
  s.world.players[0].x += 237;
  for (unsigned i = 0; i < 12; ++i)
    s.world.bullets[i] = {true, static_cast<uint16_t>(i + 1), static_cast<uint8_t>(i % 2 + 1),
        {int(100 * kUnit + i * 3), int(200 * kUnit + i * 17)}};
  uint8_t bytes[plink::kMaxPacketSize]{};
  plink::PacketWriter w(bytes, sizeof(bytes), kStateType, 1, 1, 0, 0);
  CHECK(WriteState(w, s)); const auto length = w.Finish(); CHECK(length == 191);
  plink::PacketHeader header; const uint8_t *payload = nullptr;
  CHECK(plink::ParsePacket(bytes, length, header, payload));
  State restored; plink::PayloadReader reader(payload, header.payloadLength);
  CHECK(ReadState(reader, restored)); CHECK(restored.world.players[0] == s.world.players[0]);
  for (unsigned i = 0; i < 12; ++i) CHECK(restored.world.bullets[i].p == s.world.bullets[i].p);
  for (unsigned cut = 0; cut < header.payloadLength; ++cut) {
    plink::PayloadReader r(payload, cut); CHECK(!ReadState(r, restored));
  }
  Events events; events.round = 1; events.count = 6;
  for (unsigned i = 0; i < 6; ++i) events.hits[i] = {300 + i, static_cast<uint16_t>(i + 1), 45000,
      static_cast<uint8_t>((i + 1) % 2), static_cast<uint8_t>(2 - i / 2), {100 * kUnit, 160 * kUnit},
      pcmotion::Spawn((i + 1) % 2)};
  plink::PacketWriter ew(bytes, sizeof(bytes), kImpactType, 1, 2, 0, 0);
  CHECK(WriteEvents(ew, events)); const auto eventLength = ew.Finish(); CHECK(eventLength <= 192);
  CHECK(plink::ParsePacket(bytes, eventLength, header, payload));
  Events received; plink::PayloadReader er(payload, header.payloadLength);
  CHECK(ReadEvents(er, received)); CHECK(received.hits[5].contact == events.hits[5].contact);
  CHECK(Extends(received, events)); received.hits[0].contact.x += 1; CHECK(!Extends(received, events));
  for (unsigned cut = 0; cut < header.payloadLength; ++cut) {
    plink::PayloadReader r(payload, cut); CHECK(!ReadEvents(r, received));
  }
  std::mt19937 rng(486734);
  for (unsigned i = 0; i < 10000; ++i) {
    for (auto &byte : bytes) byte = static_cast<uint8_t>(rng());
    plink::PayloadReader stateReader(bytes, rng() % 193), eventReader(bytes, rng() % 193);
    CHECK(!ReadState(stateReader, restored)); CHECK(!ReadEvents(eventReader, received));
  }
  std::puts("BATTLE_WIRE_EXACT_FRACTIONS_MAX_PACKET_TRUNCATION_EVENTS_OK"); return true;
}
bool Physics() {
  World world;
  const auto before = world.players[0];
  world.bullets[0] = {true, 2, 2, {before.x, before.y - 23 * kUnit}};
  const Command idle[2]{}; world.Step(idle);
  CHECK(world.health[0] == 2); CHECK(world.players[0] == before);
  CHECK(world.impactCount == 1); CHECK(world.impacts[0].contact.y == before.y - 17 * kUnit - kUnit / 2);
  CHECK(world.impacts[0].fraction == 10922); CHECK(world.impacts[0].healthAfter == 2);
  world.Step(idle); CHECK(world.health[0] == 2 && world.impactCount == 1);
  world.health[0] = world.health[1] = 1;
  world.bullets[0] = {true, 4, 2, world.players[0]};
  world.bullets[1] = {true, 3, 1, world.players[1]};
  world.Step(idle); CHECK(!world.health[0] && !world.health[1]);
  for (unsigned slot = 0; slot < 2; ++slot) {
    for (unsigned keys = 0; keys < 16; ++keys) {
      Point p = pcmotion::Spawn(slot);
      for (int tick = 0; tick < 300; ++tick) {
        const auto q = pcmotion::Move(p, {static_cast<uint8_t>(keys)}, slot);
        const int64_t dx = q.x - p.x, dy = q.y - p.y;
        CHECK(dx * dx + dy * dy <= int64_t(pcmotion::kStep) * pcmotion::kStep); p = q;
      }
    }
  }
  std::puts("BATTLE_EXACT_CONTACT_SIMULTANEOUS_NO_RESPAWN_SPEED_OK"); return true;
}
bool Recover() {
  Authority authority;
  authority.world.players[0].x = 30 * kUnit; authority.world.players[1].x = 210 * kUnit;
  unsigned delivered = 0;
  for (unsigned tick = 1; tick <= 400; ++tick) {
    if (tick < 100 || tick >= 112) {
      while (delivered < tick) {
        ++delivered;
        for (unsigned i = 0; i < 2; ++i) CHECK(authority.Put(i, delivered, {}));
      }
    }
    authority.Advance(tick);
    CHECK(!authority.failed);
    if (tick == 105) CHECK(authority.world.tick == 99); // bullets ALSO stopped in authority
    if (tick >= 114) CHECK(authority.world.tick == tick); // no permanent input-age growth
  }
  CHECK(!authority.Put(0, 1000, {}));
  CHECK(authority.Put(0, 401, {plink::InputRight}));
  CHECK(!authority.Put(0, 401, {plink::InputLeft}));
  for (unsigned tick = 401; tick <= 462; ++tick) authority.Advance(tick);
  CHECK(authority.failed);
  Authority fullRound;
  fullRound.world.players[0].x = 30 * kUnit; fullRound.world.players[1].x = 210 * kUnit;
  for (unsigned tick = 1; tick <= kMaxTicks; ++tick) {
    CHECK(fullRound.Put(0, tick, {}) && fullRound.Put(1, tick, {}));
    fullRound.Advance(tick); CHECK(!fullRound.failed && fullRound.world.tick == tick);
  }
  CHECK(!fullRound.world.Dead() && fullRound.world.tick == 10800);
  fullRound.Advance(kMaxTicks + 1); CHECK(fullRound.world.tick == kMaxTicks);
  CHECK(!fullRound.Put(0, kMaxTicks + 1, {}));
  std::puts("BATTLE_WHOLE_WORLD_STALL_RECOVERY_NO_BACKLOG_FUTURE_IMMUTABLE_TIMEOUT_OK"); return true;
}
bool VisualContact() {
  World base; base.players[0] = {120 * kUnit, 220 * kUnit};
  base.bullets[0] = {true, 2, 2, {120 * kUnit, 197 * kUnit}};
  Predictor p; p.Reset(base, 0);
  CHECK(p.Push({plink::InputDown})); CHECK(p.current.impactCount == 1);
  const auto hit = p.current.impacts[0];
  CHECK(hit.fraction == 21845);
  const auto before = Sample(p, .25), after = Sample(p, .5);
  CHECK(before.health[0] == 3 && before.bullets[0].active);
  CHECK(after.health[0] == 2 && !after.bullets[0].active);
  CHECK(before.players[0].y == 220 * kUnit + 96);
  const int64_t distance = (before.players[0].y - 17 * kUnit - kUnit / 2) -
      (before.bullets[0].p.y + kProjectileTip);
  CHECK(distance >= 0 && distance < kUnit / 2);
  // Authority snapshot landing on our exact render tick must retain contact
  // sub-time when the separately retransmitted event is joined to the state.
  auto exact = p.current; exact.impactCount = 0;
  Events contacts; contacts.round = 1; contacts.count = 1; contacts.hits[0] = hit;
  AttachEvents(exact, contacts); CHECK(p.Confirm(exact));
  CHECK(Sample(p, .25).health[0] == 3 && Sample(p, .5).health[0] == 2);
  const auto point = p.current.players[0];
  CHECK(p.Confirm(exact) && p.current.players[0] == point && p.current.health[0] == 2);
  World older; AttachEvents(older, contacts); CHECK(older.impactCount == 0);
  // The old rectangular air above the wing and the decorative exhaust are
  // not damage surfaces. The narrow nose remains a genuine hit surface.
  Fraction fraction;
  CHECK(!Contact({8 * kUnit, -13 * kUnit}, {8 * kUnit, -10 * kUnit}, {}, {}, fraction));
  CHECK(!Contact({0, 9 * kUnit}, {0, 12 * kUnit}, {}, {}, fraction));
  CHECK(Contact({0, -19 * kUnit}, {0, -16 * kUnit}, {}, {}, fraction));
  std::mt19937 random(98432);
  for (unsigned i = 0; i < 5000; ++i) {
    Point b0{int(random() % 40 - 20) * kUnit, int(random() % 60 - 30) * kUnit};
    Point b1{b0.x, b0.y + 3 * kUnit};
    Point p1{int(random() % 3 - 1) * kUnit, int(random() % 3 - 1) * kUnit};
    Fraction a, b;
    const bool ha = Contact(b0, b1, {}, p1, a, 0);
    const bool hb = Contact({-b0.x, -b0.y}, {-b1.x, -b1.y}, {}, {-p1.x, -p1.y}, b, 1);
    CHECK(ha == hb); if (ha) CHECK(!Less(a, b) && !Less(b, a));
  }
  std::puts("BATTLE_VISUAL_CONTACT_HP_SAME_SUBTICK_HULL_AND_MIRROR_OK"); return true;
}
Command Pattern(unsigned tick, unsigned slot) {
  // Opposite sides of the battlefield, so a full-duration networking test
  // does not end early. Includes target reversals, stops, diagonals and clamps.
  return {0, true, static_cast<uint8_t>(20 + ((tick / 43) % 2) * 50),
      static_cast<uint16_t>(185 + ((tick / (slot ? 53 : 61)) % 2) * 105)};
}
bool TerminalPresentation() {
  for (unsigned side = 0; side < 2; ++side) {
    World base; base.tick = 60; base.health[0] = base.health[1] = 1;
    base.bullets[0] = {true, 2, 2, {base.players[0].x, base.players[0].y - 26 * kUnit}};
    base.bullets[1] = {true, 1, 1, {base.players[1].x, base.players[1].y + 40 * kUnit}};
    Predictor client; client.Reset(base, side);
    for (unsigned i = 0; i < 20; ++i) CHECK(client.Push({}));
    World server = base; const Command idle[2]{};
    while (!server.Dead()) server.Step(idle);
    CHECK(server.health[0] == 0 && server.health[1] == 1);
    CHECK(client.Confirm(server));
    CHECK(client.current.health[0] == 0 && client.current.health[1] == 0);
    plink::WorldState exported; server.Export(exported);
    plink::SnapshotPayload metadata;
    metadata.phase = plink::GamePhase::Finished; metadata.winnerSlot = 2;
    for (unsigned i = 0; i < 2; ++i) metadata.players[i] = exported.players[i];
    metadata.bulletCount = 1; metadata.bullets[0] = {1, 1, 119, 88};
    for (double alpha : {0.0, .5, 1.0}) {
      const auto shown = Presentation(client, alpha, metadata);
      CHECK(shown.players[0].health == 0 && shown.players[1].health == 1);
      CHECK(shown.winnerSlot == 2 && shown.bulletCount == 1 && shown.bullets[0].y == 88);
      CHECK(shown.players[side].x == metadata.players[side].x);
    }
    metadata.phase = plink::GamePhase::Playing;
    CHECK(Presentation(client, 1.0, metadata).players[1].health == 0); // still whole-world prediction in play
  }
  std::puts("BATTLE_TERMINAL_AUTHORITY_NOT_POST_DEATH_PREDICTION_OK"); return true;
}
struct Flight { unsigned due, owner; Batch batch; };
struct Arrival { unsigned due, slot; State state; };
bool Network(unsigned up, unsigned down, unsigned jitter, unsigned loss, bool stall, unsigned fps = 60) {
  Authority authority;
  Predictor clients[2];
  clients[0].Reset(authority.world, 0); clients[1].Reset(authority.world, 1);
  std::vector<Flight> packets, relays;
  std::vector<Arrival> snapshots;
  std::mt19937 rng(13579 + up + down + loss);
  unsigned peak = 0;
  // 45 seconds, genuine production serializers on all authority snapshots.
  for (unsigned ms = 0; ms <= 45000; ++ms) {
    if (ms && ms * fps / 1000 != (ms - 1) * fps / 1000 && ms < 44000) {
      for (unsigned i = 0; i < 2; ++i) {
        if (clients[i].current.Dead()) continue;
        while (clients[i].tick < ms * kHz / 1000) CHECK(clients[i].Push(Pattern(clients[i].tick + 1, i)));
      }
    }
    if (ms % 33 == 0) {
      for (unsigned i = 0; i < 2; ++i) {
        Batch b; b.round = 1; b.first = clients[i].confirmed.tick + 1;
        b.count = static_cast<uint8_t>(std::min<uint32_t>(pcmotion::kBatchLimit, clients[i].tick - clients[i].confirmed.tick));
        for (unsigned n = 0; n < b.count; ++n) CHECK(clients[i].inputs[i].Get(b.first + n, b.commands[n]));
        if (b.count && rng() % 100 >= loss) {
          unsigned due = ms + up + rng() % (jitter + 1);
          if (stall && ms >= 6000 && ms < 6200) due = std::max(due, 6200U);
          packets.push_back({due, i, b});
        }
      }
    }
    for (auto it = packets.begin(); it != packets.end();) {
      if (it->due > ms) { ++it; continue; }
      auto f = *it; it = packets.erase(it);
      for (unsigned n = 0; n < f.batch.count; ++n) CHECK(authority.Put(f.owner, f.batch.first + n, f.batch.commands[n]));
      if (rng() % 100 >= loss) { f.due = ms + down + rng() % (jitter + 1); relays.push_back(f); }
    }
    for (auto it = relays.begin(); it != relays.end();) {
      if (it->due > ms) { ++it; continue; }
      const auto f = *it; it = relays.erase(it);
      auto &client = clients[1 - f.owner];
      for (unsigned n = 0; n < f.batch.count; ++n) CHECK(client.inputs[f.owner].Put(f.batch.first + n,
          f.batch.commands[n], client.confirmed.tick, std::min<uint32_t>(kMaxTicks, client.tick + kWindow)));
      CHECK(client.Replay());
    }
    if (ms && ms * kHz / 1000 != (ms - 1) * kHz / 1000) {
      authority.Advance(ms * kHz / 1000);
      CHECK(!authority.failed);
    }
    if (ms % 34 == 0) {
      for (unsigned i = 0; i < 2; ++i) if (rng() % 100 >= loss) {
        State state; state.world = authority.world; state.presence.roundId = 1;
        state.presence.game.phase = plink::GamePhase::Playing;
        uint8_t bytes[192]{};
        plink::PacketWriter w(bytes, sizeof(bytes), kStateType, 1, ms + 1, 0, 0);
        CHECK(WriteState(w, state)); auto length = w.Finish(); CHECK(length > 0);
        plink::PacketHeader header; const uint8_t *payload = nullptr;
        CHECK(plink::ParsePacket(bytes, length, header, payload));
        plink::PayloadReader r(payload, header.payloadLength); State decoded;
        CHECK(ReadState(r, decoded));
        snapshots.push_back({ms + down + static_cast<unsigned>(rng() % (jitter + 1)), i, decoded});
      }
    }
    for (auto it = snapshots.begin(); it != snapshots.end();) {
      if (it->due > ms) { ++it; continue; }
      const auto s = *it; it = snapshots.erase(it); auto &client = clients[s.slot];
      if (s.state.world.tick < client.confirmed.tick) continue;
      CHECK(client.Confirm(s.state.world));
    }
    for (auto &c : clients) peak = std::max(peak, c.tick - c.confirmed.tick);
    if (authority.world.Dead()) break;
    if (ms >= 44000 && authority.world.tick == clients[0].tick && authority.world.tick == clients[1].tick) break;
  }
  CHECK(authority.world.tick > 2000); CHECK(peak < kWindow);
  std::printf("BATTLE_NETWORK_OK up=%u down=%u jitter=%u loss=%u stall=%u fps=%u peak=%u tick=%u\n",
      up, down, jitter, loss, stall, fps, peak, authority.world.tick);
  return true;
}
int main() {
  if (!Wire() || !Physics() || !Recover() || !VisualContact() || !TerminalPresentation()) return 1;
  for (unsigned rtt : {0U, 50U, 100U, 200U, 300U})
    if (!Network(rtt / 2, rtt - rtt / 2, 20, 2, true)) return 1;
  if (!Network(20, 160, 35, 5, true) || !Network(160, 20, 35, 5, true)) return 1;
  for (unsigned fps : {30U, 120U}) if (!Network(150, 150, 35, 5, true, fps)) return 1;
  std::printf("BATTLE_ALL_OK checks=%llu\n", static_cast<unsigned long long>(checks));
}
