// character/wardrobe_system.c
#include "character/wardrobe_system.h"
#include "character/modular_character.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static WardrobeCatalogItem s_catalog[WARDROBE_MAX_CATALOG_ITEMS];
static int                 s_catalogCount = 0;
static bool                s_catalogInitialized = false;

static void RegisterDefaultItems(void) {
    WardrobeCatalogItem items[] = {
        {
            .id = "human_body",
            .name = "Thân Thể Thiếu Hiệp",
            .description = "Thân thể thiếu hiệp sơ nhập giang hồ, vóc dáng cân đối, khí huyết dồi dào, thân pháp nhanh nhẹn.",
            .source = "Mặc định nhân vật khởi đầu",
            .slot = MODULAR_SLOT_BASE_BODY,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_NONE,
            .rarity = WARDROBE_RARITY_COMMON,
            .unlockType = UNLOCK_TYPE_DEFAULT,
            .unlockHint = "Khởi đầu giang hồ",
            .stats = { 100.0f, 50.0f, 1.0f, 0.0f, 0.0f, 0.0f, "Căn cơ võ học sơ khai, kinh mạch thông suốt" },
            .isSkinned = true,
            .modelPath = "assets/characters/bodies/human_body.glb",
            .uiColor = (Color){235, 196, 166, 255}
        },
        {
            .id = "human_male_base",
            .name = "Võ Giả Kim Thân",
            .description = "Thân thể tôi luyện qua Kim Chung Tráo, da đồng xương sắt, tráng kiện uy dũng, định lực phi phàm.",
            .source = "Căn cơ võ học võ giả",
            .slot = MODULAR_SLOT_BASE_BODY,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_METAL,
            .rarity = WARDROBE_RARITY_COMMON,
            .unlockType = UNLOCK_TYPE_ACHIEVEMENT,
            .unlockHint = "Luyện thành Kim Chung Tráo tầng thứ 5",
            .stats = { 160.0f, 60.0f, 0.98f, 0.05f, 0.12f, 0.0f, "Kim Chung Hộ Thể: Giảm 12% sát thương ngoại lực phải chịu" },
            .isSkinned = true,
            .modelPath = "assets/characters/bodies/human_male_base.glb",
            .uiColor = (Color){220, 200, 180, 255}
        },
        {
            .id = "ao_giao_linh",
            .name = "Áo Giao Lĩnh Lam Y",
            .description = "Trang phục truyền thống giao lĩnh thêu viền mây vàng, chất lụa xanh thẫm thoát tục như mây khói non xa.",
            .source = "Túi đồ sơ nhập giang hồ",
            .slot = MODULAR_SLOT_TORSO,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_WATER,
            .rarity = WARDROBE_RARITY_UNCOMMON,
            .unlockType = UNLOCK_TYPE_DEFAULT,
            .unlockHint = "Mặc định có sẵn",
            .stats = { 40.0f, 30.0f, 1.02f, 0.08f, 0.06f, 0.10f, "Lưu Thủy Du Thân: Tăng 10% tốc độ hồi phục chân khí" },
            .isSkinned = true,
            .modelPath = "assets/characters/equipment/torso/ao_giao_linh.glb",
            .uiColor = (Color){45, 90, 140, 255}
        },
        {
            .id = "ao_tien_hiep",
            .name = "Áo Tiên Hiệp Bạch Vân",
            .description = "Áo giao lĩnh hai lớp màu bạch vân phối lam xám, thêu họa tiết mây nước thanh thoát, đai lưng ngọc bích.",
            .source = "Túi đồ sơ nhập giang hồ",
            .slot = MODULAR_SLOT_TORSO,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_WATER,
            .rarity = WARDROBE_RARITY_RARE,
            .unlockType = UNLOCK_TYPE_DEFAULT,
            .unlockHint = "Mặc định có sẵn",
            .stats = { 55.0f, 45.0f, 1.03f, 0.10f, 0.08f, 0.12f, "Bạch Vân Phiêu Diêu: Tăng 12% tốc độ hồi phục chân khí" },
            .isSkinned = true,
            .modelPath = "assets/characters/equipment/torso/ao_tien_hiep.glb",
            .uiColor = (Color){232, 230, 224, 255}
        },
        {
            .id = "quan_wuxia",
            .name = "Quần Võ Hiệp Huyền Sắc",
            .description = "Quần thụng xà cạp đen bó gọn ống chân, co giãn nhẹ nhàng, thuận tiện cho khinh công và cước pháp.",
            .source = "Túi đồ sơ nhập giang hồ",
            .slot = MODULAR_SLOT_LEGS,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_NONE,
            .rarity = WARDROBE_RARITY_COMMON,
            .unlockType = UNLOCK_TYPE_DEFAULT,
            .unlockHint = "Mặc định có sẵn",
            .stats = { 25.0f, 10.0f, 1.03f, 0.0f, 0.03f, 0.0f, "Khinh Thân Cước: Tăng 3% tốc độ di chuyển chạy nhảy" },
            .isSkinned = true,
            .modelPath = "assets/characters/equipment/legs/quan_wuxia.glb",
            .uiColor = (Color){30, 32, 40, 255}
        },
        {
            .id = "chien_hai_van_du",
            .name = "Chiến Hài Vân Du",
            .description = "Đôi giày da mềm khâu chỉ vàng vân mây, đệm lót êm ái hỗ trợ bộ pháp linh động như ngọn gió nhẹ.",
            .source = "Túi đồ sơ nhập giang hồ",
            .slot = MODULAR_SLOT_BOOTS,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_WOOD,
            .rarity = WARDROBE_RARITY_UNCOMMON,
            .unlockType = UNLOCK_TYPE_DEFAULT,
            .unlockHint = "Mặc định có sẵn",
            .stats = { 20.0f, 15.0f, 1.05f, 0.04f, 0.04f, 0.05f, "Đạp Tuyết Vô Ngân: Giảm 15% thể lực tiêu hao khi khinh công" },
            .isSkinned = true,
            .modelPath = "",
            .uiColor = (Color){90, 70, 50, 255}
        },
        {
            .id = "ho_uyen_hac_thiet",
            .name = "Hộ Uyển Hắc Thiết",
            .description = "Bao tay thép đen tôi qua hàn tuyền, hộ vệ cổ tay vững chãi khi tung chưởng hay phát kình.",
            .source = "Tìm thấy tại Hầm Mỏ Thép Đen",
            .slot = MODULAR_SLOT_BRACERS,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_METAL,
            .rarity = WARDROBE_RARITY_UNCOMMON,
            .unlockType = UNLOCK_TYPE_EXPLORATION,
            .unlockHint = "Thu thập rương báu tại Hầm Mỏ Thép Đen",
            .stats = { 30.0f, 10.0f, 1.0f, 0.06f, 0.08f, 0.0f, "Kình Lực Cương Mãnh: Tăng 10% uy lực thế đánh Quyền Cước" },
            .isSkinned = true,
            .modelPath = "",
            .uiColor = (Color){80, 85, 95, 255}
        },
        {
            .id = "non_la_hiep_khach",
            .name = "Nón Lá Lãng Khách",
            .description = "Nón lá tre đan mộc mạc che khuất nửa khuôn mặt, phong trần lãng tử ngao du bốn bể sơn hà.",
            .source = "Nhặt tại bờ cát sông Sông Quao",
            .slot = MODULAR_SLOT_HEADGEAR,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_WOOD,
            .rarity = WARDROBE_RARITY_UNCOMMON,
            .unlockType = UNLOCK_TYPE_EXPLORATION,
            .unlockHint = "Khám phá ven bờ cát sông Sông Quao",
            .stats = { 20.0f, 20.0f, 1.02f, 0.05f, 0.05f, 0.08f, "Phong Trần Ẩn Dật: Giảm 20% tầm phát hiện của kẻ địch" },
            .isSkinned = false,
            .boneSocket = "mixamorig:Head",
            .socketOffsetPos = (Vector3){0.0f, 0.04f, 0.0f},
            .socketOffsetRot = (Vector3){-4.0f, 0.0f, 0.0f},
            .modelPath = "assets/characters/equipment/headgear/non_la_hiep_khach.glb",
            .uiColor = (Color){210, 180, 120, 255}
        },
        {
            .id = "khan_che_mat_da_hanh",
            .name = "Khăn Che Mặt Dạ Hành",
            .description = "Khăn lụa đen che kín nửa dưới khuôn mặt, thấm đượm hàn sương bóng đêm, thích hợp hành sự bí mật.",
            .source = "Tìm thấy trong rương Hắc Phong Trại",
            .slot = MODULAR_SLOT_MASK,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_WATER,
            .rarity = WARDROBE_RARITY_RARE,
            .unlockType = UNLOCK_TYPE_EXPLORATION,
            .unlockHint = "Đột nhập và mở rương bí mật tại Hắc Phong Trại",
            .stats = { 25.0f, 35.0f, 1.04f, 0.06f, 0.06f, 0.12f, "Bóng Đêm Ẩn Tích: Tăng 15% sát thương chí mạng khi đánh lén" },
            .isSkinned = false,
            .boneSocket = "mixamorig:Head",
            .socketOffsetPos = (Vector3){0.0f, -0.02f, 0.08f},
            .socketOffsetRot = (Vector3){0.0f, 0.0f, 0.0f},
            .modelPath = "",
            .uiColor = (Color){35, 40, 52, 255}
        },
        {
            .id = "ngoc_boi_thanh_long",
            .name = "Thanh Long Ngọc Bội",
            .description = "Miếng ngọc bội chạm khắc rồng xanh cuộn mây, tỏa ra linh khí thanh mát dưỡng tâm an thần.",
            .source = "Kỳ trân nhặt tại hốc Cổ Thụ Ngàn Năm",
            .slot = MODULAR_SLOT_WAIST,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_WOOD,
            .rarity = WARDROBE_RARITY_RARE,
            .unlockType = UNLOCK_TYPE_EXPLORATION,
            .unlockHint = "Trèo lên ngọn Cổ Thụ Ngàn Năm tìm linh ngọc",
            .stats = { 40.0f, 60.0f, 1.0f, 0.10f, 0.08f, 0.25f, "Linh Mộc Dưỡng Tâm: Hồi phục 2.5 chân khí mỗi giây trong Thiền Định" },
            .isSkinned = false,
            .boneSocket = "mixamorig:Spine",
            .socketOffsetPos = (Vector3){0.14f, 0.02f, 0.08f},
            .socketOffsetRot = (Vector3){0.0f, 0.0f, 0.0f},
            .modelPath = "",
            .uiColor = (Color){60, 180, 110, 255}
        },
        {
            .id = "kiem_thuan_thien",
            .name = "Thuận Thiên Kiếm",
            .description = "Thanh kiếm cổ khắc chân thư Thuận Thiên, chuôi khảm hoàng ngọc, lưỡi thép ánh lam chém sắt như chém bùn.",
            .source = "Bảo vật gia truyền Lạc Long Vương",
            .slot = MODULAR_SLOT_WEAPON_R,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_METAL,
            .rarity = WARDROBE_RARITY_LEGENDARY,
            .unlockType = UNLOCK_TYPE_ACHIEVEMENT,
            .unlockHint = "Hoàn thành Thử Thách Lạc Long và đoạt lấy thần kiếm",
            .stats = { 80.0f, 50.0f, 1.05f, 0.25f, 0.15f, 0.05f, "Thuận Thiên Trảm: Khai mở bộ Kiếm Pháp, tăng 25% sát thương kiếm khí Kim" },
            .isSkinned = false,
            .boneSocket = "mixamorig:RightHand",
            .socketOffsetPos = (Vector3){0.030f, -0.070f, -0.010f},
            .socketOffsetRot = (Vector3){-4.0f, 4.0f, 0.0f},
            .modelPath = "assets/characters/equipment/weapons/kiem_thuan_thien.glb",
            .uiColor = (Color){255, 215, 0, 255}
        },
        {
            .id = "vo_kiem_son_mai",
            .name = "Vỏ Kiếm Sơn Mài Đỏ",
            .description = "Vỏ kiếm gỗ quý phủ sơn mài đỏ thắm viền kim nhũ, đeo chéo sau lưng tôn vẻ hiệp khí thanh cao.",
            .source = "Phần thưởng hạ gục Boss Mộc Linh",
            .slot = MODULAR_SLOT_BACK,
            .itemType = ITEM_TYPE_MODULAR,
            .element = WARDROBE_ELEM_FIRE,
            .rarity = WARDROBE_RARITY_RARE,
            .unlockType = UNLOCK_TYPE_BOSS_DROP,
            .unlockHint = "Đánh bại Boss Mộc Linh tại Thần Mộc Sơn",
            .stats = { 45.0f, 35.0f, 1.02f, 0.08f, 0.10f, 0.10f, "Bạt Kiếm Thuật: Tăng 20% tốc độ rút kiếm xuất chiêu đầu tiên" },
            .isSkinned = false,
            .boneSocket = "mixamorig:Spine2",
            .socketOffsetPos = (Vector3){0.02f, 0.02f, -0.19f},
            .socketOffsetRot = (Vector3){-6.0f, 8.0f, -36.0f},
            .modelPath = "assets/characters/equipment/back/vo_kiem_son_mai.glb",
            .uiColor = (Color){180, 40, 30, 255}
        },
        {
            .id = "truong_bao_ngan_long",
            .name = "Ngân Long Trường Bào",
            .description = "Bộ trường bào trắng bạc dệt vảy rồng kim loại, tà áo thướt tha phủ dài chấm gót, uy vũ lẫm liệt.",
            .source = "Rơi từ Hắc Diện Ma Quân",
            .slot = MODULAR_SLOT_FULL_OUTFIT,
            .itemType = ITEM_TYPE_FULL_OUTFIT,
            .occupiedSlots = MODULAR_OCCUPIED_OUTFIT,
            .hideBaseBody = false,
            .element = WARDROBE_ELEM_METAL,
            .rarity = WARDROBE_RARITY_EPIC,
            .unlockType = UNLOCK_TYPE_BOSS_DROP,
            .unlockHint = "Hạ gục Boss Hắc Diện Ma Quân tại Ma Thần Đàn",
            .stats = { 150.0f, 90.0f, 1.08f, 0.18f, 0.16f, 0.15f, "Ngân Long Hộ Thể: Kích hoạt khiên hộ thể chống 1 lần choáng khi trúng đòn" },
            .isSkinned = true,
            .modelPath = "assets/characters/equipment/torso/ao_giao_linh.glb",
            .uiColor = (Color){225, 230, 245, 255}
        },
        {
            .id = "dao_bao_thai_cuc",
            .name = "Thái Cực Đạo Bào",
            .description = "Đạo bào lụa âm dương đen trắng đan xen, khí tức lưu chuyển không dứt, dung hợp vạn vật quy về hư vô.",
            .source = "Lĩnh ngộ Thái Cực Quyền tại Tiên Vân Đỉnh",
            .slot = MODULAR_SLOT_FULL_OUTFIT,
            .itemType = ITEM_TYPE_FULL_OUTFIT,
            .occupiedSlots = MODULAR_OCCUPIED_OUTFIT,
            .hideBaseBody = false,
            .element = WARDROBE_ELEM_TAIJI,
            .rarity = WARDROBE_RARITY_LEGENDARY,
            .unlockType = UNLOCK_TYPE_ACHIEVEMENT,
            .unlockHint = "Đột phá Cảnh Giới Thái Cực và thông suốt kỳ kinh bát mạch",
            .stats = { 220.0f, 160.0f, 1.12f, 0.22f, 0.22f, 0.30f, "Thái Cực Quy Nhất: Hóa giải 25% sát thương tương khắc từ mọi hệ Ngũ Hành" },
            .isSkinned = true,
            .modelPath = "assets/characters/equipment/torso/ao_giao_linh.glb",
            .uiColor = (Color){245, 245, 255, 255}
        }
    };

    int count = (int)(sizeof(items) / sizeof(items[0]));
    for (int i = 0; i < count; i++) {
        Wardrobe_RegisterItem(&items[i]);
    }
}

void Wardrobe_InitCatalog(void) {
    if (s_catalogInitialized) return;
    s_catalogCount = 0;
    memset(s_catalog, 0, sizeof(s_catalog));
    s_catalogInitialized = true;

    RegisterDefaultItems();
    Wardrobe_LoadCatalogFromFile("assets/characters/item_catalog.json");
    TraceLog(LOG_INFO, "WARDROBE: Catalog initialized with %d items.", s_catalogCount);
}

void Wardrobe_ShutdownCatalog(void) {
    s_catalogCount = 0;
    s_catalogInitialized = false;
    TraceLog(LOG_INFO, "WARDROBE: Catalog shutdown.");
}

static ModularSlot ParseSlotName(const char *name) {
    if (!name) return MODULAR_SLOT_BASE_BODY;
    if (strcmp(name, "base_body") == 0) return MODULAR_SLOT_BASE_BODY;
    if (strcmp(name, "hair") == 0) return MODULAR_SLOT_HAIR;
    if (strcmp(name, "headgear") == 0) return MODULAR_SLOT_HEADGEAR;
    if (strcmp(name, "mask") == 0) return MODULAR_SLOT_MASK;
    if (strcmp(name, "torso") == 0) return MODULAR_SLOT_TORSO;
    if (strcmp(name, "legs") == 0) return MODULAR_SLOT_LEGS;
    if (strcmp(name, "boots") == 0) return MODULAR_SLOT_BOOTS;
    if (strcmp(name, "bracers") == 0) return MODULAR_SLOT_BRACERS;
    if (strcmp(name, "waist") == 0) return MODULAR_SLOT_WAIST;
    if (strcmp(name, "back") == 0) return MODULAR_SLOT_BACK;
    if (strcmp(name, "weapon_r") == 0) return MODULAR_SLOT_WEAPON_R;
    if (strcmp(name, "weapon_l") == 0) return MODULAR_SLOT_WEAPON_L;
    if (strcmp(name, "full_outfit") == 0) return MODULAR_SLOT_FULL_OUTFIT;
    return MODULAR_SLOT_BASE_BODY;
}

static WardrobeElement ParseElementName(const char *name) {
    if (!name) return WARDROBE_ELEM_NONE;
    if (strcmp(name, "metal") == 0 || strcmp(name, "kim") == 0) return WARDROBE_ELEM_METAL;
    if (strcmp(name, "wood") == 0 || strcmp(name, "moc") == 0) return WARDROBE_ELEM_WOOD;
    if (strcmp(name, "water") == 0 || strcmp(name, "thuy") == 0) return WARDROBE_ELEM_WATER;
    if (strcmp(name, "fire") == 0 || strcmp(name, "hoa") == 0) return WARDROBE_ELEM_FIRE;
    if (strcmp(name, "earth") == 0 || strcmp(name, "tho") == 0) return WARDROBE_ELEM_EARTH;
    if (strcmp(name, "taiji") == 0 || strcmp(name, "thai_cuc") == 0) return WARDROBE_ELEM_TAIJI;
    return WARDROBE_ELEM_NONE;
}

static WardrobeRarity ParseRarityName(const char *name) {
    if (!name) return WARDROBE_RARITY_COMMON;
    if (strcmp(name, "uncommon") == 0) return WARDROBE_RARITY_UNCOMMON;
    if (strcmp(name, "rare") == 0) return WARDROBE_RARITY_RARE;
    if (strcmp(name, "epic") == 0) return WARDROBE_RARITY_EPIC;
    if (strcmp(name, "legendary") == 0) return WARDROBE_RARITY_LEGENDARY;
    return WARDROBE_RARITY_COMMON;
}

static WardrobeUnlockType ParseUnlockType(const char *name) {
    if (!name) return UNLOCK_TYPE_DEFAULT;
    if (strcmp(name, "exploration") == 0) return UNLOCK_TYPE_EXPLORATION;
    if (strcmp(name, "boss_drop") == 0) return UNLOCK_TYPE_BOSS_DROP;
    if (strcmp(name, "crafting") == 0) return UNLOCK_TYPE_CRAFTING;
    if (strcmp(name, "achievement") == 0) return UNLOCK_TYPE_ACHIEVEMENT;
    return UNLOCK_TYPE_DEFAULT;
}

static void ParseJsonStringVal(const char *block, const char *key, char *outBuf, int maxLen) {
    outBuf[0] = '\0';
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *p = strstr(block, searchKey);
    if (!p) return;
    p += strlen(searchKey);
    const char *colon = strchr(p, ':');
    if (!colon) return;
    const char *q1 = strchr(colon, '\"');
    if (!q1) return;
    q1++;
    const char *q2 = strchr(q1, '\"');
    if (!q2) return;
    int len = (int)(q2 - q1);
    if (len >= maxLen) len = maxLen - 1;
    strncpy(outBuf, q1, len);
    outBuf[len] = '\0';
}

static bool ParseJsonBoolVal(const char *block, const char *key, bool defaultVal) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *p = strstr(block, searchKey);
    if (!p) return defaultVal;
    p += strlen(searchKey);
    const char *colon = strchr(p, ':');
    if (!colon) return defaultVal;
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    if (strncmp(colon, "true", 4) == 0) return true;
    if (strncmp(colon, "false", 5) == 0) return false;
    return defaultVal;
}

static float ParseJsonFloatVal(const char *block, const char *key, float defaultVal) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *p = strstr(block, searchKey);
    if (!p) return defaultVal;
    p += strlen(searchKey);
    const char *colon = strchr(p, ':');
    if (!colon) return defaultVal;
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    float val = defaultVal;
    if (sscanf(colon, "%f", &val) == 1) return val;
    return defaultVal;
}

static Vector3 ParseJsonVec3Val(const char *block, const char *key) {
    Vector3 v = {0, 0, 0};
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *p = strstr(block, searchKey);
    if (!p) return v;
    p += strlen(searchKey);
    const char *bracket = strchr(p, '[');
    if (!bracket) return v;
    bracket++;
    sscanf(bracket, "%f , %f , %f", &v.x, &v.y, &v.z);
    return v;
}

static Color ParseJsonColorVal(const char *block, const char *key, Color defaultCol) {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char *p = strstr(block, searchKey);
    if (!p) return defaultCol;
    p += strlen(searchKey);
    const char *bracket = strchr(p, '[');
    if (!bracket) return defaultCol;
    bracket++;
    int r = 255, g = 255, b = 255, a = 255;
    if (sscanf(bracket, "%d , %d , %d , %d", &r, &g, &b, &a) >= 3) {
        return (Color){(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    }
    return defaultCol;
}

bool Wardrobe_LoadCatalogFromFile(const char *jsonPath) {
    if (!jsonPath || !FileExists(jsonPath)) {
        TraceLog(LOG_INFO, "WARDROBE: Catalog file '%s' not found, using built-ins.", jsonPath ? jsonPath : "null");
        return false;
    }

    char *jsonText = LoadFileText(jsonPath);
    if (!jsonText) return false;

    const char *itemKey = "\"id\"";
    const char *cursor = jsonText;

    while ((cursor = strstr(cursor, itemKey)) != NULL) {
        const char *blockStart = cursor;
        while (blockStart > jsonText && *blockStart != '{') blockStart--;
        const char *blockEnd = strstr(cursor, "}");
        if (!blockEnd) break;

        WardrobeCatalogItem item = {0};
        item.itemType = ITEM_TYPE_MODULAR;
        item.stats.speedMultiplier = 1.0f;

        ParseJsonStringVal(blockStart, "id", item.id, sizeof(item.id));
        ParseJsonStringVal(blockStart, "name", item.name, sizeof(item.name));
        ParseJsonStringVal(blockStart, "description", item.description, sizeof(item.description));
        ParseJsonStringVal(blockStart, "source", item.source, sizeof(item.source));

        char typeBuf[32] = "";
        ParseJsonStringVal(blockStart, "item_type", typeBuf, sizeof(typeBuf));
        if (strcmp(typeBuf, "full_outfit") == 0) {
            item.itemType = ITEM_TYPE_FULL_OUTFIT;
            item.occupiedSlots = MODULAR_OCCUPIED_OUTFIT;
        }

        char unlockBuf[32] = "";
        ParseJsonStringVal(blockStart, "unlock_type", unlockBuf, sizeof(unlockBuf));
        item.unlockType = ParseUnlockType(unlockBuf);
        ParseJsonStringVal(blockStart, "unlock_hint", item.unlockHint, sizeof(item.unlockHint));

        char slotBuf[32] = "";
        ParseJsonStringVal(blockStart, "slot", slotBuf, sizeof(slotBuf));
        item.slot = ParseSlotName(slotBuf);
        if (item.slot == MODULAR_SLOT_FULL_OUTFIT) {
            item.itemType = ITEM_TYPE_FULL_OUTFIT;
            item.occupiedSlots = MODULAR_OCCUPIED_OUTFIT;
        }

        char elemBuf[32] = "";
        ParseJsonStringVal(blockStart, "element", elemBuf, sizeof(elemBuf));
        item.element = ParseElementName(elemBuf);

        char rarBuf[32] = "";
        ParseJsonStringVal(blockStart, "rarity", rarBuf, sizeof(rarBuf));
        item.rarity = ParseRarityName(rarBuf);

        item.isSkinned = ParseJsonBoolVal(blockStart, "is_skinned", true);
        item.hideBaseBody = ParseJsonBoolVal(blockStart, "hide_base_body", false);

        ParseJsonStringVal(blockStart, "bone_socket", item.boneSocket, sizeof(item.boneSocket));
        item.socketOffsetPos = ParseJsonVec3Val(blockStart, "socket_offset_pos");
        item.socketOffsetRot = ParseJsonVec3Val(blockStart, "socket_offset_rot");
        ParseJsonStringVal(blockStart, "model_path", item.modelPath, sizeof(item.modelPath));
        item.uiColor = ParseJsonColorVal(blockStart, "ui_color", WHITE);

        // Stats parsing
        const char *statsBlock = strstr(blockStart, "\"stats\"");
        if (statsBlock && statsBlock < blockEnd) {
            item.stats.bonusHealth     = ParseJsonFloatVal(statsBlock, "bonus_health", 0.0f);
            item.stats.bonusMana       = ParseJsonFloatVal(statsBlock, "bonus_mana", 0.0f);
            item.stats.speedMultiplier = ParseJsonFloatVal(statsBlock, "speed_multiplier", 1.0f);
            item.stats.elementalBonus  = ParseJsonFloatVal(statsBlock, "elemental_bonus", 0.0f);
            item.stats.elementalResist = ParseJsonFloatVal(statsBlock, "elemental_resist", 0.0f);
            item.stats.manaRegenBonus  = ParseJsonFloatVal(statsBlock, "mana_regen_bonus", 0.0f);
            ParseJsonStringVal(statsBlock, "special_perk", item.stats.specialPerk, sizeof(item.stats.specialPerk));
        }

        if (item.id[0] != '\0') {
            Wardrobe_RegisterItem(&item);
        }
        cursor = blockEnd + 1;
    }

    UnloadFileText(jsonText);
    return true;
}

bool Wardrobe_RegisterItem(const WardrobeCatalogItem *item) {
    if (!item || item->id[0] == '\0') return false;

    // Check if item already exists (update it)
    for (int i = 0; i < s_catalogCount; i++) {
        if (strcmp(s_catalog[i].id, item->id) == 0) {
            s_catalog[i] = *item;
            return true;
        }
    }

    if (s_catalogCount >= WARDROBE_MAX_CATALOG_ITEMS) {
        TraceLog(LOG_WARNING, "WARDROBE: Catalog registry full!");
        return false;
    }

    s_catalog[s_catalogCount++] = *item;
    return true;
}

const WardrobeCatalogItem* Wardrobe_GetCatalogItem(const char *id) {
    if (!id || id[0] == '\0') return NULL;
    for (int i = 0; i < s_catalogCount; i++) {
        if (strcmp(s_catalog[i].id, id) == 0) return &s_catalog[i];
    }
    return NULL;
}

const WardrobeCatalogItem* Wardrobe_GetCatalogItemByIndex(int index) {
    if (index >= 0 && index < s_catalogCount) return &s_catalog[index];
    return NULL;
}

int Wardrobe_GetCatalogCount(void) {
    return s_catalogCount;
}

// -----------------------------------------------------------------------------
// Player Wardrobe & Customization Operations
// -----------------------------------------------------------------------------

void PlayerWardrobe_Init(PlayerWardrobe *pw, const char *playerId) {
    if (!pw) return;
    memset(pw, 0, sizeof(PlayerWardrobe));
    strncpy(pw->playerId, playerId ? playerId : "Hero_1", sizeof(pw->playerId) - 1);

    Wardrobe_InitCatalog();

    // Online progression: ONLY starter clothing is unlocked initially!
    PlayerWardrobe_UnlockItem(pw, "human_body");
    PlayerWardrobe_UnlockItem(pw, "ao_giao_linh");
    PlayerWardrobe_UnlockItem(pw, "ao_tien_hiep");
    PlayerWardrobe_UnlockItem(pw, "quan_wuxia");
    PlayerWardrobe_UnlockItem(pw, "chien_hai_van_du");

    // Equip starter loadout
    PlayerWardrobe_Equip(pw, "human_body");
    PlayerWardrobe_Equip(pw, "ao_giao_linh");
    PlayerWardrobe_Equip(pw, "quan_wuxia");
    PlayerWardrobe_Equip(pw, "chien_hai_van_du");

    pw->activeStance = PlayerWardrobe_GetCombatStance(pw);

    // Initialize 5 default presets
    for (int i = 0; i < WARDROBE_MAX_PRESETS; i++) {
        snprintf(pw->presets[i].name, sizeof(pw->presets[i].name), "Diện Mạo %d", i + 1);
        for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
            strncpy(pw->presets[i].equippedIds[s], pw->equippedIds[s], WARDROBE_ID_MAX_LEN - 1);
        }
    }
    pw->activePresetIndex = 0;
}

bool PlayerWardrobe_HasItem(const PlayerWardrobe *pw, const char *itemId) {
    if (!pw || !itemId || itemId[0] == '\0') return false;
    for (int i = 0; i < pw->unlockedCount; i++) {
        if (strcmp(pw->unlockedIds[i], itemId) == 0) return true;
    }
    return false;
}

bool PlayerWardrobe_UnlockItem(PlayerWardrobe *pw, const char *itemId) {
    if (!pw || !itemId || itemId[0] == '\0') return false;
    if (PlayerWardrobe_HasItem(pw, itemId)) return false; // already unlocked

    if (pw->unlockedCount < WARDROBE_MAX_COLLECTION) {
        strncpy(pw->unlockedIds[pw->unlockedCount++], itemId, WARDROBE_ID_MAX_LEN - 1);
        const WardrobeCatalogItem *cat = Wardrobe_GetCatalogItem(itemId);
        TraceLog(LOG_INFO, "WARDROBE: Player '%s' discovered new item: '%s' (%s)",
                 pw->playerId, cat ? cat->name : itemId, itemId);
        return true;
    }
    return false;
}

bool PlayerWardrobe_Equip(PlayerWardrobe *pw, const char *itemId) {
    if (!pw || !itemId || itemId[0] == '\0') return false;
    const WardrobeCatalogItem *cat = Wardrobe_GetCatalogItem(itemId);
    if (!cat) {
        TraceLog(LOG_WARNING, "WARDROBE: Cannot equip unknown item ID '%s'", itemId);
        return false;
    }

    if (!PlayerWardrobe_HasItem(pw, itemId)) {
        PlayerWardrobe_UnlockItem(pw, itemId);
    }

    ModularSlot slot = cat->slot;
    if (slot < 0 || slot >= MODULAR_SLOT_COUNT) return false;

    // Full Outfit handling:
    if (cat->itemType == ITEM_TYPE_FULL_OUTFIT || slot == MODULAR_SLOT_FULL_OUTFIT) {
        strncpy(pw->equippedIds[MODULAR_SLOT_FULL_OUTFIT], itemId, WARDROBE_ID_MAX_LEN - 1);
    } else {
        // Equipping an individual modular piece (torso or legs) removes the active Full Outfit
        if (slot == MODULAR_SLOT_TORSO || slot == MODULAR_SLOT_LEGS) {
            pw->equippedIds[MODULAR_SLOT_FULL_OUTFIT][0] = '\0';
        }
        strncpy(pw->equippedIds[slot], itemId, WARDROBE_ID_MAX_LEN - 1);
    }

    pw->activeStance = PlayerWardrobe_GetCombatStance(pw);
    return true;
}

bool PlayerWardrobe_Unequip(PlayerWardrobe *pw, ModularSlot slot) {
    if (!pw || slot < 0 || slot >= MODULAR_SLOT_COUNT) return false;
    // Base body cannot be unequipped (humanoid base must exist)
    if (slot == MODULAR_SLOT_BASE_BODY) return false;

    pw->equippedIds[slot][0] = '\0';
    pw->activeStance = PlayerWardrobe_GetCombatStance(pw);
    return true;
}

const char* PlayerWardrobe_GetEquippedId(const PlayerWardrobe *pw, ModularSlot slot) {
    if (!pw || slot < 0 || slot >= MODULAR_SLOT_COUNT) return "";
    return pw->equippedIds[slot];
}

CombatStance PlayerWardrobe_GetCombatStance(const PlayerWardrobe *pw) {
    if (!pw) return COMBAT_STANCE_UNARMED;
    if (pw->equippedIds[MODULAR_SLOT_WEAPON_R][0] != '\0') {
        return COMBAT_STANCE_SWORD;
    }
    return COMBAT_STANCE_UNARMED;
}

bool PlayerWardrobe_ApplyToInstance(const PlayerWardrobe *pw, ModularInstance *inst) {
    if (!pw || !inst) return false;
    ModularSystem_Init();

    // Check Full Outfit
    const char *fullOutfitId = pw->equippedIds[MODULAR_SLOT_FULL_OUTFIT];
    const WardrobeCatalogItem *fullCat = (fullOutfitId && fullOutfitId[0]) ? Wardrobe_GetCatalogItem(fullOutfitId) : NULL;
    inst->occupiedSlotsMask = fullCat ? fullCat->occupiedSlots : 0;
    inst->hideBaseBody = fullCat ? fullCat->hideBaseBody : false;

    for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
        const char *itemId = pw->equippedIds[s];
        if (itemId && itemId[0] != '\0') {
            const WardrobeCatalogItem *cat = Wardrobe_GetCatalogItem(itemId);
            if (cat) {
                if (cat->hideBaseBody) inst->hideBaseBody = true;
                int regIdx = ModularSystem_FindItem(cat->id);
                if (regIdx < 0) {
                    regIdx = ModularSystem_RegisterItem(cat->id, cat->slot, cat->isSkinned,
                                                        cat->boneSocket[0] ? cat->boneSocket : NULL,
                                                        cat->socketOffsetPos, cat->socketOffsetRot,
                                                        cat->modelPath);
                }
                ModularInstance_EquipIndex(inst, (ModularSlot)s, regIdx);
            }
        } else {
            ModularInstance_EquipIndex(inst, (ModularSlot)s, -1);
        }
    }
    return true;
}

WardrobeItemStats PlayerWardrobe_GetTotalStats(const PlayerWardrobe *pw) {
    WardrobeItemStats total = {0};
    total.speedMultiplier = 1.0f; // baseline speed 100%
    if (!pw) return total;

    int fullOutfitSlot = MODULAR_SLOT_FULL_OUTFIT;
    const char *fullOutfitId = pw->equippedIds[fullOutfitSlot];
    const WardrobeCatalogItem *fullCat = (fullOutfitId && fullOutfitId[0]) ? Wardrobe_GetCatalogItem(fullOutfitId) : NULL;
    uint32_t occupied = fullCat ? fullCat->occupiedSlots : 0;

    for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
        // If a full outfit is active, skip stats from suppressed occupied slots
        if (fullCat && (occupied & (1 << s))) continue;

        const char *itemId = pw->equippedIds[s];
        if (!itemId || itemId[0] == '\0') continue;

        const WardrobeCatalogItem *item = Wardrobe_GetCatalogItem(itemId);
        if (!item) continue;

        total.bonusHealth     += item->stats.bonusHealth;
        total.bonusMana       += item->stats.bonusMana;
        total.speedMultiplier *= item->stats.speedMultiplier > 0.0f ? item->stats.speedMultiplier : 1.0f;
        total.elementalBonus  += item->stats.elementalBonus;
        total.elementalResist += item->stats.elementalResist;
        total.manaRegenBonus  += item->stats.manaRegenBonus;
        if (item->stats.specialPerk[0] && total.specialPerk[0] == '\0') {
            strncpy(total.specialPerk, item->stats.specialPerk, sizeof(total.specialPerk) - 1);
        }
    }
    return total;
}

bool PlayerWardrobe_SavePreset(PlayerWardrobe *pw, int presetIndex, const char *presetName) {
    if (!pw || presetIndex < 0 || presetIndex >= WARDROBE_MAX_PRESETS) return false;
    if (presetName && presetName[0]) {
        strncpy(pw->presets[presetIndex].name, presetName, sizeof(pw->presets[presetIndex].name) - 1);
    } else {
        snprintf(pw->presets[presetIndex].name, sizeof(pw->presets[presetIndex].name), "Diện Mạo %d", presetIndex + 1);
    }
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
        strncpy(pw->presets[presetIndex].equippedIds[s], pw->equippedIds[s], WARDROBE_ID_MAX_LEN - 1);
    }
    pw->activePresetIndex = presetIndex;
    return true;
}

bool PlayerWardrobe_LoadPreset(PlayerWardrobe *pw, int presetIndex) {
    if (!pw || presetIndex < 0 || presetIndex >= WARDROBE_MAX_PRESETS) return false;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
        const char *id = pw->presets[presetIndex].equippedIds[s];
        if (id && id[0] && PlayerWardrobe_HasItem(pw, id)) {
            strncpy(pw->equippedIds[s], id, WARDROBE_ID_MAX_LEN - 1);
        } else if (s != MODULAR_SLOT_BASE_BODY) {
            pw->equippedIds[s][0] = '\0';
        }
    }
    pw->activeStance = PlayerWardrobe_GetCombatStance(pw);
    pw->activePresetIndex = presetIndex;
    return true;
}

// -----------------------------------------------------------------------------
// Persistence (Save / Load)
// -----------------------------------------------------------------------------

bool PlayerWardrobe_SaveToFile(const PlayerWardrobe *pw, const char *filePath) {
    if (!pw || !filePath) return false;
    FILE *f = fopen(filePath, "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"player_id\": \"%s\",\n", pw->playerId);
    fprintf(f, "  \"equipped\": {\n");
    fprintf(f, "    \"base_body\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_BASE_BODY]);
    fprintf(f, "    \"torso\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_TORSO]);
    fprintf(f, "    \"legs\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_LEGS]);
    fprintf(f, "    \"boots\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_BOOTS]);
    fprintf(f, "    \"bracers\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_BRACERS]);
    fprintf(f, "    \"headgear\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_HEADGEAR]);
    fprintf(f, "    \"mask\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_MASK]);
    fprintf(f, "    \"hair\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_HAIR]);
    fprintf(f, "    \"waist\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_WAIST]);
    fprintf(f, "    \"back\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_BACK]);
    fprintf(f, "    \"weapon_r\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_WEAPON_R]);
    fprintf(f, "    \"weapon_l\": \"%s\",\n", pw->equippedIds[MODULAR_SLOT_WEAPON_L]);
    fprintf(f, "    \"full_outfit\": \"%s\"\n", pw->equippedIds[MODULAR_SLOT_FULL_OUTFIT]);
    fprintf(f, "  },\n");
    fprintf(f, "  \"unlocked\": [\n");
    for (int i = 0; i < pw->unlockedCount; i++) {
        fprintf(f, "    \"%s\"%s\n", pw->unlockedIds[i], (i + 1 < pw->unlockedCount) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");

    fclose(f);
    TraceLog(LOG_INFO, "WARDROBE: Saved player '%s' wardrobe to '%s'", pw->playerId, filePath);
    return true;
}

static ModularSlot Wardrobe_SlotFromKey(const char *key) {
    if (strcmp(key, "base_body") == 0) return MODULAR_SLOT_BASE_BODY;
    if (strcmp(key, "torso") == 0) return MODULAR_SLOT_TORSO;
    if (strcmp(key, "legs") == 0) return MODULAR_SLOT_LEGS;
    if (strcmp(key, "boots") == 0) return MODULAR_SLOT_BOOTS;
    if (strcmp(key, "bracers") == 0) return MODULAR_SLOT_BRACERS;
    if (strcmp(key, "headgear") == 0) return MODULAR_SLOT_HEADGEAR;
    if (strcmp(key, "mask") == 0) return MODULAR_SLOT_MASK;
    if (strcmp(key, "hair") == 0) return MODULAR_SLOT_HAIR;
    if (strcmp(key, "waist") == 0) return MODULAR_SLOT_WAIST;
    if (strcmp(key, "back") == 0) return MODULAR_SLOT_BACK;
    if (strcmp(key, "weapon_r") == 0) return MODULAR_SLOT_WEAPON_R;
    if (strcmp(key, "weapon_l") == 0) return MODULAR_SLOT_WEAPON_L;
    if (strcmp(key, "full_outfit") == 0) return MODULAR_SLOT_FULL_OUTFIT;
    return (ModularSlot)-1;
}

bool PlayerWardrobe_LoadFromFile(PlayerWardrobe *pw, const char *filePath) {
    if (!pw || !filePath || !FileExists(filePath)) return false;
    char *text = LoadFileText(filePath);
    if (!text) return false;

    PlayerWardrobe_Init(pw, "Player_1");

    // Clear equipped slots before parsing save file so unequipped slots remain empty
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++) {
        pw->equippedIds[s][0] = '\0';
    }

    // First unlock items from "unlocked" array
    const char *unlockedKey = "\"unlocked\"";
    const char *u = strstr(text, unlockedKey);
    if (u) {
        const char *openBracket = strchr(u, '[');
        const char *closeBracket = strchr(u, ']');
        if (openBracket && closeBracket) {
            const char *p = openBracket + 1;
            char idBuf[64];
            while (p < closeBracket && (p = strchr(p, '\"')) != NULL) {
                p++;
                const char *q = strchr(p, '\"');
                if (!q || q > closeBracket) break;
                int len = (int)(q - p);
                if (len >= (int)sizeof(idBuf)) len = sizeof(idBuf) - 1;
                strncpy(idBuf, p, len);
                idBuf[len] = '\0';
                PlayerWardrobe_UnlockItem(pw, idBuf);
                p = q + 1;
            }
        }
    }

    // Now equip items from "equipped" section
    const char *equippedKey = "\"equipped\"";
    const char *eq = strstr(text, equippedKey);
    if (eq) {
        const char *openBrace = strchr(eq, '{');
        const char *closeBrace = strchr(eq, '}');
        if (openBrace && closeBrace) {
            const char *p = openBrace + 1;
            char key[64], val[64];
            while (p < closeBrace && (p = strchr(p, '\"')) != NULL) {
                p++;
                const char *q = strchr(p, '\"');
                if (!q || q > closeBrace) break;
                int klen = (int)(q - p);
                if (klen >= (int)sizeof(key)) klen = sizeof(key) - 1;
                strncpy(key, p, klen);
                key[klen] = '\0';
                p = q + 1;

                char *colon = strchr(p, ':');
                if (!colon || colon > closeBrace) break;
                p = colon + 1;

                char *valQ = strchr(p, '\"');
                if (!valQ || valQ > closeBrace) break;
                valQ++;
                char *valEnd = strchr(valQ, '\"');
                if (!valEnd || valEnd > closeBrace) break;
                int vlen = (int)(valEnd - valQ);
                if (vlen >= (int)sizeof(val)) vlen = sizeof(val) - 1;
                strncpy(val, valQ, vlen);
                val[vlen] = '\0';
                p = valEnd + 1;

                ModularSlot slot = Wardrobe_SlotFromKey(key);
                if ((int)slot >= 0 && slot < MODULAR_SLOT_COUNT) {
                    if (val[0] != '\0') {
                        PlayerWardrobe_UnlockItem(pw, val);
                        strncpy(pw->equippedIds[slot], val, WARDROBE_ID_MAX_LEN - 1);
                    } else {
                        pw->equippedIds[slot][0] = '\0';
                    }
                }
            }
        }
    }

    // Default base body if missing
    if (pw->equippedIds[MODULAR_SLOT_BASE_BODY][0] == '\0') {
        strncpy(pw->equippedIds[MODULAR_SLOT_BASE_BODY], "human_body", WARDROBE_ID_MAX_LEN - 1);
    }

    UnloadFileText(text);
    pw->activeStance = PlayerWardrobe_GetCombatStance(pw);
    TraceLog(LOG_INFO, "WARDROBE: Loaded player '%s' wardrobe from '%s'", pw->playerId, filePath);
    return true;
}

// -----------------------------------------------------------------------------
// Global Wardrobe State Implementation
// -----------------------------------------------------------------------------
static PlayerWardrobe s_globalWardrobe;
static ModularInstance s_globalModularInst;
static bool s_globalWardrobeInitialized = false;
static char s_globalSavePath[256] = WARDROBE_DEFAULT_SAVE_PATH;

void Wardrobe_InitGlobal(const char *savePath) {
    if (s_globalWardrobeInitialized) return;
    Wardrobe_InitCatalog();
    ModularSystem_Init();

    if (savePath && savePath[0]) {
        strncpy(s_globalSavePath, savePath, sizeof(s_globalSavePath) - 1);
    }

    ModularInstance_Init(&s_globalModularInst, MODULAR_TIER_HERO);

    if (FileExists(s_globalSavePath)) {
        PlayerWardrobe_LoadFromFile(&s_globalWardrobe, s_globalSavePath);
    } else {
        PlayerWardrobe_Init(&s_globalWardrobe, "Hero_Player");
        PlayerWardrobe_SaveToFile(&s_globalWardrobe, s_globalSavePath);
    }

    PlayerWardrobe_ApplyToInstance(&s_globalWardrobe, &s_globalModularInst);
    s_globalWardrobeInitialized = true;
    TraceLog(LOG_INFO, "WARDROBE: Global wardrobe initialized with save '%s'", s_globalSavePath);
}

PlayerWardrobe* Wardrobe_GetGlobalPlayer(void) {
    if (!s_globalWardrobeInitialized) Wardrobe_InitGlobal(WARDROBE_DEFAULT_SAVE_PATH);
    return &s_globalWardrobe;
}

ModularInstance* Wardrobe_GetGlobalModularInstance(void) {
    if (!s_globalWardrobeInitialized) Wardrobe_InitGlobal(WARDROBE_DEFAULT_SAVE_PATH);
    return &s_globalModularInst;
}

bool Wardrobe_SaveGlobal(void) {
    if (!s_globalWardrobeInitialized) return false;
    return PlayerWardrobe_SaveToFile(&s_globalWardrobe, s_globalSavePath);
}

bool Wardrobe_ApplyGlobal(void) {
    if (!s_globalWardrobeInitialized) return false;
    return PlayerWardrobe_ApplyToInstance(&s_globalWardrobe, &s_globalModularInst);
}

bool Wardrobe_EquipGlobal(const char *itemId) {
    if (!s_globalWardrobeInitialized) Wardrobe_InitGlobal(WARDROBE_DEFAULT_SAVE_PATH);
    bool ok = PlayerWardrobe_Equip(&s_globalWardrobe, itemId);
    if (ok) {
        Wardrobe_ApplyGlobal();
        Wardrobe_SaveGlobal();
    }
    return ok;
}

bool Wardrobe_UnequipGlobal(ModularSlot slot) {
    if (!s_globalWardrobeInitialized) Wardrobe_InitGlobal(WARDROBE_DEFAULT_SAVE_PATH);
    bool ok = PlayerWardrobe_Unequip(&s_globalWardrobe, slot);
    if (ok) {
        Wardrobe_ApplyGlobal();
        Wardrobe_SaveGlobal();
    }
    return ok;
}
