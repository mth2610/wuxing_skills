# Net Module Agent

## Role
Owns `net/` — networking (MODULES_ROADMAP.md Module 11), peer-hosted model:
clients send serialized `PlayerIntent` to the host; the host runs the one
true simulation (combat resolves host-side only) and broadcasts agent-pool
snapshots. **Current state: wire-format core only** (versioned pack/unpack
for both directions). The ENet socket + side-thread layer is the remaining
half — it introduces a third-party dependency and is gated on the PvP
milestone and explicit approval; do not vendor ENet without instruction.

## Scope
- **Read/write:** `net/net.h`, `net/net.c`
- **Read (interface only):** `control/control.h` (PlayerIntent — the wire
  unit, keep it POD), `entities/entities.h` (snapshot fields)

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Hard rules
- Strict C99, static buffers only, no malloc.
- Bump `NET_PROTOCOL_VERSION` on ANY wire-layout change — unpackers reject
  mismatched versions instead of guessing.
- No other module may include net/ headers — networking stays invisible to
  gameplay code (main/game will own the pump when the transport lands).
- Snapshot APPLY (writing the pool from a snapshot) is deliberately not
  implemented — it needs client-prediction rules decided with the transport.

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY).
