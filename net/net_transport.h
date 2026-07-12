// net/net_transport.h
// ENet transport (Module 11, nửa còn lại của net/net.h): peer-hosted 1v1.
// HOST runs the one true simulation; the CLIENT sends PlayerIntent and
// mirrors the host's agent pool from snapshots (no prediction in v1 — LAN
// latency hides it). No other module includes this except main.c (wiring)
// and game/ (intent submission + connection HUD).
//
//   host:   ./wuxing --host [port]         (default 7777)
//   client: ./wuxing --join <ip> [port]
//
// v1 model: the remote player spawns on the HOST as a TEAM_ENEMY hero with
// the default loadout — an asymmetric invasion duel alongside the boss
// match (a dedicated PvP game state comes later with NET_API.md's next
// pass). Client-side, the whole local simulation is OFF
// (Net_ClientDrivesWorld) — snapshots own the pool.
#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

#include "control/control.h" // PlayerIntent
#include <stdbool.h>

typedef enum { NET_MODE_OFF = 0, NET_MODE_HOST, NET_MODE_CLIENT } NetMode;

#define NET_DEFAULT_PORT 7777

bool    Net_StartHost(int port);                  // false: socket/bind failed
bool    Net_StartClient(const char *ip, int port);// false: socket/resolve failed
void    Net_Stop(void);
NetMode Net_GetMode(void);
bool    Net_IsPeerConnected(void);

// Pump ENet + role work. Call once per frame BEFORE Entity_Update:
//   HOST: receive intents (edge actions applied on arrival), integrate the
//         remote hero's held movement with dt, broadcast snapshots @20Hz.
//   CLIENT: send the local intent @30Hz, apply arriving snapshots to the
//         local pool (Entity_Net* sync — deactivates unmirrored agents).
void Net_Tick(float dt);

// CLIENT: queue the local PlayerIntent for the next send (game/ calls this
// INSTEAD of Control_Apply while connected — the host applies it).
void Net_ClientSubmitIntent(const PlayerIntent *intent);

// True while this instance is a connected CLIENT: main.c must skip the
// local gameplay ticks (Entity/AI/Boss/Formation/Combat) — the host owns
// the simulation and snapshots overwrite the pool.
bool Net_ClientDrivesWorld(void);

// CLIENT: the host-pool agent id of OUR hero (mirrors 1:1 into the local
// pool), or -1 before the host assigned it. game/ points the camera/HUD at
// it. HOST: the remote player's agent id (-1 before a peer joins).
int Net_GetLocalHeroAgentId(void);
int Net_GetRemoteAgentId(void);

// --- Match outcome sync ---
// HOST: game/ reports its GameState each frame; the transport sends it on
// change (reliable). CLIENT: reads the host's last known state (-1 until
// one arrives) and maps it to its own perspective (the invader is on the
// opposing team: host VICTORY = client DEFEAT and vice versa).
void Net_HostSetMatchState(int state);
int  Net_GetRemoteMatchState(void);

// --- Online backend (Epic Online Services — EOS_SETUP.md) ---
// Internet play over Epic's free NAT-punch/relay infra: host creates a
// lobby and gets a short join code; friends join by code — no IPs, no port
// forwarding. Compiled in only with -DWUXING_EOS=ON + the SDK under
// third_party/eos-sdk (net/net_eos.c); otherwise net/net_eos_stub.c makes
// these report unavailability. The ENet path above stays for LAN/dev.
bool Net_OnlineAvailable(void);
bool Net_StartHostOnline(char *outJoinCode, int maxCodeLen);
bool Net_JoinOnline(const char *joinCode);

#endif // NET_TRANSPORT_H
