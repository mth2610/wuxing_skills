// net/net_transport.c — transport core over net/net.h's wire formats.
// Protocol v3: multi-peer (up to NET_MAX_PLAYERS-1 = 7 remote players → 4v4).
// Two packet paths share this core: ENet (built-in, LAN) and a pluggable
// backend (net_transport_internal.h — EOS for internet play). Main-thread
// polling (the side thread lands with the jitter pass), no client-side
// prediction.
#include "net/net_transport.h"
#include "net/net_prediction.h"
#include "net/net_transport_internal.h"
#include "net/net.h"
#include "entities/entities.h"
#include "skills/skill_manager.h"
#include "maps/map_manager.h"  // Map_QueryZoneAt — zone rules for remote heroes
#include "game/game_rules.h"   // cooldown/stealth table (the ONE rule table)
#include <enet/enet.h>
#include "raylib.h" // TraceLog only — no rendering in this module
#include <math.h>
#include <stddef.h>
#include <string.h>

// Channels: 0 = unreliable stream (intents / snapshots), 1 = reliable
// control (hello / match state / roster), 2 = fast probe (PING / PONG on EOS).
#define NET_CH_STREAM  0
#define NET_CH_CONTROL 1
#define NET_CH_PROBE   2

#define NET_SNAPSHOT_HZ 30.0f
#define NET_INTENT_HZ   60.0f

// Remote hero spawn base (host side) — mirrors game/game_screen.c's match
// world (VERDANT_PATH plateau), opposite the host player's spawn. Slots
// fan out around it so 7 heroes don't stack on one point.
static const Vector3 REMOTE_SPAWN = { 54.0f, 0.0f, 41.0f };

// Reliable control-channel packets: [type][version][value]. The roster
// travels on the same channel but is a net.h-packed packet (starts with
// magic 'W') — HandlePacket disambiguates by size + leading byte.
#define NET_CTRL_HELLO 1  // value = client's hero agent id (host pool)
#define NET_CTRL_STATE 2  // value = host's GameState (match outcome sync)
#define NET_CTRL_START 3  // host pressed BẮT ĐẦU — leave the lobby together
#define NET_CTRL_CAST        5 // host → clients: [5][ver][agentId][skillIdx][aim 12B] = 16B
#define NET_CTRL_LOADOUT     6 // client → host:  [6][ver][slot][skillIdx][element] = 5B
#define NET_CTRL_KILL_REWARD  7 // host → clients: [7][ver][kind][elem][isBoss][gold 2B][xp 2B] = 9B
#define NET_CTRL_SOCIAL_EMOTE 8 // bidirectional: [8][ver][agentId][emoteId][duelFlag][0] = 6B
#define NET_CTRL_TRADE_GIFT   9 // bidirectional: [9][ver][senderAgentId][itemKind][amt 2B] = 6B
#define NET_CTRL_WARDROBE     10 // bidirectional: [10][ver][agentId][slotCount][16B indices] = 20B
#define NET_CAST_PACKET_BYTES         16
#define NET_LOADOUT_PACKET_BYTES      5
#define NET_KILL_REWARD_PACKET_BYTES  9
#define NET_SOCIAL_PACKET_BYTES       6
#define NET_TRADE_PACKET_BYTES        6
#define NET_WARDROBE_PACKET_BYTES     (4 + NET_WARDROBE_SLOTS)
#define NET_CTRL_JOIN  4  // client → host 1Hz knock/heartbeat: the first one
                          // initiates the EOS NAT punch (clients in the
                          // lobby send no gameplay traffic, and EOS only
                          // connects on the first packet); afterwards it
                          // keeps proving liveness — the host drops a peer
                          // silent for NET_PEER_TIMEOUT (EOS has no
                          // process-kill disconnect event of its own).
#define NET_CTRL_PONG  11 // host → client 2Hz reply to NET_CTRL_PING for exact UDP RTT
#define NET_CTRL_PING  12 // client → host 2Hz pure-UDP probe on NET_CH_STREAM
#define NET_PEER_TIMEOUT 8.0f
typedef struct { unsigned char type, version, value; } NetCtrl;

extern const char *Net_EosGetNetworkPathLabel(void);

static int    s_pingMs         = 0;
static double s_lastJoinSendAt = 0.0;
static double s_pingSendAt[256];
static int    s_rttRing[24];
static int    s_rttRingCount   = 0;
static int    s_rttRingIdx     = 0;
static unsigned char s_joinSeq = 0;
static int    s_snapRxWindow   = 0;
static int    s_snapRxHz       = 0;
static float  s_snapRxTimer    = 0.0f;

// --- Session state -----------------------------------------------------------

static NetMode   s_mode = NET_MODE_OFF;
static ENetHost *s_host = NULL;   // our ENet endpoint (both roles, ENet path)
static ENetPeer *s_link = NULL;   // CLIENT: the single link to the host
static bool      s_connected = false; // host: ≥1 peer; client: joined
static bool      s_enetReady = false;

// HOST: one entry per connected remote player.
typedef struct {
    bool          used;
    ENetPeer     *enetPeer;   // ENet path (NULL on backend path)
    void         *backendRef; // backend path (EOS ProductUserId) — NULL on ENet
    int           agentId;    // their hero in OUR pool, -1 if spawn failed
    unsigned char team;       // 0/1 — auto-balanced at join (lobby reassigns in A2)
    PlayerIntent  held;       // last held-state intent (movement)
    float         silentFor;  // seconds since their last packet (liveness)
    float         respawnTimer; // 5s auto-respawn timer when hero dies in Explore mode
} HostPeer;
static HostPeer s_peers[NET_MAX_PLAYERS - 1];

static int   s_localHeroId = -1;     // client: our hero's host-pool id
static float s_sendAccum = 0.0f;

static PlayerIntent s_clientIntent = { 0 }; // client: queued local intent
static bool s_clientIntentValid = false;

static int s_hostMatchState = -1;   // host: last state game/ reported
static int s_hostStateSent  = -1;   // host: last state actually sent
static int s_remoteMatchState = -1; // client: host's match state
static NetKillReward s_killRewardQueue[32];
static int           s_killRewardCount = 0;

static NetSocialEvent s_socialQueue[32];
static int            s_socialCount = 0;
static NetTradeGift      s_tradeQueue[32];
static int               s_tradeCount = 0;
static NetWardrobeUpdate s_wardrobeQueue[32];
static int               s_wardrobeCount = 0;
static NetWardrobeUpdate s_storedWardrobes[MAX_AGENTS];
static bool              s_storedWardrobeValid[MAX_AGENTS];
static NetCastEvent      s_castEventQueue[32];
static int               s_castEventCount = 0;

static NetRosterEntry s_clientRoster[NET_MAX_PLAYERS]; // client: last broadcast
static int            s_clientRosterCount = 0;

static NetAgentState s_snapLatest[MAX_AGENTS];
static bool          s_exploreMode = false;

static unsigned char s_hostTeam = 0;     // lobby: the host player's own side
static int           s_botCount[2] = { 0, 0 }; // lobby: bots per side
static bool          s_matchStart = false;     // START received/armed, unread

static const float REMOTE_MOVE_MPS = 6.4f;  // match control/'s RUN_SPEED_MPS
static const float REMOTE_JUMP_FORCE = 4.6f; // match control/'s JUMP_FORCE
static const float REMOTE_DASH_MPS = 10.0f;

// Pluggable packet backend (net_transport_internal.h) — when installed it
// replaces the ENet endpoint for the whole session (s_host stays NULL).
static NetBackendSendFn s_backendSend = NULL;
static NetBackendTickFn s_backendTick = NULL;
static NetBackendStopFn s_backendStop = NULL;

// --- Send helpers ------------------------------------------------------------

static void SendToPeer(const HostPeer *p, int channel, const void *data,
                       int len, bool reliable) {
    if (s_backendSend != NULL) {
        s_backendSend(p->backendRef, channel, data, len, reliable);
        return;
    }
    if (p->enetPeer == NULL) return;
    ENetPacket *pkt = enet_packet_create(data, (size_t)len,
                                         reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(p->enetPeer, (enet_uint8)channel, pkt);
}

static void HostBroadcast(int channel, const void *data, int len, bool reliable) {
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++)
        if (s_peers[i].used) SendToPeer(&s_peers[i], channel, data, len, reliable);
}

static void ClientSendToHost(int channel, const void *data, int len, bool reliable) {
    if (s_backendSend != NULL) {
        s_backendSend(NULL, channel, data, len, reliable);
        return;
    }
    if (s_link == NULL) return;
    ENetPacket *pkt = enet_packet_create(data, (size_t)len,
                                         reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(s_link, (enet_uint8)channel, pkt);
}

// --- Lifecycle ---------------------------------------------------------------

static bool EnsureEnet(void) {
    if (!s_enetReady) s_enetReady = (enet_initialize() == 0);
    return s_enetReady;
}

bool Net_StartHost(int port) {
    if (s_mode != NET_MODE_OFF || !EnsureEnet()) return false;
    ENetAddress addr = { 0 };
    addr.host = ENET_HOST_ANY;
    addr.port = (enet_uint16)((port > 0) ? port : NET_DEFAULT_PORT);
    s_host = enet_host_create(&addr, NET_MAX_PLAYERS - 1, 2 /*channels*/, 0, 0);
    if (s_host == NULL) return false;
    s_mode = NET_MODE_HOST;
    return true;
}

bool Net_StartClient(const char *ip, int port) {
    if (s_mode != NET_MODE_OFF || ip == NULL || !EnsureEnet()) return false;
    s_host = enet_host_create(NULL, 1, 2, 0, 0);
    if (s_host == NULL) return false;
    ENetAddress addr = { 0 };
    if (enet_address_set_host(&addr, ip) != 0) { enet_host_destroy(s_host); s_host = NULL; return false; }
    addr.port = (enet_uint16)((port > 0) ? port : NET_DEFAULT_PORT);
    s_link = enet_host_connect(s_host, &addr, 2, 0);
    if (s_link == NULL) { enet_host_destroy(s_host); s_host = NULL; return false; }
    s_mode = NET_MODE_CLIENT;
    return true;
}

void Net_Stop(void) {
    if (s_backendStop != NULL) s_backendStop();
    s_backendSend = NULL;
    s_backendTick = NULL;
    s_backendStop = NULL;
    if (s_host != NULL) {
        if (s_mode == NET_MODE_CLIENT && s_link && s_connected)
            enet_peer_disconnect_now(s_link, 0);
        if (s_mode == NET_MODE_HOST)
            for (int i = 0; i < NET_MAX_PLAYERS - 1; i++)
                if (s_peers[i].used && s_peers[i].enetPeer)
                    enet_peer_disconnect_now(s_peers[i].enetPeer, 0);
        enet_host_destroy(s_host);
    }
    s_host = NULL;
    s_link = NULL;
    s_connected = false;
    s_mode = NET_MODE_OFF;
    memset(s_peers, 0, sizeof(s_peers));
    s_localHeroId = -1;
    s_clientIntentValid = false;
    s_hostMatchState = s_hostStateSent = s_remoteMatchState = -1;
    s_clientRosterCount = 0;
    s_hostTeam = 0;
    s_botCount[0] = s_botCount[1] = 0;
    s_matchStart = false;
    s_exploreMode = false;
    s_pingMs = 0;
    s_rttRingCount = 0;
    s_rttRingIdx = 0;
    s_snapRxWindow = 0;
    s_snapRxHz = 0;
    s_snapRxTimer = 0.0f;
    NetPred_Reset();
}

int Net_GetPingMs(void) {
    if (s_mode == NET_MODE_OFF) return 0;
    if (s_mode == NET_MODE_CLIENT && s_link != NULL) return (int)s_link->roundTripTime;
    if (s_mode == NET_MODE_HOST && s_host != NULL) {
        for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
            if (s_peers[i].used && s_peers[i].enetPeer != NULL)
                return (int)s_peers[i].enetPeer->roundTripTime;
        }
    }
    return s_pingMs;
}

int Net_GetSnapshotRxHz(void) {
    if (s_mode == NET_MODE_OFF) return 0;
    if (s_mode == NET_MODE_HOST) return s_connected ? (int)NET_SNAPSHOT_HZ : 0;
    return s_snapRxHz;
}

const char *Net_GetConnectionPathLabel(void) {
    if (s_mode == NET_MODE_OFF) return "SOLO";
    if (s_backendSend != NULL) {
        if (!s_connected || (s_mode == NET_MODE_HOST && Net_GetPeerCount() == 0))
            return "EOS HOST (0 KHACH)";
        return Net_EosGetNetworkPathLabel();
    }
    return s_connected ? "LAN ENET" : "LAN HOST";
}

void Net_SetExploreMode(bool on) { s_exploreMode = on; }
bool Net_IsExploreMode(void) { return s_exploreMode; }

NetMode Net_GetMode(void) { return s_mode; }
bool Net_IsPeerConnected(void) { return s_connected; }
bool Net_ClientDrivesWorld(void) { return s_mode == NET_MODE_CLIENT && s_connected; }
int Net_GetLocalHeroAgentId(void) { return (s_mode == NET_MODE_CLIENT) ? s_localHeroId : -1; }

int Net_GetRemoteAgentId(void) {
    if (s_mode != NET_MODE_HOST) return -1;
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++)
        if (s_peers[i].used) return s_peers[i].agentId;
    return -1;
}

int Net_GetPeerCount(void) {
    if (s_mode == NET_MODE_CLIENT) return s_connected ? 1 : 0;
    if (s_mode != NET_MODE_HOST) return 0;
    int n = 0;
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++)
        if (s_peers[i].used) n++;
    return n;
}

void Net_ClientSubmitIntent(const PlayerIntent *intent) {
    if (intent == NULL || s_mode != NET_MODE_CLIENT) return;
    if (!s_clientIntentValid) {
        s_clientIntent = *intent;
        s_clientIntentValid = true;
    } else {
        // Preserve edge-triggered actions pressed between sends so
        // 60+ FPS frames never overwrite them before transmission.
        bool keepJump = s_clientIntent.jump || intent->jump;
        bool keepDash = s_clientIntent.dash || intent->dash;
        bool keepMed  = s_clientIntent.meditate || intent->meditate;
        int  keepSlot = (intent->castSkillSlot >= 0) ? intent->castSkillSlot : s_clientIntent.castSkillSlot;
        int  keepAtk  = (intent->basicAttack > 0) ? intent->basicAttack : s_clientIntent.basicAttack;
        s_clientIntent = *intent;
        s_clientIntent.jump = keepJump;
        s_clientIntent.dash = keepDash;
        s_clientIntent.meditate = keepMed;
        s_clientIntent.castSkillSlot = keepSlot;
        s_clientIntent.basicAttack = keepAtk;
    }

    // Immediate flush for discrete actions (skills, attacks, Khinh Công, dash,
    // meditate) OR when the 60Hz intent interval has elapsed, so intents sent
    // during GameScreen_Update leave on the same frame with zero queueing lag.
    bool hasEdge = (s_clientIntent.castSkillSlot >= 0 || s_clientIntent.basicAttack > 0 ||
                    s_clientIntent.jump || s_clientIntent.dash || s_clientIntent.meditate);
    if ((hasEdge || s_sendAccum >= 1.0f / NET_INTENT_HZ) && s_connected) {
        unsigned char buf[128];
        int bytes = Net_PackIntent(&s_clientIntent, buf, (int)sizeof(buf));
        if (bytes > 0) {
            ClientSendToHost(NET_CH_STREAM, buf, bytes, s_clientIntent.castSkillSlot >= 0);
            s_sendAccum = 0.0f;
            s_clientIntent.jump = s_clientIntent.dash = s_clientIntent.meditate = false;
            s_clientIntent.castSkillSlot = -1;
            s_clientIntent.basicAttack = 0;
        }
    }
}

// --- Roster ------------------------------------------------------------------

static void HostSpawnPeerHero(int idx); // defined with the join/leave block

int Net_GetRoster(NetRosterEntry *out, int maxEntries) {
    if (out == NULL || maxEntries <= 0) return 0;
    if (s_mode == NET_MODE_CLIENT) {
        int n = (s_clientRosterCount < maxEntries) ? s_clientRosterCount : maxEntries;
        memcpy(out, s_clientRoster, (size_t)n * sizeof(NetRosterEntry));
        return n;
    }
    if (s_mode != NET_MODE_HOST) return 0;
    int n = 0;
    // Slot 0 = the host player itself.
    int hostAgent = Control_GetAgentId();
    out[n++] = (NetRosterEntry){ 0, s_hostTeam,
        (unsigned char)((hostAgent >= 0 && hostAgent < 255) ? hostAgent : NET_ROSTER_NONE),
        NET_ROSTER_OCCUPIED | NET_ROSTER_HOST };
    for (int i = 0; i < NET_MAX_PLAYERS - 1 && n < maxEntries; i++) {
        if (!s_peers[i].used) continue;
        out[n++] = (NetRosterEntry){ (unsigned char)(i + 1), s_peers[i].team,
            (unsigned char)((s_peers[i].agentId >= 0) ? s_peers[i].agentId : NET_ROSTER_NONE),
            NET_ROSTER_OCCUPIED };
    }
    // Bots trail the humans (lobby metadata — Đợt A4 spawns their heroes).
    for (int t = 0; t < 2; t++)
        for (int b = 0; b < s_botCount[t] && n < maxEntries; b++) {
            unsigned char slot = (unsigned char)n;
            out[n++] = (NetRosterEntry){ slot, (unsigned char)t,
                NET_ROSTER_NONE, NET_ROSTER_OCCUPIED | NET_ROSTER_BOT };
        }
    return n;
}

static void HostBroadcastRoster(void) {
    NetRosterEntry entries[NET_MAX_PLAYERS];
    int count = Net_GetRoster(entries, NET_MAX_PLAYERS);
    unsigned char buf[64];
    int bytes = Net_PackRoster(entries, count, buf, (int)sizeof(buf));
    if (bytes > 0) HostBroadcast(NET_CH_CONTROL, buf, bytes, true);
}

// --- Lobby room management (Đợt A2) -----------------------------------------

void Net_HostToggleTeam(int rosterIndex) {
    if (s_mode != NET_MODE_HOST || rosterIndex < 0) return;
    NetRosterEntry entries[NET_MAX_PLAYERS];
    int count = Net_GetRoster(entries, NET_MAX_PLAYERS);
    if (rosterIndex >= count) return;
    const NetRosterEntry *e = &entries[rosterIndex];

    if (e->flags & NET_ROSTER_BOT) { // move the bot to the other side
        int from = e->team, to = 1 - e->team;
        if (s_botCount[from] > 0) { s_botCount[from]--; s_botCount[to]++; }
    } else if (e->flags & NET_ROSTER_HOST) {
        s_hostTeam = (unsigned char)(1 - s_hostTeam);
        int hostAgent = Control_GetAgentId();
        if (hostAgent >= 0)
            Entity_SetAgentTeam(hostAgent, s_hostTeam == 0 ? TEAM_ALLY : TEAM_ENEMY);
    } else { // human peer — roster index 1.. maps to join order
        int seen = 0;
        for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
            if (!s_peers[i].used) continue;
            if (++seen == rosterIndex) {
                s_peers[i].team = (unsigned char)(1 - s_peers[i].team);
                if (s_peers[i].agentId >= 0)
                    Entity_SetAgentTeam(s_peers[i].agentId,
                                        s_peers[i].team == 0 ? TEAM_ALLY : TEAM_ENEMY);
                break;
            }
        }
    }
    HostBroadcastRoster();
}

void Net_HostAddBot(int team) {
    if (s_mode != NET_MODE_HOST || team < 0 || team > 1) return;
    NetRosterEntry entries[NET_MAX_PLAYERS];
    if (Net_GetRoster(entries, NET_MAX_PLAYERS) >= NET_MAX_PLAYERS) return; // room full
    s_botCount[team]++;
    HostBroadcastRoster();
}

void Net_HostRemoveBot(int team) {
    if (s_mode != NET_MODE_HOST || team < 0 || team > 1) return;
    if (s_botCount[team] <= 0) return;
    s_botCount[team]--;
    HostBroadcastRoster();
}

void Net_HostStartMatch(void) {
    if (s_mode != NET_MODE_HOST) return;
    NetCtrl start = { NET_CTRL_START, NET_PROTOCOL_VERSION, 0 };
    HostBroadcast(NET_CH_CONTROL, &start, (int)sizeof(start), true);
    s_matchStart = true;
}

bool Net_ConsumeMatchStart(void) {
    bool v = s_matchStart;
    s_matchStart = false;
    return v;
}

void Net_HostRespawnPeerHeroes(void) {
    if (s_mode != NET_MODE_HOST) return;
    bool changed = false;
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
        if (!s_peers[i].used) continue;
        if (Entity_GetAgent(s_peers[i].agentId) != NULL) continue; // still alive
        HostSpawnPeerHero(i); // re-sends HELLO with the fresh agent id
        changed = true;
    }
    if (changed) HostBroadcastRoster();
}

void Net_HostNotifyCast(int agentId, int skillIndex, Vector3 aim) {
    if (s_mode != NET_MODE_HOST || !s_connected) return;
    if (agentId < 0 || agentId > 255 || skillIndex < 0 || skillIndex > 255) return;
    unsigned char buf[NET_CAST_PACKET_BYTES];
    buf[0] = NET_CTRL_CAST;
    buf[1] = NET_PROTOCOL_VERSION;
    buf[2] = (unsigned char)agentId;
    buf[3] = (unsigned char)skillIndex;
    memcpy(buf + 4, &aim, sizeof(Vector3));
    HostBroadcast(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
}

void Net_ClientSendLoadout(int slot, int skillIndex, int element) {
    if (s_mode != NET_MODE_CLIENT || !s_connected) return;
    if (slot < 0 || slot >= AGENT_SKILL_SLOTS || skillIndex < 0 || skillIndex > 255) return;
    unsigned char buf[NET_LOADOUT_PACKET_BYTES] = {
        NET_CTRL_LOADOUT, NET_PROTOCOL_VERSION,
        (unsigned char)slot, (unsigned char)skillIndex, (unsigned char)element,
    };
    ClientSendToHost(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
}

void Net_HostNotifyKillReward(int monsterKind, int element, bool isBoss, int gold, int xp) {
    if (s_mode != NET_MODE_HOST) return;
    if (gold < 0) gold = 0; if (gold > 65535) gold = 65535;
    if (xp < 0) xp = 0;     if (xp > 65535) xp = 65535;
    unsigned char buf[NET_KILL_REWARD_PACKET_BYTES] = {
        NET_CTRL_KILL_REWARD, NET_PROTOCOL_VERSION,
        (unsigned char)monsterKind,
        (unsigned char)element,
        (unsigned char)(isBoss ? 1 : 0),
        (unsigned char)(gold & 0xFF),
        (unsigned char)((gold >> 8) & 0xFF),
        (unsigned char)(xp & 0xFF),
        (unsigned char)((xp >> 8) & 0xFF),
    };
    HostBroadcast(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
}

int Net_ClientPollKillRewards(NetKillReward *out, int max) {
    if (out == NULL || max <= 0) { s_killRewardCount = 0; return 0; }
    int n = (s_killRewardCount < max) ? s_killRewardCount : max;
    for (int i = 0; i < n; i++) out[i] = s_killRewardQueue[i];
    s_killRewardCount = 0;
    return n;
}

static void PushSocialEvent(int agentId, int emoteId, bool duelFlag) {
    if (s_socialCount < 32) {
        s_socialQueue[s_socialCount++] = (NetSocialEvent){
            .agentId = agentId, .emoteId = emoteId, .duelFlag = duelFlag
        };
    }
}

static void PushTradeGift(int senderAgentId, int itemKind, int amount) {
    if (s_tradeCount < 32) {
        s_tradeQueue[s_tradeCount++] = (NetTradeGift){
            .senderAgentId = senderAgentId, .itemKind = itemKind, .amount = amount
        };
    }
}

void Net_SendSocialState(int agentId, int emoteId, bool duelFlag) {
    if (agentId < 0 || agentId > 255) return;
    unsigned char pkMode = (unsigned char)Entity_GetPkMode(agentId);
    unsigned char buf[NET_SOCIAL_PACKET_BYTES] = {
        NET_CTRL_SOCIAL_EMOTE, NET_PROTOCOL_VERSION,
        (unsigned char)agentId, (unsigned char)emoteId, (unsigned char)(duelFlag ? 1 : 0), pkMode
    };
    if (s_mode == NET_MODE_HOST) {
        HostBroadcast(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
    } else if (s_mode == NET_MODE_CLIENT && s_connected) {
        ClientSendToHost(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
    }
}

int Net_PollSocialEvents(NetSocialEvent *out, int max) {
    if (out == NULL || max <= 0) { s_socialCount = 0; return 0; }
    int n = (s_socialCount < max) ? s_socialCount : max;
    for (int i = 0; i < n; i++) out[i] = s_socialQueue[i];
    s_socialCount = 0;
    return n;
}

void Net_SendTradeGift(int senderAgentId, int itemKind, int amount) {
    if (senderAgentId < 0 || senderAgentId > 255 || amount <= 0) return;
    if (amount > 65535) amount = 65535;
    unsigned char buf[NET_TRADE_PACKET_BYTES] = {
        NET_CTRL_TRADE_GIFT, NET_PROTOCOL_VERSION,
        (unsigned char)senderAgentId, (unsigned char)itemKind,
        (unsigned char)(amount & 0xFF), (unsigned char)((amount >> 8) & 0xFF)
    };
    if (s_mode == NET_MODE_HOST) {
        HostBroadcast(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
    } else if (s_mode == NET_MODE_CLIENT && s_connected) {
        ClientSendToHost(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
    }
}

int Net_PollTradeGifts(NetTradeGift *out, int max) {
    if (out == NULL || max <= 0) { s_tradeCount = 0; return 0; }
    int n = (s_tradeCount < max) ? s_tradeCount : max;
    for (int i = 0; i < n; i++) out[i] = s_tradeQueue[i];
    s_tradeCount = 0;
    return n;
}

static void PushWardrobeUpdate(int agentId, int slotCount, const uint8_t *indices) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !indices) return;
    if (slotCount < 0) slotCount = 0;
    if (slotCount > NET_WARDROBE_SLOTS) slotCount = NET_WARDROBE_SLOTS;
    NetWardrobeUpdate u = { 0 };
    u.agentId = agentId;
    u.slotCount = slotCount;
    memset(u.catalogIndices, 0xFF, sizeof(u.catalogIndices));
    if (slotCount > 0) memcpy(u.catalogIndices, indices, (size_t)slotCount);
    s_storedWardrobes[agentId] = u;
    s_storedWardrobeValid[agentId] = true;
    if (s_wardrobeCount < 32) s_wardrobeQueue[s_wardrobeCount++] = u;
}

static void PackWardrobePacket(unsigned char *buf, int agentId, int slotCount, const uint8_t *indices) {
    buf[0] = NET_CTRL_WARDROBE;
    buf[1] = NET_PROTOCOL_VERSION;
    buf[2] = (unsigned char)agentId;
    buf[3] = (unsigned char)slotCount;
    memset(buf + 4, 0xFF, NET_WARDROBE_SLOTS);
    int n = (slotCount < NET_WARDROBE_SLOTS) ? slotCount : NET_WARDROBE_SLOTS;
    if (n > 0 && indices) memcpy(buf + 4, indices, (size_t)n);
}

void Net_SendWardrobe(int agentId, const uint8_t *catalogIndices, int slotCount) {
    if (agentId < 0 || agentId >= MAX_AGENTS || !catalogIndices || slotCount <= 0) return;
    if (slotCount > NET_WARDROBE_SLOTS) slotCount = NET_WARDROBE_SLOTS;
    unsigned char buf[NET_WARDROBE_PACKET_BYTES];
    PackWardrobePacket(buf, agentId, slotCount, catalogIndices);
    if (s_mode == NET_MODE_HOST) {
        NetWardrobeUpdate u = { 0 };
        u.agentId = agentId;
        u.slotCount = slotCount;
        memset(u.catalogIndices, 0xFF, sizeof(u.catalogIndices));
        memcpy(u.catalogIndices, catalogIndices, (size_t)slotCount);
        s_storedWardrobes[agentId] = u;
        s_storedWardrobeValid[agentId] = true;
        HostBroadcast(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
    } else if (s_mode == NET_MODE_CLIENT && s_connected) {
        ClientSendToHost(NET_CH_CONTROL, buf, (int)sizeof(buf), true);
    }
}

int Net_PollWardrobeUpdates(NetWardrobeUpdate *out, int max) {
    if (out == NULL || max <= 0) { s_wardrobeCount = 0; return 0; }
    int n = (s_wardrobeCount < max) ? s_wardrobeCount : max;
    for (int i = 0; i < n; i++) out[i] = s_wardrobeQueue[i];
    s_wardrobeCount = 0;
    return n;
}

static void PushCastEvent(int agentId, int skillIndex, Vector3 aim) {
    if (s_castEventCount < 32) {
        s_castEventQueue[s_castEventCount++] = (NetCastEvent){
            .agentId = agentId, .skillIndex = skillIndex, .aim = aim
        };
    }
}

int Net_PollCastEvents(NetCastEvent *out, int max) {
    if (out == NULL || max <= 0) { s_castEventCount = 0; return 0; }
    int n = (s_castEventCount < max) ? s_castEventCount : max;
    for (int i = 0; i < n; i++) out[i] = s_castEventQueue[i];
    s_castEventCount = 0;
    return n;
}

// --- Host: peers join/leave, drive their heroes ------------------------------

static int HostFindPeer(ENetPeer *ep, void *ref) {
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
        if (!s_peers[i].used) continue;
        if (ep != NULL && s_peers[i].enetPeer == ep) return i;
        if (ref != NULL && s_peers[i].backendRef == ref) return i;
    }
    return -1;
}

// Pick the team with fewer members (host counts on team 0). Tie → team 1,
// so the first joiner lands opposite the host (keeps 1v1 sane pre-lobby).
static unsigned char HostAutoBalanceTeam(void) {
    int t0 = 1, t1 = 0; // host on team 0
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
        if (!s_peers[i].used) continue;
        if (s_peers[i].team == 0) t0++; else t1++;
    }
    return (unsigned char)((t1 <= t0) ? 1 : 0);
}

static void HostSpawnPeerHero(int idx) {
    HostPeer *p = &s_peers[idx];
    if (s_exploreMode) p->team = 0; // Explore Co-op: all players share TEAM_ALLY
    // Always spawn peers at the fixed active map spawn point (independent of
    // where player 1 has moved), fanned out slightly so heroes don't overlap.
    Vector3 pos = MapManager_GetActiveSpawnPoint();
    pos.x += (float)(idx % 4) * 1.8f + 1.8f;
    pos.z += (float)(idx / 4) * 1.8f;
    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
    p->agentId = Entity_SpawnAgent(pos, 100.0f, 0,
                                   (p->team == 0) ? TEAM_ALLY : TEAM_ENEMY,
                                   ARCH_HERO);
    if (p->agentId < 0) return;
    // Same default loadout the local player gets (game/game_screen.c).
    static const struct { const char *name; int element; } kLoadout[AGENT_SKILL_SLOTS] = {
        { "ICE_CRYSTAL_LANCE", 0 }, { "INFERNO_SURGE", 2 }, { "EARTH_DISC_VOLLEY", 3 },
        { "METAL_BLADE_STORM", 4 }, { "VINE_SNARE", 1 },
    };
    for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
        int skillIdx = Skill_GetIndexByName(kLoadout[slot].name);
        if (skillIdx >= 0) Entity_SetEquippedSkill(p->agentId, slot, skillIdx, kLoadout[slot].element);
    }
    // Tell the client which agent is theirs (reliable channel).
    NetCtrl hello = { NET_CTRL_HELLO, NET_PROTOCOL_VERSION, (unsigned char)p->agentId };
    SendToPeer(p, NET_CH_CONTROL, &hello, (int)sizeof(hello), true);
    // Send all known player wardrobes so the newly joined peer sees everyone's outfit.
    for (int a = 0; a < MAX_AGENTS; a++) {
        if (s_storedWardrobeValid[a]) {
            unsigned char wbuf[NET_WARDROBE_PACKET_BYTES];
            PackWardrobePacket(wbuf, a, s_storedWardrobes[a].slotCount, s_storedWardrobes[a].catalogIndices);
            SendToPeer(p, NET_CH_CONTROL, wbuf, (int)sizeof(wbuf), true);
        }
    }
}

static void HostPeerJoined(ENetPeer *ep, void *ref) {
    if (HostFindPeer(ep, ref) >= 0) return; // duplicate connect event — ignore
    int idx = -1;
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++)
        if (!s_peers[i].used) { idx = i; break; }
    if (idx < 0) {
        TraceLog(LOG_WARNING, "[NET] room full — peer ignored");
        return;
    }
    HostPeer *p = &s_peers[idx];
    memset(p, 0, sizeof(*p));
    p->used = true;
    p->enetPeer = ep;
    p->backendRef = ref;
    p->agentId = -1;
    p->team = HostAutoBalanceTeam();
    HostSpawnPeerHero(idx);
    s_connected = true;
    s_hostStateSent = -1; // re-announce the match state to everyone
    HostBroadcastRoster();
    TraceLog(LOG_INFO, "[NET] peer joined slot=%d team=%d hero=%d (players=%d)",
             idx + 1, p->team, p->agentId, Net_GetPeerCount() + 1);
}

static void HostPeerLeft(int idx) {
    if (idx < 0) return;
    HostPeer *p = &s_peers[idx];
    if (p->agentId >= 0) Entity_ApplyDamage(p->agentId, 1e9f, (Vector3){ 0 });
    memset(p, 0, sizeof(*p));
    s_connected = (Net_GetPeerCount() > 0);
    HostBroadcastRoster();
    TraceLog(LOG_INFO, "[NET] peer left slot=%d (players=%d)", idx + 1,
             Net_GetPeerCount() + 1);
}

// Edge-triggered actions fire once per received intent packet; held
// movement integrates every Net_Tick. This intentionally mirrors
// control/Control_Apply's rules (grounded/CC gates; zone cooldown mult is
// Đợt A5).
static void HostApplyRemoteEdges(int idx, const PlayerIntent *in) {
    int agentId = s_peers[idx].agentId;
    const Agent *a = Entity_GetAgent(agentId);
    if (a == NULL) return;

    // Hybrid Client-Authoritative Hero Pose: trust client's smooth local
    // movement within a speed/latency sanity envelope (<= 8.5m) so melee,
    // skills, and monster hitboxes evaluate at the client's true position.
    if (in->hasClientPose && !Entity_IsCrowdControlled(agentId)) {
        float dx = in->clientPos.x - a->position.x;
        float dz = in->clientPos.z - a->position.z;
        if (dx * dx + dz * dz <= 8.5f * 8.5f) {
            Vector3 pos = in->clientPos;
            float gy = MapManager_GetGroundHeightAt(pos.x, pos.z);
            MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &gy);
            if (!in->clientInAir && a->vState == AGENT_GROUNDED) {
                pos.y = gy;
            } else if (pos.y < gy) {
                pos.y = gy;
            }
            Entity_SetPosition(agentId, pos);
            a = Entity_GetAgent(agentId);
            if (a == NULL) return;
        }
        Entity_SetFacingYaw(agentId, in->clientYaw);
    }

    if (in->jump && (a->vState == AGENT_GROUNDED || a->vState == AGENT_JUMPING) &&
        !Entity_IsCrowdControlled(agentId)) {
        float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
        Vector3 hv = { 0 };
        if (len > 0.0001f) {
            hv.x = (in->moveDir.x / len) * REMOTE_MOVE_MPS;
            hv.z = (in->moveDir.y / len) * REMOTE_MOVE_MPS;
        }
        float vForce = (a->vState == AGENT_JUMPING) ? (REMOTE_JUMP_FORCE * 1.05f) : REMOTE_JUMP_FORCE;
        Entity_PerformJump(agentId, vForce, hv);
        Entity_ArmDodgeGrace(agentId, 0.50f);
    }
    if (in->hasClientPose && in->clientInAir) {
        Entity_ArmDodgeGrace(agentId, 0.25f);
    }
    if (in->dash || (in->hasClientPose && in->clientDashing)) {
        Entity_ArmDodgeGrace(agentId, 0.60f);
        if (a->dashTimer <= 0.0f) {
            float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
            Vector3 dir = (len > 0.0001f)
                ? (Vector3){ in->moveDir.x, 0.0f, in->moveDir.y }
                : (Vector3){ sinf(a->facingYaw), 0.0f, cosf(a->facingYaw) };
            Entity_Dash(agentId, dir, REMOTE_DASH_MPS); // gated inside, arms dashTimer i-frames
        }
    }
    if (in->meditate) Entity_StartMeditate(agentId);

    if (in->basicAttack > 0) {
        Vector3 tp = {0}, wp = {0}; int we = -1;
        Entity_ExecuteBasicAttack(agentId,
                                  (BasicAttackType)(in->basicAttack - 1),
                                  &tp, &wp, &we);
        Vector3 swingAim = (tp.x != 0.0f || tp.y != 0.0f || tp.z != 0.0f) ? tp : in->aimPoint;
        int meleeCode = 250 + (in->basicAttack - 1);
        Net_HostNotifyCast(agentId, meleeCode, swingAim);
        PushCastEvent(agentId, meleeCode, swingAim);
    }

    if (in->castSkillSlot >= 0 && in->castSkillSlot < AGENT_SKILL_SLOTS &&
        !Entity_IsStunned(agentId)) {
        int skillIndex = a->equippedSkills[in->castSkillSlot];
        if (skillIndex >= 0 && SkillManager_CanCast(skillIndex, agentId)) {
            if (CastSkill(skillIndex, agentId, a->position, in->aimPoint,
                          (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f })) {
                // Zone rule (Đợt A5): remote heroes get the same cooldown
                // modifier the local player gets from game/'s rule table.
                float cdMult = GameRules_CooldownMult(
                    a->currentElement, Map_QueryZoneAt(a->position));
                SkillManager_TriggerCooldown(skillIndex, agentId, 1.0f * cdMult);
                Net_HostNotifyCast(agentId, skillIndex, in->aimPoint);
                PushCastEvent(agentId, skillIndex, in->aimPoint);
            }
        }
    }
}

static void HostMoveRemotes(float dt) {
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
        if (!s_peers[i].used) continue;
        const Agent *a = (s_peers[i].agentId >= 0) ? Entity_GetAgent(s_peers[i].agentId) : NULL;
        if (a == NULL) {
            if (s_exploreMode) {
                s_peers[i].respawnTimer += dt;
                if (s_peers[i].respawnTimer >= 5.0f) {
                    s_peers[i].respawnTimer = 0.0f;
                    HostSpawnPeerHero(i);
                    HostBroadcastRoster();
                }
            }
            continue;
        }
        s_peers[i].respawnTimer = 0.0f;
        const PlayerIntent *in = &s_peers[i].held;
        // Zone rule (Đợt A5): Mộc ẩn hình trong Rừng — same as the local player.
        Entity_SetStealth(s_peers[i].agentId,
                          GameRules_GrantsStealth(a->currentElement,
                                                  Map_QueryZoneAt(a->position)));
        float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
        if (len > 0.0001f && a->vState == AGENT_GROUNDED &&
            !Entity_IsCrowdControlled(s_peers[i].agentId) && a->dashTimer <= 0.0f) {
            float speed = REMOTE_MOVE_MPS * Entity_GetSpeedMult(s_peers[i].agentId);
            Vector3 pos = a->position;
            pos.x += (in->moveDir.x / len) * speed * dt;
            pos.z += (in->moveDir.y / len) * speed * dt;
            pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
            MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &pos.y);
            Entity_SetPosition(s_peers[i].agentId, pos);
            if (!in->hasClientPose) {
                Entity_SetFacingYaw(s_peers[i].agentId, atan2f(in->moveDir.x, in->moveDir.y));
            }
        }
    }
}

// --- Match outcome sync ------------------------------------------------------

void Net_HostSetMatchState(int state) {
    if (s_mode == NET_MODE_HOST) s_hostMatchState = state;
}

int Net_GetRemoteMatchState(void) {
    return (s_mode == NET_MODE_CLIENT) ? s_remoteMatchState : -1;
}

// --- Client: apply snapshots (interpolated — Đợt A5) --------------------------

static void ClientStoreSnapshot(const unsigned char *data, int len) {
    static bool s_loggedFirst = false;
    int n = Net_UnpackAgentSnapshot(s_snapLatest, MAX_AGENTS, data, len);
    if (n < 0) return;
    s_snapRxWindow++;
    NetPred_OnSnapshot(s_snapLatest, n, s_localHeroId);
    if (!s_loggedFirst) {
        s_loggedFirst = true;
        TraceLog(LOG_INFO, "[NET] first snapshot: %d agents", n);
    }
}

// --- Shared event core (ENet events and backend callbacks both land here) ---

static void HandlePeerConnected(ENetPeer *ep, void *ref) {
    if (s_mode == NET_MODE_HOST) {
        HostPeerJoined(ep, ref);
    } else {
        s_connected = true;
        TraceLog(LOG_INFO, "[NET] connected to host");
    }
}

static void HandlePeerDisconnected(ENetPeer *ep, void *ref) {
    if (s_mode == NET_MODE_HOST) {
        HostPeerLeft(HostFindPeer(ep, ref));
    } else {
        s_connected = false;
        s_localHeroId = -1;
        TraceLog(LOG_INFO, "[NET] disconnected from host");
    }
}

static void HandlePacket(ENetPeer *ep, void *ref, int channel,
                         const unsigned char *data, int len) {
    if (s_mode == NET_MODE_HOST) {
        int idx = HostFindPeer(ep, ref);
        if (idx < 0) {
            // Unknown sender. A JOIN knock registers the peer DIRECTLY —
            // the EOS connection-request notify is NOT a reliable join
            // signal (it can be skipped when the SDK considers the
            // connection already open, e.g. a rejoining client or a stale
            // session), and a knock is proof enough the packet path works.
            if (channel == NET_CH_CONTROL && len == (int)sizeof(NetCtrl) &&
                data[0] == NET_CTRL_JOIN && data[1] == NET_PROTOCOL_VERSION) {
                HandlePeerConnected(ep, ref);
                idx = HostFindPeer(ep, ref);
            }
            if (idx < 0) return; // still a stranger (room full / junk packet)
        }
        s_peers[idx].silentFor = 0.0f; // any packet proves liveness
        if (channel == NET_CH_STREAM) {
            if (len == (int)sizeof(NetCtrl) && data[0] == NET_CTRL_PING &&
                data[1] == NET_PROTOCOL_VERSION) {
                NetCtrl pong = { NET_CTRL_PONG, NET_PROTOCOL_VERSION, data[2] };
                SendToPeer(&s_peers[idx], NET_CH_STREAM, &pong, (int)sizeof(pong), false);
                return;
            }
            PlayerIntent in;
            if (Net_UnpackIntent(&in, data, len)) {
                HostApplyRemoteEdges(idx, &in);
                s_peers[idx].held = in; // held state for per-frame movement
            }
        } else if (channel == NET_CH_CONTROL &&
                   len == NET_LOADOUT_PACKET_BYTES &&
                   data[0] == NET_CTRL_LOADOUT &&
                   data[1] == NET_PROTOCOL_VERSION) {
            // TAB loadout sync (Đợt A5): re-equip their hero; the setter
            // recomputes Vô Hệ so element/Thái Cực stay truthful for all.
            int agentId = s_peers[idx].agentId;
            if (agentId >= 0 && data[2] < AGENT_SKILL_SLOTS)
                Entity_SetEquippedSkill(agentId, data[2], data[3], data[4]);
        } else if (channel == NET_CH_CONTROL &&
                   len == NET_SOCIAL_PACKET_BYTES &&
                   data[0] == NET_CTRL_SOCIAL_EMOTE &&
                   data[1] == NET_PROTOCOL_VERSION) {
            int agentId = s_peers[idx].agentId;
            if (agentId >= 0) {
                bool duel = (data[4] != 0);
                AgentPkMode pkMode = (data[5] <= 2) ? (AgentPkMode)data[5] : (duel ? PK_MODE_SLAUGHTER : PK_MODE_PEACE);
                Entity_SetPkMode(agentId, pkMode);
                PushSocialEvent(agentId, (int)data[3], duel);
                unsigned char fwd[NET_SOCIAL_PACKET_BYTES] = {
                    NET_CTRL_SOCIAL_EMOTE, NET_PROTOCOL_VERSION,
                    (unsigned char)agentId, data[3], data[4], data[5]
                };
                HostBroadcast(NET_CH_CONTROL, fwd, (int)sizeof(fwd), true);
            }
        } else if (channel == NET_CH_CONTROL &&
                   len == NET_TRADE_PACKET_BYTES &&
                   data[0] == NET_CTRL_TRADE_GIFT &&
                   data[1] == NET_PROTOCOL_VERSION) {
            int amt = (int)data[4] | ((int)data[5] << 8);
            PushTradeGift(s_peers[idx].agentId, (int)data[3], amt);
            HostBroadcast(NET_CH_CONTROL, data, len, true);
        } else if (channel == NET_CH_CONTROL &&
                   len == NET_WARDROBE_PACKET_BYTES &&
                   data[0] == NET_CTRL_WARDROBE &&
                   data[1] == NET_PROTOCOL_VERSION) {
            int agentId = s_peers[idx].agentId;
            if (agentId >= 0) {
                int slotCount = (int)data[3];
                PushWardrobeUpdate(agentId, slotCount, data + 4);
                unsigned char fwd[NET_WARDROBE_PACKET_BYTES];
                PackWardrobePacket(fwd, agentId, slotCount, data + 4);
                HostBroadcast(NET_CH_CONTROL, fwd, (int)sizeof(fwd), true);
            }
        } else if (channel == NET_CH_CONTROL &&
                   len == (int)sizeof(NetCtrl) &&
                   data[0] == NET_CTRL_JOIN &&
                   data[1] == NET_PROTOCOL_VERSION) {
            NetCtrl pong = { NET_CTRL_PONG, NET_PROTOCOL_VERSION, data[2] };
            SendToPeer(&s_peers[idx], NET_CH_CONTROL, &pong, (int)sizeof(pong), false);
        }
    } else if (s_mode == NET_MODE_CLIENT) {
        if (channel == NET_CH_CONTROL) {
            if (len == NET_CAST_PACKET_BYTES && data[0] == NET_CTRL_CAST &&
                data[1] == NET_PROTOCOL_VERSION) {
                // VFX replay of a host-side cast (Đợt A5): free-cast mode
                // skips the mana gate (our mirrored mana is post-debit).
                int agentId = data[2], skillIndex = data[3];
                Vector3 aim;
                memcpy(&aim, data + 4, sizeof(Vector3));
                if (agentId == s_localHeroId) {
                    // Client already played its own cast optimistically at 0ms.
                    return;
                }
                const Agent *caster = Entity_GetAgent(agentId);
                if (caster != NULL) {
                    if (skillIndex >= 250 && skillIndex <= 252) {
                        PushCastEvent(agentId, skillIndex, aim);
                    } else {
                        SkillManager_SetFreeCast(true);
                        bool played = CastSkill(skillIndex, agentId, caster->position, aim,
                                                (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f });
                        SkillManager_SetFreeCast(false);
                        if (played) {
                            PushCastEvent(agentId, skillIndex, aim);
                            TraceLog(LOG_INFO, "[NET] cast mirror agent=%d skill=%d", agentId, skillIndex);
                        }
                    }
                }
            } else if (len == NET_KILL_REWARD_PACKET_BYTES && data[0] == NET_CTRL_KILL_REWARD &&
                       data[1] == NET_PROTOCOL_VERSION) {
                if (s_killRewardCount < 32) {
                    NetKillReward *kr = &s_killRewardQueue[s_killRewardCount++];
                    kr->monsterKind = (int)data[2];
                    kr->element     = (int)data[3];
                    kr->isBoss      = (data[4] != 0);
                    kr->gold        = (int)data[5] | ((int)data[6] << 8);
                    kr->xp          = (int)data[7] | ((int)data[8] << 8);
                }
            } else if (len == NET_SOCIAL_PACKET_BYTES && data[0] == NET_CTRL_SOCIAL_EMOTE &&
                       data[1] == NET_PROTOCOL_VERSION) {
                int aid = (int)data[2];
                bool duel = (data[4] != 0);
                AgentPkMode pkMode = (data[5] <= 2) ? (AgentPkMode)data[5] : (duel ? PK_MODE_SLAUGHTER : PK_MODE_PEACE);
                Entity_SetPkMode(aid, pkMode);
                PushSocialEvent(aid, (int)data[3], duel);
            } else if (len == NET_TRADE_PACKET_BYTES && data[0] == NET_CTRL_TRADE_GIFT &&
                       data[1] == NET_PROTOCOL_VERSION) {
                int sender = (int)data[2];
                int amt = (int)data[4] | ((int)data[5] << 8);
                if (sender != s_localHeroId) {
                    PushTradeGift(sender, (int)data[3], amt);
                }
            } else if (len == NET_WARDROBE_PACKET_BYTES && data[0] == NET_CTRL_WARDROBE &&
                       data[1] == NET_PROTOCOL_VERSION) {
                int aid = (int)data[2];
                if (aid != s_localHeroId) {
                    PushWardrobeUpdate(aid, (int)data[3], data + 4);
                }
            } else if (len == (int)sizeof(NetCtrl) && data[0] != 'W') {
                NetCtrl ctrl;
                memcpy(&ctrl, data, sizeof(ctrl));
                if (ctrl.version == NET_PROTOCOL_VERSION) {
                    if (ctrl.type == NET_CTRL_HELLO) {
                        s_localHeroId = ctrl.value;
                        TraceLog(LOG_INFO, "[NET] hero assigned agent=%d", s_localHeroId);
                    } else if (ctrl.type == NET_CTRL_STATE) {
                        s_remoteMatchState = ctrl.value;
                    } else if (ctrl.type == NET_CTRL_START) {
                        s_matchStart = true;
                        TraceLog(LOG_INFO, "[NET] match start received");
                    } else if (ctrl.type == NET_CTRL_PONG && ctrl.value == s_joinSeq) {
                        int rtt = (int)((GetTime() - s_lastJoinSendAt) * 1000.0);
                        if (rtt >= 0 && rtt < 5000) {
                            s_pingMs = (s_pingMs > 0) ? ((s_pingMs * 3 + rtt) / 4) : rtt;
                        }
                    }
                }
            } else {
                int n = Net_UnpackRoster(s_clientRoster, NET_MAX_PLAYERS, data, len);
                if (n >= 0 && n != s_clientRosterCount)
                    TraceLog(LOG_INFO, "[NET] roster: %d players", n);
                if (n >= 0) s_clientRosterCount = n;
            }
        } else if (channel == NET_CH_STREAM) {
            if (len == (int)sizeof(NetCtrl) && data[0] == NET_CTRL_PONG &&
                data[1] == NET_PROTOCOL_VERSION) {
                unsigned char pseq = data[2];
                double sentAt = s_pingSendAt[pseq];
                if (sentAt > 0.0) {
                    int rtt = (int)((GetTime() - sentAt) * 1000.0 + 0.5);
                    if (rtt >= 1 && rtt < 2000) {
                        // Minimum-filter over the last 24 PONGs (~1.5s window):
                        // eliminates Host/Client render-frame phase wait (which
                        // only adds positive delay) and locks onto true wire RTT!
                        s_rttRing[s_rttRingIdx] = rtt;
                        s_rttRingIdx = (s_rttRingIdx + 1) % 24;
                        if (s_rttRingCount < 24) s_rttRingCount++;
                        int minRtt = s_rttRing[0];
                        for (int k = 1; k < s_rttRingCount; k++) {
                            if (s_rttRing[k] < minRtt) minRtt = s_rttRing[k];
                        }
                        if (s_pingMs <= 0 || minRtt <= s_pingMs) {
                            s_pingMs = minRtt;
                        } else {
                            s_pingMs = (s_pingMs * 3 + minRtt) / 4;
                        }
                    }
                }
                return;
            }
            // Piggyback a fast PING probe onto every arriving snapshot:
            // since a snapshot means the Host JUST called EosSend/EOS_Platform_Tick,
            // the Host's main thread is guaranteed to be awake in its update phase!
            {
                s_joinSeq++;
                double nowT = GetTime();
                s_lastJoinSendAt = nowT;
                s_pingSendAt[s_joinSeq] = nowT;
                NetCtrl ping = { NET_CTRL_PING, NET_PROTOCOL_VERSION, s_joinSeq };
                ClientSendToHost(NET_CH_STREAM, &ping, (int)sizeof(ping), false);
            }
            ClientStoreSnapshot(data, len);
        }
    }
}

// --- Backend entry points (net_transport_internal.h) ------------------------

void NetTransport_BackendConnected(void *peerRef)    { HandlePeerConnected(NULL, peerRef); }
void NetTransport_BackendDisconnected(void *peerRef) { HandlePeerDisconnected(NULL, peerRef); }
void NetTransport_BackendPacket(void *peerRef, int channel,
                                const unsigned char *data, int len) {
    HandlePacket(NULL, peerRef, channel, data, len);
}

void NetTransport_SetBackend(NetMode mode, NetBackendSendFn send,
                             NetBackendTickFn tick, NetBackendStopFn stop) {
    s_mode = mode;
    s_backendSend = send;
    s_backendTick = tick;
    s_backendStop = stop;
}

// --- Tick -------------------------------------------------------------------

void Net_PumpSubFrame(void) {
    if (s_mode == NET_MODE_OFF) return;
    if (s_backendTick != NULL) {
        s_backendTick(0.0f);
    } else if (s_host != NULL) {
        ENetEvent ev;
        while (enet_host_service(s_host, &ev, 0) > 0) {
            switch (ev.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    HandlePeerConnected(ev.peer, NULL);
                    break;
                case ENET_EVENT_TYPE_DISCONNECT:
                    HandlePeerDisconnected(ev.peer, NULL);
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                    HandlePacket(ev.peer, NULL, (int)ev.channelID,
                                 ev.packet->data, (int)ev.packet->dataLength);
                    enet_packet_destroy(ev.packet);
                    break;
                default: break;
            }
        }
    }
}

void Net_Tick(float dt) {
    if (s_mode == NET_MODE_OFF) return;
    Net_PumpSubFrame();
    if (s_backendTick == NULL && s_host == NULL) return;

    if (!s_connected) return;
    s_sendAccum += dt;

    if (s_mode == NET_MODE_HOST) {
        // Liveness: the ENet path raises real DISCONNECT events, but a
        // killed EOS peer just goes silent — sweep them out ourselves.
        // (Backend path only; ENet's own 5s timeout handles its peers.)
        if (s_backendSend != NULL) {
            for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
                if (!s_peers[i].used) continue;
                s_peers[i].silentFor += dt;
                if (s_peers[i].silentFor > NET_PEER_TIMEOUT) {
                    TraceLog(LOG_INFO, "[NET] peer slot=%d timed out", i + 1);
                    HostPeerLeft(i);
                }
            }
        }
        HostMoveRemotes(dt);
        if (s_hostMatchState != s_hostStateSent && s_hostMatchState >= 0) {
            NetCtrl st = { NET_CTRL_STATE, NET_PROTOCOL_VERSION, (unsigned char)s_hostMatchState };
            HostBroadcast(NET_CH_CONTROL, &st, (int)sizeof(st), true);
            s_hostStateSent = s_hostMatchState;
        }
        if (s_sendAccum >= 1.0f / NET_SNAPSHOT_HZ) {
            s_sendAccum = 0.0f;
            static unsigned char buf[16 * 1024];
            float cullRadius = s_exploreMode ? NET_INTEREST_RADIUS : 65.0f;
            // Always send per-client priority-culled single-packet snapshots
            // (all Heroes + Bosses first, then up to 54 closest/attacking
            // monsters <= 60 total entities = 1084B <= 1167B EOS MTU) so
            // snapshots never fragment in either Open World or MOBA.
            for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
                if (!s_peers[i].used) continue;
                const Agent *pa = (s_peers[i].agentId >= 0) ? Entity_GetAgent(s_peers[i].agentId) : NULL;
                Vector3 focus = pa ? pa->position : MapManager_GetActiveSpawnPoint();
                int bytes = Net_PackAgentSnapshotFor(focus, cullRadius, buf, (int)sizeof(buf));
                if (bytes > 0) SendToPeer(&s_peers[i], NET_CH_STREAM, buf, bytes, false);
            }
        }
    } else { // client
        s_snapRxTimer += dt;
        if (s_snapRxTimer >= 1.0f) {
            s_snapRxHz = s_snapRxWindow;
            s_snapRxWindow = 0;
            s_snapRxTimer = 0.0f;
        }
        NetPred_Tick(dt, s_localHeroId); // 60 FPS smooth interpolation & prediction reconciliation
        // 1Hz knock/heartbeat on control channel + 2Hz pure-UDP ping probe on
        // stream channel (measures true UDP datagram RTT without SCTP queue jitter).
        {
            static float s_joinAccum = 1e9f; // first knock immediately
            static float s_pingAccum = 0.0f;
            s_joinAccum += dt;
            s_pingAccum += dt;
            if (s_joinAccum >= 1.0f) {
                s_joinAccum = 0.0f;
                NetCtrl join = { NET_CTRL_JOIN, NET_PROTOCOL_VERSION, 0 };
                ClientSendToHost(NET_CH_CONTROL, &join, (int)sizeof(join), true);
            }
            if (s_pingAccum >= 0.25f) {
                s_pingAccum = 0.0f;
                s_joinSeq++;
                double nowT = GetTime();
                s_lastJoinSendAt = nowT;
                s_pingSendAt[s_joinSeq] = nowT;
                NetCtrl ping = { NET_CTRL_PING, NET_PROTOCOL_VERSION, s_joinSeq };
                ClientSendToHost(NET_CH_STREAM, &ping, (int)sizeof(ping), false);
            }
        }
        if (s_clientIntentValid && s_sendAccum >= 1.0f / NET_INTENT_HZ) {
            s_sendAccum = 0.0f;
            unsigned char buf[128];
            int bytes = Net_PackIntent(&s_clientIntent, buf, (int)sizeof(buf));
            if (bytes > 0) ClientSendToHost(NET_CH_STREAM, buf, bytes, false);
            // Edge flags must not re-fire on the host — clear them until the
            // next real press updates the queued intent.
            s_clientIntent.jump = s_clientIntent.dash = s_clientIntent.meditate = false;
            s_clientIntent.castSkillSlot = -1;
            s_clientIntent.basicAttack = 0;
        }
    }
}
