// net/net_transport.h
// Transport (Module 11, nửa còn lại của net/net.h): peer-hosted, multi-peer
// (protocol v3, up to NET_MAX_PLAYERS = 8 → 4v4). HOST runs the one true
// simulation; each CLIENT sends PlayerIntent and mirrors the host's agent
// pool from snapshots. No other module includes this except main.c (wiring)
// and game/ / ui/ (intent submission, connection HUD, lobby roster).
//
//   LAN:    ./wuxing --host [port]  /  --join <ip> [port]   (ENet, default 7777)
//   online: ./wuxing --host-online  /  --join-online <code> (EOS backend)
//
// Every remote player spawns on the HOST as a hero (team auto-balanced in
// join order until the lobby screen assigns teams — Đợt A2); the roster
// packet tells everyone who's who. Client-side, the whole local simulation
// is OFF (Net_ClientDrivesWorld) — snapshots own the pool.
#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

#include "control/control.h" // PlayerIntent
#include "net/net.h"         // NetRosterEntry, NET_MAX_PLAYERS
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
// it. HOST: the FIRST remote player's agent id (-1 before any peer joins) —
// kept for the invasion dev mode; team battle reads the roster instead.
int Net_GetLocalHeroAgentId(void);
int Net_GetRemoteAgentId(void);

// --- Room roster (protocol v3) ---
// HOST: built live (slot 0 = host, then peers in join order, then bots).
// CLIENT: the last roster broadcast received (host rebroadcasts on every
// change). Returns the number of entries written into out. Lobby UI (Đợt
// A2) and team battle (Đợt A3) render/derive from this.
int Net_GetRoster(NetRosterEntry *out, int maxEntries);
// HOST: connected remote players. CLIENT: 1 while connected, else 0.
int Net_GetPeerCount(void);

// --- Lobby room management (Đợt A2 — HOST only, no-ops elsewhere) ---
// rosterIndex = position in Net_GetRoster's output. Toggling a HUMAN entry
// flips its team (0 ↔ 1) and retags its spawned hero via
// Entity_SetAgentTeam; toggling a BOT entry moves the bot to the other
// side. Every change rebroadcasts the roster.
void Net_HostToggleTeam(int rosterIndex);
// Add/remove a bot on a side (0/1). Bots are roster metadata until the
// match starts — Đợt A4's hero-bot brain spawns/drives them host-side.
void Net_HostAddBot(int team);
void Net_HostRemoveBot(int team);

// HOST presses BẮT ĐẦU: broadcasts a reliable START to every peer and arms
// the local flag too. Net_ConsumeMatchStart() returns true exactly once on
// every instance (host included) — main.c switches lobby → match on it.
void Net_HostStartMatch(void);
bool Net_ConsumeMatchStart(void);

// HOST rematch (Đợt A3): every connected peer whose hero died gets a fresh
// one (new HELLO with the new agent id) and the roster rebroadcasts.
// game/'s team-battle reset calls this before the next round.
void Net_HostRespawnPeerHeroes(void);

// --- Đợt A5: VFX cast mirroring + loadout sync ---
// HOST: broadcast "agent X cast skill Y at Z" (reliable). Call sites: game/
// for the host player (Control_ConsumeCastFired), main.c for bot casts
// (AI_PollHeroCasts); the transport emits its own for remote-intent casts.
// Clients re-play it via CastSkill in free-cast mode — pure VFX, damage
// still arrives through snapshots (clients never tick Combat_Update).
void Net_HostNotifyCast(int agentId, int skillIndex, Vector3 aim);

// CLIENT: TAB loadout change → tell the host (reliable). The host applies
// Entity_SetEquippedSkill on our hero (element passed along — the client
// resolved it from the registry), which recomputes Vô Hệ for everyone.
void Net_ClientSendLoadout(int slot, int skillIndex, int element);

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
