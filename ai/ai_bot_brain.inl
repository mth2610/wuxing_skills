// ai/ai_bot_brain.inl
// 3-Layer Data-Driven Utility AI & Humanized Motor for Simulated Players (Hero Bots).
// Included directly by ai/ai.c. Pure logic — no rendering headers.

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static const float BOT_MOVE_MPS       = 3.5f;
static const float BOT_RUN_MPS        = 4.75f;
static const float BOT_EDGE_MARGIN    = 2.5f;
static const float BOT_DODGE_COOLDOWN = 2.4f;
static const float BOT_DODGE_RANGE    = 4.4f;

typedef enum {
    GOAL_SURVIVE_RECOVER = 0,
    GOAL_MOBA_CONTEST_ORB,
    GOAL_MOBA_ESCORT_BOSS,
    GOAL_MOBA_ASSAULT_OBJECTIVE,
    GOAL_COMBAT_DUEL_HERO,
    GOAL_OW_WORLD_BOSS_RAID,
    GOAL_OW_HUNT_MONSTERS,
    GOAL_OW_TOWN_QUEST,
    BOT_GOAL_COUNT
} BotGoalId;

typedef struct {
    bool         used;
    int          agentId;
    AIBotPersona persona;
    int          outfitSeed;
    char         displayName[32];

    // Timers & humanization
    float        thinkTimer;
    float        reactionTimer;
    float        dodgeTimer;
    float        basicAtkTimer;
    float        jumpTimer;
    float        strafePhase;
    float        goalLockTimer;
    float        townPauseTimer;
    int          activePoiIdx;
    BotGoalId    currentGoal;

    // Social response state
    int          pendingSocialEmote;
    float        socialDelayTimer;
    int          sparRequesterId;

    // Velocity estimation for predictive aim
    Vector3      lastTargetPos;
    int          lastTargetId;
    Vector3      targetVelEst;
    int          lastCastElem;
} HeroBot;

static HeroBot           s_bots[AI_MAX_HERO_BOTS];
static HeroBotCast       s_botCasts[16];
static int               s_botCastCount = 0;
static AIBotWorldContext s_botCtx = { 0 };
static bool              s_hasBotCtx = false;
static AIBotSocialEvent  s_botSocialQueue[16];
static int               s_botSocialCount = 0;

static const char *kWuxiaBotNames[] = {
    "Độc Cô Phong", "Hàn Giang Tuyết", "Tuyết Nguyệt", "Lăng Vân",
    "Mạc Thiên", "Thanh Phong", "Vân Dao", "Lục Tiểu Phụng",
    "Yến Nam Thiên", "Tử Yên", "Bạch Ngọc Đường", "Kinh Vô Mệnh"
};
static const int kWuxiaBotNameCount = (int)(sizeof(kWuxiaBotNames) / sizeof(kWuxiaBotNames[0]));

// Persona weights per goal: [persona][goal]
static const float kPersonaGoalWeights[AI_PERSONA_COUNT][BOT_GOAL_COUNT] = {
    /* BALANCED */  { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },
    /* VANGUARD */  { 0.75f, 1.15f, 0.75f, 1.35f, 1.25f, 1.20f, 1.15f, 0.60f },
    /* GUARDIAN */  { 1.10f, 0.90f, 1.45f, 0.85f, 0.85f, 1.05f, 0.90f, 0.85f },
    /* DUELIST  */  { 1.05f, 1.10f, 0.85f, 0.95f, 1.40f, 1.00f, 0.95f, 0.70f },
    /* WANDERER */  { 1.10f, 1.00f, 1.00f, 1.00f, 0.80f, 1.30f, 1.35f, 1.25f },
    /* RIVAL    */  { 0.95f, 1.05f, 0.90f, 1.10f, 1.35f, 1.10f, 0.90f, 0.80f }
};

static float BotDistXZ(Vector3 a, Vector3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

// --- Layer 1: Perception & Dynamic Skill Introspection -----------------------

typedef struct {
    int   skillIndex;
    int   element;
    bool  isSupport;
    bool  hasHeal;
    bool  hasShield;
    bool  hasCleanse;
    bool  hasCC;
    bool  isProjectile;
    float idealRange;
} BotSlotMeta;

typedef struct {
    const Agent *me;
    int          nearestEnemyHero;
    float        nearestEnemyHeroDist;
    int          weakestEnemyHero;
    float        weakestEnemyHeroScore;
    int          nearestMonster;
    float        nearestMonsterDist;
    int          nearestMinionNearAllyBoss;
    float        nearestMinionNearAllyBossDist;
    int          allyBossId;
    const Agent *allyBoss;
    int          enemyBossId;
    const Agent *enemyBoss;
    int          humanTeammateId;
    const Agent *humanTeammate;
    int          incomingProjIdx;
    Vector3      incomingProjPos;
    int          incomingProjElem;
    float        incomingProjDist;
    int          nearbyEnemyCount;
    BotSlotMeta  slots[AGENT_SKILL_SLOTS];
} BotBlackboard;

static void InspectSlotMetadata(const Agent *me, int slot, BotSlotMeta *out) {
    memset(out, 0, sizeof(*out));
    out->skillIndex = me->equippedSkills[slot];
    out->element    = me->equippedElements[slot];
    out->idealRange = 6.5f;
    if (out->skillIndex < 0) return;

    const char *name = GetRegisteredSkillName(out->skillIndex);
    const VoHocSkillDef *def = name ? VoHoc_FindByName(name) : NULL;
    if (!def) {
        out->isSupport = (slot >= AGENT_STRIKE_SLOTS);
        return;
    }
    out->element    = def->element;
    out->isSupport  = (def->role == VH_ROLE_SUPPORT);
    out->isProjectile = (def->hit == VH_HIT_FAST || def->hit == VH_HIT_SLOW || def->hit == VH_HIT_WAVE);
    if (def->range == VH_RANGE_MELEE)     out->idealRange = 2.4f;
    else if (def->range == VH_RANGE_MID)  out->idealRange = 6.5f;
    else if (def->range == VH_RANGE_LONG) out->idealRange = 10.5f;
    else                                  out->idealRange = 0.0f;

    for (int f = 0; f < def->fxCount; f++) {
        VoHocFx fx = def->fx[f].fx;
        if (fx == VH_FX_HEAL)    out->hasHeal = true;
        if (fx == VH_FX_SHIELD)  out->hasShield = true;
        if (fx == VH_FX_CLEANSE) out->hasCleanse = true;
        if (fx == VH_FX_STUN || fx == VH_FX_FREEZE || fx == VH_FX_LAUNCH ||
            fx == VH_FX_ROOT || fx == VH_FX_PULL || fx == VH_FX_KB) {
            out->hasCC = true;
        }
    }
}

static void BotPerceive(HeroBot *bot, const Agent *me, BotBlackboard *bb) {
    memset(bb, 0, sizeof(*bb));
    bb->me = me;
    bb->nearestEnemyHero = -1;
    bb->nearestEnemyHeroDist = 1e9f;
    bb->weakestEnemyHero = -1;
    bb->weakestEnemyHeroScore = 1e9f;
    bb->nearestMonster = -1;
    bb->nearestMonsterDist = 1e9f;
    bb->nearestMinionNearAllyBoss = -1;
    bb->nearestMinionNearAllyBossDist = 1e9f;
    bb->allyBossId = s_hasBotCtx ? s_botCtx.allyBossId : -1;
    bb->enemyBossId = s_hasBotCtx ? s_botCtx.enemyBossId : -1;
    bb->humanTeammateId = s_hasBotCtx ? s_botCtx.primaryHumanId : -1;
    bb->incomingProjIdx = -1;
    bb->incomingProjDist = 1e9f;

    if (me->team == TEAM_ENEMY) {
        int tmp = bb->allyBossId;
        bb->allyBossId = bb->enemyBossId;
        bb->enemyBossId = tmp;
    }
    bb->allyBoss = (bb->allyBossId >= 0) ? Entity_GetAgent(bb->allyBossId) : NULL;
    bb->enemyBoss = (bb->enemyBossId >= 0) ? Entity_GetAgent(bb->enemyBossId) : NULL;
    if (bb->humanTeammateId >= 0) {
        const Agent *ht = Entity_GetAgent(bb->humanTeammateId);
        if (ht && ht->team == me->team && bb->humanTeammateId != bot->agentId) {
            bb->humanTeammate = ht;
        }
    }

    for (int s = 0; s < AGENT_SKILL_SLOTS; s++) {
        InspectSlotMetadata(me, s, &bb->slots[s]);
    }

    for (int i = 0; i < MAX_AGENTS; i++) {
        if (i == bot->agentId) continue;
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->isStatic) continue;

        float d = BotDistXZ(me->position, a->position);
        bool canHit = Entity_CanDamageTarget(bot->agentId, i);
        if (canHit && d < 12.0f) bb->nearbyEnemyCount++;

        if (a->archetype == ARCH_HERO) {
            if (canHit) {
                if (d < bb->nearestEnemyHeroDist) {
                    bb->nearestEnemyHeroDist = d;
                    bb->nearestEnemyHero = i;
                }
                float wScore = d + a->health * 0.08f;
                if (wScore < bb->weakestEnemyHeroScore) {
                    bb->weakestEnemyHeroScore = wScore;
                    bb->weakestEnemyHero = i;
                }
            }
        } else if (a->archetype == ARCH_MINION) {
            if (canHit && d < bb->nearestMonsterDist) {
                bb->nearestMonsterDist = d;
                bb->nearestMonster = i;
            }
            if (bb->allyBoss && a->team != me->team) {
                float bd = BotDistXZ(a->position, bb->allyBoss->position);
                if (bd < 9.0f && bd < bb->nearestMinionNearAllyBossDist) {
                    bb->nearestMinionNearAllyBossDist = bd;
                    bb->nearestMinionNearAllyBoss = i;
                }
            }
        } else if (a->archetype == ARCH_BOSS && canHit && bb->enemyBoss == NULL) {
            bb->enemyBossId = i;
            bb->enemyBoss = a;
        }
    }

    CombatProjectileInfo proj[32];
    int np = Combat_QueryProjectiles(proj, 32);
    for (int p = 0; p < np; p++) {
        if (!Entity_CanDamageTarget(bot->agentId, proj[p].ownerAgentId)) continue;
        float d = BotDistXZ(proj[p].pos, me->position);
        if (d < bb->incomingProjDist) {
            bb->incomingProjDist = d;
            bb->incomingProjIdx  = p;
            bb->incomingProjPos  = proj[p].pos;
            bb->incomingProjElem = (int)proj[p].elem;
        }
    }
}

// --- Layer 2: Pluggable Utility AI Goal Scoring ------------------------------

static float ScoreGoal(const HeroBot *bot, const BotBlackboard *bb, BotGoalId g) {
    const Agent *me = bb->me;
    float hpFrac = (me->maxHealth > 0.0f) ? (me->health / me->maxHealth) : 1.0f;
    float mpFrac = (me->maxMana > 0.0f)   ? (me->mana / me->maxMana) : 1.0f;
    AIBotGameMode mode = s_hasBotCtx ? s_botCtx.mode : AI_BOT_MODE_ARENA_DUEL;

    float base = 0.0f;
    switch (g) {
        case GOAL_SURVIVE_RECOVER:
            if (hpFrac < 0.30f) base = 0.92f;
            else if (mpFrac < 0.22f && bb->nearestEnemyHeroDist > 7.5f) base = 0.78f;
            else if (me->isMeditating && mpFrac < 0.85f && bb->nearestEnemyHeroDist > 7.0f) base = 0.85f;
            break;

        case GOAL_MOBA_CONTEST_ORB:
            if (mode == AI_BOT_MODE_MOBA_ESCORT && s_hasBotCtx && s_botCtx.spiritOrbActive) {
                float dOrb = BotDistXZ(me->position, s_botCtx.spiritOrbPos);
                base = (dOrb < 28.0f) ? (0.86f - dOrb * 0.01f) : 0.45f;
            }
            break;

        case GOAL_MOBA_ESCORT_BOSS:
            if (mode == AI_BOT_MODE_MOBA_ESCORT && bb->allyBoss) {
                if (bb->nearestMinionNearAllyBoss >= 0) {
                    base = 0.84f;
                } else {
                    float dBoss = BotDistXZ(me->position, bb->allyBoss->position);
                    bool humanAlreadyEscorting = (bb->humanTeammate &&
                        BotDistXZ(bb->humanTeammate->position, bb->allyBoss->position) < 6.5f);
                    base = humanAlreadyEscorting ? 0.30f : (dBoss > 5.5f ? 0.62f : 0.56f);
                }
            }
            break;

        case GOAL_MOBA_ASSAULT_OBJECTIVE:
            if (mode == AI_BOT_MODE_MOBA_ESCORT && (bb->enemyBoss || s_hasBotCtx)) {
                base = 0.60f;
                if (bb->enemyBoss) {
                    float eHp = bb->enemyBoss->health / fmaxf(1.0f, bb->enemyBoss->maxHealth);
                    if (eHp < 0.45f) base = 0.80f;
                }
            }
            break;

        case GOAL_COMBAT_DUEL_HERO:
            if (bb->weakestEnemyHero >= 0) {
                if (mode == AI_BOT_MODE_ARENA_DUEL) base = 0.88f;
                else if (mode == AI_BOT_MODE_OPEN_WORLD) base = 0.90f; // Active Spar / PK
                else base = (bb->nearestEnemyHeroDist < 12.0f) ? 0.74f : 0.42f;
            }
            break;

        case GOAL_OW_WORLD_BOSS_RAID:
            if (mode == AI_BOT_MODE_OPEN_WORLD && s_hasBotCtx && s_botCtx.worldBossActive) {
                base = 0.85f;
            }
            break;

        case GOAL_OW_HUNT_MONSTERS:
            if (mode == AI_BOT_MODE_OPEN_WORLD) {
                base = (bb->nearestMonster >= 0 && bb->nearestMonsterDist < 22.0f) ? 0.68f : 0.56f;
            }
            break;

        case GOAL_OW_TOWN_QUEST:
            if (mode == AI_BOT_MODE_OPEN_WORLD) {
                base = (bot->townPauseTimer > 0.0f) ? 0.76f : 0.28f;
            }
            break;

        default:
            break;
    }

    if (base <= 0.0f) return 0.0f;
    float weight = kPersonaGoalWeights[bot->persona][g];
    float score = base * weight;
    if (bot->currentGoal == g) score += 0.14f; // Hysteresis prevents oscillation
    return score;
}

static BotGoalId SelectBestGoal(HeroBot *bot, const BotBlackboard *bb) {
    BotGoalId best = bot->currentGoal;
    float bestScore = -1.0f;
    for (int g = 0; g < BOT_GOAL_COUNT; g++) {
        float sc = ScoreGoal(bot, bb, (BotGoalId)g);
        if (sc > bestScore) {
            bestScore = sc;
            best = (BotGoalId)g;
        }
    }
    return best;
}

// --- Layer 3: Humanized Tactical Combat & Context Steering -------------------

static bool IsElementCounter(int atkElem, int defElem) {
    // Water(0)>Fire(2)>Metal(4)>Wood(1)>Earth(3)>Water(0)
    static const int kBeats[5] = { 2, 3, 4, 0, 1 };
    return (atkElem >= 0 && atkElem < 5 && kBeats[atkElem] == defElem);
}

static int PickBestSkillSlot(const HeroBot *bot, const BotBlackboard *bb,
                             int targetId, const Agent *target, float targetDist,
                             bool *outCounterProj) {
    const Agent *me = bb->me;
    *outCounterProj = false;
    float hpFrac = (me->maxHealth > 0.0f) ? (me->health / me->maxHealth) : 1.0f;

    // 1. Cleanse if CC'd / debuffed
    if (Entity_IsCrowdControlled(bot->agentId) || Entity_HasStatus(bot->agentId, STATUS_POISON) ||
        Entity_HasStatus(bot->agentId, STATUS_BURN)) {
        for (int s = AGENT_STRIKE_SLOTS; s < AGENT_SKILL_SLOTS; s++) {
            int idx = bb->slots[s].skillIndex;
            if (idx >= 0 && bb->slots[s].hasCleanse && SkillManager_CanCast(idx, bot->agentId)) {
                return s;
            }
        }
    }

    // 2. Defensive Heal / Shield when injured or outnumbered
    if (hpFrac < 0.65f || (bb->nearbyEnemyCount >= 2 && hpFrac < 0.85f)) {
        for (int s = AGENT_STRIKE_SLOTS; s < AGENT_SKILL_SLOTS; s++) {
            int idx = bb->slots[s].skillIndex;
            if (idx >= 0 && (bb->slots[s].hasHeal || bb->slots[s].hasShield) &&
                SkillManager_CanCast(idx, bot->agentId)) {
                return s;
            }
        }
    }

    // 3. Dau Phap Counter-Projectile (Duelist priority or close incoming bolt)
    if (bb->incomingProjIdx >= 0 && bb->incomingProjDist < 7.5f) {
        for (int s = 0; s < AGENT_STRIKE_SLOTS; s++) {
            int idx = bb->slots[s].skillIndex;
            if (idx < 0 || !bb->slots[s].isProjectile) continue;
            if (!SkillManager_CanCast(idx, bot->agentId)) continue;
            if (IsElementCounter(bb->slots[s].element, bb->incomingProjElem)) {
                *outCounterProj = true;
                return s;
            }
        }
    }

    if (!target) return -1;

    // 4. Score Strike Slots (Range match + Tuong Sinh follow-up + Marks + CC)
    int bestSlot = -1;
    float bestScore = -1e9f;
    int nextTuongSinh = (bot->lastCastElem >= 0) ? ((bot->lastCastElem + 1) % 5) : -1;

    for (int s = 0; s < AGENT_STRIKE_SLOTS; s++) {
        const BotSlotMeta *sm = &bb->slots[s];
        if (sm->skillIndex < 0 || !SkillManager_CanCast(sm->skillIndex, bot->agentId)) continue;

        float score = 1.0f + (float)(rand() % 20) * 0.01f;
        float rangeDiff = fabsf(targetDist - sm->idealRange);
        score -= rangeDiff * 0.12f;
        if (sm->element == nextTuongSinh) score += 0.45f;
        if (IsElementCounter(sm->element, target->currentElement)) score += 0.35f;
        if (Entity_GetMarkStacks(targetId, sm->element) > 0) {
            score += 0.30f;
        }
        if (sm->hasCC && targetDist < 7.0f) score += 0.25f;

        if (score > bestScore) {
            bestScore = score;
            bestSlot = s;
        }
    }
    return bestSlot;
}

static void SteerHumanizedMove(HeroBot *bot, const Agent *me, Vector3 moveTarget,
                               float desiredMinDist, float desiredMaxDist,
                               bool wantSprint, float dt) {
    Vector3 arenaC; float arenaR;
    Entity_GetArenaBounds(&arenaC, &arenaR);

    float dx = moveTarget.x - me->position.x;
    float dz = moveTarget.z - me->position.z;
    float dist = sqrtf(dx * dx + dz * dz);

    float rawX = 0.0f, rawZ = 0.0f;
    if (dist > 0.05f) {
        float nx = dx / dist, nz = dz / dist;
        if (dist > desiredMaxDist)      { rawX = nx;  rawZ = nz;  }
        else if (dist < desiredMinDist) { rawX = -nx; rawZ = -nz; }
        else if (desiredMinDist > 0.5f) {
            float side = sinf(bot->strafePhase * 1.15f) >= 0.0f ? 1.0f : -1.0f;
            rawX = -nz * side;
            rawZ = nx * side;
        }
    }

    if (fabsf(rawX) < 0.01f && fabsf(rawZ) < 0.01f) {
        Entity_SetRunning(bot->agentId, false);
        return;
    }

    // 8-direction Context Steering (avoids cliffs / ring-out / ally crowding)
    float bestDirX = rawX, bestDirZ = rawZ, bestSteerScore = -1e9f;
    for (int r = 0; r < 8; r++) {
        float ang = ((float)r / 8.0f) * 2.0f * PI;
        float cx = cosf(ang), cz = sinf(ang);
        float dot = cx * rawX + cz * rawZ;
        float probeX = me->position.x + cx * 2.2f;
        float probeZ = me->position.z + cz * 2.2f;
        float probeY = MapManager_GetGroundHeightAt(probeX, probeZ);
        if (probeY < -35.0f) continue;

        float score = dot;
        if (arenaR <= 50.0f) {
            float ex = probeX - arenaC.x, ez = probeZ - arenaC.z;
            if (sqrtf(ex * ex + ez * ez) > arenaR - BOT_EDGE_MARGIN) score -= 2.5f;
        }
        if (score > bestSteerScore) {
            bestSteerScore = score;
            bestDirX = cx;
            bestDirZ = cz;
        }
    }

    Entity_SetRunning(bot->agentId, wantSprint && !me->staminaExhausted);
    float baseSpd = (wantSprint && !me->staminaExhausted) ? BOT_RUN_MPS : BOT_MOVE_MPS;
    float speed = baseSpd * Entity_GetSpeedMult(bot->agentId);

    Vector3 pos = me->position;
    pos.x += bestDirX * speed * dt;
    pos.z += bestDirZ * speed * dt;
    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
    if (pos.y > -40.0f) {
        Entity_SetPosition(bot->agentId, pos);
        Entity_SetFacingYaw(bot->agentId, atan2f(bestDirX, bestDirZ));
    }
}

static void UpdateHeroBots(float dt) {
    for (int b = 0; b < AI_MAX_HERO_BOTS; b++) {
        HeroBot *bot = &s_bots[b];
        if (!bot->used) continue;
        const Agent *me = Entity_GetAgent(bot->agentId);
        if (me == NULL) { bot->used = false; continue; }

        bot->thinkTimer    -= dt;
        bot->reactionTimer -= dt;
        bot->dodgeTimer    -= dt;
        bot->basicAtkTimer -= dt;
        bot->jumpTimer     -= dt;
        bot->goalLockTimer -= dt;
        bot->strafePhase   += dt;
        if (bot->townPauseTimer > 0.0f) bot->townPauseTimer -= dt;

        // Pending humanized social reply
        if (bot->pendingSocialEmote > 0) {
            bot->socialDelayTimer -= dt;
            if (bot->socialDelayTimer <= 0.0f) {
                if (s_botSocialCount < 16) {
                    s_botSocialQueue[s_botSocialCount++] = (AIBotSocialEvent){
                        .agentId = bot->agentId,
                        .emoteId = bot->pendingSocialEmote
                    };
                }
                bot->pendingSocialEmote = 0;
            }
        }

        // Layer 1: Perceive world
        BotBlackboard bb;
        BotPerceive(bot, me, &bb);

        // Layer 2: Update Utility Goal at ~4-6 Hz (with lock hysteresis)
        if (bot->goalLockTimer <= 0.0f) {
            bot->currentGoal = SelectBestGoal(bot, &bb);
            bot->goalLockTimer = 0.25f + 0.05f * (float)(rand() % 4);
        }

        // Reflex Dodge: perpendicular Dash against close hostile projectile
        if (bot->dodgeTimer <= 0.0f && bb.incomingProjIdx >= 0 &&
            bb.incomingProjDist <= BOT_DODGE_RANGE && !Entity_IsCrowdControlled(bot->agentId)) {
            float dx = bb.incomingProjPos.x - me->position.x;
            float dz = bb.incomingProjPos.z - me->position.z;
            float sign = ((rand() & 1) == 0) ? 1.0f : -1.0f;
            Entity_Dash(bot->agentId, (Vector3){ -dz * sign, 0.0f, dx * sign }, 10.0f);
            bot->dodgeTimer = BOT_DODGE_COOLDOWN + 0.3f * (float)(rand() % 3);
        }

        // Determine Combat Target & Move Objective from active Goal
        int combatTargetId = -1;
        Vector3 moveObjective = me->position;
        float desiredMinDist = 0.0f, desiredMaxDist = 1.2f;
        bool wantSprint = false;
        bool allowAttack = true;

        switch (bot->currentGoal) {
            case GOAL_SURVIVE_RECOVER: {
                allowAttack = false;
                float mpFrac = (me->maxMana > 0.0f) ? (me->mana / me->maxMana) : 1.0f;
                if (bb.nearestEnemyHeroDist > 8.5f && mpFrac < 0.85f &&
                    me->vState == AGENT_GROUNDED && !Entity_IsCrowdControlled(bot->agentId)) {
                    if (!me->isMeditating) Entity_StartMeditate(bot->agentId);
                } else {
                    if (bb.allyBoss) moveObjective = bb.allyBoss->position;
                    else if (s_hasBotCtx && s_botCtx.safeRadius > 1.0f) moveObjective = s_botCtx.safeCenter;
                    else if (bb.nearestEnemyHero >= 0) {
                        const Agent *eh = Entity_GetAgent(bb.nearestEnemyHero);
                        if (eh) {
                            moveObjective = (Vector3){
                                me->position.x * 2.0f - eh->position.x,
                                me->position.y,
                                me->position.z * 2.0f - eh->position.z
                            };
                        }
                    }
                    wantSprint = true;
                }
            } break;

            case GOAL_MOBA_CONTEST_ORB:
                moveObjective = s_hasBotCtx ? s_botCtx.spiritOrbPos : me->position;
                desiredMaxDist = 0.8f;
                wantSprint = true;
                combatTargetId = bb.nearestEnemyHero;
                break;

            case GOAL_MOBA_ESCORT_BOSS:
                if (bb.nearestMinionNearAllyBoss >= 0) {
                    combatTargetId = bb.nearestMinionNearAllyBoss;
                } else if (bb.nearestEnemyHero >= 0 && bb.nearestEnemyHeroDist < 10.0f) {
                    combatTargetId = bb.nearestEnemyHero;
                }
                if (bb.allyBoss) {
                    moveObjective = bb.allyBoss->position;
                    desiredMinDist = 1.5f;
                    desiredMaxDist = 4.2f;
                }
                break;

            case GOAL_MOBA_ASSAULT_OBJECTIVE:
                if (bb.enemyBoss) {
                    combatTargetId = bb.enemyBossId;
                    moveObjective = bb.enemyBoss->position;
                    desiredMinDist = 4.5f;
                    desiredMaxDist = 8.0f;
                } else if (s_hasBotCtx) {
                    moveObjective = s_botCtx.pillarPos;
                    desiredMinDist = 1.0f;
                    desiredMaxDist = 4.5f;
                    combatTargetId = bb.nearestEnemyHero;
                }
                break;

            case GOAL_COMBAT_DUEL_HERO:
                combatTargetId = (bb.weakestEnemyHero >= 0) ? bb.weakestEnemyHero : bb.nearestEnemyHero;
                if (combatTargetId >= 0) {
                    const Agent *tgt = Entity_GetAgent(combatTargetId);
                    if (tgt) {
                        moveObjective = tgt->position;
                        desiredMinDist = 4.5f;
                        desiredMaxDist = 8.5f;
                    }
                }
                break;

            case GOAL_OW_WORLD_BOSS_RAID:
                if (s_hasBotCtx && s_botCtx.worldBossActive) {
                    moveObjective = s_botCtx.worldBossPos;
                    desiredMinDist = 4.5f;
                    desiredMaxDist = 8.5f;
                    wantSprint = (BotDistXZ(me->position, moveObjective) > 16.0f);
                }
                combatTargetId = (bb.enemyBossId >= 0) ? bb.enemyBossId : bb.nearestMonster;
                break;

            case GOAL_OW_HUNT_MONSTERS:
                if (bb.nearestMonster >= 0 && bb.nearestMonsterDist < 18.0f) {
                    combatTargetId = bb.nearestMonster;
                    const Agent *ma = Entity_GetAgent(combatTargetId);
                    if (ma) {
                        moveObjective = ma->position;
                        desiredMinDist = 3.8f;
                        desiredMaxDist = 7.2f;
                    }
                } else if (s_hasBotCtx && s_botCtx.poiCount > 0) {
                    if (bot->activePoiIdx < 0 || bot->activePoiIdx >= s_botCtx.poiCount ||
                        BotDistXZ(me->position, s_botCtx.pois[bot->activePoiIdx]) < 5.5f) {
                        bot->activePoiIdx = rand() % s_botCtx.poiCount;
                        if ((rand() % 4) == 0) bot->townPauseTimer = 6.0f;
                    }
                    moveObjective = s_botCtx.pois[bot->activePoiIdx];
                    desiredMaxDist = 4.0f;
                    wantSprint = true;
                }
                break;

            case GOAL_OW_TOWN_QUEST:
                allowAttack = false;
                if (s_hasBotCtx) {
                    moveObjective = ((bot->outfitSeed & 1) == 0) ? s_botCtx.npcQuestPos : s_botCtx.npcAlchemistPos;
                    desiredMaxDist = 2.6f;
                    if (BotDistXZ(me->position, moveObjective) <= 3.2f && bot->townPauseTimer <= 0.0f) {
                        bot->townPauseTimer = 3.5f + 0.5f * (float)(rand() % 4);
                    }
                }
                break;

            default:
                break;
        }

        // Layer 3: Movement
        if (me->vState == AGENT_GROUNDED && !Entity_IsCrowdControlled(bot->agentId) &&
            me->dashTimer <= 0.0f && !me->isMeditating) {
            SteerHumanizedMove(bot, me, moveObjective, desiredMinDist, desiredMaxDist, wantSprint, dt);

            // Occasional Khinh Cong jump while sprinting long distances
            if (wantSprint && bot->jumpTimer <= 0.0f && BotDistXZ(me->position, moveObjective) > 14.0f) {
                Entity_Jump(bot->agentId, 4.2f);
                bot->jumpTimer = 4.0f + 0.5f * (float)(rand() % 5);
            }
        }

        // Layer 3: Combat (Skill Cast + Basic Attack Weaving)
        const Agent *target = (combatTargetId >= 0) ? Entity_GetAgent(combatTargetId) : NULL;
        if (target && allowAttack && !me->isMeditating && !Entity_IsStunned(bot->agentId)) {
            if (bot->lastTargetId == combatTargetId && dt > 0.0001f) {
                bot->targetVelEst.x = (target->position.x - bot->lastTargetPos.x) / dt;
                bot->targetVelEst.z = (target->position.z - bot->lastTargetPos.z) / dt;
            } else {
                bot->targetVelEst = (Vector3){ 0 };
            }
            bot->lastTargetId = combatTargetId;
            bot->lastTargetPos = target->position;

            float tDist = BotDistXZ(me->position, target->position);
            if (bot->thinkTimer <= 0.0f) {
                // Humanized reaction delay: 0.22s..0.48s cadence + occasional hesitation
                bot->thinkTimer = 0.28f + 0.06f * (float)(rand() % 5);
                bool counterProj = false;
                int slot = PickBestSkillSlot(bot, &bb, combatTargetId, target, tDist, &counterProj);
                if (slot >= 0) {
                    int skillIndex = me->equippedSkills[slot];
                    Vector3 aimPt = counterProj ? bb.incomingProjPos : (Vector3){
                        target->position.x + bot->targetVelEst.x * 0.18f + ((float)(rand() % 100) * 0.01f - 0.5f) * 0.45f,
                        target->position.y,
                        target->position.z + bot->targetVelEst.z * 0.18f + ((float)(rand() % 100) * 0.01f - 0.5f) * 0.45f
                    };
                    if (CastSkill(skillIndex, bot->agentId, me->position, aimPt,
                                  (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f })) {
                        SkillManager_TriggerCooldown(skillIndex, bot->agentId, Skill_GetCooldown(skillIndex));
                        Entity_SetFacingYaw(bot->agentId, atan2f(aimPt.x - me->position.x, aimPt.z - me->position.z));
                        bot->lastCastElem = bb.slots[slot].element;
                        bot->thinkTimer = 0.55f + 0.10f * (float)(rand() % 5);
                        if (s_botCastCount < 16) {
                            s_botCasts[s_botCastCount++] = (HeroBotCast){ bot->agentId, skillIndex, aimPt };
                        }
                    }
                }
            }

            // Melee weaving when close and skills on cooldown
            if (tDist <= 1.75f && bot->basicAtkTimer <= 0.0f) {
                BasicAttackType bt = (BasicAttackType)(rand() % 3);
                Vector3 tp = { 0 }, wp = { 0 };
                int we = 0;
                Entity_ExecuteBasicAttack(bot->agentId, bt, &tp, &wp, &we);
                Entity_SetFacingYaw(bot->agentId, atan2f(target->position.x - me->position.x,
                                                         target->position.z - me->position.z));
                bot->basicAtkTimer = 0.85f + 0.10f * (float)(rand() % 4);
                if (s_botCastCount < 16) {
                    s_botCasts[s_botCastCount++] = (HeroBotCast){
                        bot->agentId, 250 + (int)bt, target->position
                    };
                }
            }
        }
    }
}

int AI_SpawnHeroBotEx(Vector3 pos, AgentTeam team, AIBotPersona persona, const char *nameOverride) {
    int slot = -1;
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++) {
        if (!s_bots[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;
    int agentId = Entity_SpawnAgent(pos, 100.0f, 0, team, ARCH_HERO);
    if (agentId < 0) return -1;

    HeroBot *b = &s_bots[slot];
    memset(b, 0, sizeof(*b));
    b->used        = true;
    b->agentId     = agentId;
    b->persona     = (persona >= 0 && persona < AI_PERSONA_COUNT) ? persona : AI_PERSONA_BALANCED;
    b->outfitSeed  = (slot * 131 + agentId * 37 + (rand() % 97)) & 0x7FFF;
    b->thinkTimer  = 0.25f + 0.15f * (float)slot;
    b->strafePhase = (float)slot * 1.7f;
    b->activePoiIdx = -1;
    b->lastTargetId = -1;
    b->lastCastElem = -1;
    b->sparRequesterId = -1;

    if (nameOverride && nameOverride[0] != '\0') {
        snprintf(b->displayName, sizeof(b->displayName), "%s", nameOverride);
    } else {
        snprintf(b->displayName, sizeof(b->displayName), "%s",
                 kWuxiaBotNames[(slot + (b->outfitSeed % kWuxiaBotNameCount)) % kWuxiaBotNameCount]);
    }
    return agentId;
}

int AI_SpawnHeroBot(Vector3 pos, AgentTeam team) {
    static const AIBotPersona kCycle[4] = {
        AI_PERSONA_VANGUARD, AI_PERSONA_GUARDIAN, AI_PERSONA_DUELIST, AI_PERSONA_BALANCED
    };
    int existing = AI_GetHeroBotCount(TEAM_ALLY) + AI_GetHeroBotCount(TEAM_ENEMY);
    return AI_SpawnHeroBotEx(pos, team, kCycle[existing & 3], NULL);
}

void AI_ClearHeroBots(void) {
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++) {
        if (s_bots[i].used && Entity_GetAgent(s_bots[i].agentId) != NULL)
            Entity_ApplyDamage(s_bots[i].agentId, 1e9f, (Vector3){ 0 });
        s_bots[i].used = false;
    }
    s_botCastCount = 0;
    s_botSocialCount = 0;
}

bool AI_IsHeroBot(int agentId) {
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++)
        if (s_bots[i].used && s_bots[i].agentId == agentId) return true;
    return false;
}

int AI_GetHeroBotCount(AgentTeam team) {
    int n = 0;
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++) {
        if (!s_bots[i].used) continue;
        const Agent *a = Entity_GetAgent(s_bots[i].agentId);
        if (a != NULL && a->team == team) n++;
    }
    return n;
}

void AI_SetBotWorldContext(const AIBotWorldContext *ctx) {
    if (!ctx) { s_hasBotCtx = false; return; }
    s_botCtx = *ctx;
    s_hasBotCtx = true;
}

bool AI_GetHeroBotProfile(int agentId, AIBotProfile *outProfile) {
    if (!outProfile) return false;
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++) {
        if (s_bots[i].used && s_bots[i].agentId == agentId) {
            outProfile->agentId = agentId;
            outProfile->persona = s_bots[i].persona;
            outProfile->outfitSeed = s_bots[i].outfitSeed;
            snprintf(outProfile->displayName, sizeof(outProfile->displayName),
                     "%s", s_bots[i].displayName);
            return true;
        }
    }
    return false;
}

void AI_NotifyBotSocialEvent(int senderAgentId, int targetBotAgentId, int emoteId) {
    const Agent *sender = Entity_GetAgent(senderAgentId);
    if (!sender) return;
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++) {
        if (!s_bots[i].used) continue;
        if (targetBotAgentId >= 0 && s_bots[i].agentId != targetBotAgentId) continue;
        const Agent *ba = Entity_GetAgent(s_bots[i].agentId);
        if (!ba || BotDistXZ(sender->position, ba->position) > 22.0f) continue;

        if (emoteId == 1) { // Greet -> bow back
            s_bots[i].pendingSocialEmote = 1;
            s_bots[i].socialDelayTimer = 0.6f + 0.2f * (float)(rand() % 4);
        } else if (emoteId == 2) { // CallBoss -> rally
            s_bots[i].pendingSocialEmote = 2;
            s_bots[i].socialDelayTimer = 0.8f + 0.2f * (float)(rand() % 3);
        } else if (emoteId == 10) { // OW_EMOTE_SPAR_ASK -> accept
            s_bots[i].pendingSocialEmote = 11;
            s_bots[i].socialDelayTimer = 0.9f;
            s_bots[i].sparRequesterId = senderAgentId;
        }
    }
}

int AI_PollBotSocialEvents(AIBotSocialEvent *out, int max) {
    if (!out || max <= 0) { s_botSocialCount = 0; return 0; }
    int n = (s_botSocialCount < max) ? s_botSocialCount : max;
    for (int i = 0; i < n; i++) out[i] = s_botSocialQueue[i];
    s_botSocialCount = 0;
    return n;
}

int AI_PollHeroCasts(HeroBotCast *out, int max) {
    if (out == NULL || max <= 0) { s_botCastCount = 0; return 0; }
    int n = (s_botCastCount < max) ? s_botCastCount : max;
    for (int i = 0; i < n; i++) out[i] = s_botCasts[i];
    s_botCastCount = 0;
    return n;
}
