# Net Module Agent

## Docs layout
- `docs/API.md` — pure interface (signatures, contracts, invariants)
- `docs/LANDMINES.md` — distilled Symptom→Cause→Rule lessons
- `docs/PROGRESS.md` — backlog / in-progress / log
- `docs/EOS_SETUP.md` — topical setup guide (human-only Epic Dev Portal steps)
Read root `ENGINE_LANDMINES.md` before touching GL/shaders.

## Role
Owns `net/` — networking (Module 11), peer-hosted model:
clients send serialized `PlayerIntent` to the host; the host runs the one
true simulation (combat resolves host-side only) and broadcasts agent-pool
snapshots. **Current state: fully landed** — wire core (`net.c`), ENet
transport for LAN (`net_transport.c`, vendored ENet 1.3.18 via CMake with
user approval 07/2026), and the EOS online backend for internet play
(`net_eos.c`, gated behind `-DWUXING_EOS=ON`; `net_eos_stub.c` otherwise).
See `docs/API.md` §3b/3c.

## Scope
- **Read/write:** `net/net.h`, `net/net.c`, `net/net_transport.h`,
  `net/net_transport.c`, `net/net_transport_internal.h`, `net/net_eos.c`,
  `net/net_eos_stub.c`
- **Read (interface only):** `control/control.h` (PlayerIntent — the wire
  unit, keep it POD), `entities/entities.h` (snapshot fields + Net sync),
  `core/skill_manager.h` (host applies remote casts)
- **EOS SDK headers** (`third_party/eos-sdk/SDK/Include`) readable from
  `net_eos.c` only. The SDK + `eos_keys.cfg` are gitignored — NEVER commit
  either, never print key values into logs or responses.

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Hard rules
- Strict C99, static buffers only, no malloc.
- Bump `NET_PROTOCOL_VERSION` on ANY wire-layout change — unpackers reject
  mismatched versions instead of guessing.
- No other module may include net/ headers except main.c (pump + CLI) and
  game/ (intent submission, connection HUD, match-state sync).
- `net_transport_internal.h` is the backend seam: the transport core stays
  backend-agnostic; ENet is built-in, EOS installs send/tick/stop hooks via
  `NetTransport_SetBackend`. New backends must NOT fork the host/client
  logic — feed `NetTransport_Backend{Connected,Disconnected,Packet}`.
- EOS packets ride a 3-byte `[seq|idx|count]` fragment header (MTU 1170 <
  full snapshot). macOS: blocking setup waits must pump
  `CFRunLoopRunInMode` — the SDK's HTTP stack starves under plain sleep.

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY).
