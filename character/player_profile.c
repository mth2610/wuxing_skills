// character/player_profile.c
#include "character/player_profile.h"
#include "skills/skill_manager.h"
#include "raylib.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PlayerProfile s_profile = {0};
static char s_savePath[256] = PLAYER_PROFILE_DEFAULT_SAVE_PATH;
static bool s_initialized = false;
static bool s_dirty = false;
static float s_dirtyFlushTimer = 0.0f;

static const char *kDefaultStarterSkills[PROFILE_SKILL_SLOTS] = {
    "TTQP_PHA_LANG_QUYEN",
    "TTQP_NGHICH_LUU_CAM_NA",
    "TTQP_BANG_TRIEU_DAO_HAI",
    "TTQP_HAN_UYEN_QUYEN_TOAN",
    "TTQP_NO_HAI_LONG_QUYEN",
    "TTQP_THUY_KINH_QUYEN_THUAN",
    "TTQP_TRIEU_TICH_LINH_TUYEN"
};

static uint32_t ComputeProfileHash(const PlayerProfile *p) {
    uint32_t h = 2166136261u;
    const unsigned char *bytes = (const unsigned char *)p;
    size_t len = offsetof(PlayerProfile, integrityHash);
    for (size_t i = 0; i < len; i++) {
        h ^= bytes[i];
        h *= 16777619u;
    }
    return h;
}

static void InitDefaultProfile(PlayerProfile *p) {
    memset(p, 0, sizeof(*p));
    p->gold = 500;
    p->daTauPoints = 0;
    p->daTauTokens = 2;
    p->herbs = 0;
    p->potions[POTION_HEAL_HP] = 2;
    p->potions[POTION_RESTORE_MANA] = 1;
    p->potions[POTION_EARTH_MIGHT] = 0;
    p->potions[POTION_SKILL_XP_BOOST] = 0;
    p->daTauStreak = 0;
    p->daTauTotalCompleted = 0;

    for (int i = 0; i < MAX_PROFILE_SKILLS; i++) {
        p->skillLevel[i] = 1;
        p->skillXp[i] = 0;
        p->skillUnlocked[i] = true;
    }

    for (int s = 0; s < PROFILE_SKILL_SLOTS; s++) {
        snprintf(p->equippedSkillNames[s], PROFILE_SKILL_NAME_LEN, "%s", kDefaultStarterSkills[s]);
        int idx = Skill_GetIndexByName(kDefaultStarterSkills[s]);
        if (idx >= 0 && idx < MAX_PROFILE_SKILLS) {
            p->skillUnlocked[idx] = true;
        }
    }
    p->integrityHash = ComputeProfileHash(p);
}

static bool ParseJsonInt(const char *json, const char *key, int *outVal) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '\0') return false;
    *outVal = atoi(p);
    return true;
}

static bool ParseJsonString(const char *json, const char *key, char *out, int outSize) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p = strchr(p, '\"');
    if (!p) return false;
    p++;
    const char *end = strchr(p, '\"');
    if (!end) return false;
    int len = (int)(end - p);
    if (len >= outSize) len = outSize - 1;
    memcpy(out, p, (size_t)len);
    out[len] = '\0';
    return true;
}

bool PlayerProfile_LoadFromFile(PlayerProfile *out, const char *filePath) {
    if (!out || !filePath || !FileExists(filePath)) return false;
    char *text = LoadFileText(filePath);
    if (!text) return false;

    InitDefaultProfile(out);
    ParseJsonInt(text, "gold", &out->gold);
    ParseJsonInt(text, "daTauPoints", &out->daTauPoints);
    ParseJsonInt(text, "daTauTokens", &out->daTauTokens);
    ParseJsonInt(text, "herbs", &out->herbs);
    ParseJsonInt(text, "daTauStreak", &out->daTauStreak);
    ParseJsonInt(text, "daTauTotalCompleted", &out->daTauTotalCompleted);

    for (int pt = 0; pt < POTION_TYPE_COUNT; pt++) {
        char key[32];
        snprintf(key, sizeof(key), "potion_%d", pt);
        ParseJsonInt(text, key, &out->potions[pt]);
    }

    for (int e = 0; e < 5; e++) {
        char key[32];
        snprintf(key, sizeof(key), "stone_%d", e);
        ParseJsonInt(text, key, &out->elementalStones[e]);
    }
    for (int st = 0; st < PROFILE_MAX_SETS; st++) {
        char key[32];
        snprintf(key, sizeof(key), "scroll_%d", st);
        ParseJsonInt(text, key, &out->setScrolls[st]);
    }
    ParseJsonInt(text, "pvpMatchesToday", &out->pvpMatchesToday);
    int dayStamp = 0;
    if (ParseJsonInt(text, "pvpDayStamp", &dayStamp) && dayStamp >= 0) {
        out->pvpDayStamp = (uint32_t)dayStamp;
    }

    for (int s = 0; s < PROFILE_SKILL_SLOTS; s++) {
        char key[32];
        snprintf(key, sizeof(key), "slot_%d", s);
        char buf[PROFILE_SKILL_NAME_LEN] = {0};
        if (ParseJsonString(text, key, buf, sizeof(buf)) && buf[0] != '\0') {
            snprintf(out->equippedSkillNames[s], PROFILE_SKILL_NAME_LEN, "%s", buf);
        }
    }

    int count = GetRegisteredSkillCount();
    if (count > MAX_PROFILE_SKILLS) count = MAX_PROFILE_SKILLS;
    for (int i = 0; i < count; i++) {
        const char *name = GetRegisteredSkillName(i);
        if (!name || name[0] == '\0') continue;
        char kUnl[64], kLvl[64], kXp[64];
        snprintf(kUnl, sizeof(kUnl), "sk_unl_%s", name);
        snprintf(kLvl, sizeof(kLvl), "sk_lvl_%s", name);
        snprintf(kXp, sizeof(kXp), "sk_xp_%s", name);
        int v = 0;
        if (ParseJsonInt(text, kUnl, &v)) out->skillUnlocked[i] = (v != 0);
        if (ParseJsonInt(text, kLvl, &v)) {
            if (v < 1) v = 1;
            if (v > 10) v = 10;
            out->skillLevel[i] = (uint8_t)v;
        }
        if (ParseJsonInt(text, kXp, &v) && v >= 0) {
            out->skillXp[i] = (uint32_t)v;
        }
    }

    UnloadFileText(text);
    out->integrityHash = ComputeProfileHash(out);
    return true;
}

bool PlayerProfile_SaveToFile(const PlayerProfile *p, const char *filePath) {
    if (!p || !filePath) return false;
    FILE *f = fopen(filePath, "wb");
    if (!f) return false;

    uint32_t hash = ComputeProfileHash(p);
    fprintf(f, "{\n");
    fprintf(f, "  \"gold\": %d,\n", p->gold);
    fprintf(f, "  \"daTauPoints\": %d,\n", p->daTauPoints);
    fprintf(f, "  \"daTauTokens\": %d,\n", p->daTauTokens);
    fprintf(f, "  \"herbs\": %d,\n", p->herbs);
    fprintf(f, "  \"daTauStreak\": %d,\n", p->daTauStreak);
    fprintf(f, "  \"daTauTotalCompleted\": %d,\n", p->daTauTotalCompleted);
    for (int pt = 0; pt < POTION_TYPE_COUNT; pt++) {
        fprintf(f, "  \"potion_%d\": %d,\n", pt, p->potions[pt]);
    }
    for (int e = 0; e < 5; e++) {
        fprintf(f, "  \"stone_%d\": %d,\n", e, p->elementalStones[e]);
    }
    for (int st = 0; st < PROFILE_MAX_SETS; st++) {
        if (p->setScrolls[st] > 0) {
            fprintf(f, "  \"scroll_%d\": %d,\n", st, p->setScrolls[st]);
        }
    }
    fprintf(f, "  \"pvpMatchesToday\": %d,\n", p->pvpMatchesToday);
    fprintf(f, "  \"pvpDayStamp\": %u,\n", (unsigned int)p->pvpDayStamp);
    for (int s = 0; s < PROFILE_SKILL_SLOTS; s++) {
        fprintf(f, "  \"slot_%d\": \"%s\",\n", s, p->equippedSkillNames[s]);
    }
    int count = GetRegisteredSkillCount();
    if (count > MAX_PROFILE_SKILLS) count = MAX_PROFILE_SKILLS;
    for (int i = 0; i < count; i++) {
        const char *name = GetRegisteredSkillName(i);
        if (!name || name[0] == '\0') continue;
        if (!p->skillUnlocked[i] && p->skillLevel[i] <= 1 && p->skillXp[i] == 0) continue;
        fprintf(f, "  \"sk_unl_%s\": %d,\n", name, p->skillUnlocked[i] ? 1 : 0);
        fprintf(f, "  \"sk_lvl_%s\": %d,\n", name, (int)p->skillLevel[i]);
        fprintf(f, "  \"sk_xp_%s\": %u,\n", name, (unsigned int)p->skillXp[i]);
    }
    fprintf(f, "  \"integrityHash\": %u\n", (unsigned int)hash);
    fprintf(f, "}\n");
    fclose(f);
    return true;
}

void PlayerProfile_InitGlobal(const char *savePath) {
    if (savePath && savePath[0] != '\0') {
        snprintf(s_savePath, sizeof(s_savePath), "%s", savePath);
    }
    if (!PlayerProfile_LoadFromFile(&s_profile, s_savePath)) {
        InitDefaultProfile(&s_profile);
    }
    s_initialized = true;
}

PlayerProfile *PlayerProfile_GetGlobal(void) {
    if (!s_initialized) {
        PlayerProfile_InitGlobal(s_savePath);
    }
    return &s_profile;
}

bool PlayerProfile_SaveGlobal(void) {
    if (!s_initialized) return false;
    s_dirty = false;
    s_dirtyFlushTimer = 0.0f;
    s_profile.integrityHash = ComputeProfileHash(&s_profile);
    return PlayerProfile_SaveToFile(&s_profile, s_savePath);
}

void PlayerProfile_MarkDirty(void) {
    if (!s_initialized) PlayerProfile_InitGlobal(s_savePath);
    s_dirty = true;
}

void PlayerProfile_FlushIfDirty(float dt, bool forceNow) {
    if (!s_initialized || !s_dirty) return;
    s_dirtyFlushTimer += dt;
    if (forceNow || s_dirtyFlushTimer >= 12.0f) {
        PlayerProfile_SaveGlobal();
    }
}

bool PlayerProfile_UnlockSkill(int skillIndex) {
    PlayerProfile *p = PlayerProfile_GetGlobal();
    if (skillIndex < 0 || skillIndex >= MAX_PROFILE_SKILLS) return false;
    if (p->skillUnlocked[skillIndex]) return false;
    p->skillUnlocked[skillIndex] = true;
    if (p->skillLevel[skillIndex] < 1) p->skillLevel[skillIndex] = 1;
    p->integrityHash = ComputeProfileHash(p);
    return true;
}

bool PlayerProfile_IsSkillUnlocked(int skillIndex) {
    const PlayerProfile *p = PlayerProfile_GetGlobal();
    if (skillIndex < 0 || skillIndex >= MAX_PROFILE_SKILLS) return false;
    return p->skillUnlocked[skillIndex];
}

uint32_t PlayerProfile_XpForNextLevel(int currentLevel) {
    if (currentLevel < 1) currentLevel = 1;
    if (currentLevel >= 10) return 0;
    return (uint32_t)(currentLevel * 100);
}

bool PlayerProfile_AddSkillXp(int skillIndex, uint32_t xpAmount) {
    PlayerProfile *p = PlayerProfile_GetGlobal();
    if (skillIndex < 0 || skillIndex >= MAX_PROFILE_SKILLS) return false;
    if (!p->skillUnlocked[skillIndex]) p->skillUnlocked[skillIndex] = true;
    if (p->skillLevel[skillIndex] < 1) p->skillLevel[skillIndex] = 1;
    if (p->skillLevel[skillIndex] >= 10) return false;

    p->skillXp[skillIndex] += xpAmount;
    bool leveledUp = false;
    while (p->skillLevel[skillIndex] < 10) {
        uint32_t req = PlayerProfile_XpForNextLevel((int)p->skillLevel[skillIndex]);
        if (req == 0 || p->skillXp[skillIndex] < req) break;
        p->skillXp[skillIndex] -= req;
        p->skillLevel[skillIndex]++;
        leveledUp = true;
    }
    if (p->skillLevel[skillIndex] >= 10) {
        p->skillXp[skillIndex] = 0;
    }
    p->integrityHash = ComputeProfileHash(p);
    return leveledUp;
}

void PlayerProfile_EquipSkillSlot(int slot, int skillIndex) {
    PlayerProfile *p = PlayerProfile_GetGlobal();
    if (slot < 0 || slot >= PROFILE_SKILL_SLOTS) return;
    if (skillIndex < 0) {
        p->equippedSkillNames[slot][0] = '\0';
        return;
    }
    const char *name = GetRegisteredSkillName(skillIndex);
    if (name && name[0] != '\0') {
        snprintf(p->equippedSkillNames[slot], PROFILE_SKILL_NAME_LEN, "%s", name);
        p->integrityHash = ComputeProfileHash(p);
    }
}

int PlayerProfile_GetEffectiveSkillLevel(int skillIndex, bool isPvP) {
    if (isPvP) return 1; // 100% Fair-play normalization in PvP
    const PlayerProfile *p = PlayerProfile_GetGlobal();
    if (skillIndex < 0 || skillIndex >= MAX_PROFILE_SKILLS) return 1;
    int lvl = (int)p->skillLevel[skillIndex];
    if (lvl < 1) lvl = 1;
    if (lvl > 10) lvl = 10;
    return lvl;
}

void PlayerProfile_GetSkillScaling(int skillIndex, bool isPvP,
                                   int *outLevel, int *outMilestone,
                                   int *outQuantity, float *outSizeScale,
                                   float *outDamageMult, float *outCooldownMult) {
    int lvl = PlayerProfile_GetEffectiveSkillLevel(skillIndex, isPvP);
    int milestone = 0;
    int qty = 1;
    float size = 1.0f;
    float dmgMult = 1.0f;
    float cdMult = 1.0f;

    if (!isPvP && lvl > 1) {
        dmgMult = 1.0f + 0.18f * (float)(lvl - 1);
        if (lvl >= 10) {
            milestone = 3; qty = 4; size = 1.50f; cdMult = 0.70f;
        } else if (lvl >= 7) {
            milestone = 2; qty = 3; size = 1.35f; cdMult = 0.80f;
        } else if (lvl >= 4) {
            milestone = 1; qty = 2; size = 1.20f; cdMult = 0.90f;
        }
    }

    if (outLevel)        *outLevel = lvl;
    if (outMilestone)    *outMilestone = milestone;
    if (outQuantity)     *outQuantity = qty;
    if (outSizeScale)    *outSizeScale = size;
    if (outDamageMult)   *outDamageMult = dmgMult;
    if (outCooldownMult) *outCooldownMult = cdMult;
}

void PlayerProfile_AddPotion(PotionType type, int count) {
    if (type < 0 || type >= POTION_TYPE_COUNT || count <= 0) return;
    PlayerProfile *p = PlayerProfile_GetGlobal();
    p->potions[type] += count;
    p->integrityHash = ComputeProfileHash(p);
}

bool PlayerProfile_ConsumePotion(PotionType type) {
    if (type < 0 || type >= POTION_TYPE_COUNT) return false;
    PlayerProfile *p = PlayerProfile_GetGlobal();
    if (p->potions[type] <= 0) return false;
    p->potions[type]--;
    p->integrityHash = ComputeProfileHash(p);
    return true;
}
