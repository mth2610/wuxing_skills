// game/quest_datau.c
#include "game/quest_datau.h"
#include "character/player_profile.h"
#include "ai/ai.h"
#include <stdio.h>
#include <stdlib.h>

static DaTauQuest s_quest = { 0 };
static int s_realmId = 0;
static float s_completeBannerTimer = 0.0f;
static float s_zoneHoldAccum = 0.0f;
static char s_lastRewardText[128] = { 0 };

static const char *kRealmNames[5] = {
    "Han Thuy Dam", "Thanh Moc Lam", "Xich Hoa Son", "Hoang Tho Dai", "Kim Tuyet Coc"
};

static void RollNextQuest(void) {
    PlayerProfile *prof = PlayerProfile_GetGlobal();
    int nextNum = prof->daTauStreak + 1;
    s_quest.active = true;
    s_quest.realmId = s_realmId;
    s_quest.currentCount = 0;
    s_zoneHoldAccum = 0.0f;

    if (nextNum % 5 == 0) {
        s_quest.type = DATAU_BOSS_TRIAL;
        s_quest.targetCount = 2;
        s_quest.rewardGold = 350 + nextNum * 15;
        s_quest.rewardPoints = 80 + nextNum * 3;
        snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Tram Ta Tinh Linh", nextNum);
        snprintf(s_quest.desc, sizeof(s_quest.desc), "Ha %d Tinh Anh / Boss tai %s",
                 s_quest.targetCount, kRealmNames[s_realmId % 5]);
    } else {
        int roll = rand() % 5;
        if (roll == 0) {
            s_quest.type = DATAU_SLAY_ANY;
            s_quest.targetCount = 5 + (rand() % 4);
            s_quest.rewardGold = 120 + nextNum * 8;
            s_quest.rewardPoints = 25 + nextNum;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Tru Yeu Giang Ho", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Tieu diet %d quai vat tai %s",
                     s_quest.targetCount, kRealmNames[s_realmId % 5]);
        } else if (roll == 1) {
            s_quest.type = DATAU_SLAY_RANGED;
            s_quest.targetCount = 3 + (rand() % 3);
            s_quest.rewardGold = 150 + nextNum * 9;
            s_quest.rewardPoints = 35 + nextNum;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Pha Phap Yeu", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Tieu diet %d Phap Yeu tam xa",
                     s_quest.targetCount);
        } else if (roll == 2) {
            s_quest.type = DATAU_SLAY_ELITE;
            s_quest.targetCount = 1 + ((nextNum >= 4) ? 1 : 0);
            s_quest.rewardGold = 180 + nextNum * 10;
            s_quest.rewardPoints = 45 + nextNum;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Diet Thu Linh", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Ha guc %d Tinh Anh Ho Phap",
                     s_quest.targetCount);
        } else if (roll == 3) {
            s_quest.type = DATAU_CLASH_MASTERY;
            s_quest.targetCount = 2 + (rand() % 3);
            s_quest.rewardGold = 160 + nextNum * 10;
            s_quest.rewardPoints = 40 + nextNum;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Ngo Dao Dau Phap", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Thang %d lan dau dan hoac ha Phap Yeu",
                     s_quest.targetCount);
        } else {
            s_quest.type = DATAU_ZONE_HOLD;
            s_quest.targetCount = 25;
            s_quest.rewardGold = 140 + nextNum * 8;
            s_quest.rewardPoints = 30 + nextNum;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Tran Thu Long Mach", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Giu vung dia ban %s (%ds)",
                     kRealmNames[s_realmId % 5], s_quest.targetCount);
        }
    }
}

void DaTau_Init(int realmId) {
    s_realmId = (realmId >= 0 && realmId < 5) ? realmId : 0;
    s_completeBannerTimer = 0.0f;
    s_lastRewardText[0] = '\0';
    RollNextQuest();
}

void DaTau_SetRealm(int realmId) {
    s_realmId = (realmId >= 0 && realmId < 5) ? realmId : 0;
    if (!s_quest.active) RollNextQuest();
}

const DaTauQuest *DaTau_GetCurrent(void) {
    if (!s_quest.active) RollNextQuest();
    return &s_quest;
}

static bool CompleteCurrentQuest(void) {
    PlayerProfile *prof = PlayerProfile_GetGlobal();
    prof->gold += s_quest.rewardGold;
    prof->daTauPoints += s_quest.rewardPoints;
    prof->elementalStones[s_realmId % 5] += 1;
    prof->daTauStreak++;
    prof->daTauTotalCompleted++;

    bool milestone = (prof->daTauStreak % 5 == 0);
    if (milestone) {
        prof->daTauTokens += 1;
        prof->daTauPoints += 100;
        prof->gold += 300;
    }
    PlayerProfile_SaveGlobal();

    if (milestone) {
        snprintf(s_lastRewardText, sizeof(s_lastRewardText),
                 "CHUOI %d! +%d Vang, +%d Diem, +1 Lenh Bai!",
                 prof->daTauStreak, s_quest.rewardGold + 300, s_quest.rewardPoints + 100);
    } else {
        snprintf(s_lastRewardText, sizeof(s_lastRewardText),
                 "XONG DA TAU! +%d Vang, +%d Diem, +1 Linh Thach",
                 s_quest.rewardGold, s_quest.rewardPoints);
    }
    s_completeBannerTimer = 3.5f;
    RollNextQuest();
    return true;
}

bool DaTau_OnMonsterKilled(int monsterKind, int element, bool isBoss) {
    (void)element;
    if (!s_quest.active) RollNextQuest();

    bool match = false;
    switch (s_quest.type) {
        case DATAU_SLAY_ANY:
            match = true;
            break;
        case DATAU_SLAY_RANGED:
            match = (monsterKind == MONSTER_RANGED_CASTER);
            break;
        case DATAU_CLASH_MASTERY:
            match = (monsterKind == MONSTER_RANGED_CASTER || isBoss);
            break;
        case DATAU_SLAY_ELITE:
        case DATAU_BOSS_TRIAL:
            match = (monsterKind == MONSTER_ELITE_GUARD || isBoss);
            break;
        case DATAU_ZONE_HOLD:
            break;
    }
    if (!match) return false;

    s_quest.currentCount++;
    if (s_quest.currentCount >= s_quest.targetCount) {
        return CompleteCurrentQuest();
    }
    return false;
}

bool DaTau_OnClashWon(void) {
    if (!s_quest.active) RollNextQuest();
    if (s_quest.type != DATAU_CLASH_MASTERY) return false;
    s_quest.currentCount++;
    if (s_quest.currentCount >= s_quest.targetCount) {
        return CompleteCurrentQuest();
    }
    return false;
}

void DaTau_TickZoneHold(float dt, bool playerAlive) {
    if (!s_quest.active || s_quest.type != DATAU_ZONE_HOLD || !playerAlive) return;
    s_zoneHoldAccum += dt;
    while (s_zoneHoldAccum >= 1.0f) {
        s_zoneHoldAccum -= 1.0f;
        s_quest.currentCount++;
        if (s_quest.currentCount >= s_quest.targetCount) {
            CompleteCurrentQuest();
            return;
        }
    }
}

bool DaTau_SkipWithToken(void) {
    PlayerProfile *prof = PlayerProfile_GetGlobal();
    if (prof->daTauTokens <= 0) return false;
    prof->daTauTokens--;
    PlayerProfile_SaveGlobal();
    RollNextQuest();
    return true;
}

float DaTau_GetCompleteBannerTimer(void) { return s_completeBannerTimer; }
const char *DaTau_GetLastRewardText(void) { return s_lastRewardText; }

void DaTau_Update(float dt) {
    if (s_completeBannerTimer > 0.0f) {
        s_completeBannerTimer -= dt;
        if (s_completeBannerTimer < 0.0f) s_completeBannerTimer = 0.0f;
    }
}
