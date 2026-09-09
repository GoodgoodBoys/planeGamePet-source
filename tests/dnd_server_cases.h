// Executes the real server state machine, without production credentials/stores.
int RunDndServerCases(PetServer &server) {
  using plink::GamePhase;
  using plink::PlayerAction;
  Room room;
  for (int i = 0; i < 2; ++i) {
    auto &client = room.clients[i];
    client.active = true;
    client.slot = static_cast<uint8_t>(i + 1);
    client.lastSeen = Clock::now();
    client.supportsDnd = true;
  }
  auto &a = room.clients[0]; auto &b = room.clients[1];
  server.HandleAction(room, a, PlayerAction::Invite, 1);
  if (room.phase != GamePhase::Menu || a.blockedReason != pcpair::InviteBlock::Synchronizing) return 40;
  server.ApplyDnd(room, a, {1, false});
  server.ApplyDnd(room, b, {1, true});
  server.HandleAction(room, a, PlayerAction::Invite, 2);
  if (room.phase != GamePhase::Menu || room.inviteId != 0 || a.blockedOperation != 2 ||
      a.blockedReason != pcpair::InviteBlock::DoNotDisturb) return 41;
  server.ApplyDnd(room, b, {1, false}); // same revision must not reverse preference
  if (!b.doNotDisturb) return 42;
  server.ApplyDnd(room, b, {2, false});
  server.ApplyDnd(room, b, {1, true});
  if (b.doNotDisturb || b.dndRevision != 2) return 43;
  server.HandleAction(room, a, PlayerAction::Invite, 3);
  if (room.phase != GamePhase::Waiting || room.inviterSlot != 1 || room.inviteId == 0) return 44;
  const auto invite = room.inviteId;
  server.ApplyDnd(room, b, {3, true});
  if (room.phase != GamePhase::Menu || !room.dndInterrupted || room.inviteId != invite ||
      room.dndInterruptedInviter != 1) return 45;
  server.HandleAction(room, b, PlayerAction::Accept, 4);
  if (room.phase != GamePhase::Menu) return 46;
  // DND user may initiate, and their own preference does not invalidate acceptance.
  server.HandleAction(room, b, PlayerAction::Invite, 5);
  if (room.phase != GamePhase::Waiting || room.inviterSlot != 2 || room.dndInterrupted) return 47;
  server.ApplyDnd(room, b, {4, false});
  server.ApplyDnd(room, b, {5, true});
  if (room.phase != GamePhase::Waiting) return 48;
  server.HandleAction(room, a, PlayerAction::Accept, 6);
  if (room.phase != GamePhase::Countdown) return 49;
  server.ApplyDnd(room, a, {2, true});
  if (room.phase != GamePhase::Countdown) return 50;
  room.phase = GamePhase::Playing;
  server.ApplyDnd(room, a, {3, false});
  if (room.phase != GamePhase::Playing) return 51;
  server.EnterMenu(room, "dnd_test");
  server.ApplyDnd(room, a, {4, true});
  server.HandleAction(room, b, PlayerAction::Invite, 7);
  if (room.phase != GamePhase::Menu || b.blockedReason != pcpair::InviteBlock::DoNotDisturb) return 52;
  server.HandleAction(room, a, PlayerAction::EmoteLaugh, 8);
  if (a.lastEmote.time_since_epoch().count() == 0) return 53;
  a.supportsDnd = false;
  const auto priorId = room.inviteId;
  server.HandleAction(room, a, PlayerAction::Invite, 9);
  if (room.phase != GamePhase::Menu || room.inviteId != priorId || !b.active) return 54;
  b.supportsDnd = false;
  server.HandleAction(room, a, PlayerAction::Invite, 10);
  if (room.phase != GamePhase::Waiting) return 55;
  std::puts("DND_SERVER_REVISIONS_BLOCK_INTERRUPT_INITIATE_GAME_EMOTE_LEGACY_OK");

  uint8_t bytes[plink::kMaxPacketSize]{};
  pcpair::PresenceSnapshot sent;
  sent.acknowledgedRevision = 999; sent.knownMask = sent.capableMask = 3; sent.enabledMask = 2;
  sent.roundId = UINT64_C(0x1234567890abcdef); sent.inviteId = UINT64_C(0xfedcba0987654321);
  sent.game.bulletCount = plink::kMaxBullets;
  plink::PacketWriter writer(bytes, sizeof(bytes), pcpair::kPresenceSnapshotType, 123, 1, 0, 0);
  if (!pcpair::WritePresenceSnapshot(writer, sent)) return 56;
  const auto size = writer.Finish();
  plink::PacketHeader header; const uint8_t *payload = nullptr;
  if (size > sizeof(bytes) || !plink::ParsePacket(bytes, size, header, payload)) return 57;
  pcpair::PresenceSnapshot read;
  plink::PayloadReader reader(payload, header.payloadLength);
  if (!pcpair::ReadPresenceSnapshot(reader, read) || read.roundId != sent.roundId ||
      read.inviteId != sent.inviteId || read.game.bulletCount != plink::kMaxBullets) return 58;
  // Invalid enum, zero revision, invalid Boolean and trailing bytes rejected.
  for (const std::array<uint8_t, 6> bad : {std::array<uint8_t, 6>{0,0,0,0,1,0},
      std::array<uint8_t, 6>{1,0,0,0,2,0}, std::array<uint8_t, 6>{1,0,0,0,1,9}}) {
    pcpair::DndPreference preference;
    const size_t length = bad[5] ? 6 : 5;
    plink::PayloadReader malformed(bad.data(), length);
    if (pcpair::ReadDndPreference(malformed, preference)) return 59;
  }
  std::puts("DND_FULL_BULLET_PACKET_AND_STRICT_PREFERENCE_PARSE_OK");
  return 0;
}
