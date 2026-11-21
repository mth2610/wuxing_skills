// net/net.h
// Networking (Module 11) — peer-hosted model: clients serialize PlayerIntent
// (+ ackSnapSeq) to the host; the host runs the authoritative simulation and
// sends per-peer interest-culled, ack-based delta snapshots back.
#ifndef NET_H
#define NET_H

#include "control/control.h"   // PlayerIntent — the client->host payload
#include "entities/entities.h" // Agent snapshot fields, MAX_AGENTS
#include <stdbool.h>
#include <stdint.h>

#define NET_PROTOCOL_VERSION 5 // v5: ack-based delta snapshots + quantized aim + seq ordering

// Team-battle vision: up to 4v4 = 7 remote players + host.
// Explore mode caps at 5 players/host for low-latency P2P.
#define NET_MAX_PLAYERS 8
#define NET_EXPLORE_MAX_PLAYERS 5
#define NET_INTEREST_RADIUS 45.0f

#define NET_ROSTER_NONE 255     // agentId value: no hero spawned yet
#define NET_ROSTER_OCCUPIED 0x1 // flags
#define NET_ROSTER_BOT 0x2      // slot filled by a host-driven bot
#define NET_ROSTER_HOST 0x4     // this slot is the host player

// Delta-snapshot history depth (16 frames @ 30Hz = ~533ms RTT/loss tolerance).
#define NET_SNAP_HIST_SIZE 16

typedef struct
{
    unsigned char slot;    // 0..NET_MAX_PLAYERS-1 (0 = host by convention)
    unsigned char team;    // 0/1 — maps to AgentTeam ALLY/ENEMY on the host
    unsigned char agentId; // host-pool hero id, NET_ROSTER_NONE before spawn
    unsigned char flags;   // NET_ROSTER_* bits
} NetRosterEntry;

// Pack/unpack the full room roster (host -> clients, reliable channel).
int Net_PackRoster(const NetRosterEntry *entries, int count,
                   unsigned char *buf, int maxBytes);
int Net_UnpackRoster(NetRosterEntry *out, int maxEntries,
                     const unsigned char *buf, int len);

// --- Compact quantized Vector3 helper (6 bytes vs 12 bytes raw float) ---
void Net_PackVec3Quantized(Vector3 v, unsigned char out[6]);
Vector3 Net_UnpackVec3Quantized(const unsigned char in[6]);

// --- Client -> Host: PlayerIntent ---
// Ex variants piggyback the client's last received snapshot seq (2B) so the
// host can delta-compress against a confirmed baseline.
int Net_PackIntentEx(const PlayerIntent *in, uint16_t ackSnapSeq,
                     unsigned char *buf, int maxBytes);
bool Net_UnpackIntentEx(PlayerIntent *out, uint16_t *outAckSnapSeq,
                        const unsigned char *buf, int len);

// Legacy wrappers (ackSnapSeq = 0).
int Net_PackIntent(const PlayerIntent *in, unsigned char *buf, int maxBytes);
bool Net_UnpackIntent(PlayerIntent *out, const unsigned char *buf, int len);

// --- Host -> Client: agent pool snapshot ---
typedef struct
{
    unsigned char agentId;
    unsigned char team;      // AgentTeam
    unsigned char archetype; // AgentArchetype
    unsigned char element;   // currentElement
    Vector3 position;
    float health, maxHealth;
    float mana, maxMana;
    float yaw;           // facing angle in radians
    unsigned char flags; // bit0 taiji, bit1 meditating, bit2 stealthed, bit3 duelFlag (or netMonsterMeta)
} NetAgentState;

// Quantized wire-space agent record used for bit-exact delta compression.
typedef struct
{
    uint16_t px, pz;
    int16_t py;
    uint16_t hp, mhp;
    uint16_t mp, mmp;
    uint8_t meta;  // team(2) | archetype(2) | element(3) | hasMana(1)
    uint8_t flags; // hero flags or netMonsterMeta
    uint8_t yaw;
    uint8_t present; // 0 = absent, 1 = active in this frame
} NetPackedAgent;

// Captured once per host snapshot tick and shared across all peer encodes.
typedef struct
{
    NetPackedAgent agents[MAX_AGENTS];
    float posX[MAX_AGENTS];
    float posZ[MAX_AGENTS];
    uint8_t heroIds[MAX_AGENTS];
    uint8_t minionIds[MAX_AGENTS];
    int heroCount;
    int minionCount;
} NetWorldSnap;

typedef struct
{
    uint16_t seq;   // 0 = empty slot, 1..65535 = valid snapshot seq
    uint16_t count; // active agent count in this frame
    uint8_t ids[MAX_AGENTS];
    NetPackedAgent agents[MAX_AGENTS];
} NetSnapFrame;

typedef struct
{
    NetSnapFrame frames[NET_SNAP_HIST_SIZE];
    uint16_t lastSeq;
    int writeIdx;
} NetSnapHistory;

// Reset a host-peer or client snapshot history ring.
void Net_SnapHistoryReset(NetSnapHistory *hist);

// Advance a 16-bit non-zero sequence number (wraps 65535 -> 1).
uint16_t Net_NextSeq(uint16_t seq);

// Capture & quantize all live agents once per host snapshot tick.
void Net_CaptureWorldSnapshot(NetWorldSnap *out);

// Encode a per-peer interest-culled delta (or keyframe if ackSeq is 0 / aged
// out) into buf (<= maxBytes). Updates peerHist with the exact frame sent.
int Net_PackDeltaSnapshotFor(const NetWorldSnap *world, Vector3 focusPos,
                             float interestRadius, NetSnapHistory *peerHist,
                             uint16_t ackSeq, uint16_t nextSeq,
                             unsigned char *buf, int maxBytes);

// Unpack a delta or keyframe snapshot into clientHist and emit all currently
// active agents into out (up to maxAgents).
// Return values:
//   >= 0 : decoded active agent count
//   -1   : malformed / version mismatch
//   -2   : baseline missing (clientHist->lastSeq reset to 0 to request keyframe)
//   -3   : stale / out-of-order duplicate packet (safely ignored)
int Net_UnpackDeltaSnapshot(NetSnapHistory *clientHist, NetAgentState *out,
                            int maxAgents, const unsigned char *buf, int len,
                            uint16_t *outSeq);

// Stateless full-snapshot helpers (kept for loopback/tests).
int Net_PackAgentSnapshot(unsigned char *buf, int maxBytes);
int Net_PackAgentSnapshotFor(Vector3 focusPos, float interestRadius,
                             unsigned char *buf, int maxBytes);
int Net_UnpackAgentSnapshot(NetAgentState *out, int maxAgents,
                            const unsigned char *buf, int len);

#endif // NET_H