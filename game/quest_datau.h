// game/quest_datau.h
// Sequential Dã Tẩu Quest System (Võ Lâm Truyền Kỳ style):
// Only ONE active quest at a time; completing it immediately grants rewards,
// increments the streak, and rolls the next quest.
#ifndef QUEST_DATAU_H
#define QUEST_DATAU_H

#include <stdbool.h>

typedef enum {
    DATAU_SLAY_ANY = 0,     // Tiêu diệt X quái thường
    DATAU_SLAY_RANGED,      // Tiêu diệt X Pháp Yêu tầm xa
    DATAU_SLAY_ELITE,       // Tiêu diệt X Tinh Anh Thủ Lĩnh
    DATAU_CLASH_MASTERY,    // Ngộ Đạo Đấu Pháp: Thắng X lần đấu đạn
    DATAU_ZONE_HOLD,        // Tuần Tra Long Mạch: Giữ địa bàn X giây
    DATAU_BOSS_TRIAL        // Mốc chuỗi chia hết cho 5: Hạ gục Boss / Tinh Anh
} DaTauQuestType;

typedef struct {
    bool           active;
    DaTauQuestType type;
    int            realmId;       // 0..4 (Thủy, Mộc, Hỏa, Thổ, Kim)
    int            targetCount;
    int            currentCount;
    int            rewardGold;
    int            rewardPoints;
    char           title[96];
    char           desc[128];
} DaTauQuest;

void DaTau_Init(int realmId);
void DaTau_SetRealm(int realmId);
const DaTauQuest *DaTau_GetCurrent(void);

// Call when a camp monster or boss is killed in Explore mode.
// Returns true if the current quest just completed!
bool DaTau_OnMonsterKilled(int monsterKind, int element, bool isBoss);
bool DaTau_OnClashWon(void);
void DaTau_TickZoneHold(float dt, bool playerAlive);

// Skip current quest using 1 Lệnh Bài Dã Tẩu (keeps streak).
bool DaTau_SkipWithToken(void);

// Banner timer for completion celebration HUD
float DaTau_GetCompleteBannerTimer(void);
const char *DaTau_GetLastRewardText(void);
void DaTau_Update(float dt);

#endif // QUEST_DATAU_H
