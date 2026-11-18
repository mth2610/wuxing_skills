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

void NetPred_Reset(void) {
    memset(s_interp, 0, sizeof(s_interp));
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
                // Only snap if deviation is large (e.g. knockback/stun/respawn);
                // otherwise keep predicted pos and let NetPred_Tick gently blend.
                if (distSq < 1.44f) {
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
    }
    Entity_NetSyncEnd();

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (!Entity_GetAgent(i)) s_interp[i].hasTarget = false;
    }
}

void NetPred_ApplyLocalPreview(int localHeroId, const PlayerIntent *in, float dt) {
    if (localHeroId < 0 || !in || dt <= 0.0f) return;
    const Agent *a = Entity_GetAgent(localHeroId);
    if (!a || a->vState != AGENT_GROUNDED || Entity_IsCrowdControlled(localHeroId)) return;

    float len = sqrtf(in->moveDir.x * in->moveDir.x + in->moveDir.y * in->moveDir.y);
    if (len <= 0.0001f) return;

    float speed = 3.5f * Entity_GetSpeedMult(localHeroId);
    Vector3 pos = a->position;
    pos.x += (in->moveDir.x / len) * speed * dt;
    pos.z += (in->moveDir.y / len) * speed * dt;
    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
    Entity_SetPosition(localHeroId, pos);
}

void NetPred_Tick(float dt, int localHeroId) {
    if (dt <= 0.0f) return;
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
        float alpha = (i == localHeroId) ? localAlpha : remoteAlpha;
        Vector3 tgt = s_interp[i].targetPos;
        Vector3 nxt = {
            a->position.x + (tgt.x - a->position.x) * alpha,
            a->position.y + (tgt.y - a->position.y) * alpha,
            a->position.z + (tgt.z - a->position.z) * alpha,
        };
        Entity_SetPosition(i, nxt);
    }
}
