// character/player_profile.h
// Persistent player progression profile for Wuxing Skills:
//   - Currencies: Gold (Ngân Lượng), Dã Tẩu Points, 5 Elemental Stones
//   - Dã Tẩu sequential quest streak & tokens
//   - Unlocked martial arts (Võ Học) & PvE mastery levels (Lv.1..10)
//   - Global 5-skill loadout persisted by skill name
//   - PvE vs PvP normalization: PvP always returns level 1 & disables gear bonuses
//
// Pure data + JSON persistence. Never includes entities/entities.h.
#ifndef PLAYER_PROFILE_H
#define PLAYER_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_PROFILE_SKILLS 64
#define PROFILE_SKILL_SLOTS 5
#define PROFILE_SKILL_NAME_LEN 48
#define PLAYER_PROFILE_DEFAULT_SAVE_PATH "assets/saves/player_profile.json"

typedef struct {
    int      gold;
    int      daTauPoints;
    int      daTauTokens;                          // Lệnh Bài Dã Tẩu (đổi nhiệm vụ giữ chuỗi)
    int      elementalStones[5];                   // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal
    int      daTauStreak;
    int      daTauTotalCompleted;
    bool     skillUnlocked[MAX_PROFILE_SKILLS];
    uint8_t  skillLevel[MAX_PROFILE_SKILLS];       // 1..10 (PvE mastery)
    uint32_t skillXp[MAX_PROFILE_SKILLS];
    char     equippedSkillNames[PROFILE_SKILL_SLOTS][PROFILE_SKILL_NAME_LEN];
    uint32_t integrityHash;
} PlayerProfile;

void           PlayerProfile_InitGlobal(const char *savePath);
PlayerProfile *PlayerProfile_GetGlobal(void);
bool           PlayerProfile_SaveGlobal(void);
bool           PlayerProfile_LoadFromFile(PlayerProfile *out, const char *filePath);
bool           PlayerProfile_SaveToFile(const PlayerProfile *p, const char *filePath);

// Progression helpers
bool PlayerProfile_UnlockSkill(int skillIndex);
bool PlayerProfile_IsSkillUnlocked(int skillIndex);
bool PlayerProfile_AddSkillXp(int skillIndex, uint32_t xpAmount);
uint32_t PlayerProfile_XpForNextLevel(int currentLevel);
void PlayerProfile_EquipSkillSlot(int slot, int skillIndex);

// Fair-play normalization: returns 1 in PvP regardless of mastery,
// or the unlocked mastery level (1..10) in PvE.
int  PlayerProfile_GetEffectiveSkillLevel(int skillIndex, bool isPvP);

#endif // PLAYER_PROFILE_H
