# NET MODULE API SPECIFICATION

> Module: `net/` (`net.h` / `net.c`) — ../../ROADMAP.md Module 11
> (Networking, peer-hosted). Owner agent: **Net Agent** (see `net/CLAUDE.md`).

## 1. Scope & Design

Peer-hosted model (design doc §XI): clients serialize **PlayerIntent** to the
host; the host runs the one true simulation (all combat resolves host-side)
and broadcasts **agent-pool snapshots** back. Module 4 split input/intent
precisely so this module can treat `PlayerIntent` as its wire unit.

The wire core (versioned pack/unpack for both directions, §2-3) is
transport-agnostic. Two transports back it: ENet for LAN (§3b) and Epic
Online Services for internet play (§3c), both feeding the same backend seam
(§3c). Multi-peer lobby/room support (up to 8 players) sits on top (§3d).

## 2. Wire formats (`NET_PROTOCOL_VERSION 1`)

Every packet: `'W'`, kind byte (`0x01` intent / `0x02` snapshot), version
byte. Unpackers reject wrong magic/kind/version (return false / -1) — bump
the version on ANY layout change. Little-endian float memcpy (same-arch
peer assumption, fine for the mobile target).

- **Intent** (25 bytes): flags (jump/dash/meditate), castSkillSlot (int8),
  moveDir.xy, aimPoint.xyz.
- **Snapshot** (4 + 33×count bytes): per active agent — id, team, archetype,
  element, flags (taiji/meditate/stealth), position, health/maxHealth,
  mana/maxMana.

## 3. API

```c
int  Net_PackIntent(const PlayerIntent *in, unsigned char *buf, int maxBytes);
bool Net_UnpackIntent(PlayerIntent *out, const unsigned char *buf, int len);
int  Net_PackAgentSnapshot(unsigned char *buf, int maxBytes); // active agents
int  Net_UnpackAgentSnapshot(NetAgentState *out, int maxAgents,
                             const unsigned char *buf, int len); // pure parse
```

## 3b. Transport (`net/net_transport.h`)

ENet v1.3.18 vendored via CMake FetchContent, main-thread polling
(`enet_host_service` timeout 0; the side thread is a later jitter pass).
Single peer (1v1). CLI: `./wuxing --host [port]` / `./wuxing --join <ip>
[port]` (default port 7777) — both drop straight into SCREEN_GAME.

- **Host** owns the one true simulation. On connect it spawns the remote
  hero (TEAM_ENEMY ARCH_HERO, default loadout, VERDANT_PATH spawn), tells
  the client its agent id (reliable channel 1 hello packet), applies
  incoming intents (edge actions once per packet, held movement integrated
  per frame), broadcasts full agent snapshots @20Hz (channel 0, unreliable).
- **Client** sends its `PlayerIntent` @30Hz instead of `Control_Apply`
  (game/ routes via `Net_ClientSubmitIntent`), and mirrors the host pool
  1:1 by agent id through `Entity_NetSyncBegin/Agent/End` — while connected,
  `Net_ClientDrivesWorld()` makes main.c skip ALL local gameplay ticks
  (Entity/AI/Boss/Formation/Combat); snapshots own the pool.
- Client-side rendering of mirrored agents: game/ draws foreign ARCH_HERO
  agents as mannequins and the snapshot boss as core+ring (the boss/ module
  state lives host-side only).

## 3c. Online backend (`net/net_eos.c` — EOS)

Internet play over Epic Online Services' free NAT-punch/relay. CLI:
`./wuxing --host-online` (prints a 5-char join code) / `./wuxing
--join-online <code>`. Needs `-DWUXING_EOS=ON` + SDK + `eos_keys.cfg`
(`docs/EOS_SETUP.md`); the default build compiles `net_eos_stub.c` (reports
unavailable).

- **Seam**: `net/net_transport_internal.h`. The transport core (host sim,
  snapshots, intents, ctrl packets) is backend-agnostic — a backend
  installs `send/tick/stop` hooks via `NetTransport_SetBackend` and feeds
  events back through `NetTransport_Backend{Connected,Disconnected,Packet}`.
  ENet stays built-in for LAN; EOS replaces it per session, same wire
  formats and protocol version on both.
- **Auth**: Device ID (EOS Connect) — anonymous, no Epic account, no popup.
- **Flow**: host creates a public lobby (max 2, bucket `wuxing:duel:v1`)
  carrying the join code as a searchable attribute; the client finds it by
  code, reads the owner's ProductUserId, and opens P2P (socket `WUXING`).
  Client marks itself connected optimistically — its queued intents
  (`bAllowDelayedDelivery`) trigger the NAT punch; the host flips connected
  on the incoming connection request.
- **Fragmentation**: EOS MTU is 1170 bytes < a full 256-agent snapshot, so
  logical packets carry a 3-byte `[seq|idx|count]` header (channel 1 =
  ReliableOrdered, channel 0 = UnreliableUnordered; a lost fragment drops
  that snapshot only).
- **Dev envs**: `WUXING_EOS_VERBOSE=1` (SDK verbose logs),
  `WUXING_EOS_FRESH_DEVICE=1` (discard the machine's device id → new
  anonymous user; required to test host+join on one machine because lobby
  search hides lobbies the searcher is already in).

## 3d. Multi-peer + lobby room (protocol v3)

Up to `NET_MAX_PLAYERS` = 8 (host + 7 remote players → 4v4). Every backend
callback carries an opaque `peerRef` (EOS: the ProductUserId; ENet: the
`ENetPeer*`); the core maps it to a `HostPeer` slot with its own hero,
held intent, team, and liveness clock.

- **Roster** (`net.h`): `NetRosterEntry {slot, team, agentId, flags}` —
  flags = OCCUPIED / BOT / HOST, agentId = NET_ROSTER_NONE pre-spawn.
  `Net_PackRoster/UnpackRoster` (kind 0x03). The host builds it live
  (slot 0 = host, peers in join order, bots trailing) and rebroadcasts on
  every change; clients read their copy via `Net_GetRoster`.
- **Room management** (host-only): `Net_HostToggleTeam(rosterIndex)` (flips
  a human's side + retags the spawned hero via `Entity_SetAgentTeam`; moves
  a bot across), `Net_HostAddBot/RemoveBot(team)` (metadata until the
  hero-bot brain phase spawns brains), `Net_HostStartMatch()` → reliable
  `NET_CTRL_START`;
  every instance (host included) consumes it once via
  `Net_ConsumeMatchStart()` — main.c's lobby screen switches on it.
  `Net_HostRespawnPeerHeroes()` re-spawns dead peer heroes (fresh HELLO)
  for the team-battle rematch.
- **Join knock / heartbeat**: clients send `NET_CTRL_JOIN` at 1Hz. The
  first one initiates the EOS NAT punch (nothing else flows while the room
  idles in the lobby — without it the host NEVER sees the peer); afterwards
  it feeds the host's liveness sweep: a peer silent for `NET_PEER_TIMEOUT`
  (8s) is dropped (EOS raises no disconnect event for a killed process;
  ENet's own 5s timeout covers the LAN path).
- **Team auto-balance at join**: host counts as team 0; a joiner lands on
  the smaller side, tie → team 1 (first joiner opposes the host). The
  lobby reassigns freely afterwards.

## 3d-bis. Cast mirroring, interpolation, loadout/zone sync

- **Cast mirroring**: `Net_HostNotifyCast(agentId, skillIndex, aim)` →
  reliable `NET_CTRL_CAST` [5][ver][agent][skill][aim 12B] broadcast. Call
  sites: game/ (host player, at Control_ConsumeCastFired), main.c (bot
  casts via `AI_PollHeroCasts`), transport itself (remote-intent casts).
  Clients re-play with `CastSkill` under `SkillManager_SetFreeCast(true)`
  (their mirrored mana is post-debit — the real gate would misfire): pure
  VFX, no Combat_Update ticks client-side, damage stays snapshot-driven.
  Caveat: non-registry skills that mutate state inside their own Update
  drift briefly on clients until the next snapshot (50ms) corrects it.
- **Snapshot interpolation**: the client no longer snaps — each frame it
  eases every agent from its currently DISPLAYED position toward the
  newest snapshot, completing in one snapshot interval (20Hz). No
  prediction/extrapolation; non-positional fields (HP/mana/flags) apply
  immediately.
- **Loadout sync**: ui/'s loadout panel calls
  `Net_ClientSendLoadout(slot, skillIndex, element)` (reliable
  `NET_CTRL_LOADOUT`, 5B) after a client equip; the host re-equips that
  hero (`Entity_SetEquippedSkill` → Vô Hệ recomputes for everyone).
- **Zone rules for remote heroes** (host): cast cooldown ×
  `GameRules_CooldownMult`, stealth via `GameRules_GrantsStealth` per
  frame — same table the local player uses (net/ now reads
  `game/game_rules.h` + `core/map_manager.h` for this).

## 3e. Lobby screen (`ui/ui_lobby.c`)

`UI_LobbyUpdateDraw(joinCode, isHost)` draws the room between menu and
match (two team columns from `Net_GetRoster`; host clicks: entry → flip
side, empty slot → add bot, bot's X → remove; BAT DAU gated on ≥1 member
per side) and returns `UI_LOBBY_START/LEAVE` for main.c to execute.
main.c's `SCREEN_LOBBY` pumps `Net_Tick`, honors `WUXING_LOBBY_AUTOSTART=
<sec>` (dev: headless host can't click), and leaves on host-vanish.

## 4. Explicitly NOT in this version

- Side thread (main-thread polling for now), reconnection/session management.
- Snapshot APPLY to the local pool — needs client-prediction rules decided
  together with the transport (interpolation vs. snap, ownership of the
  local player agent).
- Delta compression / interest management (full snapshots only).
- Only `main.c` (pump + CLI) and `game/` (intent submission, connection HUD,
  match-state sync) include `net/` headers — no other module does.

## Autotest

`net_wire_format` in `main.c`: bit-exact intent round trip, version-mismatch
rejection, snapshot pack→parse with a probe agent's fields verified.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Doc migration: moved from root `net/docs/API.md` to `net/docs/API.md` per `DOC_ARCHITECTURE.md`; rewrote §1 to current state (transport is landed, not a future plan); stripped "landed"/date status annotations from §3b-3e headers; moved verification-log sentences and "Transport v1 known limits" section to `PROGRESS.md`; moved macOS EOS lesson to `LANDMINES.md` | root `net/docs/API.md` (read directly) | Ground-truth |
