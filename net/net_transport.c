// net/net_transport.c — ENet layer over net/net.h's wire formats.
// v1: single peer (1v1), main-thread polling (enet_host_service timeout 0 —
// the roadmap's side-thread lands with the jitter pass), no client-side
// prediction.
#include "net/net_transport.h"
#include "net/net.h"
#include "entities/entities.h"
#include "core/skill_manager.h"
#include <enet/enet.h>
#include "raylib.h" // TraceLog only — no rendering in this module
#include <math.h>
#include <stddef.h>
#include <string.h>

// Channels: 0 = unreliable stream (intents / snapshots), 1 = reliable
// control (hello packet assigning the client's hero agent id).
#define NET_CH_STREAM  0
#define NET_CH_CONTROL 1

#define NET_SNAPSHOT_HZ 20.0f
#define NET_INTENT_HZ   30.0f

// Remote hero spawn (host side) — mirrors game/game_screen.c's match world
// (VERDANT_PATH plateau), opposite the host player's spawn.
static const Vector3 REMOTE_SPAWN = { 54.0f, 0.0f, 41.0f };

// Reliable control-channel packets: [type][version][value].
#define NET_CTRL_HELLO 1  // value = client's hero agent id (host pool)
#define NET_CTRL_STATE 2  // value = host's GameState (match outcome sync)
typedef struct { unsigned char type, version, value; } NetCtrl;

static NetMode   s_mode = NET_MODE_OFF;
static ENetHost *s_host = NULL;   // our endpoint (both roles)
static ENetPeer *s_peer = NULL;   // the single remote peer
static bool      s_connected = false;
static bool      s_enetReady = false;

static int   s_remoteAgentId = -1;   // host: the remote hero in OUR pool
static int   s_localHeroId = -1;     // client: our hero's host-pool id
static float s_sendAccum = 0.0f;

static PlayerIntent s_remoteIntent = { 0 }; // host: last held state from client
static PlayerIntent s_clientIntent = { 0 }; // client: queued local intent
static bool s_clientIntentValid = false;

static int s_hostMatchState = -1;   // host: last state game/ reported
static int s_hostStateSent  = -1;   // host: last state actually sent
static int s_remoteMatchState = -1; // client: host's match state

static const float REMOTE_MOVE_MPS = 3.5f;  // mirror control/'s walk speed
static const float REMOTE_JUMP_FORCE = 4.0f;
static const float REMOTE_DASH_MPS = 10.0f;

static bool EnsureEnet(void) {
    if (!s_enetReady) s_enetReady = (enet_initialize() == 0);
    return s_enetReady;
}

bool Net_StartHost(int port) {
    if (s_mode != NET_MODE_OFF || !EnsureEnet()) return false;
    ENetAddress addr = { 0 };
    addr.host = ENET_HOST_ANY;
    addr.port = (enet_uint16)((port > 0) ? port : NET_DEFAULT_PORT);
    s_host = enet_host_create(&addr, 1 /*peer*/, 2 /*channels*/, 0, 0);
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
    s_peer = enet_host_connect(s_host, &addr, 2, 0);
    if (s_peer == NULL) { enet_host_destroy(s_host); s_host = NULL; return false; }
    s_mode = NET_MODE_CLIENT;
    return true;
}

void Net_Stop(void) {
    if (s_peer && s_connected) enet_peer_disconnect_now(s_peer, 0);
    if (s_host) enet_host_destroy(s_host);
    s_host = NULL;
    s_peer = NULL;
    s_connected = false;
    s_mode = NET_MODE_OFF;
    s_remoteAgentId = -1;
    s_localHeroId = -1;
    s_clientIntentValid = false;
    s_hostMatchState = s_hostStateSent = s_remoteMatchState = -1;
}

NetMode Net_GetMode(void) { return s_mode; }
bool Net_IsPeerConnected(void) { return s_connected; }
bool Net_ClientDrivesWorld(void) { return s_mode == NET_MODE_CLIENT && s_connected; }
int Net_GetLocalHeroAgentId(void) { return (s_mode == NET_MODE_CLIENT) ? s_localHeroId : -1; }
int Net_GetRemoteAgentId(void) { return (s_mode == NET_MODE_HOST) ? s_remoteAgentId : -1; }

void Net_ClientSubmitIntent(const PlayerIntent *intent) {
    if (intent == NULL || s_mode != NET_MODE_CLIENT) return;
    s_clientIntent = *intent;
    s_clientIntentValid = true;
}

// --- Host: spawn + drive the remote hero -----------------------------------

static void HostSpawnRemoteHero(void) {
    s_remoteAgentId = Entity_SpawnAgent(REMOTE_SPAWN, 100.0f, 0, TEAM_ENEMY, ARCH_HERO);
    if (s_remoteAgentId < 0) return;
    // Same default loadout the local player gets (game/game_screen.c).
    static const struct { const char *name; int element; } kLoadout[AGENT_SKILL_SLOTS] = {
        { "GLACIAL_CANNON", 0 }, { "FIRE", 2 }, { "STONE_PRISON", 3 }, { "LEAF_WHIRLWIND", 1 },
    };
    for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
        int idx = Skill_GetIndexByName(kLoadout[slot].name);
        if (idx >= 0) Entity_SetEquippedSkill(s_remoteAgentId, slot, idx, kLoadout[slot].element);
    }
    // Tell the client which agent is theirs (reliable channel).
    NetCtrl hello = { NET_CTRL_HELLO, NET_PROTOCOL_VERSION, (unsigned char)s_remoteAgentId };
    ENetPacket *pkt = enet_packet_create(&hello, sizeof(hello), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(s_peer, NET_CH_CONTROL, pkt);
    s_hostStateSent = -1; // re-announce the match state to the fresh peer
}

void Net_HostSetMatchState(int state) {
    if (s_mode == NET_MODE_HOST) s_hostMatchState = state;
}

int Net_GetRemoteMatchState(void) {
    return (s_mode == NET_MODE_CLIENT) ? s_remoteMatchState : -1;
}

// Edge-triggered actions fire once per received intent packet; held
// movement integrates every Net_Tick. This intentionally mirrors
// control/Control_Apply's rules (grounded/CC gates, cooldown mult omitted —
// zone rules are host-side polish for a later pass).
static void HostApplyRemoteEdges(const PlayerIntent *in) {
    const Agent *a = Entity_GetAgent(s_remoteAgentId);
    if (a == NULL) return;

    if (in->jump && a->vState == AGENT_GROUNDED && !Entity_IsCrowdControlled(s_remoteAgentId)) {
        float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
        Vector3 hv = { 0 };
        if (len > 0.0001f) {
            hv.x = (in->moveDir.x / len) * REMOTE_MOVE_MPS;
            hv.z = (in->moveDir.y / len) * REMOTE_MOVE_MPS;
        }
        Entity_ApplyLaunch(s_remoteAgentId, REMOTE_JUMP_FORCE, hv);
    }
    if (in->dash) {
        Vector3 dir = { in->moveDir.x, 0.0f, in->moveDir.y };
        Entity_Dash(s_remoteAgentId, dir, REMOTE_DASH_MPS); // gated inside
    }
    if (in->meditate) Entity_StartMeditate(s_remoteAgentId);

    if (in->basicAttack > 0) {
        // Melee for the remote hero — gameplay only; its VFX/anim are the
        // client's local feedback (v1: no event mirroring yet).
        Vector3 tp, wp; int we;
        Entity_ExecuteBasicAttack(s_remoteAgentId,
                                  (BasicAttackType)(in->basicAttack - 1),
                                  &tp, &wp, &we);
    }

    if (in->castSkillSlot >= 0 && in->castSkillSlot < AGENT_SKILL_SLOTS &&
        !Entity_IsStunned(s_remoteAgentId)) {
        int skillIndex = a->equippedSkills[in->castSkillSlot];
        if (skillIndex >= 0 && SkillManager_CanCast(skillIndex, s_remoteAgentId)) {
            if (CastSkill(skillIndex, s_remoteAgentId, a->position, in->aimPoint,
                          (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f })) {
                SkillManager_TriggerCooldown(skillIndex, s_remoteAgentId, 1.0f);
            }
        }
    }
}

static void HostMoveRemote(float dt) {
    const Agent *a = Entity_GetAgent(s_remoteAgentId);
    if (a == NULL) return;
    float len = sqrtf(s_remoteIntent.moveDir.x * s_remoteIntent.moveDir.x +
                      s_remoteIntent.moveDir.y * s_remoteIntent.moveDir.y);
    if (len > 0.0001f && a->vState == AGENT_GROUNDED &&
        !Entity_IsCrowdControlled(s_remoteAgentId) && a->dashTimer <= 0.0f) {
        float speed = REMOTE_MOVE_MPS * Entity_GetSpeedMult(s_remoteAgentId);
        Vector3 pos = a->position;
        pos.x += (s_remoteIntent.moveDir.x / len) * speed * dt;
        pos.z += (s_remoteIntent.moveDir.y / len) * speed * dt;
        Entity_SetPosition(s_remoteAgentId, pos);
    }
}

// --- Client: apply snapshots ------------------------------------------------

static void ClientApplySnapshot(const unsigned char *data, int len) {
    static NetAgentState agents[MAX_AGENTS];
    int n = Net_UnpackAgentSnapshot(agents, MAX_AGENTS, data, len);
    if (n < 0) return;
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

// --- Tick -------------------------------------------------------------------

void Net_Tick(float dt) {
    if (s_mode == NET_MODE_OFF || s_host == NULL) return;

    ENetEvent ev;
    while (enet_host_service(s_host, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT:
                s_connected = true;
                TraceLog(LOG_INFO, "[NET] peer connected (%s)",
                         s_mode == NET_MODE_HOST ? "host" : "client");
                if (s_mode == NET_MODE_HOST) {
                    s_peer = ev.peer;
                    HostSpawnRemoteHero();
                    TraceLog(LOG_INFO, "[NET] remote hero agent=%d", s_remoteAgentId);
                }
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                s_connected = false;
                TraceLog(LOG_INFO, "[NET] peer disconnected");
                if (s_mode == NET_MODE_HOST && s_remoteAgentId >= 0) {
                    Entity_ApplyDamage(s_remoteAgentId, 1e9f, (Vector3){ 0 });
                    s_remoteAgentId = -1;
                }
                if (s_mode == NET_MODE_CLIENT) s_localHeroId = -1;
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                if (s_mode == NET_MODE_HOST && ev.channelID == NET_CH_STREAM) {
                    PlayerIntent in;
                    if (Net_UnpackIntent(&in, ev.packet->data, (int)ev.packet->dataLength)) {
                        HostApplyRemoteEdges(&in);
                        s_remoteIntent = in; // held state for per-frame movement
                    }
                } else if (s_mode == NET_MODE_CLIENT) {
                    if (ev.channelID == NET_CH_CONTROL &&
                        ev.packet->dataLength == sizeof(NetCtrl)) {
                        NetCtrl ctrl;
                        memcpy(&ctrl, ev.packet->data, sizeof(ctrl));
                        if (ctrl.version == NET_PROTOCOL_VERSION) {
                            if (ctrl.type == NET_CTRL_HELLO) s_localHeroId = ctrl.value;
                            else if (ctrl.type == NET_CTRL_STATE) s_remoteMatchState = ctrl.value;
                        }
                    } else if (ev.channelID == NET_CH_STREAM) {
                        ClientApplySnapshot(ev.packet->data, (int)ev.packet->dataLength);
                    }
                }
                enet_packet_destroy(ev.packet);
                break;
            default: break;
        }
    }

    if (!s_connected) return;
    s_sendAccum += dt;

    if (s_mode == NET_MODE_HOST) {
        HostMoveRemote(dt);
        if (s_hostMatchState != s_hostStateSent && s_hostMatchState >= 0) {
            NetCtrl st = { NET_CTRL_STATE, NET_PROTOCOL_VERSION, (unsigned char)s_hostMatchState };
            ENetPacket *pkt = enet_packet_create(&st, sizeof(st), ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(s_peer, NET_CH_CONTROL, pkt);
            s_hostStateSent = s_hostMatchState;
        }
        if (s_sendAccum >= 1.0f / NET_SNAPSHOT_HZ) {
            s_sendAccum = 0.0f;
            static unsigned char buf[16 * 1024];
            int bytes = Net_PackAgentSnapshot(buf, (int)sizeof(buf));
            if (bytes > 0) {
                ENetPacket *pkt = enet_packet_create(buf, (size_t)bytes, 0 /*unreliable*/);
                enet_peer_send(s_peer, NET_CH_STREAM, pkt);
            }
        }
    } else { // client
        if (s_clientIntentValid && s_sendAccum >= 1.0f / NET_INTENT_HZ) {
            s_sendAccum = 0.0f;
            unsigned char buf[128];
            int bytes = Net_PackIntent(&s_clientIntent, buf, (int)sizeof(buf));
            if (bytes > 0) {
                ENetPacket *pkt = enet_packet_create(buf, (size_t)bytes, 0);
                enet_peer_send(s_peer, NET_CH_STREAM, pkt);
            }
            // Edge flags must not re-fire on the host — clear them until the
            // next real press updates the queued intent.
            s_clientIntent.jump = s_clientIntent.dash = s_clientIntent.meditate = false;
            s_clientIntent.castSkillSlot = -1;
            s_clientIntent.basicAttack = 0;
        }
    }
}
