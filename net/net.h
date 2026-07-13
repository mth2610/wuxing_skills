// net/net.h
// Networking (MODULES_ROADMAP.md Module 11) — peer-hosted model: clients
// serialize PlayerIntent to the host, the host runs the one true simulation
// (combat resolved host-side only) and broadcasts agent-pool snapshots back.
//
// THIS PASS ships the deterministic core: versioned wire formats +
// pack/unpack for both directions, exercised over a loopback. The ENet
// socket/thread layer is the remaining half — it adds a third-party
// dependency and is gated on the PvP milestone (thiết kế: only after the
// offline game mode has soaked). No other module knows about networking:
// control/'s PlayerIntent is already the exact unit that travels.
#ifndef NET_H
#define NET_H

#include "control/control.h"   // PlayerIntent — the client→host payload
#include "entities/entities.h" // Agent snapshot fields
#include <stdbool.h>

#define NET_PROTOCOL_VERSION 3 // v3: multi-peer — roster packet, 8 players

// Team-battle vision (KE_HOACH_TIEP_THEO.md Đợt A): up to 4v4 = 7 remote
// players + the host. The roster is the host-owned room list — who occupies
// which slot, on which team, driving which hero agent. Broadcast (reliable)
// whenever it changes; clients render lobby/team UI from it.
#define NET_MAX_PLAYERS 8

#define NET_ROSTER_NONE      255  // agentId value: no hero spawned yet
#define NET_ROSTER_OCCUPIED  0x1  // flags
#define NET_ROSTER_BOT       0x2  // slot filled by a host-driven bot (Đợt A4)
#define NET_ROSTER_HOST      0x4  // this slot is the host player

typedef struct {
    unsigned char slot;    // 0..NET_MAX_PLAYERS-1 (0 = host by convention)
    unsigned char team;    // 0/1 — maps to AgentTeam ALLY/ENEMY on the host
    unsigned char agentId; // host-pool hero id, NET_ROSTER_NONE before spawn
    unsigned char flags;   // NET_ROSTER_* bits
} NetRosterEntry;

// Pack/unpack the full room roster (host → clients, reliable channel).
// Returns bytes written (0 on undersized buffer) / entry count (-1 on
// malformed or version mismatch).
int Net_PackRoster(const NetRosterEntry *entries, int count,
                   unsigned char *buf, int maxBytes);
int Net_UnpackRoster(NetRosterEntry *out, int maxEntries,
                     const unsigned char *buf, int len);

// --- Client → Host: PlayerIntent ---
// Returns bytes written (0 on undersized buffer).
int  Net_PackIntent(const PlayerIntent *in, unsigned char *buf, int maxBytes);
// Returns true when buf held a valid, version-matching intent packet.
bool Net_UnpackIntent(PlayerIntent *out, const unsigned char *buf, int len);

// --- Host → Client: agent pool snapshot ---
// Wire form of one agent — the fields a client needs to render/predict.
typedef struct {
    unsigned char  agentId;
    unsigned char  team;        // AgentTeam
    unsigned char  archetype;   // AgentArchetype
    unsigned char  element;     // currentElement
    Vector3        position;
    float          health, maxHealth;
    float          mana, maxMana;
    unsigned char  flags;       // bit0 taijiActive, bit1 isMeditating, bit2 isStealthed
} NetAgentState;

// Packs every ACTIVE agent. Returns bytes written (0 on undersized buffer).
int Net_PackAgentSnapshot(unsigned char *buf, int maxBytes);
// Parses a snapshot into out (up to maxAgents). Returns agent count, or -1
// on malformed/version-mismatched input. Pure parse — applying it to the
// local pool ships with the transport layer (needs client-side prediction
// rules decided first).
int Net_UnpackAgentSnapshot(NetAgentState *out, int maxAgents,
                            const unsigned char *buf, int len);

#endif // NET_H
