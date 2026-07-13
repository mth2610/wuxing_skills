// net/net_transport.c — transport core over net/net.h's wire formats.
// Protocol v3: multi-peer (up to NET_MAX_PLAYERS-1 = 7 remote players → 4v4).
// Two packet paths share this core: ENet (built-in, LAN) and a pluggable
// backend (net_transport_internal.h — EOS for internet play). Main-thread
// polling (the side thread lands with the jitter pass), no client-side
// prediction.
#include "net/net_transport.h"
#include "net/net_transport_internal.h"
#include "net/net.h"
#include "entities/entities.h"
#include "core/skill_manager.h"
#include <enet/enet.h>
#include "raylib.h" // TraceLog only — no rendering in this module
#include <math.h>
#include <stddef.h>
#include <string.h>

// Channels: 0 = unreliable stream (intents / snapshots), 1 = reliable
// control (hello / match state / roster).
#define NET_CH_STREAM  0
#define NET_CH_CONTROL 1

#define NET_SNAPSHOT_HZ 20.0f
#define NET_INTENT_HZ   30.0f

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
typedef struct { unsigned char type, version, value; } NetCtrl;

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
} HostPeer;
static HostPeer s_peers[NET_MAX_PLAYERS - 1];

static int   s_localHeroId = -1;     // client: our hero's host-pool id
static float s_sendAccum = 0.0f;

static PlayerIntent s_clientIntent = { 0 }; // client: queued local intent
static bool s_clientIntentValid = false;

static int s_hostMatchState = -1;   // host: last state game/ reported
static int s_hostStateSent  = -1;   // host: last state actually sent
static int s_remoteMatchState = -1; // client: host's match state

static NetRosterEntry s_clientRoster[NET_MAX_PLAYERS]; // client: last broadcast
static int            s_clientRosterCount = 0;

static unsigned char s_hostTeam = 0;     // lobby: the host player's own side
static int           s_botCount[2] = { 0, 0 }; // lobby: bots per side
static bool          s_matchStart = false;     // START received/armed, unread

static const float REMOTE_MOVE_MPS = 3.5f;  // mirror control/'s walk speed
static const float REMOTE_JUMP_FORCE = 4.0f;
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
}

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
    s_clientIntent = *intent;
    s_clientIntentValid = true;
}

// --- Roster ------------------------------------------------------------------

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
        for (int b = 0; b < s_botCount[t] && n < maxEntries; b++)
            out[n++] = (NetRosterEntry){ (unsigned char)n, (unsigned char)t,
                NET_ROSTER_NONE, NET_ROSTER_OCCUPIED | NET_ROSTER_BOT };
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
    // Fan slots out around the base spawn so heroes never stack.
    Vector3 pos = REMOTE_SPAWN;
    pos.x += (float)(idx % 4) * 1.6f - 2.4f;
    pos.z += (float)(idx / 4) * 1.6f;
    p->agentId = Entity_SpawnAgent(pos, 100.0f, 0,
                                   (p->team == 0) ? TEAM_ALLY : TEAM_ENEMY,
                                   ARCH_HERO);
    if (p->agentId < 0) return;
    // Same default loadout the local player gets (game/game_screen.c).
    static const struct { const char *name; int element; } kLoadout[AGENT_SKILL_SLOTS] = {
        { "GLACIAL_CANNON", 0 }, { "FIRE", 2 }, { "STONE_PRISON", 3 }, { "LEAF_WHIRLWIND", 1 },
    };
    for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
        int skillIdx = Skill_GetIndexByName(kLoadout[slot].name);
        if (skillIdx >= 0) Entity_SetEquippedSkill(p->agentId, slot, skillIdx, kLoadout[slot].element);
    }
    // Tell the client which agent is theirs (reliable channel).
    NetCtrl hello = { NET_CTRL_HELLO, NET_PROTOCOL_VERSION, (unsigned char)p->agentId };
    SendToPeer(p, NET_CH_CONTROL, &hello, (int)sizeof(hello), true);
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

    if (in->jump && a->vState == AGENT_GROUNDED && !Entity_IsCrowdControlled(agentId)) {
        float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
        Vector3 hv = { 0 };
        if (len > 0.0001f) {
            hv.x = (in->moveDir.x / len) * REMOTE_MOVE_MPS;
            hv.z = (in->moveDir.y / len) * REMOTE_MOVE_MPS;
        }
        Entity_ApplyLaunch(agentId, REMOTE_JUMP_FORCE, hv);
    }
    if (in->dash) {
        Vector3 dir = { in->moveDir.x, 0.0f, in->moveDir.y };
        Entity_Dash(agentId, dir, REMOTE_DASH_MPS); // gated inside
    }
    if (in->meditate) Entity_StartMeditate(agentId);

    if (in->basicAttack > 0) {
        // Melee for the remote hero — gameplay only; its VFX/anim are the
        // client's local feedback (event mirroring is Đợt A5).
        Vector3 tp, wp; int we;
        Entity_ExecuteBasicAttack(agentId,
                                  (BasicAttackType)(in->basicAttack - 1),
                                  &tp, &wp, &we);
    }

    if (in->castSkillSlot >= 0 && in->castSkillSlot < AGENT_SKILL_SLOTS &&
        !Entity_IsStunned(agentId)) {
        int skillIndex = a->equippedSkills[in->castSkillSlot];
        if (skillIndex >= 0 && SkillManager_CanCast(skillIndex, agentId)) {
            if (CastSkill(skillIndex, agentId, a->position, in->aimPoint,
                          (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f })) {
                SkillManager_TriggerCooldown(skillIndex, agentId, 1.0f);
            }
        }
    }
}

static void HostMoveRemotes(float dt) {
    for (int i = 0; i < NET_MAX_PLAYERS - 1; i++) {
        if (!s_peers[i].used || s_peers[i].agentId < 0) continue;
        const PlayerIntent *in = &s_peers[i].held;
        const Agent *a = Entity_GetAgent(s_peers[i].agentId);
        if (a == NULL) continue;
        float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
        if (len > 0.0001f && a->vState == AGENT_GROUNDED &&
            !Entity_IsCrowdControlled(s_peers[i].agentId) && a->dashTimer <= 0.0f) {
            float speed = REMOTE_MOVE_MPS * Entity_GetSpeedMult(s_peers[i].agentId);
            Vector3 pos = a->position;
            pos.x += (in->moveDir.x / len) * speed * dt;
            pos.z += (in->moveDir.y / len) * speed * dt;
            Entity_SetPosition(s_peers[i].agentId, pos);
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

// --- Client: apply snapshots -------------------------------------------------

static void ClientApplySnapshot(const unsigned char *data, int len) {
    static NetAgentState agents[MAX_AGENTS];
    static bool s_loggedFirst = false;
    int n = Net_UnpackAgentSnapshot(agents, MAX_AGENTS, data, len);
    if (n < 0) return;
    if (!s_loggedFirst) {
        s_loggedFirst = true;
        TraceLog(LOG_INFO, "[NET] first snapshot: %d agents", n);
    }
    Entity_NetSyncBegin();
    for (int i = 0; i < n; i++) {
        Entity_NetSyncAgent(agents[i].agentId, agents[i].position,
                            agents[i].health, agents[i].maxHealth,
                            agents[i].mana, agents[i].maxMana,
                            agents[i].element,
                            (AgentTeam)agents[i].team,
                            (AgentArchetype)agents[i].archetype,
                            (agents[i].flags & 0x1) != 0,
                            (agents[i].flags & 0x2) != 0,
                            (agents[i].flags & 0x4) != 0);
    }
    Entity_NetSyncEnd();
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
    if (s_mode == NET_MODE_HOST && channel == NET_CH_STREAM) {
        int idx = HostFindPeer(ep, ref);
        if (idx < 0) return; // stranger — not in the room
        PlayerIntent in;
        if (Net_UnpackIntent(&in, data, len)) {
            HostApplyRemoteEdges(idx, &in);
            s_peers[idx].held = in; // held state for per-frame movement
        }
    } else if (s_mode == NET_MODE_CLIENT) {
        if (channel == NET_CH_CONTROL) {
            if (len == (int)sizeof(NetCtrl) && data[0] != 'W') {
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
                    }
                }
            } else {
                int n = Net_UnpackRoster(s_clientRoster, NET_MAX_PLAYERS, data, len);
                if (n >= 0 && n != s_clientRosterCount)
                    TraceLog(LOG_INFO, "[NET] roster: %d players", n);
                if (n >= 0) s_clientRosterCount = n;
            }
        } else if (channel == NET_CH_STREAM) {
            ClientApplySnapshot(data, len);
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

void Net_Tick(float dt) {
    if (s_mode == NET_MODE_OFF) return;

    if (s_backendTick != NULL) {
        s_backendTick(dt); // pumps the backend; events land in Handle* above
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
    } else {
        return;
    }

    if (!s_connected) return;
    s_sendAccum += dt;

    if (s_mode == NET_MODE_HOST) {
        HostMoveRemotes(dt);
        if (s_hostMatchState != s_hostStateSent && s_hostMatchState >= 0) {
            NetCtrl st = { NET_CTRL_STATE, NET_PROTOCOL_VERSION, (unsigned char)s_hostMatchState };
            HostBroadcast(NET_CH_CONTROL, &st, (int)sizeof(st), true);
            s_hostStateSent = s_hostMatchState;
        }
        if (s_sendAccum >= 1.0f / NET_SNAPSHOT_HZ) {
            s_sendAccum = 0.0f;
            static unsigned char buf[16 * 1024];
            int bytes = Net_PackAgentSnapshot(buf, (int)sizeof(buf));
            if (bytes > 0) HostBroadcast(NET_CH_STREAM, buf, bytes, false);
        }
    } else { // client
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
