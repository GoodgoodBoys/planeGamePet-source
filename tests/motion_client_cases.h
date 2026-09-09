int RunMotionClientCases(PetClient &client, const std::filesystem::path &directory) {
  client.ResetDndConnection();
  client.slot_ = 1; client.motionSupported_ = true;
  client.start_ = Clock::now();
  pcmotion::Snapshot state;
  state.presence.roundId = 987654;
  state.presence.game.players[0] = {120, 264, 3, 1};
  state.presence.game.players[1] = {119, 55, 3, 1};
  state.presence.game.phase = plink::GamePhase::Playing;
  client.snapshot_ = state.presence.game;
  client.ApplyMotionSnapshot(state, plink::GamePhase::Countdown);
  for (int n = 0; n < 12; ++n) client.motionPredictor_.Push({plink::InputRight});
  const auto predicted = client.motionPredictor_.position;
  state.presence.game.lastProcessedInput = 6;
  state.presence.game.players[0].x += 9;
  client.ApplyMotionSnapshot(state, plink::GamePhase::Playing);
  if (client.motionFault_ || client.motionPredictor_.position != predicted ||
      client.motionPredictor_.acknowledged != 6) return 90;
  // An impossible authority correction must freeze/abort, never teleport.
  state.presence.game.players[0].x += 30;
  client.ApplyMotionSnapshot(state, plink::GamePhase::Playing);
  if (!client.motionFault_ || client.motionPredictor_.position != predicted) return 91;
  client.motionFault_ = false;
  client.motionClock_ = Clock::now() - std::chrono::seconds(1);
  client.AdvanceMotion(Clock::now());
  if (!client.motionFault_ || client.motionPredictor_.position != predicted) return 92;
  // Interruptions are neither wins nor draws, and cannot repeat old records.
  client.historyPath_ = directory / L"motion.history";
  client.currentRoundId_ = 987654; client.syncFailed_ = true;
  const auto total = client.historyTotal_;
  client.RecordMatchHistory(); client.RecordMatchHistory();
  if (client.historyTotal_ != total || client.lastRecordedRoundId_ != 987654) return 93;
  client.ResetDndConnection();
  if (client.motionSupported_ || client.motionPredictor_.ready || client.motionRound_) return 94;
  std::puts("MOTION_REAL_CLIENT_ACK_NO_SNAP_STALL_ABORT_HISTORY_RESET_OK");
  return 0;
}
