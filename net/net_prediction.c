// net/net_prediction.c
#include "net/net_prediction.h"
#include "maps/map_manager.h"
#include <math.h>
#include <string.h>

typedef struct {
    bool    hasTarget;
    Vector3 targetPos;
} NetInterpSlot;

static NetInterpSlot s_interp[MAX_AGENTS];

// Client-side local kinematic state for 60 FPS Khinh Công & dash (since Entity_Update
// is skipped on clients when Net_ClientDrivesWorld() is true).
static bool  s_localInAir      = false;
static int   s_localAirJumps   = 0;
static int   s_localJumpFired  = 0;
static float s_localVelX       = 0.0f;
static float s_localVelY       = 0.0f;
static float s_localVelZ       = 0.0f;
static float s_localDashTimer  = 0.0f;
static float s_localDashCd     = 0.0f;
static float s_localDashVx     = 0.0f;
static float s_localDashVz     = 0.0f;

void NetPred_Reset(void) {
    memset(s_interp, 0, sizeof(s_interp));
    s_localInAir     = false;
    s_localAirJumps  = 0;
    s_localJumpFired = 0;
    s_localVelX      = 0.0f;
    s_localVelY      = 0.0f;
    s_localVelZ      = 0.0f;
    s_localDashTimer = 0.0f;
    s_localDashCd    = 0.0f;
    s_localDashVx    = 0.0f;
    s_localDashVz    = 0.0f;
}

bool NetPred_IsLocalAirborne(float *outVelY) {
    if (outVelY) *outVelY = s_localVelY;
    return s_localInAir;
}

int NetPred_ConsumeLocalJumpFired(void) {
    int j = s_localJumpFired;
    s_localJumpFired = 0;
    return j;
}

void NetPred_OnSnapshot(const NetAgentState *states, int count, int localHeroId) {
    if (!states || count < 0) return;

    Entity_NetSyncBegin();
    for (int i = 0; i < count; i++) {
        const NetAgentState *s = &states[i];
        int id = (int)s->agentId;
        if (id < 0 || id >= MAX_AGENTS) continue;

        const Agent *cur = Entity_GetAgent(id);
        Vector3 syncPos = s->position;

        if (cur != NULL && s_interp[id].hasTarget) {
            float dx = s->position.x - cur->position.x;
            float dy = s->position.y - cur->position.y;
            float dz = s->position.z - cur->position.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (id == localHeroId) {
                // Client-side prediction reconciliation:
                // Never hard-snap local movement or Khinh Công arcs on small RTT drift;
                // only snap on true teleports/respawns (>= 8m), and let NetPred_Tick
                // gently bleed off any smaller drift once grounded.
                float hDistSq = dx * dx + dz * dz;
                if (hDistSq < 64.0f) {
                    syncPos = cur->position;
                }
            } else {
                // Remote entities: keep current visual pos, update interpolation target.
                if (distSq < 25.0f) {
                    syncPos = cur->position;
                }
            }
        }

        s_interp[id].hasTarget = true;
        s_interp[id].targetPos = s->position;

        Entity_NetSyncAgent(id, syncPos, s->health, s->maxHealth,
                            s->mana, s->maxMana, s->element,
                            (AgentTeam)s->team, (AgentArchetype)s->archetype,
                            (s->flags & 1) != 0, (s->flags & 2) != 0,
                            (s->flags & 4) != 0, (s->flags & 8) != 0);
        int pkModeVal = (s->flags >> 4) & 0x3;
        if (pkModeVal <= 2) Entity_SetPkMode(id, (AgentPkMode)pkModeVal);
    }
    Entity_NetSyncEnd();

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!Entity_GetAgent(i)) s_interp[i].hasTarget = false;
    }
}

void NetPred_OnLocalIntent(int localHeroId, const PlayerIntent *in) {
    if (localHeroId < 0 || !in) return;
    const Agent *a = Entity_GetAgent(localHeroId);
    if (!a || Entity_IsCrowdControlled(localHeroId)) return;

    float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
    float speed = 6.4f * Entity_GetSpeedMult(localHeroId);
    if (in->jump && (!s_localInAir || s_localAirJumps < 2)) {
        if (len > 0.0001f) {
            s_localVelX = (in->moveDir.x / len) * speed;
            s_localVelZ = (in->moveDir.y / len) * speed;
        } else if (!s_localInAir) {
            s_localVelX = 0.0f;
            s_localVelZ = 0.0f;
        }
        s_localVelY = (s_localInAir && s_localAirJumps > 0) ? (4.6f * 1.05f) : 4.6f;
        s_localInAir = true;
        s_localAirJumps++;
        s_localJumpFired = s_localAirJumps;
    }
    if (in->dash && s_localDashCd <= 0.0f) {
        float dx = (len > 0.0001f) ? (in->moveDir.x / len) : sinf(Control_GetYaw());
        float dz = (len > 0.0001f) ? (in->moveDir.y / len) : cosf(Control_GetYaw());
        s_localDashVx = dx * 10.0f;
        s_localDashVz = dz * 10.0f;
        s_localDashTimer = 0.15f;
        s_localDashCd = 1.0f;
    }
}

void NetPred_ApplyLocalPreview(int localHeroId, const PlayerIntent *in, float dt) {
    if (localHeroId < 0 || !in || dt <= 0.0f) return;
    const Agent *a = Entity_GetAgent(localHeroId);
    if (!a || a->vState == AGENT_RING_OUT_FALLING || Entity_IsCrowdControlled(localHeroId)) return;
    if (s_localInAir || s_localDashTimer > 0.0f) return; // NetPred_Tick already integrates air & dash velocity

    float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
    if (len <= 0.0001f) return;

    float speed = 6.4f * Entity_GetSpeedMult(localHeroId);
    Vector3 pos = a->position;
    pos.x += (in->moveDir.x / len) * speed * dt;
    pos.z += (in->moveDir.y / len) * speed * dt;
    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
    Entity_SetPosition(localHeroId, pos);
}

void NetPred_Tick(float dt, int localHeroId) {
    if (dt <= 0.0f) return;
    if (s_localDashCd > 0.0f) {
        s_localDashCd -= dt;
        if (s_localDashCd < 0.0f) s_localDashCd = 0.0f;
    }

    if (localHeroId >= 0) {
        const Agent *la = Entity_GetAgent(localHeroId);
        if (la && (s_localDashTimer > 0.0f || s_localInAir)) {
            Vector3 pos = la->position;
            if (s_localDashTimer > 0.0f) {
                float step = (dt < s_localDashTimer) ? dt : s_localDashTimer;
                pos.x += s_localDashVx * step;
                pos.z += s_localDashVz * step;
                s_localDashTimer -= step;
                if (!s_localInAir) {
                    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
                }
            }
            if (s_localInAir) {
                s_localVelY -= 5.0f * dt; // match entities/entities.c GRAVITY = 5.0f
                pos.x += s_localVelX * dt;
                pos.y += s_localVelY * dt;
                pos.z += s_localVelZ * dt;
                float groundY = MapManager_GetGroundHeightAt(pos.x, pos.z);
                MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &groundY);
                if (pos.y <= groundY) {
                    pos.y = groundY;
                    s_localInAir = false;
                    s_localAirJumps = 0;
                    s_localVelX = s_localVelY = s_localVelZ = 0.0f;
                }
            }
            Entity_SetPosition(localHeroId, pos);
        }
    }

    float remoteAlpha = dt * 16.0f;
    if (remoteAlpha > 1.0f) remoteAlpha = 1.0f;
    float localAlpha = dt * 6.0f;
    if (localAlpha > 0.35f) localAlpha = 0.35f;

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!s_interp[i].hasTarget) continue;
        const Agent *a = Entity_GetAgent(i);
        if (!a) {
            s_interp[i].hasTarget = false;
            continue;
        }
        if (i == localHeroId) {
            if (s_localInAir || s_localDashTimer > 0.0f) continue;
            float dx = s_interp[i].targetPos.x - a->position.x;
            float dz = s_interp[i].targetPos.z - a->position.z;
            if (dx * dx + dz * dz < 9.0f) continue; // trust local prediction within 3m
            Vector3 nxt = {
                a->position.x + dx * localAlpha,
                a->position.y,
                a->position.z + dz * localAlpha,
            };
            Entity_SetPosition(i, nxt);
            continue;
        }
        Vector3 tgt = s_interp[i].targetPos;
        Vector3 nxt = {
            a->position.x + (tgt.x - a->position.x) * remoteAlpha,
            a->position.y + (tgt.y - a->position.y) * remoteAlpha,
            a->position.z + (tgt.z - a->position.z) * remoteAlpha,
        };
        Entity_SetPosition(i, nxt);
    }
}
