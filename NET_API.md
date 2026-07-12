# NET MODULE API SPECIFICATION

> Module: `net/` (`net.h` / `net.c`) — MODULES_ROADMAP.md Module 11
> (Networking, peer-hosted). Owner agent: **Net Agent** (see `net/CLAUDE.md`).

## 1. Scope & Status — wire core shipped, transport gated

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

## 4. Explicitly NOT in this version

- ENet transport, side thread, connection/session management (gated, above).
- Snapshot APPLY to the local pool — needs client-prediction rules decided
  together with the transport (interpolation vs. snap, ownership of the
  local player agent).
- Delta compression / interest management (full snapshots only).
- No other module includes `net/` — the pump will live in main/game when
  the transport lands.

## Autotest

`net_wire_format` in `main.c`: bit-exact intent round trip, version-mismatch
rejection, snapshot pack→parse with a probe agent's fields verified.
