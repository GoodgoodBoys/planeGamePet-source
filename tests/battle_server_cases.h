int RunBattleServerCases(PetServer &server) {
  const auto publicVersion = pcpair::CurrentAppVersion();
  if (publicVersion.major != 1 || publicVersion.releaseEpoch != 1 ||
      pcpair::GameProtocolFor(publicVersion) != 3) return 120;
  for (const auto legacy : {pcpair::AppVersion{1,0,0}, pcpair::AppVersion{1,0,2},
                           pcpair::AppVersion{2,0,0}, pcpair::AppVersion{3,0,12}}) {
    const auto oldFlags = server.CompatibilityFlags(legacy, publicVersion);
    const auto newFlags = server.CompatibilityFlags(publicVersion, legacy);
    if (!(oldFlags & pcpair::MajorMismatch) || !(oldFlags & pcpair::LocalUpdateRequired) ||
        !(newFlags & pcpair::MajorMismatch) || !(newFlags & pcpair::PeerUpdateRequired) ||
        (newFlags & pcpair::LocalUpdateRequired)) return 121;
  }
  Room releaseRoom;
  for (unsigned i = 0; i < 2; ++i) {
    auto &c = releaseRoom.clients[i]; c.active = c.supportsDnd = c.supportsMotion = true;
    c.slot = static_cast<uint8_t>(i + 1); c.appVersion = publicVersion;
    c.lastSeen = Clock::now(); c.dndRevision = 1;
  }
  server.HandleAction(releaseRoom, releaseRoom.clients[0], plink::PlayerAction::Invite);
  server.HandleAction(releaseRoom, releaseRoom.clients[1], plink::PlayerAction::Accept);
  if (!releaseRoom.battleEnabled || !releaseRoom.motionEnabled ||
      releaseRoom.phase != plink::GamePhase::Countdown) return 122;
  std::puts("PUBLIC_1_0_0_BATTLE_PROTOCOL_PRESERVED_LEGACY_MIGRATION_GATED_OK");
  Room room;
  for (unsigned i = 0; i < 2; ++i) {
    auto &c = room.clients[i]; c.active = c.supportsDnd = c.supportsMotion = true;
    c.slot = static_cast<uint8_t>(i + 1); c.appVersion = {3, 0, 0};
    c.lastSeen = Clock::now(); c.dndRevision = 1;
  }
  room.clients[1].appVersion.major = 2;
  server.HandleAction(room, room.clients[0], plink::PlayerAction::Invite);
  if (room.phase != plink::GamePhase::Menu) return 100;
  room.clients[1].appVersion = {3, 0, 1};
  server.HandleAction(room, room.clients[0], plink::PlayerAction::Invite);
  server.HandleAction(room, room.clients[1], plink::PlayerAction::Accept);
  if (!room.battleEnabled || !room.motionEnabled || room.phase != plink::GamePhase::Countdown) return 101;
  auto now = Clock::now(); room.phaseStarted = now - std::chrono::seconds(4);
  server.TickRoom(room, now);
  if (room.phase != plink::GamePhase::Playing || room.battle.world.tick) return 102;
  for (unsigned i = 0; i < 2; ++i) if (!room.battle.Put(i, 1, {plink::InputRight})) return 103;
  const auto original = room.battle.world.players[0];
  server.TickRoom(room, now + std::chrono::milliseconds(17));
  if (room.battle.world.tick != 1 || room.battle.world.players[0].x != original.x + pcmotion::kStep) return 104;
  if (!room.battle.Put(0, 2, {plink::InputRight})) return 105;
  const auto before = room.battle.world;
  server.TickRoom(room, now + std::chrono::milliseconds(100));
  if (room.battle.world.tick != before.tick || room.battle.world.players[0] != before.players[0]) return 106;
  for (unsigned n = 2; n <= 6; ++n) for (unsigned i = 0; i < 2; ++i)
    if (!room.battle.Put(i, n, {plink::InputRight})) return 107;
  server.TickRoom(room, now + std::chrono::milliseconds(101));
  if (room.battle.world.tick != 6) return 108; // full-world catchup, not input-only speedup
  server.TickRoom(room, now + std::chrono::seconds(2));
  if (!room.syncFailed || room.phase != plink::GamePhase::Finished || room.winnerSlot) return 109;
  server.EnterMenu(room, "battle_regression_reset");
  if (room.battleEnabled || room.battle.world.tick || room.battle.failed) return 110;
  for (auto phase : {plink::GamePhase::Countdown, plink::GamePhase::Playing}) {
    room.motionEnabled = room.battleEnabled = true; room.syncFailed = false;
    room.phase = phase; room.phaseStarted = room.matchStarted = Clock::now();
    room.clients[0].active = room.clients[1].active = true;
    room.clients[0].lastSeen = Clock::now() - std::chrono::seconds(11);
    room.clients[1].lastSeen = Clock::now();
    server.TickRoom(room, Clock::now());
    if (!room.syncFailed || room.winnerSlot || room.phase != plink::GamePhase::Finished ||
        room.endReason != plink::MatchEndReason::ServerUnavailable) return 111;
  }
  // Explicit authenticated exit still forfeits; a late Goodbye cannot change
  // an already authoritative abnormal termination into a win/loss.
  Binding binding; binding.id = 54321; binding.devices[0] = 654321;
  binding.devices[1] = 654322; binding.tokenLow[0] = 77; binding.tokenHigh[0] = 88;
  server.bindings_.push_back(binding);
  pcpair::Message bye; bye.bindingId = binding.id; bye.deviceId = binding.devices[0];
  bye.tokenLow = 77; bye.tokenHigh = 88;
  auto &owned = server.GetRoom(binding.id);
  for (auto phase : {plink::GamePhase::Countdown, plink::GamePhase::Playing}) {
    owned = room; owned.bindingId = binding.id; owned.phase = phase; owned.syncFailed = false;
    for (auto &c : owned.clients) { c.active = true; c.lastSeen = Clock::now(); }
    server.HandleGoodbye(bye);
    if (owned.phase != plink::GamePhase::Finished || owned.syncFailed || owned.winnerSlot != 2 ||
        owned.endReason != plink::MatchEndReason::PlayerDisconnected) return 112;
    owned.phase = phase; owned.clients[0].active = true;
    server.EndSyncFailure(owned); server.HandleGoodbye(bye);
    if (!owned.syncFailed || owned.winnerSlot || owned.endReason != plink::MatchEndReason::ServerUnavailable) return 113;
  }
  std::puts("BATTLE_PASSIVE_EXPIRY_COUNTDOWN_AND_PLAYING_ABORT_EXPLICIT_EXIT_STILL_FORFEITS_OK");
  std::puts("BATTLE_REAL_SERVER_MAJOR_GATE_TIMED_BUDGET_STALL_RECOVERY_ABORT_RESET_OK"); return 0;
}
