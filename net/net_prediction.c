// net/net_prediction.c
#include "net/net_prediction.h"
#include "maps/map_manager.h"
#include <math.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

typedef struct {
    bool    hasTarget;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 vel;
    float   segElapsed;
    float   segDuration;
    float   startYaw;
    float   targetYaw;
    float   currentYaw;
} NetInterpSlot;

static NetInterpSlot s_interp[MAX_AGENTS];

static float LerpAngleShortest(float a, float b, float t) {
    float diff = fmodf(b - a, 2.0f * PI);
    if (diff >  PI) diff -= 2.0f * PI;
    if (diff < -PI) diff += 2.0f * PI;
    return a + diff * t;
}

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

bool NetPred_IsLocalDashing(void) {
    return s_localDashTimer > 0.0f;
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
        NetInterpSlot *slot = &s_interp[id];

        if (cur != NULL && slot->hasTarget) {
            float dx = s->position.x - cur->position.x;
            float dy = s->position.y - cur->position.y;
            float dz = s->position.z - cur->position.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (id == localHeroId) {
                // Client-authoritative hero pose: never drag the local player
                // backward toward a 1-RTT-old snapshot; only snap on true
                // teleports/respawns (>= 10m).
                float hDistSq = dx * dx + dz * dz;
                if (hDistSq < 100.0f) {
                    syncPos = cur->position;
                }
                slot->startPos = syncPos;
                slot->targetPos = s->position;
                slot->segElapsed = 0.0f;
            } else if (distSq < 36.0f) {
                bool isMinion = ((AgentArchetype)s->archetype == ARCH_MINION);
                unsigned int monState = isMinion ? ((s->flags >> 2) & 0x7u) : 0u;
                // Only update velocity when enough real time elapsed between packets
                // (>= 18ms) so back-to-back burst packets don't cause speed spikes.
                if (slot->segElapsed >= 0.018f && slot->segElapsed < 0.25f) {
                    Vector3 rawVel = {
                        (s->position.x - slot->targetPos.x) / slot->segElapsed,
                        (s->position.y - slot->targetPos.y) / slot->segElapsed,
                        (s->position.z - slot->targetPos.z) / slot->segElapsed
                    };
                    float maxV = isMinion ? ((monState == 4u) ? 8.2f : 4.8f) : 11.0f;
                    float vSq = rawVel.x * rawVel.x + rawVel.y * rawVel.y + rawVel.z * rawVel.z;
                    if (vSq > maxV * maxV) {
                        float scale = maxV / sqrtf(vSq);
                        rawVel.x *= scale; rawVel.y *= scale; rawVel.z *= scale;
                    }
                    slot->vel.x = slot->vel.x * 0.60f + rawVel.x * 0.40f;
                    slot->vel.y = slot->vel.y * 0.60f + rawVel.y * 0.40f;
                    slot->vel.z = slot->vel.z * 0.60f + rawVel.z * 0.40f;
                    float targetDur = slot->segElapsed * 1.10f;
                    if (targetDur < 0.040f) targetDur = 0.040f;
                    if (targetDur > 0.085f) targetDur = 0.085f;
                    slot->segDuration = (slot->segDuration > 0.01f)
                                            ? (0.75f * slot->segDuration + 0.25f * targetDur)
                                            : targetDur;
                }
                // Stationary monster states (TELEGRAPH = 3, RECOVER = 5) plant feet immediately
                if (isMinion && (monState == 3u || monState == 5u)) {
                    slot->vel = (Vector3){ 0 };
                }
                slot->startPos = cur->position;
                slot->targetPos = s->position;
                slot->segElapsed = 0.0f;
                slot->startYaw = slot->currentYaw;
                slot->targetYaw = s->yaw;
                syncPos = cur->position;
            } else {
                slot->startPos = slot->targetPos = s->position;
                slot->vel = (Vector3){ 0 };
                slot->segElapsed = 0.0f;
                slot->segDuration = 0.036f;
                slot->startYaw = slot->targetYaw = slot->currentYaw = s->yaw;
            }
        } else {
            slot->startPos = slot->targetPos = s->position;
            slot->vel = (Vector3){ 0 };
            slot->segElapsed = 0.0f;
            slot->segDuration = 0.036f;
            slot->startYaw = slot->targetYaw = slot->currentYaw = s->yaw;
        }

        slot->hasTarget = true;

        bool isMinion = ((AgentArchetype)s->archetype == ARCH_MINION);
        Entity_NetSyncAgent(id, syncPos, s->health, s->maxHealth,
                            s->mana, s->maxMana, s->element,
                            (AgentTeam)s->team, (AgentArchetype)s->archetype,
                            !isMinion && (s->flags & 1) != 0,
                            !isMinion && (s->flags & 2) != 0,
                            !isMinion && (s->flags & 4) != 0,
                            !isMinion && (s->flags & 8) != 0);
        Entity_SetFacingYaw(id, slot->currentYaw);
        if (isMinion) {
            Entity_SetNetMonsterMeta(id, s->flags);
        } else {
            int pkModeVal = (s->flags >> 4) & 0x3;
            if (pkModeVal <= 2) Entity_SetPkMode(id, (AgentPkMode)pkModeVal);
        }
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
    MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &pos.y);
    Entity_SetPosition(localHeroId, pos);
    Entity_SetFacingYaw(localHeroId, Control_GetYaw());
}

void NetPred_Tick(float dt, int localHeroId) {
    if (dt <= 0.0f) return;
    Entity_TickClientVisuals(dt);
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
                    MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &pos.y);
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

    for (int i = 0; i < MAX_AGENTS; i++) {
        NetInterpSlot *slot = &s_interp[i];
        if (!slot->hasTarget) continue;
        const Agent *a = Entity_GetAgent(i);
        if (!a) {
            slot->hasTarget = false;
            continue;
        }
        if (i == localHeroId) continue;

        slot->segElapsed += dt;
        bool isMinion = (a->archetype == ARCH_MINION);
        unsigned int monState = isMinion ? ((a->netMonsterMeta >> 2) & 0x7u) : 0u;

        // Dead-reckoned target horizon (capped to 75ms across packet gaps)
        float overDt = slot->segElapsed;
        if (overDt > 0.075f) overDt = 0.075f;
        float leadScale = (isMinion && (monState == 3u || monState == 5u)) ? 0.0f : 0.45f;
        Vector3 goal = {
            slot->targetPos.x + slot->vel.x * overDt * leadScale,
            slot->targetPos.y + slot->vel.y * overDt * leadScale,
            slot->targetPos.z + slot->vel.z * overDt * leadScale
        };

        // Physically speed-clamped smooth follower: eliminates burst-packet
        // sprinting and start-stop micro-stutter on 50 monsters under 20-90ms jitter.
        float errX = goal.x - a->position.x;
        float errY = goal.y - a->position.y;
        float errZ = goal.z - a->position.z;
        float errXZ = sqrtf(errX * errX + errZ * errZ);

        Vector3 nxt = a->position;
        if (errXZ > 0.001f) {
            float gain = isMinion ? 11.5f : 15.0f;
            float stepXZ = errXZ * (1.0f - expf(-gain * dt));
            float maxSpd = isMinion
                ? ((monState == 4u) ? 8.5f : ((monState == 3u || monState == 5u) ? 2.5f : 5.4f))
                : 12.0f;
            // Allow graceful catch-up if error exceeds 1.5m
            if (errXZ > 1.5f) maxSpd += (errXZ - 1.5f) * 3.0f;
            float maxStep = maxSpd * dt;
            if (stepXZ > maxStep) stepXZ = maxStep;
            nxt.x += (errX / errXZ) * stepXZ;
            nxt.z += (errZ / errXZ) * stepXZ;
        }
        nxt.y += errY * (1.0f - expf(-16.0f * dt));

        float yawRate = 1.0f - expf(-18.0f * dt);
        slot->currentYaw = LerpAngleShortest(slot->currentYaw, slot->targetYaw, yawRate);
        Entity_SetPosition(i, nxt);
        Entity_SetFacingYaw(i, slot->currentYaw);
    }
}
