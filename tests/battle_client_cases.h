int RunBattleClientCases(PetClient &client, const std::filesystem::path &directory) {
  client.ResetDndConnection(); client.slot_ = 1; client.battleSupported_ = client.motionSupported_ = true;
  client.start_ = Clock::now();
  pcbattle::State state; state.presence.roundId = 43219876;
  state.presence.game.phase = plink::GamePhase::Playing;
  client.snapshot_ = state.presence.game;
  client.ApplyBattleState(state, plink::GamePhase::Countdown);
  for (unsigned i = 0; i < 12; ++i) if (!client.battlePredictor_.Push({plink::InputRight})) return 120;
  const auto point = client.battlePredictor_.current.players[0];
  const pcbattle::Command commands[2]{{plink::InputRight}, {}};
  for (unsigned i = 0; i < 6; ++i) state.world.Step(commands);
  client.ApplyBattleState(state, plink::GamePhase::Playing);
  if (client.motionFault_ || client.battlePredictor_.confirmed.tick != 6 || client.battlePredictor_.current.players[0] != point) return 121;
  state.world.players[0].x += 256;
  client.ApplyBattleState(state, plink::GamePhase::Playing);
  if (!client.motionFault_ || client.battlePredictor_.current.players[0] != point) return 122;
  client.motionFault_ = false; client.motionClock_ = Clock::now() - std::chrono::seconds(1);
  client.AdvanceMotion(Clock::now());
  if (!client.motionFault_) return 123;
  client.historyPath_ = directory / L"battle.history";
  client.currentRoundId_ = 43219876; client.syncFailed_ = true;
  client.historyRecordedForRound_ = false; client.haveSnapshot_ = true;
  const auto total = client.historyTotal_;
  client.RecordMatchHistory(); client.RecordMatchHistory();
  if (client.historyTotal_ != total || client.lastRecordedRoundId_ != 43219876) return 124;
  client.ResetDndConnection();
  if (client.battleSupported_ || client.battlePredictor_.ready || client.battleEvents_.count) return 125;
  for (unsigned side = 0; side < 2; ++side) {
    PetClient peer;
    peer.telemetryEnabled_ = false; peer.slot_ = static_cast<uint8_t>(side + 1);
    peer.historyPath_ = directory / ("battle-end-" + std::to_string(side) + ".history");
    peer.haveSnapshot_ = peer.battleSupported_ = peer.motionSupported_ = true;
    pcbattle::State terminal;
    terminal.presence.roundId = 123400 + side;
    terminal.presence.game.phase = plink::GamePhase::Finished;
    terminal.presence.game.winnerSlot = 2;
    terminal.world.tick = 62; terminal.world.health[0] = 0; terminal.world.health[1] = 1;
    terminal.presence.game.players[0].health = 0; terminal.presence.game.players[1].health = 1;
    peer.snapshot_ = terminal.presence.game; peer.currentRoundId_ = terminal.presence.roundId;
    peer.motionRound_ = peer.currentRoundId_; peer.battlePredictor_.Reset({}, side);
    for (unsigned i = 0; i < 80; ++i) {
      if (i == 40) peer.battlePredictor_.confirmed = peer.battlePredictor_.current;
      if (!peer.battlePredictor_.Push({plink::InputRight})) return 126;
    }
    peer.motionFault_ = true; // a final packet MUST work even after a local stall
    peer.ApplyBattleState(terminal, plink::GamePhase::Playing);
    if (peer.motionFault_ || peer.battlePredictor_.tick != 62 || peer.battlePredictor_.current.health[1] != 1) return 127;
    peer.RecordMatchHistory(); peer.RecordMatchHistory();
    if (peer.historyTotal_ != 1 || peer.historyWins_ != side || peer.historyLosses_ != 1 - side) return 128;
    const auto &entry = peer.recentHistory_[0];
    if (entry.ownHealth != side || entry.peerHealth != 1 - side) return 129;
    // Passive silence during countdown/playing records neither defeat nor a
    // tombstone; a legitimate late terminal result can still be recorded once.
    for (const auto phase : {plink::GamePhase::Countdown, plink::GamePhase::Playing}) {
      ++peer.currentRoundId_; peer.historyRecordedForRound_ = false;
      peer.haveSnapshot_ = true; peer.snapshot_.phase = phase;
      peer.lastPacketAt_ = Clock::now() - std::chrono::seconds(6);
      const auto last = peer.lastRecordedRoundId_;
      const auto count = peer.historyTotal_;
      peer.CheckConnectionTimeout(Clock::now()); peer.CheckConnectionTimeout(Clock::now());
      if (peer.historyTotal_ != count || peer.lastRecordedRoundId_ != last || !peer.abandonedMatch_) return 130;
      peer.haveSnapshot_ = true; peer.snapshot_.phase = plink::GamePhase::Finished;
      peer.syncFailed_ = true; peer.RecordMatchHistory();
      if (peer.historyTotal_ != count || peer.lastRecordedRoundId_ != peer.currentRoundId_) return 131;
    }
    ++peer.currentRoundId_; peer.historyRecordedForRound_ = false; peer.syncFailed_ = false;
    peer.snapshot_.phase = plink::GamePhase::Playing; peer.haveSnapshot_ = true;
    peer.lastPacketAt_ = Clock::now() - std::chrono::seconds(6);
    peer.CheckConnectionTimeout(Clock::now());
    peer.haveSnapshot_ = true; peer.snapshot_.phase = plink::GamePhase::Finished;
    peer.snapshot_.winnerSlot = peer.slot_; peer.RecordMatchHistory(); peer.RecordMatchHistory();
    if (peer.historyTotal_ != 2 || peer.historyWins_ != side + 1) return 132;
    ++peer.currentRoundId_; peer.historyRecordedForRound_ = false;
    peer.snapshot_.phase = plink::GamePhase::Playing;
    peer.RecordAbandonedMatch(); peer.RecordAbandonedMatch();
    if (peer.historyTotal_ != 3 || peer.historyLosses_ != 2 - side) return 133;
  }
  std::puts("BATTLE_FINAL_AFTER_FAULT_AND_PASSIVE_TIMEOUT_LATE_RESULT_VOLUNTARY_EXIT_OK");
  std::puts("BATTLE_REAL_CLIENT_FULL_REPLAY_NO_LOCAL_SNAP_ABORT_HISTORY_RESET_OK"); return 0;
}
