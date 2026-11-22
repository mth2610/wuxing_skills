#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/skill_manager.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define VH_LAMBDA_CD      3.0f
#define VH_HP_PVP         100.0f
#define VH_DEV_MAX_PCT    5.0f
#define VH_STANDALONE_MIN 0.95f
#define VH_BURST_CAP      (0.90f * VH_HP_PVP)
#define VH_HARD_CC_SINGLE 1.0f
#define VH_HARD_CC_SET    2.5f
#define VH_CC_SHARE_MAX   45.0f

static const float kRangeMult[4] = {
    1.20f, /* MELEE */
    1.00f, /* MID */
    0.90f, /* LONG */
    1.00f  /* SELF */
};

static const float kHitProb[7] = {
    0.85f, /* MELEE */
    0.75f, /* FAST */
    0.65f, /* SLOW */
    0.80f, /* WAVE */
    0.55f, /* GROUND */
    1.00f, /* ZONE */
    1.00f  /* SELF */
};

static const float kEnemyCount[7][2] = {
    { 1.0f, 1.0f }, /* SINGLE */
    { 1.0f, 1.4f }, /* SMALL */
    { 1.0f, 1.8f }, /* LINE */
    { 1.0f, 2.0f }, /* CONE */
    { 1.0f, 2.0f }, /* MEDIUM */
    { 1.0f, 2.4f }, /* LARGE */
    { 0.0f, 0.0f }  /* NONE */
};

static const float kFxUnitUL[VH_FX_COUNT] = {
    1.00f,  /* DMG */
    0.85f,  /* BURN */
    0.85f,  /* POISON_DMG */
    4.00f,  /* POISON_ANTIHEAL */
    0.80f,  /* BLEED */
    12.00f, /* STUN */
    14.00f, /* FREEZE */
    13.00f, /* LAUNCH */
    2.50f,  /* KB */
    3.50f,  /* PULL */
    8.00f,  /* ROOT */
    9.00f,  /* SILENCE */
    10.00f, /* SLOW */
    12.00f, /* VULN */
    10.00f, /* WEAKEN */
    7.00f,  /* BLIND */
    3.50f,  /* MARK (Trieu An: 3 stacks -> 0.75s Freeze = 10.5 UL -> 3.5/stack) */
    0.80f,  /* SHIELD */
    1.00f,  /* HEAL */
    8.00f,  /* BLOCK */
    10.00f, /* CLEANSE */
    0.50f,  /* MANA */
    5.00f,  /* SPEED */
    3.00f   /* CDR */
};

/* Radar axis: 0=Công, 1=Thủ, 2=Khống, 3=Trợ */
static const int kFxAxis[VH_FX_COUNT] = {
    0, /* DMG */
    0, /* BURN */
    0, /* POISON_DMG */
    2, /* POISON_ANTIHEAL */
    0, /* BLEED */
    2, /* STUN */
    2, /* FREEZE */
    2, /* LAUNCH */
    2, /* KB */
    2, /* PULL */
    2, /* ROOT */
    2, /* SILENCE */
    2, /* SLOW */
    3, /* VULN */
    1, /* WEAKEN */
    1, /* BLIND */
    2, /* MARK */
    1, /* SHIELD */
    1, /* HEAL */
    1, /* BLOCK */
    1, /* CLEANSE */
    3, /* MANA */
    3, /* SPEED */
    3  /* CDR */
};

static const VoHocSkillDef kVoHocSkills[] = {
    /* ===== BỘ 1: THỦY TRIỀU QUYỀN PHÁP (Hệ Thủy — 5 Đả + 2 Trợ, nhánh SWE) ===== */
    {
        .skillName = "TTQP_PHA_LANG_QUYEN",
        .displayName = "Phá Lãng Quyền",
        .setId = VH_SET_THUY_TRIEU_QUYEN_PHAP,
        .element = 0,
        .role = VH_ROLE_STRIKE,
        .mana = 14.0f, .cooldown = 1.4f, .castSecs = 0.25f,
        .range = VH_RANGE_MID, .hit = VH_HIT_FAST, .shape = VH_SHAPE_SINGLE,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 18.5f,
        .fx = {
            { VH_FX_DMG,  VH_TGT_ENEMY, 18.5f },
            { VH_FX_SLOW, VH_TGT_ENEMY, 0.20f * 1.0f },
            { VH_FX_MARK, VH_TGT_ENEMY, 1.0f }
        },
        .fxCount = 3
    },
    {
        .skillName = "TTQP_NGHICH_LUU_CAM_NA",
        .displayName = "Nghịch Lưu Cầm Nã",
        .setId = VH_SET_THUY_TRIEU_QUYEN_PHAP,
        .element = 0,
        .role = VH_ROLE_STRIKE,
        .mana = 20.0f, .cooldown = 5.5f, .castSecs = 0.35f,
        .range = VH_RANGE_MID, .hit = VH_HIT_FAST, .shape = VH_SHAPE_SMALL,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 18.0f,
        .fx = {
            { VH_FX_DMG,  VH_TGT_ENEMY, 18.0f },
            { VH_FX_PULL, VH_TGT_ENEMY, 4.0f },
            { VH_FX_ROOT, VH_TGT_ENEMY, 0.80f },
            { VH_FX_MARK, VH_TGT_ENEMY, 1.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "TTQP_BANG_TRIEU_DAO_HAI",
        .displayName = "Băng Triều Đảo Hải",
        .setId = VH_SET_THUY_TRIEU_QUYEN_PHAP,
        .element = 0,
        .role = VH_ROLE_STRIKE,
        .mana = 24.0f, .cooldown = 7.5f, .castSecs = 0.40f,
        .range = VH_RANGE_MID, .hit = VH_HIT_WAVE, .shape = VH_SHAPE_CONE,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 16.0f,
        .fx = {
            { VH_FX_DMG,   VH_TGT_ENEMY, 16.0f },
            { VH_FX_KB,    VH_TGT_ENEMY, 5.5f },
            { VH_FX_SLOW,  VH_TGT_ENEMY, 0.30f * 1.5f },
            { VH_FX_MARK,  VH_TGT_ENEMY, 1.0f },
            { VH_FX_BLOCK, VH_TGT_ONCE,  0.6f }
        },
        .fxCount = 5
    },
    {
        .skillName = "TTQP_HAN_UYEN_QUYEN_TOAN",
        .displayName = "Hàn Uyên Quyền Toản",
        .setId = VH_SET_THUY_TRIEU_QUYEN_PHAP,
        .element = 0,
        .role = VH_ROLE_STRIKE,
        .mana = 26.0f, .cooldown = 8.0f, .castSecs = 0.70f,
        .range = VH_RANGE_MID, .hit = VH_HIT_GROUND, .shape = VH_SHAPE_LARGE,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 27.5f,
        .fx = {
            { VH_FX_DMG,    VH_TGT_ENEMY, 27.5f },
            { VH_FX_PULL,   VH_TGT_ENEMY, 3.0f },
            { VH_FX_FREEZE, VH_TGT_ENEMY, 0.85f },
            { VH_FX_MARK,   VH_TGT_ENEMY, 2.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "TTQP_NO_HAI_LONG_QUYEN",
        .displayName = "Nộ Hải Long Quyền",
        .setId = VH_SET_THUY_TRIEU_QUYEN_PHAP,
        .element = 0,
        .role = VH_ROLE_STRIKE,
        .mana = 24.0f, .cooldown = 7.0f, .castSecs = 0.35f,
        .range = VH_RANGE_MELEE, .hit = VH_HIT_MELEE, .shape = VH_SHAPE_SMALL,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 38.0f,
        .fx = {
            /* Standalone expected mark consumption: 0.5 stack -> 1.06x dmg */
            { VH_FX_DMG,  VH_TGT_ENEMY, 38.0f * 1.06f },
            { VH_FX_STUN, VH_TGT_ENEMY, 0.60f },
            { VH_FX_KB,   VH_TGT_ENEMY, 3.5f }
        },
        .fxCount = 3
    },
    {
        .skillName = "TTQP_THUY_KINH_QUYEN_THUAN",
        .displayName = "Thủy Kính Quyền Thuẫn",
        .setId = VH_SET_THUY_TRIEU_QUYEN_PHAP,
        .element = 0,
        .role = VH_ROLE_SUPPORT,
        .mana = 26.0f, .cooldown = 12.0f, .castSecs = 0.20f,
        .range = VH_RANGE_SELF, .hit = VH_HIT_SELF, .shape = VH_SHAPE_NONE,
        .allyTeamScope = true, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 0.0f,
        .fx = {
            /* Self 34 shield + ally 50% -> average 0.75 * 34 per ally slot */
            { VH_FX_SHIELD,  VH_TGT_ALLY, 34.0f * 0.75f },
            { VH_FX_SPEED,   VH_TGT_ALLY, 0.20f * 3.0f },
            { VH_FX_BLOCK,   VH_TGT_ONCE, 2.0f },
            { VH_FX_CLEANSE, VH_TGT_ONCE, 1.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "TTQP_TRIEU_TICH_LINH_TUYEN",
        .displayName = "Triều Tịch Linh Tuyền",
        .setId = VH_SET_THUY_TRIEU_QUYEN_PHAP,
        .element = 0,
        .role = VH_ROLE_SUPPORT,
        .mana = 26.0f, .cooldown = 12.0f, .castSecs = 0.40f,
        .range = VH_RANGE_SELF, .hit = VH_HIT_ZONE, .shape = VH_SHAPE_LARGE,
        .allyTeamScope = true, .uptimeEnemy = 0.40f, .uptimeAlly = 0.60f,
        .baseDamage = 0.0f,
        .fx = {
            { VH_FX_SLOW, VH_TGT_ENEMY, 0.30f * 5.0f },
            { VH_FX_MARK, VH_TGT_ENEMY, 2.0f },
            { VH_FX_HEAL, VH_TGT_ALLY,  6.0f * 5.0f },
            { VH_FX_MANA, VH_TGT_ALLY,  8.0f * 5.0f },
            { VH_FX_CDR,  VH_TGT_ALLY,  2.0f }
        },
        .fxCount = 5
    },

    /* ===== BỘ 2: BÀN THẠCH BÁT CỰC QUYỀN (Hệ Thổ — 5 Đả + 2 Trợ) ===== */
    {
        .skillName = "BTBC_KHAI_SON_THACH_DAN",
        .displayName = "Khai Sơn Thạch Đạn",
        .setId = VH_SET_BAN_THACH_BAT_CUC_QUYEN,
        .element = 3,
        .role = VH_ROLE_STRIKE,
        .mana = 14.0f, .cooldown = 1.6f, .castSecs = 0.20f,
        .range = VH_RANGE_MID, .hit = VH_HIT_FAST, .shape = VH_SHAPE_SINGLE,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 17.5f,
        .fx = {
            { VH_FX_DMG,  VH_TGT_ENEMY, 17.5f },
            { VH_FX_KB,   VH_TGT_ENEMY, 1.8f },
            { VH_FX_MARK, VH_TGT_ENEMY, 1.0f }
        },
        .fxCount = 3
    },
    {
        .skillName = "BTBC_THIET_SON_KHAO",
        .displayName = "Thiết Sơn Khảo",
        .setId = VH_SET_BAN_THACH_BAT_CUC_QUYEN,
        .element = 3,
        .role = VH_ROLE_STRIKE,
        .mana = 20.0f, .cooldown = 5.5f, .castSecs = 0.25f,
        .range = VH_RANGE_MELEE, .hit = VH_HIT_MELEE, .shape = VH_SHAPE_SMALL,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 21.5f,
        .fx = {
            { VH_FX_DMG,    VH_TGT_ENEMY, 21.5f },
            { VH_FX_STUN,   VH_TGT_ENEMY, 0.60f },
            { VH_FX_MARK,   VH_TGT_ENEMY, 1.0f },
            { VH_FX_SHIELD, VH_TGT_ONCE,  14.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "BTBC_DIA_LIET_CHAN_BA",
        .displayName = "Địa Liệt Chấn Ba",
        .setId = VH_SET_BAN_THACH_BAT_CUC_QUYEN,
        .element = 3,
        .role = VH_ROLE_STRIKE,
        .mana = 24.0f, .cooldown = 7.0f, .castSecs = 0.35f,
        .range = VH_RANGE_MID, .hit = VH_HIT_WAVE, .shape = VH_SHAPE_CONE,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 18.0f,
        .fx = {
            { VH_FX_DMG,   VH_TGT_ENEMY, 18.0f },
            { VH_FX_KB,    VH_TGT_ENEMY, 4.5f },
            { VH_FX_BLOCK, VH_TGT_ONCE,  1.0f },
            { VH_FX_MARK,  VH_TGT_ENEMY, 1.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "BTBC_THACH_TRU_XUNG_THIEN",
        .displayName = "Thạch Trụ Xung Thiên",
        .setId = VH_SET_BAN_THACH_BAT_CUC_QUYEN,
        .element = 3,
        .role = VH_ROLE_STRIKE,
        .mana = 24.0f, .cooldown = 7.5f, .castSecs = 0.65f,
        .range = VH_RANGE_MID, .hit = VH_HIT_GROUND, .shape = VH_SHAPE_LARGE,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 27.5f,
        .fx = {
            { VH_FX_DMG,    VH_TGT_ENEMY, 27.5f },
            { VH_FX_LAUNCH, VH_TGT_ENEMY, 0.85f },
            { VH_FX_WEAKEN, VH_TGT_ENEMY, 0.35f * 2.5f },
            { VH_FX_MARK,   VH_TGT_ENEMY, 2.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "BTBC_BANG_SON_TOAI_CHUONG",
        .displayName = "Băng Sơn Toái Chưởng",
        .setId = VH_SET_BAN_THACH_BAT_CUC_QUYEN,
        .element = 3,
        .role = VH_ROLE_STRIKE,
        .mana = 22.0f, .cooldown = 6.5f, .castSecs = 0.35f,
        .range = VH_RANGE_MELEE, .hit = VH_HIT_MELEE, .shape = VH_SHAPE_SMALL,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 28.0f,
        .fx = {
            /* Standalone expected mark consumption: 0.5 stack -> 1.06x dmg */
            { VH_FX_DMG,  VH_TGT_ENEMY, 28.0f * 1.06f },
            { VH_FX_ROOT, VH_TGT_ENEMY, 1.30f },
            { VH_FX_VULN, VH_TGT_ENEMY, 0.25f * 3.5f }
        },
        .fxCount = 3
    },
    {
        .skillName = "BTBC_BAN_THACH_KIM_CANG_GIAP",
        .displayName = "Bàn Thạch Kim Cang Giáp",
        .setId = VH_SET_BAN_THACH_BAT_CUC_QUYEN,
        .element = 3,
        .role = VH_ROLE_SUPPORT,
        .mana = 26.0f, .cooldown = 12.0f, .castSecs = 0.20f,
        .range = VH_RANGE_SELF, .hit = VH_HIT_SELF, .shape = VH_SHAPE_NONE,
        .allyTeamScope = true, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 0.0f,
        .fx = {
            /* Self 39 shield + ally 50% -> average 0.75 * 39 per ally slot */
            { VH_FX_SHIELD,  VH_TGT_ALLY, 39.0f * 0.75f },
            { VH_FX_CLEANSE, VH_TGT_ONCE, 1.0f },
            { VH_FX_BLOCK,   VH_TGT_ONCE, 2.0f }
        },
        .fxCount = 3
    },
    {
        .skillName = "BTBC_HAU_THO_TRAN_NHAC_TRAN",
        .displayName = "Hậu Thổ Trấn Nhạc Trận",
        .setId = VH_SET_BAN_THACH_BAT_CUC_QUYEN,
        .element = 3,
        .role = VH_ROLE_SUPPORT,
        .mana = 24.0f, .cooldown = 12.0f, .castSecs = 0.35f,
        .range = VH_RANGE_SELF, .hit = VH_HIT_ZONE, .shape = VH_SHAPE_LARGE,
        .allyTeamScope = true, .uptimeEnemy = 0.50f, .uptimeAlly = 0.55f,
        .baseDamage = 0.0f,
        .fx = {
            { VH_FX_HEAL,   VH_TGT_ALLY,  6.0f * 5.0f },
            { VH_FX_SHIELD, VH_TGT_ALLY,  6.0f * 5.0f },
            { VH_FX_SLOW,   VH_TGT_ENEMY, 0.30f * 5.0f },
            { VH_FX_MARK,   VH_TGT_ENEMY, 2.0f }
        },
        .fxCount = 4
    },

    /* ===== BỘ 3: THIÊN CANG ĐOẠN KIM KIẾM PHÁP (Hệ Kim — 5 Đả + 2 Trợ) ===== */
    {
        .skillName = "TCDK_LUU_TINH_THU",
        .displayName = "Lưu Tinh Thứ",
        .setId = VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
        .element = 4,
        .role = VH_ROLE_STRIKE,
        .mana = 12.0f, .cooldown = 1.5f, .castSecs = 0.15f,
        .range = VH_RANGE_MID, .hit = VH_HIT_FAST, .shape = VH_SHAPE_LINE,
        .allyTeamScope = false, .uptimeEnemy = 0.92f, .uptimeAlly = 1.0f,
        .baseDamage = 13.5f,
        .fx = {
            { VH_FX_DMG,  VH_TGT_ENEMY, 13.5f },
            { VH_FX_MARK, VH_TGT_ENEMY, 1.0f }
        },
        .fxCount = 2
    },
    {
        .skillName = "TCDK_TOAI_GIAP_TRAM",
        .displayName = "Toái Giáp Trảm",
        .setId = VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
        .element = 4,
        .role = VH_ROLE_STRIKE,
        .mana = 18.0f, .cooldown = 5.0f, .castSecs = 0.25f,
        .range = VH_RANGE_MELEE, .hit = VH_HIT_MELEE, .shape = VH_SHAPE_SMALL,
        .allyTeamScope = false, .uptimeEnemy = 0.92f, .uptimeAlly = 1.0f,
        .baseDamage = 24.0f,
        .fx = {
            { VH_FX_DMG,   VH_TGT_ENEMY, 24.0f },
            { VH_FX_VULN,  VH_TGT_ENEMY, 0.20f * 3.0f },
            { VH_FX_BLEED, VH_TGT_ENEMY, 10.0f },
            { VH_FX_MARK,  VH_TGT_ENEMY, 1.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "TCDK_THIEN_LA_KIEM_VONG",
        .displayName = "Thiên La Kiếm Võng",
        .setId = VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
        .element = 4,
        .role = VH_ROLE_STRIKE,
        .mana = 24.0f, .cooldown = 7.0f, .castSecs = 0.28f,
        .range = VH_RANGE_MID, .hit = VH_HIT_WAVE, .shape = VH_SHAPE_CONE,
        .allyTeamScope = false, .uptimeEnemy = 1.05f, .uptimeAlly = 1.0f,
        .baseDamage = 16.0f,
        .fx = {
            { VH_FX_DMG,   VH_TGT_ENEMY, 16.0f },
            { VH_FX_SLOW,  VH_TGT_ENEMY, 0.35f * 1.5f },
            { VH_FX_BLEED, VH_TGT_ENEMY, 12.0f },
            { VH_FX_MARK,  VH_TGT_ENEMY, 1.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "TCDK_CHAN_AM_KIEM_TRAN",
        .displayName = "Chấn Âm Kiếm Trận",
        .setId = VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
        .element = 4,
        .role = VH_ROLE_STRIKE,
        .mana = 26.0f, .cooldown = 8.5f, .castSecs = 0.65f,
        .range = VH_RANGE_LONG, .hit = VH_HIT_GROUND, .shape = VH_SHAPE_MEDIUM,
        .allyTeamScope = false, .uptimeEnemy = 1.35f, .uptimeAlly = 1.0f,
        .baseDamage = 22.0f,
        .fx = {
            { VH_FX_DMG,     VH_TGT_ENEMY, 22.0f },
            { VH_FX_LAUNCH,  VH_TGT_ENEMY, 0.60f },
            { VH_FX_SILENCE, VH_TGT_ENEMY, 0.80f },
            { VH_FX_MARK,    VH_TGT_ENEMY, 2.0f }
        },
        .fxCount = 4
    },
    {
        .skillName = "TCDK_NHAT_KIEM_DOAN_THIEN",
        .displayName = "Nhất Kiếm Đoạn Thiên",
        .setId = VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
        .element = 4,
        .role = VH_ROLE_STRIKE,
        .mana = 22.0f, .cooldown = 7.5f, .castSecs = 0.25f,
        .range = VH_RANGE_MELEE, .hit = VH_HIT_MELEE, .shape = VH_SHAPE_SINGLE,
        .allyTeamScope = false, .uptimeEnemy = 1.12f, .uptimeAlly = 1.0f,
        .baseDamage = 40.0f,
        .fx = {
            { VH_FX_DMG,  VH_TGT_ENEMY, 40.0f },
            { VH_FX_STUN, VH_TGT_ENEMY, 0.55f },
            { VH_FX_KB,   VH_TGT_ENEMY, 3.0f }
        },
        .fxCount = 3
    },
    {
        .skillName = "TCDK_KIM_CANG_PHAN_CHAN",
        .displayName = "Kim Cang Phản Chấn",
        .setId = VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
        .element = 4,
        .role = VH_ROLE_SUPPORT,
        .mana = 24.0f, .cooldown = 11.5f, .castSecs = 0.15f,
        .range = VH_RANGE_SELF, .hit = VH_HIT_SELF, .shape = VH_SHAPE_NONE,
        .allyTeamScope = false, .uptimeEnemy = 1.0f, .uptimeAlly = 1.0f,
        .baseDamage = 0.0f,
        .fx = {
            { VH_FX_SHIELD,  VH_TGT_ALLY, 30.0f },
            { VH_FX_BLOCK,   VH_TGT_ONCE, 2.40f },
            { VH_FX_CLEANSE, VH_TGT_ONCE, 1.0f },
            { VH_FX_SPEED,   VH_TGT_ALLY, 0.25f * 2.5f }
        },
        .fxCount = 4
    },
    {
        .skillName = "TCDK_CANH_KIM_KIEM_VUC",
        .displayName = "Canh Kim Kiếm Vực",
        .setId = VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
        .element = 4,
        .role = VH_ROLE_SUPPORT,
        .mana = 24.0f, .cooldown = 12.0f, .castSecs = 0.25f,
        .range = VH_RANGE_SELF, .hit = VH_HIT_ZONE, .shape = VH_SHAPE_LARGE,
        .allyTeamScope = true, .uptimeEnemy = 1.05f, .uptimeAlly = 0.95f,
        .baseDamage = 0.0f,
        .fx = {
            { VH_FX_MANA,   VH_TGT_ALLY,  6.0f * 5.0f },
            { VH_FX_CDR,    VH_TGT_ALLY,  1.5f },
            { VH_FX_SPEED,  VH_TGT_ALLY,  0.15f * 5.0f },
            { VH_FX_WEAKEN, VH_TGT_ENEMY, 0.15f * 5.0f },
            { VH_FX_MARK,   VH_TGT_ENEMY, 2.0f }
        },
        .fxCount = 5
    }
};

static const int kVoHocSkillCount = (int)(sizeof(kVoHocSkills) / sizeof(kVoHocSkills[0]));

const VoHocSkillDef *VoHoc_GetAllSkills(int *outCount) {
    if (outCount) *outCount = kVoHocSkillCount;
    return kVoHocSkills;
}

const VoHocSkillDef *VoHoc_FindByName(const char *skillName) {
    if (!skillName || skillName[0] == '\0') return NULL;
    for (int i = 0; i < kVoHocSkillCount; i++) {
        if (strcmp(kVoHocSkills[i].skillName, skillName) == 0) {
            return &kVoHocSkills[i];
        }
    }
    return NULL;
}

const char *VoHoc_GetSetName(VoHocSetId setId) {
    switch (setId) {
        case VH_SET_THUY_TRIEU_QUYEN_PHAP: return "Thủy Triều Quyền Pháp";
        case VH_SET_BAN_THACH_BAT_CUC_QUYEN: return "Bàn Thạch Bát Cực Quyền";
        case VH_SET_THIEN_CANG_DOAN_KIM_KIEM: return "Thiên Cang Đoạn Kim Kiếm Pháp";
        default: return "Cơ Bản";
    }
}

static float CastWeight(float castSecs) {
    if (castSecs < 0.30f) return 1.00f;
    if (castSecs <= 0.60f) return 1.05f;
    return 1.10f;
}

VoHocEval VoHoc_Evaluate(const VoHocSkillDef *d) {
    VoHocEval ev;
    memset(&ev, 0, sizeof(ev));
    if (!d) return ev;

    ev.budget = (d->mana + VH_LAMBDA_CD * d->cooldown)
              * kRangeMult[d->range]
              * CastWeight(d->castSecs);

    float tot[2] = { 0.0f, 0.0f };
    for (int sc = 0; sc < 2; sc++) {
        float ne = kEnemyCount[d->shape][sc];
        float na = d->allyTeamScope ? (sc == 0 ? 1.0f : 2.0f) : 1.0f;
        float p  = kHitProb[d->hit];
        for (int i = 0; i < d->fxCount; i++) {
            const VoHocEffect *e = &d->fx[i];
            float ul = kFxUnitUL[e->fx] * e->amount;
            float val = 0.0f;
            if (e->tgt == VH_TGT_ENEMY) {
                val = ul * p * ne * d->uptimeEnemy;
            } else if (e->tgt == VH_TGT_ALLY) {
                val = ul * na * d->uptimeAlly;
            } else {
                val = ul;
            }
            tot[sc] += val;
            ev.share[kFxAxis[e->fx]] += 0.5f * val;
        }
    }

    ev.vSolo = tot[0];
    ev.vGroup = tot[1];
    ev.value = 0.5f * (ev.vSolo + ev.vGroup);
    ev.devPct = (ev.budget > 0.0001f) ? ((ev.value - ev.budget) / ev.budget * 100.0f) : 0.0f;
    ev.q = (ev.vSolo > 0.0001f) ? (ev.vGroup / ev.vSolo) : 0.0f;
    if (ev.value > 0.0001f) {
        for (int a = 0; a < 4; a++) {
            ev.radar[a] = (int)lroundf(10.0f * ev.share[a] / ev.value);
        }
        ev.ccPct = 100.0f * ev.share[2] / ev.value;
    }
    return ev;
}

int VoHoc_CountSetPieces(const int *equippedSkillIds, int count, VoHocSetId setId) {
    if (!equippedSkillIds || count <= 0 || setId == VH_SET_NONE) return 0;
    int seen[64];
    int seenCount = 0;
    int pieces = 0;
    for (int i = 0; i < count; i++) {
        int id = equippedSkillIds[i];
        if (id < 0) continue;
        bool dup = false;
        for (int j = 0; j < seenCount; j++) {
            if (seen[j] == id) { dup = true; break; }
        }
        if (dup) continue;
        if (seenCount < 64) seen[seenCount++] = id;

        const char *name = GetRegisteredSkillName(id);
        const VoHocSkillDef *def = VoHoc_FindByName(name);
        if (def && def->setId == setId) pieces++;
    }
    return pieces;
}

VoHocLoadoutSummary VoHoc_EvalLoadout(const int *equippedSkillIds, int count) {
    VoHocLoadoutSummary sum;
    memset(&sum, 0, sizeof(sum));
    if (!equippedSkillIds || count <= 0) return sum;

    float share[4] = { 0, 0, 0, 0 };
    float qAcc = 0.0f;
    int qCnt = 0;
    int setCounts[VH_SET_COUNT];
    memset(setCounts, 0, sizeof(setCounts));

    for (int i = 0; i < count; i++) {
        int id = equippedSkillIds[i];
        if (id < 0) continue;
        const char *name = GetRegisteredSkillName(id);
        const VoHocSkillDef *def = VoHoc_FindByName(name);
        if (!def) continue;
        VoHocEval ev = VoHoc_Evaluate(def);
        sum.totalBudget += ev.budget;
        sum.totalValue  += ev.value;
        for (int a = 0; a < 4; a++) share[a] += ev.share[a];
        qAcc += ev.q;
        qCnt++;
        if (def->setId > VH_SET_NONE && def->setId < VH_SET_COUNT) {
            setCounts[def->setId]++;
        }
    }

    if (sum.totalValue > 0.0001f) {
        for (int a = 0; a < 4; a++) {
            sum.radar[a] = (int)lroundf(10.0f * share[a] / sum.totalValue);
        }
    }
    if (qCnt > 0) sum.avgQ = qAcc / (float)qCnt;

    for (int s = 1; s < VH_SET_COUNT; s++) {
        if (setCounts[s] > sum.dominantCount) {
            sum.dominantCount = setCounts[s];
            sum.dominantSetId = s;
        }
    }
    if (sum.dominantCount >= VO_HOC_TIER_MAJOR_COUNT) sum.setTier = VO_HOC_TIER_MAJOR_COUNT;
    else if (sum.dominantCount >= VO_HOC_TIER_MINOR_COUNT) sum.setTier = VO_HOC_TIER_MINOR_COUNT;
    return sum;
}

VoHocRole VoHoc_GetSkillRoleByName(const char *skillName) {
    const VoHocSkillDef *def = VoHoc_FindByName(skillName);
    if (def) return def->role;
    return VH_ROLE_STRIKE; /* Legacy skills default to strike role */
}

bool VoHoc_IsSkillRoleCompatibleWithSlot(const char *skillName, int slot) {
    if (slot < 0 || slot >= 7) return false;
    VoHocRole role = VoHoc_GetSkillRoleByName(skillName);
    if (slot < 5) return role == VH_ROLE_STRIKE;
    return role == VH_ROLE_SUPPORT;
}

bool VoHoc_ValidateAll(char *errBuf, int errLen) {
    for (int setId = 1; setId < VH_SET_COUNT; setId++) {
        int strikes = 0, supports = 0, elem = -1;
        float sumB = 0.0f, sumV = 0.0f;
        float share[4] = { 0, 0, 0, 0 };
        float burstExp = 0.0f;
        float hardCcDirect = 0.0f;

        for (int i = 0; i < kVoHocSkillCount; i++) {
            const VoHocSkillDef *d = &kVoHocSkills[i];
            if (d->setId != setId) continue;

            if (elem < 0) elem = d->element;
            else if (d->element != elem) {
                snprintf(errBuf, (size_t)errLen, "C7: set %d mixed elements", setId);
                return false;
            }
            if (d->role == VH_ROLE_STRIKE) strikes++;
            else supports++;

            VoHocEval ev = VoHoc_Evaluate(d);
            /* C1: |V - B| / B <= 5% */
            if (fabsf(ev.devPct) > VH_DEV_MAX_PCT) {
                snprintf(errBuf, (size_t)errLen, "C1: %s dev=%.2f%% > 5%%", d->skillName, ev.devPct);
                return false;
            }
            /* C2: standalone V >= 0.95 * B */
            if (ev.value < VH_STANDALONE_MIN * ev.budget) {
                snprintf(errBuf, (size_t)errLen, "C2: %s standalone V=%.1f < 0.95*B=%.1f",
                         d->skillName, ev.value, VH_STANDALONE_MIN * ev.budget);
                return false;
            }

            /* C9: Distant ground eruption/drop must telegraph >= 0.60s for dodge/block counterplay */
            if (d->hit == VH_HIT_GROUND && d->castSecs < 0.60f) {
                snprintf(errBuf, (size_t)errLen, "C9: %s distant ground skill telegraph %.2fs < 0.60s",
                         d->skillName, d->castSecs);
                return false;
            }

            /* C4 single-cast hard CC <= 1.0s & C6 max effects */
            if (d->fxCount > 5) {
                snprintf(errBuf, (size_t)errLen, "C6: %s fxCount=%d > 5", d->skillName, d->fxCount);
                return false;
            }
            for (int f = 0; f < d->fxCount; f++) {
                VoHocFx fx = d->fx[f].fx;
                if (fx == VH_FX_STUN || fx == VH_FX_FREEZE || fx == VH_FX_LAUNCH) {
                    if (d->fx[f].amount > VH_HARD_CC_SINGLE) {
                        snprintf(errBuf, (size_t)errLen, "C4: %s hard CC=%.2fs > 1.0s",
                                 d->skillName, d->fx[f].amount);
                        return false;
                    }
                    hardCcDirect += d->fx[f].amount;
                }
                if (d->role == VH_ROLE_STRIKE && fx == VH_FX_DMG) {
                    burstExp += d->fx[f].amount * kHitProb[d->hit];
                }
            }

            sumB += ev.budget;
            sumV += ev.value;
            for (int a = 0; a < 4; a++) share[a] += ev.share[a];
        }

        /* C7: 5 Strikes + 2 Supports */
        if (strikes != 5 || supports != 2) {
            snprintf(errBuf, (size_t)errLen, "C7: set %d has %d strikes + %d supports (need 5+2)",
                     setId, strikes, supports);
            return false;
        }
        /* C3: 5-Strike expected burst <= 90 */
        if (burstExp > VH_BURST_CAP) {
            snprintf(errBuf, (size_t)errLen, "C3: set %d burst=%.1f > %.1f",
                     setId, burstExp, VH_BURST_CAP);
            return false;
        }
        /* C4: total hard CC (direct + 0.75s mark freeze) <= 2.5s */
        if (hardCcDirect + 0.75f > VH_HARD_CC_SET) {
            snprintf(errBuf, (size_t)errLen, "C4: set %d hardCC=%.2fs > 2.5s",
                     setId, hardCcDirect + 0.75f);
            return false;
        }
        /* C6: set CC share <= 45% */
        float ccShare = (sumV > 0.0001f) ? (100.0f * share[2] / sumV) : 0.0f;
        if (ccShare > VH_CC_SHARE_MAX) {
            snprintf(errBuf, (size_t)errLen, "C6: set %d ccShare=%.1f%% > 45%%", setId, ccShare);
            return false;
        }
        /* C8: identity (top-2 axes >= 6.0 and min axis <= 1.5) */
        float ax[4];
        for (int a = 0; a < 4; a++) ax[a] = (sumV > 0.0001f) ? (10.0f * share[a] / sumV) : 0.0f;
        float top1 = -1.0f, top2 = -1.0f, minAx = 100.0f;
        for (int a = 0; a < 4; a++) {
            if (ax[a] > top1) { top2 = top1; top1 = ax[a]; }
            else if (ax[a] > top2) { top2 = ax[a]; }
            if (ax[a] < minAx) minAx = ax[a];
        }
        if (top1 + top2 < 6.0f || minAx > 1.5f) {
            snprintf(errBuf, (size_t)errLen, "C8: set %d identity failed (top2=%.1f min=%.1f)",
                     setId, top1 + top2, minAx);
            return false;
        }
        (void)sumB;
    }
    return true;
}

void VoHoc_GetSkillUnlockCost(const char *skillName, const bool *unlockedFlags, int skillCount,
                              int *outGold, int *outElement, int *outStones, VoHocSetId *outSetId) {
    const VoHocSkillDef *def = VoHoc_FindByName(skillName);
    int gold = 300;
    int stones = 6;
    int elem = def ? def->element : 0;
    VoHocSetId sid = def ? def->setId : VH_SET_NONE;

    /* 50% discount for the first skill unlocked in a set */
    if (def && unlockedFlags && skillCount > 0) {
        bool hasAnyInSet = false;
        for (int i = 0; i < skillCount; i++) {
            if (!unlockedFlags[i]) continue;
            const VoHocSkillDef *other = VoHoc_FindByName(GetRegisteredSkillName(i));
            if (other && other->setId == def->setId) {
                hasAnyInSet = true;
                break;
            }
        }
        if (!hasAnyInSet) {
            gold /= 2;
            stones /= 2;
        }
    }

    if (outGold) *outGold = gold;
    if (outElement) *outElement = elem;
    if (outStones) *outStones = stones;
    if (outSetId) *outSetId = sid;
}
