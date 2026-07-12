# NET MODULE API SPECIFICATION

> Module: `net/` (`net.h` / `net.c`) — MODULES_ROADMAP.md Module 11
> (Networking, peer-hosted). Owner agent: **Net Agent** (see `net/CLAUDE.md`).

## 1. Scope & Status — wire core + ENet transport shipped

Peer-hosted model (thiết kế §XI): clients serialize **PlayerIntent** to the
host; the host runs the one true simulation (all combat resolves host-side)
and broadcasts **agent-pool snapshots** back. Module 4 split input/intent
precisely so this module can treat `PlayerIntent` as its wire unit.

**This pass ships the deterministic core**: versioned wire formats +
pack/unpack for both directions (autotested round-trip). **The ENet
socket/side-thread layer is deliberately NOT included** — it adds a
third-party dependency and per the roadmap only starts once the offline
game mode has soaked; vendor ENet only with explicit approval.

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

## 3b. Transport (`net/net_transport.h` — landed with user approval, 07/2026)

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

## 4. Explicitly NOT in this version

- Side thread (main-thread polling for now), reconnection/session management.
- Snapshot APPLY to the local pool — needs client-prediction rules decided
  together with the transport (interpolation vs. snap, ownership of the
  local player agent).
- Delta compression / interest management (full snapshots only).
- No other module includes `net/` — the pump will live in main/game when
  the transport lands.

## Autotest

`net_wire_format` in `main.c`: bit-exact intent round trip, version-mismatch
rejection, snapshot pack→parse with a probe agent's fields verified.

## Transport v1 known limits (next pass)

- No client prediction/interpolation — 20Hz snapshots snap (fine on LAN).
- Skill VFX are host-local: the client sees positions/HP but not the host's
  particle effects (event mirroring is the planned fix).
- ~~Basic attack not in PlayerIntent~~ FIXED (protocol v2): intent carries
  `basicAttack` (0 = none, else BasicAttackType+1); the host executes the
  remote hero's melee, the client plays its swing anim locally.
- ~~Match outcomes not replicated~~ FIXED: typed reliable control packets
  (`NET_CTRL_HELLO` / `NET_CTRL_STATE`); the host resends state on change,
  the client maps it perspective-swapped (invader: host VICTORY = your
  DEFEAT). The remote hero's own death still doesn't end the match — the
  invader has no respawn yet.
