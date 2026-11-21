// net/net_prediction.c
#include "net/net_prediction.h"
#include "maps/map_manager.h"
#include <math.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define INTERP_BUF_SIZE 6

typedef struct
{
    double time;
    Vector3 pos;
    float yaw;
} NetInterpSample;

typedef struct
{
    NetInterpSample samples[INTERP_BUF_SIZE];
    int sampleCount;
    Vector3 extrapVel;
    Vector3 visualErr;
    float currentYaw;
    bool wasExtrapolating;
    bool hasLocalHost;
    Vector3 localHostPos;
} NetInterpSlot;

static NetInterpSlot s_interp[MAX_AGENTS];

// Monotonic client-side interpolation clock & adaptive jitter buffer delay.
static double s_simTime = 0.0;
static double s_lastSnapTime = 0.0;
static bool s_hasSnapTime = false;
static float s_snapInterval = 1.0f / 30.0f; // tracks host snapshot cadence (~33.3ms)
static float s_snapJitter = 0.005f;         // tracks arrival jitter EMA
static float s_interpDelay = 0.048f;        // smoothed render delay behind newest sample

// Client-side local kinematic state for 60 FPS Khinh Công & dash (since Entity_Update
// is skipped on clients when Net_ClientDrivesWorld() is true).
static bool s_localInAir = false;
static int s_localAirJumps = 0;
static int s_localJumpFired = 0;
static float s_localVelX = 0.0f;
static float s_localVelY = 0.0f;
static float s_localVelZ = 0.0f;
static float s_localDashTimer = 0.0f;
static float s_localDashCd = 0.0f;
static float s_localDashVx = 0.0f;
static float s_localDashVz = 0.0f;

static float LerpAngleShortest(float a, float b, float t)
{
    float diff = fmodf(b - a, 2.0f * PI);
    if (diff > PI)
        diff -= 2.0f * PI;
    if (diff < -PI)
        diff += 2.0f * PI;
    return a + diff * t;
}

static void ClearLocalKinematics(void)
{
    s_localInAir = false;
    s_localAirJumps = 0;
    s_localVelX = 0.0f;
    s_localVelY = 0.0f;
    s_localVelZ = 0.0f;
    s_localDashTimer = 0.0f;
    s_localDashVx = 0.0f;
    s_localDashVz = 0.0f;
}

static void ResetSlotWithSample(NetInterpSlot *slot, double t, Vector3 pos, float yaw)
{
    slot->sampleCount = 1;
    slot->samples[0] = (NetInterpSample){.time = t, .pos = pos, .yaw = yaw};
    slot->extrapVel = (Vector3){0};
    slot->visualErr = (Vector3){0};
    slot->currentYaw = yaw;
    slot->wasExtrapolating = false;
}

void NetPred_Reset(void)
{
    memset(s_interp, 0, sizeof(s_interp));
    s_simTime = 0.0;
    s_lastSnapTime = 0.0;
    s_hasSnapTime = false;
    s_snapInterval = 1.0f / 30.0f;
    s_snapJitter = 0.005f;
    s_interpDelay = 0.048f;
    s_localJumpFired = 0;
    s_localDashCd = 0.0f;
    ClearLocalKinematics();
}

bool NetPred_IsLocalAirborne(float *outVelY)
{
    if (outVelY)
        *outVelY = s_localVelY;
    return s_localInAir;
}

bool NetPred_IsLocalDashing(void)
{
    return s_localDashTimer > 0.0f;
}

int NetPred_ConsumeLocalJumpFired(void)
{
    int j = s_localJumpFired;
    s_localJumpFired = 0;
    return j;
}

void NetPred_OnSnapshot(const NetAgentState *states, int count, int localHeroId)
{
    if (!states || count < 0)
        return;

    // Track real snapshot cadence & arrival jitter to size the interp buffer cleanly.
    if (s_hasSnapTime)
    {
        double dtArr = s_simTime - s_lastSnapTime;
        if (dtArr >= 0.008 && dtArr <= 0.25)
        {
            float dtF = (float)dtArr;
            float dev = fabsf(dtF - s_snapInterval);
            s_snapInterval = s_snapInterval * 0.85f + dtF * 0.15f;
            if (s_snapInterval < 0.016f)
                s_snapInterval = 0.016f;
            if (s_snapInterval > 0.085f)
                s_snapInterval = 0.085f;
            s_snapJitter = s_snapJitter * 0.80f + dev * 0.20f;
            if (s_snapJitter > 0.060f)
                s_snapJitter = 0.060f;
        }
    }
    s_lastSnapTime = s_simTime;
    s_hasSnapTime = true;

    Entity_NetSyncBegin();
    for (int i = 0; i < count; i++)
    {
        const NetAgentState *s = &states[i];
        int id = (int)s->agentId;
        if (id < 0 || id >= MAX_AGENTS)
            continue;

        const Agent *cur = Entity_GetAgent(id);
        NetInterpSlot *slot = &s_interp[id];
        Vector3 syncPos = s->position;
        bool isMinion = ((AgentArchetype)s->archetype == ARCH_MINION);

        if (id == localHeroId)
        {
            slot->localHostPos = s->position;
            slot->hasLocalHost = true;
            if (cur != NULL)
            {
                float dx = s->position.x - cur->position.x;
                float dy = s->position.y - cur->position.y;
                float dz = s->position.z - cur->position.z;
                float hDistSq = dx * dx + dz * dz;
                // Snap immediately on true teleports/respawns; otherwise keep
                // 0ms client-predicted pose (CC/knockback blends in NetPred_Tick).
                if (hDistSq >= 64.0f || fabsf(dy) >= 6.0f)
                {
                    syncPos = s->position;
                    ClearLocalKinematics();
                }
                else
                {
                    syncPos = cur->position;
                }
            }
            else
            {
                slot->currentYaw = s->yaw;
                ClearLocalKinematics();
            }
        }
        else if (cur != NULL && slot->sampleCount > 0)
        {
            NetInterpSample *prev = &slot->samples[slot->sampleCount - 1];
            float dx = s->position.x - prev->pos.x;
            float dy = s->position.y - prev->pos.y;
            float dz = s->position.z - prev->pos.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq >= 36.0f)
            {
                // Teleport / respawn (> 6m jump in a single snapshot): snap cleanly.
                ResetSlotWithSample(slot, s_simTime, s->position, s->yaw);
                syncPos = s->position;
            }
            else
            {
                double segDt = s_simTime - prev->time;
                if (segDt < 0.004)
                {
                    // Burst packet arrived on the same frame: replace newest sample
                    // in-place so segment duration stays non-zero.
                    prev->pos = s->position;
                    prev->yaw = s->yaw;
                }
                else
                {
                    if (slot->sampleCount >= INTERP_BUF_SIZE)
                    {
                        memmove(&slot->samples[0], &slot->samples[1],
                                (INTERP_BUF_SIZE - 1) * sizeof(NetInterpSample));
                        slot->sampleCount = INTERP_BUF_SIZE - 1;
                    }
                    slot->samples[slot->sampleCount++] = (NetInterpSample){
                        .time = s_simTime,
                        .pos = s->position,
                        .yaw = s->yaw};
                }

                // Update clamped velocity estimate for brief packet-loss extrapolation.
                unsigned int monState = isMinion ? ((s->flags >> 2) & 0x7u) : 0u;
                if (isMinion && (monState == 3u || monState == 5u))
                {
                    slot->extrapVel = (Vector3){0};
                }
                else if (slot->sampleCount >= 2)
                {
                    const NetInterpSample *a = &slot->samples[slot->sampleCount - 2];
                    const NetInterpSample *b = &slot->samples[slot->sampleCount - 1];
                    double dtVel = b->time - a->time;
                    if (dtVel >= 0.008 && dtVel < 0.30)
                    {
                        float invDt = (float)(1.0 / dtVel);
                        Vector3 rawVel = {
                            (b->pos.x - a->pos.x) * invDt,
                            (b->pos.y - a->pos.y) * invDt,
                            (b->pos.z - a->pos.z) * invDt};
                        float maxV = isMinion ? ((monState == 4u) ? 8.2f : 4.8f) : 11.0f;
                        float vSq = rawVel.x * rawVel.x + rawVel.y * rawVel.y + rawVel.z * rawVel.z;
                        if (vSq > maxV * maxV)
                        {
                            float scale = maxV / sqrtf(vSq);
                            rawVel.x *= scale;
                            rawVel.y *= scale;
                            rawVel.z *= scale;
                        }
                        slot->extrapVel.x = slot->extrapVel.x * 0.50f + rawVel.x * 0.50f;
                        slot->extrapVel.y = slot->extrapVel.y * 0.50f + rawVel.y * 0.50f;
                        slot->extrapVel.z = slot->extrapVel.z * 0.50f + rawVel.z * 0.50f;
                    }
                }
                // Preserve smooth visual position; NetPred_Tick advances it at 60 FPS.
                syncPos = cur->position;
            }
        }
        else
        {
            ResetSlotWithSample(slot, s_simTime, s->position, s->yaw);
            syncPos = s->position;
        }

        Entity_NetSyncAgent(id, syncPos, s->health, s->maxHealth,
                            s->mana, s->maxMana, s->element,
                            (AgentTeam)s->team, (AgentArchetype)s->archetype,
                            !isMinion && (s->flags & 1) != 0,
                            !isMinion && (s->flags & 2) != 0,
                            !isMinion && (s->flags & 4) != 0,
                            !isMinion && (s->flags & 8) != 0);

        // Never overwrite local hero's live facing yaw with a stale host snapshot.
        if (id != localHeroId)
        {
            Entity_SetFacingYaw(id, slot->currentYaw);
        }
        else if (cur == NULL)
        {
            Entity_SetFacingYaw(id, s->yaw);
        }

        if (isMinion)
        {
            Entity_SetNetMonsterMeta(id, s->flags);
        }
        else
        {
            int pkModeVal = (s->flags >> 4) & 0x3;
            if (pkModeVal <= 2)
                Entity_SetPkMode(id, (AgentPkMode)pkModeVal);
        }
    }
    Entity_NetSyncEnd();

    for (int i = 0; i < MAX_AGENTS; i++)
    {
        if (!Entity_GetAgent(i))
        {
            s_interp[i].sampleCount = 0;
            s_interp[i].hasLocalHost = false;
        }
    }
}

void NetPred_OnLocalIntent(int localHeroId, const PlayerIntent *in)
{
    if (localHeroId < 0 || !in)
        return;
    const Agent *a = Entity_GetAgent(localHeroId);
    if (!a || Entity_IsCrowdControlled(localHeroId))
        return;

    float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
    float speed = 6.4f * Entity_GetSpeedMult(localHeroId);
    if (in->jump && (!s_localInAir || s_localAirJumps < 2))
    {
        if (len > 0.0001f)
        {
            s_localVelX = (in->moveDir.x / len) * speed;
            s_localVelZ = (in->moveDir.y / len) * speed;
        }
        else if (!s_localInAir)
        {
            s_localVelX = 0.0f;
            s_localVelZ = 0.0f;
        }
        s_localVelY = (s_localInAir && s_localAirJumps > 0) ? (4.6f * 1.05f) : 4.6f;
        s_localInAir = true;
        s_localAirJumps++;
        s_localJumpFired = s_localAirJumps;
    }
    if (in->dash && s_localDashCd <= 0.0f)
    {
        float dx = (len > 0.0001f) ? (in->moveDir.x / len) : sinf(Control_GetYaw());
        float dz = (len > 0.0001f) ? (in->moveDir.y / len) : cosf(Control_GetYaw());
        s_localDashVx = dx * 10.0f;
        s_localDashVz = dz * 10.0f;
        s_localDashTimer = 0.15f;
        s_localDashCd = 1.0f;
    }
}

void NetPred_ApplyLocalPreview(int localHeroId, const PlayerIntent *in, float dt)
{
    if (localHeroId < 0 || !in || dt <= 0.0f)
        return;
    if (dt > 0.1f)
        dt = 0.1f;
    const Agent *a = Entity_GetAgent(localHeroId);
    if (!a || a->vState == AGENT_RING_OUT_FALLING || Entity_IsCrowdControlled(localHeroId))
        return;
    if (s_localInAir || s_localDashTimer > 0.0f)
        return; // NetPred_Tick integrates air & dash

    float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
    if (len <= 0.0001f)
        return;

    float speed = 6.4f * Entity_GetSpeedMult(localHeroId);
    Vector3 pos = a->position;
    pos.x += (in->moveDir.x / len) * speed * dt;
    pos.z += (in->moveDir.y / len) * speed * dt;
    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
    MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &pos.y);
    Entity_SetPosition(localHeroId, pos);
    Entity_SetFacingYaw(localHeroId, Control_GetYaw());
}

void NetPred_Tick(float dt, int localHeroId)
{
    if (dt <= 0.0f)
        return;
    if (dt > 0.1f)
        dt = 0.1f;

    s_simTime += (double)dt;
    Entity_TickClientVisuals(dt);

    if (s_localDashCd > 0.0f)
    {
        s_localDashCd -= dt;
        if (s_localDashCd < 0.0f)
            s_localDashCd = 0.0f;
    }

    // Smoothly steer interpolation delay toward ~1.25 snapshot intervals + 2x jitter.
    // Max skew is +/-15% of dt so remote motion never visibly speeds up or slows down.
    float targetDelay = s_snapInterval * 1.25f + s_snapJitter * 2.0f;
    if (targetDelay < 0.042f)
        targetDelay = 0.042f;
    if (targetDelay > 0.110f)
        targetDelay = 0.110f;
    float delayDiff = targetDelay - s_interpDelay;
    float maxSkew = 0.15f * dt;
    if (delayDiff > maxSkew)
        delayDiff = maxSkew;
    if (delayDiff < -maxSkew)
        delayDiff = -maxSkew;
    s_interpDelay += delayDiff;

    // Precompute frame decay factors once outside the agent loop.
    const float errDecay = expf(-16.0f * dt);
    const float ccAlpha = 1.0f - expf(-18.0f * dt);
    const double renderTime = s_simTime - (double)s_interpDelay;

    // --- Local hero kinematics & CC/knockback reconciliation ---
    if (localHeroId >= 0 && localHeroId < MAX_AGENTS)
    {
        const Agent *la = Entity_GetAgent(localHeroId);
        if (la != NULL)
        {
            bool hostDriven = (la->vState == AGENT_RING_OUT_FALLING ||
                               Entity_IsCrowdControlled(localHeroId));
            if (hostDriven)
            {
                ClearLocalKinematics();
                NetInterpSlot *lslot = &s_interp[localHeroId];
                if (lslot->hasLocalHost)
                {
                    Vector3 pos = {
                        la->position.x + (lslot->localHostPos.x - la->position.x) * ccAlpha,
                        la->position.y + (lslot->localHostPos.y - la->position.y) * ccAlpha,
                        la->position.z + (lslot->localHostPos.z - la->position.z) * ccAlpha};
                    Entity_SetPosition(localHeroId, pos);
                }
            }
            else if (s_localDashTimer > 0.0f || s_localInAir)
            {
                Vector3 pos = la->position;
                float dashStep = 0.0f;
                if (s_localDashTimer > 0.0f)
                {
                    dashStep = (dt < s_localDashTimer) ? dt : s_localDashTimer;
                    pos.x += s_localDashVx * dashStep;
                    pos.z += s_localDashVz * dashStep;
                    s_localDashTimer -= dashStep;
                    if (!s_localInAir)
                    {
                        pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
                        MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &pos.y);
                    }
                }
                if (s_localInAir)
                {
                    // Do not stack horizontal jump velocity on top of active dash burst.
                    float airHorizDt = dt - dashStep;
                    s_localVelY -= 5.0f * dt; // match entities/entities.c GRAVITY = 5.0f
                    pos.x += s_localVelX * airHorizDt;
                    pos.y += s_localVelY * dt;
                    pos.z += s_localVelZ * airHorizDt;
                    float groundY = MapManager_GetGroundHeightAt(pos.x, pos.z);
                    MapManager_ResolveCollision(&pos, 0.35f, 0.45f, &groundY);
                    if (pos.y <= groundY)
                    {
                        pos.y = groundY;
                        s_localInAir = false;
                        s_localAirJumps = 0;
                        s_localVelX = s_localVelY = s_localVelZ = 0.0f;
                    }
                }
                Entity_SetPosition(localHeroId, pos);
            }
        }
    }

    // --- Remote heroes, monsters, and bosses: constant-velocity buffered interpolation ---
    for (int i = 0; i < MAX_AGENTS; i++)
    {
        if (i == localHeroId)
            continue;
        NetInterpSlot *slot = &s_interp[i];
        if (slot->sampleCount == 0)
            continue;

        const Agent *a = Entity_GetAgent(i);
        if (!a)
        {
            slot->sampleCount = 0;
            continue;
        }

        const NetInterpSample *first = &slot->samples[0];
        const NetInterpSample *last = &slot->samples[slot->sampleCount - 1];
        Vector3 desiredPos;
        float desiredYaw;
        bool nowExtrapolating = false;

        if (slot->sampleCount == 1 || renderTime <= first->time)
        {
            desiredPos = first->pos;
            desiredYaw = first->yaw;
        }
        else if (renderTime <= last->time)
        {
            // Find exact [k, k+1] bracket containing renderTime.
            int k = slot->sampleCount - 2;
            while (k > 0 && slot->samples[k].time > renderTime)
                k--;
            const NetInterpSample *s0 = &slot->samples[k];
            const NetInterpSample *s1 = &slot->samples[k + 1];
            double span = s1->time - s0->time;
            float t = (span > 1e-6) ? (float)((renderTime - s0->time) / span) : 1.0f;
            if (t < 0.0f)
                t = 0.0f;
            if (t > 1.0f)
                t = 1.0f;
            desiredPos = (Vector3){
                s0->pos.x + (s1->pos.x - s0->pos.x) * t,
                s0->pos.y + (s1->pos.y - s0->pos.y) * t,
                s0->pos.z + (s1->pos.z - s0->pos.z) * t};
            desiredYaw = LerpAngleShortest(s0->yaw, s1->yaw, t);
        }
        else
        {
            // Brief packet gap: extrapolate gently up to 65ms along smoothed velocity.
            nowExtrapolating = true;
            float over = (float)(renderTime - last->time);
            if (over > 0.065f)
                over = 0.065f;
            bool isMinion = (a->archetype == ARCH_MINION);
            unsigned int monState = isMinion ? ((a->netMonsterMeta >> 2) & 0x7u) : 0u;
            float leadScale = (isMinion && (monState == 3u || monState == 5u)) ? 0.0f : 0.75f;
            desiredPos = (Vector3){
                last->pos.x + slot->extrapVel.x * (over * leadScale),
                last->pos.y + slot->extrapVel.y * (over * leadScale),
                last->pos.z + slot->extrapVel.z * (over * leadScale)};
            desiredYaw = last->yaw;
        }

        // Absorb tiny discontinuity when recovering from extrapolation back into buffer.
        if (slot->wasExtrapolating && !nowExtrapolating)
        {
            Vector3 gap = {
                a->position.x - desiredPos.x,
                a->position.y - desiredPos.y,
                a->position.z - desiredPos.z};
            float gapSq = gap.x * gap.x + gap.y * gap.y + gap.z * gap.z;
            if (gapSq <= 2.25f)
            { // <= 1.5m: blend smoothly instead of popping
                slot->visualErr = gap;
            }
            else
            {
                slot->visualErr = (Vector3){0};
            }
        }
        slot->wasExtrapolating = nowExtrapolating;

        slot->visualErr.x *= errDecay;
        slot->visualErr.y *= errDecay;
        slot->visualErr.z *= errDecay;

        Vector3 nxt = {
            desiredPos.x + slot->visualErr.x,
            desiredPos.y + slot->visualErr.y,
            desiredPos.z + slot->visualErr.z};
        slot->currentYaw = desiredYaw;

        Entity_SetPosition(i, nxt);
        Entity_SetFacingYaw(i, slot->currentYaw);
    }
}