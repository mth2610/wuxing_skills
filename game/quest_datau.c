// game/quest_datau.c
#include "game/quest_datau.h"
#include "character/player_profile.h"
#include "ai/ai.h"
#include <stdio.h>
#include <stdlib.h>

static DaTauQuest s_quest = { 0 };
static int s_realmId = 0;
static bool s_npcFlowEnabled = false;
static int s_lastQuestType = -1;
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
    s_quest.npcFlowEnabled = s_npcFlowEnabled;
    s_quest.phase = s_npcFlowEnabled ? DATAU_PHASE_WAIT_ACCEPT : DATAU_PHASE_IN_PROGRESS;
    s_quest.realmId = s_realmId;
    s_quest.currentCount = 0;
    s_zoneHoldAccum = 0.0f;

    if (nextNum % 5 == 0) {
        s_quest.type = DATAU_BOSS_TRIAL;
        s_quest.targetCount = 1;
        s_quest.rewardGold = 420 + nextNum * 18;
        s_quest.rewardPoints = 100 + nextNum * 4;
        snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Tram Ta Tinh Linh", nextNum);
        snprintf(s_quest.desc, sizeof(s_quest.desc), "Ha %d Tinh Anh / Boss tai %s",
                 s_quest.targetCount, kRealmNames[s_realmId % 5]);
    } else if (s_npcFlowEnabled && s_realmId == 3) {
        // Xoay vòng 6 kịch bản nhiệm vụ mới mẻ cho Sa Mạc, không lặp 2 lần liên tiếp
        int pool[6] = {
            DATAU_ANCIENT_SEAL,
            DATAU_SAND_THIEF,
            DATAU_CARAVAN_RESCUE,
            DATAU_PURIFY_TOTEMS,
            DATAU_CLASH_MASTERY,
            DATAU_FORAGE_COLLECT
        };
        int pick = pool[rand() % 6];
        if (nextNum == 1) pick = DATAU_CARAVAN_RESCUE; // NV đầu tiên cực dễ bắt nhịp & kịch tính
        else if (pick == s_lastQuestType) pick = pool[(rand() % 5 + 1) % 6];
        s_quest.type = (DaTauQuestType)pick;
        s_lastQuestType = pick;

        if (pick == DATAU_ANCIENT_SEAL) {
            s_quest.targetCount = 3; // 3 đợt quái
            s_quest.rewardGold = 240 + nextNum * 12;
            s_quest.rewardPoints = 55 + nextNum * 2;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Giai An Co Tran", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Toi Thach Lam [F] khai an & pha 3 dot quai");
        } else if (pick == DATAU_SAND_THIEF) {
            s_quest.targetCount = 1;
            s_quest.rewardGold = 280 + nextNum * 12;
            s_quest.rewardPoints = 60 + nextNum * 2;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Truy Bat Dao Tac Cat", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Duoi bat & ha Dao Tac Cat Vang truoc khi tron");
        } else if (pick == DATAU_CARAVAN_RESCUE) {
            s_quest.targetCount = 4;
            s_quest.rewardGold = 210 + nextNum * 10;
            s_quest.rewardPoints = 45 + nextNum * 2;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Cuu Thuong Nhan Cat", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Diet 4 quai vay ham Thuong Nhan tai Doi Cat");
        } else if (pick == DATAU_PURIFY_TOTEMS) {
            s_quest.targetCount = 3;
            s_quest.rewardGold = 230 + nextNum * 11;
            s_quest.rewardPoints = 50 + nextNum * 2;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Thanh Tay Ta Tru", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Den tan tich pha vo 3 Tru Ta Khi [F]");
        } else if (pick == DATAU_CLASH_MASTERY) {
            s_quest.targetCount = 3;
            s_quest.rewardGold = 220 + nextNum * 10;
            s_quest.rewardPoints = 50 + nextNum * 2;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Ngo Dao Dau Phap", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Thang 3 lan Dau Phap hoac ha Phap Yeu");
        } else {
            s_quest.targetCount = 3;
            s_quest.rewardGold = 190 + nextNum * 10;
            s_quest.rewardPoints = 45 + nextNum * 2;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Tam Bao Sa Mac", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Khai khoang / Hai thao / Mo 3 ruong cat");
        }
    } else if (nextNum == 1 && s_realmId == 3) {
        // Nhiệm vụ đầu tiên tại Map Sa Mạc: Dễ dàng bắt nhịp trong 30 giây!
        s_quest.type = DATAU_SLAY_ANY;
        s_quest.targetCount = 3;
        s_quest.rewardGold = 150;
        s_quest.rewardPoints = 35;
        snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #1: Khoi Dau Sa Mac");
        snprintf(s_quest.desc, sizeof(s_quest.desc), "Ra Doi Cat Phong Sa diet 3 Tieu Yeu");
    } else {
        int roll = rand() % 6;
        if (roll == 0) {
            s_quest.type = DATAU_SLAY_ANY;
            s_quest.targetCount = 4 + (rand() % 4);
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
        } else if (roll == 4) {
            s_quest.type = DATAU_FORAGE_COLLECT;
            s_quest.targetCount = 3 + (rand() % 2);
            s_quest.rewardGold = 145 + nextNum * 9;
            s_quest.rewardPoints = 35 + nextNum;
            snprintf(s_quest.title, sizeof(s_quest.title), "Da Tau #%d: Tam Bao Khoang Thao", nextNum);
            snprintf(s_quest.desc, sizeof(s_quest.desc), "Thu thap %d Khoang / Linh Thao / Ruong",
                     s_quest.targetCount);
        } else {
            s_quest.type = DATAU_ZONE_HOLD;
            s_quest.targetCount = 20;
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
    s_npcFlowEnabled = false;
    s_completeBannerTimer = 0.0f;
    s_lastRewardText[0] = '\0';
    RollNextQuest();
}

void DaTau_SetRealm(int realmId) {
    s_realmId = (realmId >= 0 && realmId < 5) ? realmId : 0;
    if (!s_quest.active) RollNextQuest();
}

void DaTau_SetNpcFlowEnabled(bool enabled) {
    s_npcFlowEnabled = enabled;
    s_quest.npcFlowEnabled = enabled;
    if (enabled && s_quest.active && s_quest.currentCount == 0 && s_quest.phase == DATAU_PHASE_IN_PROGRESS) {
        s_quest.phase = DATAU_PHASE_WAIT_ACCEPT;
    } else if (!enabled && s_quest.active) {
        s_quest.phase = DATAU_PHASE_IN_PROGRESS;
    }
}

bool DaTau_AcceptFromNPC(void) {
    if (!s_quest.active) RollNextQuest();
    if (s_quest.phase != DATAU_PHASE_WAIT_ACCEPT) return false;
    s_quest.phase = DATAU_PHASE_IN_PROGRESS;
    return true;
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
    prof->herbs += 1;
    prof->daTauStreak++;
    prof->daTauTotalCompleted++;

    bool milestone = (prof->daTauStreak % 5 == 0);
    if (milestone) {
        prof->daTauTokens += 1;
        prof->daTauPoints += 120;
        prof->gold += 350;
        PlayerProfile_AddPotion(POTION_SKILL_XP_BOOST, 1);
    }
    PlayerProfile_SaveGlobal();

    if (milestone) {
        snprintf(s_lastRewardText, sizeof(s_lastRewardText),
                 "CHUOI %d! +%d Vang, +%d Diem, +1 Lenh Bai & Linh Tra!",
                 prof->daTauStreak, s_quest.rewardGold + 350, s_quest.rewardPoints + 120);
    } else {
        snprintf(s_lastRewardText, sizeof(s_lastRewardText),
                 "XONG DA TAU! +%d Vang, +%d Diem, +1 Khoang, +1 Thao",
                 s_quest.rewardGold, s_quest.rewardPoints);
    }
    s_completeBannerTimer = 3.5f;
    RollNextQuest();
    return true;
}

bool DaTau_TurnInToNPC(void) {
    if (!s_quest.active || s_quest.phase != DATAU_PHASE_READY_RETURN) return false;
    return CompleteCurrentQuest();
}

bool DaTau_AdvanceObjective(int count) {
    if (count <= 0) return false;
    if (!s_quest.active) RollNextQuest();
    if (s_npcFlowEnabled && s_quest.phase != DATAU_PHASE_IN_PROGRESS) return false;

    s_quest.currentCount += count;
    if (s_quest.currentCount >= s_quest.targetCount) {
        s_quest.currentCount = s_quest.targetCount;
        if (s_npcFlowEnabled) {
            s_quest.phase = DATAU_PHASE_READY_RETURN;
            snprintf(s_lastRewardText, sizeof(s_lastRewardText),
                     "HOAN THANH! THEO LUONG SANG VE GAP DA TAU TRA NV!");
            s_completeBannerTimer = 3.5f;
            return true;
        }
        return CompleteCurrentQuest();
    }
    return false;
}

bool DaTau_OnMonsterKilled(int monsterKind, int element, bool isBoss) {
    (void)element;
    if (!s_quest.active) RollNextQuest();
    if (s_npcFlowEnabled && s_quest.phase != DATAU_PHASE_IN_PROGRESS) return false;

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
        case DATAU_ANCIENT_SEAL:
        case DATAU_SAND_THIEF:
        case DATAU_CARAVAN_RESCUE:
        case DATAU_PURIFY_TOTEMS:
        case DATAU_FORAGE_COLLECT:
        case DATAU_ZONE_HOLD:
            break;
    }
    if (!match) return false;

    return DaTau_AdvanceObjective(1);
}

bool DaTau_OnClashWon(void) {
    if (!s_quest.active) RollNextQuest();
    if (s_npcFlowEnabled && s_quest.phase != DATAU_PHASE_IN_PROGRESS) return false;
    if (s_quest.type != DATAU_CLASH_MASTERY) return false;
    return DaTau_AdvanceObjective(1);
}

bool DaTau_OnForageCollected(int count) {
    if (count <= 0) return false;
    if (!s_quest.active) RollNextQuest();
    if (s_npcFlowEnabled && s_quest.phase != DATAU_PHASE_IN_PROGRESS) return false;
    if (s_quest.type != DATAU_FORAGE_COLLECT) return false;
    return DaTau_AdvanceObjective(count);
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
