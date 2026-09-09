int RunMotionServerCases(PetServer &server) {
  Room room;
  for (unsigned i = 0; i < 2; ++i) {
    auto &c = room.clients[i];
    c.active = c.supportsDnd = c.supportsMotion = true;
    c.slot = static_cast<uint8_t>(i + 1); c.appVersion = {2, 0, 0};
    c.lastSeen = Clock::now(); c.dndRevision = 1;
  }
  auto &a = room.clients[0], &b = room.clients[1];
  b.appVersion = {1, 0, 16};
  server.HandleAction(room, a, plink::PlayerAction::Invite);
  if (room.phase != plink::GamePhase::Menu) return 80;
  b.appVersion = {2, 0, 1};
  server.HandleAction(room, a, plink::PlayerAction::Invite);
  server.HandleAction(room, b, plink::PlayerAction::Accept);
  if (room.phase != plink::GamePhase::Countdown || !room.motionEnabled || !room.roundId) return 81;
  room.phaseStarted = Clock::now() - std::chrono::seconds(4);
  server.TickRoom(room, Clock::now());
  if (room.phase != plink::GamePhase::Playing) return 82;
  for (auto &c : room.clients)
    for (uint32_t seq = 1; seq <= 60; ++seq) c.motionInputs.Put(seq, {plink::InputRight});
  const auto before = room.motion.players[0];
  for (int tick = 0; tick < 10; ++tick) server.TickRoom(room, Clock::now());
  if (a.motionInputs.acknowledged != 10 ||
      room.motion.players[0].x - before.x != 15 * pcmotion::kUnit) return 83;
  // A full send window cannot make the server consume more than one per tick.
  a.lastMotionProcessed = b.lastMotionProcessed = Clock::now() - std::chrono::seconds(3);
  a.motionInputs = pcmotion::InputQueue{};
  server.TickRoom(room, Clock::now());
  if (!room.syncFailed || room.phase != plink::GamePhase::Finished || room.winnerSlot) return 84;
  const auto stopped = room.motion.tick;
  server.TickRoom(room, Clock::now());
  if (room.motion.tick != stopped) return 85;
  server.EnterMenu(room, "motion_test_restart");
  server.HandleAction(room, a, plink::PlayerAction::Invite);
  server.HandleAction(room, b, plink::PlayerAction::Accept);
  if (a.motionInputs.acknowledged || room.motion.tick || room.syncFailed) return 86;
  std::puts("MOTION_REAL_SERVER_MAJOR_MINOR_RATE_TIMEOUT_NEW_ROUND_OK");
  return 0;
}
