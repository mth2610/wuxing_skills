#ifndef SKILLS_VO_HOC_CODEX_H
#define SKILLS_VO_HOC_CODEX_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VO_HOC_MAX_FX_PER_SKILL 6
#define VO_HOC_MAX_SETS         16
#define VO_HOC_TIER_MINOR_COUNT 4 /* Tiểu Thành: >= 4 chiêu cùng bộ */
#define VO_HOC_TIER_MAJOR_COUNT 7 /* Đại Thành: đủ 7/7 chiêu cùng bộ */

typedef enum {
    VH_SET_NONE = 0,
    VH_SET_THUY_TRIEU_QUYEN_PHAP = 1, /* Bộ Thủy 7 chiêu (SWE) */
    VH_SET_COUNT
} VoHocSetId;

typedef enum {
    VH_ROLE_STRIKE = 0,  /* Chiêu Đả — chỉ lắp ô 0..4 (phím 1..5) */
    VH_ROLE_SUPPORT = 1  /* Chiêu Trợ/Buff — chỉ lắp ô 5..6 (phím 6..7) */
} VoHocRole;

typedef enum {
    VH_RANGE_MELEE = 0,
    VH_RANGE_MID,
    VH_RANGE_LONG,
    VH_RANGE_SELF
} VoHocRange;

typedef enum {
    VH_HIT_MELEE = 0,
    VH_HIT_FAST,
    VH_HIT_SLOW,
    VH_HIT_WAVE,
    VH_HIT_GROUND,
    VH_HIT_ZONE,
    VH_HIT_SELF
} VoHocHit;

typedef enum {
    VH_SHAPE_SINGLE = 0,
    VH_SHAPE_SMALL,
    VH_SHAPE_LINE,
    VH_SHAPE_CONE,
    VH_SHAPE_MEDIUM,
    VH_SHAPE_LARGE,
    VH_SHAPE_NONE
} VoHocShape;

typedef enum {
    VH_FX_DMG = 0,
    VH_FX_BURN,
    VH_FX_POISON_DMG,
    VH_FX_POISON_ANTIHEAL,
    VH_FX_BLEED,
    VH_FX_STUN,
    VH_FX_FREEZE,
    VH_FX_LAUNCH,
    VH_FX_KB,
    VH_FX_PULL,
    VH_FX_ROOT,
    VH_FX_SILENCE,
    VH_FX_SLOW,
    VH_FX_VULN,
    VH_FX_WEAKEN,
    VH_FX_BLIND,
    VH_FX_MARK,
    VH_FX_SHIELD,
    VH_FX_HEAL,
    VH_FX_BLOCK,
    VH_FX_CLEANSE,
    VH_FX_MANA,
    VH_FX_SPEED,
    VH_FX_CDR,
    VH_FX_COUNT
} VoHocFx;

typedef enum {
    VH_TGT_ENEMY = 0,
    VH_TGT_ALLY,
    VH_TGT_ONCE
} VoHocTarget;

typedef struct {
    VoHocFx     fx;
    VoHocTarget tgt;
    float       amount;
} VoHocEffect;

typedef struct {
    const char *skillName;      /* Exact RegisterSkill name */
    const char *displayName;    /* Vietnamese display name */
    VoHocSetId  setId;
    int         element;        /* 0=Water,1=Wood,2=Fire,3=Earth,4=Metal */
    VoHocRole   role;
    float       mana;
    float       cooldown;
    float       castSecs;
    VoHocRange  range;
    VoHocHit    hit;
    VoHocShape  shape;
    bool        allyTeamScope;  /* Buff affects nearby allies (N_ally 1 -> 2) */
    float       uptimeEnemy;    /* 1.0f for instant; <1.0f for persistent enemy zone */
    float       uptimeAlly;     /* 1.0f for instant; <1.0f for persistent ally zone */
    float       baseDamage;     /* Direct PvP Lv.1 base damage (0 for pure support) */
    VoHocEffect fx[VO_HOC_MAX_FX_PER_SKILL];
    int         fxCount;
} VoHocSkillDef;

typedef struct {
    float budget;
    float value;
    float devPct;
    float vSolo;
    float vGroup;
    float q;
    float share[4];   /* 0=Công, 1=Thủ, 2=Khống, 3=Trợ (raw UL) */
    int   radar[4];   /* 0..10 normalized */
    float ccPct;      /* % of value in Khống */
} VoHocEval;

typedef struct {
    float totalBudget;
    float totalValue;
    float avgQ;
    int   radar[4];      /* 0..10 normalized for equipped loadout */
    int   dominantSetId; /* Set with highest piece count (or VH_SET_NONE) */
    int   dominantCount; /* Number of pieces of dominantSetId */
    int   setTier;       /* 0 = none, 4 = Tiểu Thành, 7 = Đại Thành */
} VoHocLoadoutSummary;

const VoHocSkillDef *VoHoc_GetAllSkills(int *outCount);
const VoHocSkillDef *VoHoc_FindByName(const char *skillName);
const char          *VoHoc_GetSetName(VoHocSetId setId);

/* Evaluates a single skill's Budget (B), Value (V), Q, and 4-axis Radar. */
VoHocEval VoHoc_Evaluate(const VoHocSkillDef *def);

/* Counts how many equipped skill indices belong to setId. */
int VoHoc_CountSetPieces(const int *equippedSkillIds, int count, VoHocSetId setId);

/* Computes combined radar, Q, and active set tier for a 7-slot loadout. */
VoHocLoadoutSummary VoHoc_EvalLoadout(const int *equippedSkillIds, int count);

/* Role check for 7-slot loadout: slots 0..4 accept STRIKE (or legacy), slots 5..6 accept SUPPORT. */
bool VoHoc_IsSkillRoleCompatibleWithSlot(const char *skillName, int slot);
VoHocRole VoHoc_GetSkillRoleByName(const char *skillName);

/* Machine-checks constraints C1-C8 for all registered sets. Returns true iff all pass. */
bool VoHoc_ValidateAll(char *errBuf, int errLen);

/* Shop pricing helper (VO_HOC_BALANCE.md §7): 300 gold + 6 stones, 50% off for 1st skill in set. */
void VoHoc_GetSkillUnlockCost(const char *skillName, const bool *unlockedFlags, int skillCount,
                              int *outGold, int *outElement, int *outStones, VoHocSetId *outSetId);

#ifdef __cplusplus
}
#endif

#endif /* SKILLS_VO_HOC_CODEX_H */
