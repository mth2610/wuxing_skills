# net — Progress

## Done
- Wire core (`net.c`), ENet LAN transport (`net_transport.c`, vendored ENet 1.3.18 via CMake, user approval 07/2026), and the EOS online backend (`net_eos.c`, gated behind `-DWUXING_EOS=ON`) are all landed. See `docs/API.md`.
- EOS backend verified end-to-end 2026-07-12: two instances, real Epic lobby + P2P, host spawned the remote hero, session held with no drops.
- Multi-peer + lobby room (protocol v3) verified 2026-07-13 (3 instances over real EOS): two clients with separate heroes + live roster updates on join, slot reuse after a leave, 8s timeout sweep on a killed client, START propagating everyone into the match together.
- ~~Basic attack not in PlayerIntent~~ FIXED (protocol v2): intent carries `basicAttack` (0 = none, else BasicAttackType+1); the host executes the remote hero's melee, the client plays its swing anim locally.
- ~~Match outcomes not replicated~~ FIXED: typed reliable control packets (`NET_CTRL_HELLO` / `NET_CTRL_STATE`); the host resends state on change, the client maps it perspective-swapped (invader: host VICTORY = your DEFEAT).

## Backlog
- No client prediction/interpolation for the transport's raw snapshot path — 20Hz snapshots snap (fine on LAN); note the later interpolation pass (`docs/API.md` §3d-bis) addresses this for the multi-peer path.
- Skill VFX are host-local: the client sees positions/HP but not the host's particle effects (event mirroring is the planned fix).
- The remote hero's own death still doesn't end the match — the invader has no respawn yet.
- Side thread (main-thread polling for now), reconnection/session management.
- Delta compression / interest management (full snapshots only).
