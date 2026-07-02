# MODULES ROADMAP — Kế Hoạch Triển Khai Module Tiếp Theo

> Nguồn gốc: `nguhanhtyvo_kehoach.md` (thiết kế v3.5) đối chiếu với hiện trạng codebase (07/2026).
> Tài liệu này là **thứ tự triển khai + hợp đồng API** cho các module gameplay còn thiếu.
> Mỗi module khi bắt đầu triển khai PHẢI tạo file `<MODULE>_API.md` riêng ở root (theo mẫu `ENTITIES_API.md`) và `CLAUDE.md` riêng trong thư mục module.

---

## 0. Hiện trạng (đã có — KHÔNG làm lại)

| Layer | Trạng thái |
|---|---|
| `core/` | VFX engine hoàn chỉnh: particle, trail, force field, decal, metaball, post FX, proc ray, skill manager (cooldown/mana **formula** đã có), map manager, tuning |
| `compute/` | GPU particle system hoạt động |
| `environment/` | Lighting / fog / fake shadow hoạt động |
| `maps/` | 4 map (default_arena, bamboo_valley, meadow_night, soft_test_ground) — **chưa có Virtual Trigger Zones** |
| `skills/` | ~11 skill thuộc 5 hệ — thuần VFX + gọi damage qua entities |
| `entities/` | Agent pool tối thiểu (`MAX_AGENTS=256`), damage entry point, vertical physics/ring-out, AoE, modifier slot, nearby query — **chưa có team, mana, Vô Hệ** |
| `sandbox/` | Dev harness (WASD, autotest, debugger) — không phải gameplay ship |

**Thiếu so với thiết kế:** team/Linh Khí/Thiền Định, Vô Hệ, Trigger Zones (Sông/Rừng/Cát), Clash Matrix (Đấu Pháp), player controller thật, Boss Đại Tinh Linh, Thái Cực (Phong/Lôi), Minion, Trận Pháp, HUD/Auto-targeting, game mode, networking.

---

## 1. Nguyên tắc kiến trúc (áp dụng cho MỌI module mới)

1. **Độc lập qua API:** module chỉ `#include` file `.h` của module khác, không bao giờ đọc `.c`. Mọi giao tiếp qua hàm public khai báo trong header + mô tả trong `<MODULE>_API.md`.
2. **Data-driven để AI sáng tạo:** phần "nội dung" (map, boss, skill, trận pháp) phải là **dữ liệu khai báo** (mảng tĩnh, config) tách khỏi phần "engine" (logic tick/resolve). AI tạo nội dung mới = thêm 1 file data + 1 dòng đăng ký, không sửa engine.
3. **Mảng tĩnh, cấm malloc** (C99, Android). Pool cố định như thiết kế §VI.
4. **Logic tách khỏi render:** module gameplay (combat, ai, formations) KHÔNG include VFX header; expose event/hook để layer VFX (skills/core) đọc và vẽ (theo mẫu `Entity_OnDash`).
5. **Một Canvas tổng duy nhất** — không module nào tự tạo RenderTexture riêng (thiết kế §VIII).
6. **Hằng số arena dùng chung:** center `(600,0,440)`, radius `1800` — chỉ định nghĩa ở một nơi (hiện tại `MAP_API.md` §3 / entities), module mới tham chiếu, không copy.
7. **Breaking change API → cập nhật file `_API.md` TRƯỚC khi sửa code.**

---

## 2. BẢNG THỨ TỰ TRIỂN KHAI

| # | Module | Thư mục | API doc | Phụ thuộc (chỉ .h) | Mở khóa | Game Phase |
|---|---|---|---|---|---|---|
| 1 | Entities Combat v2 (Team, Linh Khí, Vô Hệ) | `entities/` (mở rộng) | `ENTITIES_API.md` §12+ | — | mọi module dưới | 0 |
| 2 | Map Virtual Trigger Zones | `maps/` + `core/map_manager.h` | `MAP_API.md` §9 (mới) | map_manager | modifier địa hình, Trận Pháp cộng hưởng | 0 |
| 3 | Combat — Projectile Registry + Clash Matrix | `combat/` (MỚI) | `COMBAT_API.md` | entities.h | Đấu Pháp, auto-targeting, boss AI né đạn | 0 |
| 4 | Player Controller (Khinh công, Thiền định, cast) | `control/` (MỚI) | `CONTROL_API.md` | entities.h, skill_manager.h, combat.h | chơi thật thay vì sandbox | 0 |
| 5 | Boss Đại Tinh Linh | `boss/` (MỚI) | `BOSS_API.md` | entities.h, combat.h + core VFX .h (chỉ phần draw) | Phase 0 DoD, Thái Cực | 0 |
| 6 | Thái Cực State + Phong/Lôi | `entities/` (state) + `core/post_fx` (shader) + `skills/taiji/` (2 skill) | `ENTITIES_API.md` + `CORE_API.md` | entities, combat, boss | cao trào trận đấu | 0 |
| 7 | Game Mode (vòng lặp trận đấu offline) | `game/` (MỚI) | `GAME_API.md` | tất cả .h trên | bản Test Nội Bộ hoàn chỉnh | 0 |
| 8 | Minion Pool + Minion AI | `ai/` (MỚI) + entities archetype | `AI_API.md` | entities.h, combat.h | 4v4 với minion | 1 |
| 9 | HUD + Auto-Targeting | `ui/` (MỚI) | `UI_API.md` | entities.h, combat.h, control.h | mobile UX | 1 |
| 10 | Trận Pháp (Formation Pool) | `formations/` (MỚI) | `FORMATIONS_API.md` | entities.h, map zones, combat.h | khống chế không gian | 2 |
| 11 | Networking ENet (peer-hosted) | `net/` (MỚI) | `NET_API.md` | game.h, entities.h | PvP 1v1 | 1→2 |

Quy tắc thứ tự: **không nhảy cóc quá 1 bậc** — module #N chỉ bắt đầu khi #N-1 build sạch + có autotest sandbox pass. Ngoại lệ: #2 (Map Zones) và #3 (Combat) độc lập nhau, có thể làm song song bởi 2 agent.

---

## 3. CHI TIẾT TỪNG MODULE

### Module 1 — Entities Combat v2 `entities/`

**Mục tiêu:** hoàn thiện Agent thành thực thể chiến đấu đúng thiết kế: phe, Linh Khí, Vô Hệ.

Bổ sung vào `Agent` (không breaking — thêm field):

```c
typedef enum { TEAM_ALLY, TEAM_ENEMY, TEAM_NEUTRAL } AgentTeam;
typedef enum { ARCH_HERO, ARCH_MINION, ARCH_BOSS } AgentArchetype; // pool trộn, xem memory MAX_AGENTS=256

// Thêm vào Agent:
AgentTeam      team;
AgentArchetype archetype;
float          mana, maxMana;          // Linh Khí
bool           isMeditating;           // Thiền Định: bất động 3s, hồi mana nhanh
float          meditateTimer;
int            equippedSkills[4];      // id skill trang bị — nguồn tính Vô Hệ
```

API mới:

```c
bool  Entity_SpendMana(int agentId, float cost);          // false nếu không đủ — skill PHẢI check trước khi cast
void  Entity_StartMeditate(int agentId);                  // hủy nếu di chuyển/trúng đòn
int   Entity_RecomputeElement(int agentId);               // Vô Hệ: hệ đa số trong equippedSkills[4]
int   Entity_GetNearbyTargetsTeam(Vector3 c, float r, AgentTeam filter, int *out, int max); // bản có lọc phe
```

Sửa các hàm cũ: `Entity_ApplyAoEDamage/Buff` nhận thêm tham số phe (fix limitation §9 ENTITIES_API). `Entity_SpawnAgent` nhận `team`/`archetype`.

**DoD:** autotest — spawn 2 phe, AoE chỉ trúng địch, buff chỉ trúng đồng minh; mana cạn → cast fail; thiền 3s hồi đầy; đổi equippedSkills → element đổi đúng luật đa số.

---

### Module 2 — Map Virtual Trigger Zones `maps/`

**Mục tiêu:** địa hình tĩnh thành vùng dữ liệu nguyên tố (thiết kế Trụ cột 2, Tầng 3). Map = pure data; luật modifier nằm ở consumer (entities/game), KHÔNG nằm trong map.

Thêm vào `core/map_manager.h`:

```c
typedef enum { NAT_NONE, NAT_RIVER, NAT_FOREST, NAT_DESERT_ZONE } NatureZoneType;

typedef struct {
    NatureZoneType type;
    Vector3 center;   // bám sàn, y = 0
    float   radius;   // check bằng khoảng cách XZ, giống Entity_GetNearbyTargets
} MapZone;

#define MAX_MAP_ZONES 16

// Mỗi map khai báo trong <map>.h/.c:
int            Map_GetZoneCount(void);                 // của map đang active
const MapZone *Map_GetZone(int index);
NatureZoneType Map_QueryZoneAt(Vector3 pos);           // NAT_NONE nếu ngoài mọi vùng
```

Luật modifier (bảng tra cứu tĩnh, đặt ở `game/` hoặc `entities/`, KHÔNG ở map):

| Zone | Hệ hưởng lợi | Hệ chịu thiệt |
|---|---|---|
| `NAT_RIVER` | Thủy: -50% cooldown | Hỏa: -50% damage |
| `NAT_FOREST` | Mộc: +50% độc, ẩn hình (isStealthed) | Kim: -tốc độ xuất chiêu; đạn Thổ bay vào -50% dmg |
| `NAT_DESERT_ZONE` | Thổ: +knockback | Thủy: -50% tầm đánh |

**AI sáng tạo map mới:** thêm map = 1 thư mục theo `MAP_API.md` §7 hiện có + 1 mảng `static MapZone zones[]` + vẽ visual cue (mesh sông/rừng/cát khớp vị trí zone). Engine không đổi. Zone phải có tín hiệu thị giác rõ (No Tutorial — người chơi tự Ngộ).

**DoD:** debug_draw vẽ được vòng zone; agent đứng trong sông nhận đúng modifier (test qua Module 1 API); map không có zone vẫn chạy bình thường.

---

### Module 3 — Combat: Projectile Registry + Clash Matrix `combat/` (MỚI)

**Mục tiêu:** Đấu Pháp — trái tim gameplay. Skill↔Skill (Tầng 1) và chuẩn hóa Skill↔Entity (Tầng 2).

Thiết kế **immediate-mode**: skill vẫn tự sở hữu chuyển động + VFX của đạn, mỗi frame chỉ *nộp* collider vào registry; combat resolve va chạm rồi trả **event** để skill tự vẽ hiệu ứng clash. Nhờ vậy skills và combat độc lập hoàn toàn.

```c
typedef enum { ELEM_WATER, ELEM_WOOD, ELEM_FIRE, ELEM_EARTH, ELEM_METAL } CombatElement; // khớp Agent.currentElement

typedef enum { CLASH_MUTUAL_DESTROY, CLASH_A_WINS, CLASH_B_WINS, CLASH_PASS_THROUGH } ClashOutcome;

#define MAX_COMBAT_PROJECTILES 128

// Skill gọi MỖI FRAME khi đạn còn sống (immediate-mode submit):
int  Combat_SubmitProjectile(int ownerAgentId, CombatElement elem,
                             Vector3 pos, float radius, float damage,
                             float knockback, int skillInstanceId);

// Cuối Combat_Update(dt): resolve bằng CheckCollisionSpheres + Clash Matrix 5x5 (const CPU table)
void Combat_Update(float dt);

// Skill poll kết quả để hủy đạn / spawn VFX clash:
typedef struct {
    int skillInstanceId;      // đạn của mình
    ClashOutcome outcome;
    Vector3 clashPoint;
    CombatElement otherElem;  // để chọn màu VFX tương khắc
} ClashEvent;
int  Combat_PollEvents(ClashEvent *out, int max);   // drain queue mỗi frame

// Skill↔Agent: combat tự gọi Entity_ApplyDamage khi đạn chạm agent khác phe.
// Ma trận 5x5: hằng số const — Thủy khắc Hỏa, Hỏa khắc Kim, Kim khắc Mộc, Mộc khắc Thổ, Thổ khắc Thủy.
```

Quy ước: skill KHÔNG tự gọi `Entity_ApplyDamage` cho đạn projectile nữa — nộp qua registry để hit detection + Đấu Pháp + phe nằm một chỗ. Skill AoE/melee vẫn dùng `Entity_ApplyAoEDamage` trực tiếp.

**Cân bằng Thổ (thiết kế §XI):** trường `castTime`/`projectileSpeed` là tham số của skill, không của combat — combat chỉ resolve; penalty Thổ ở Rừng đọc `Map_QueryZoneAt(pos)` trong `Combat_Update`.

**DoD:** autotest bắn 2 đạn Thủy–Hỏa đối đầu → Thủy thắng, event trả đúng; đạn cùng phe xuyên qua nhau; 128 đạn đồng thời không tụt frame.

---

### Module 4 — Player Controller `control/` (MỚI)

**Mục tiêu:** chính thức hóa điều khiển (đang tạm nằm trong sandbox): di chuyển, khinh công, thiền định, cast 4 skill. Chuẩn bị sẵn abstraction cho touch (Phase 1).

```c
// Tách INPUT (thiết bị) khỏi INTENT (gameplay) — sau này touch/gamepad/net chỉ thay layer input.
typedef struct {
    Vector2 moveDir;        // đã chuẩn hóa theo camera isometric
    bool    jump, dash, meditate;
    int     castSkillSlot;  // -1 = không cast; 0..3 = equippedSkills slot
    Vector3 aimPoint;       // điểm nhắm trên sàn (ray từ chuột / auto-target sau này)
} PlayerIntent;

void Control_Init(int agentId);                 // gắn controller vào 1 agent trong pool
PlayerIntent Control_ReadIntent(void);          // đọc thiết bị → intent
void Control_Apply(const PlayerIntent *in, float dt);
// Apply: di chuyển (đọc speedMult modifiers — wiring còn thiếu của ENTITIES §8),
// Entity_Jump/Entity_Dash (implement dash THẬT ở đây: velocity burst + gọi Entity_OnDash),
// Entity_StartMeditate, cast qua skill_manager (check Entity_SpendMana trước).
```

Kèm việc nhỏ ở entities: implement thân `Entity_Dash` thật (đang stub) + wire `speedMult`.

**DoD:** chơi được trong game thật: WASD lướt, space nhảy, dash có cooldown + hook bắn, thiền hồi mana, cast trừ mana; sandbox chuyển sang dùng `control/` (xóa PlayerEntity trùng lặp).

---

### Module 5 — Boss Đại Tinh Linh `boss/` (MỚI)

**Mục tiêu:** Boss Hắc Diện Tôn Giả cho Phase 0 (thiết kế §V.2). Boss = 1 agent `ARCH_BOSS` trong agentPool (logic) + lớp visual metaball/emitter (render) + state machine phase.

**Tách 2 nửa để AI sáng tạo boss mới dễ dàng:**

```c
// ---- Nửa DATA (mỗi boss 1 file boss/<ten_boss>_def.c — AI tạo boss mới ở đây) ----
typedef struct {
    const char   *name;
    float         maxHealth;
    CombatElement phaseElements[4];  // hệ đổi theo phase (Hắc Diện biến hệ)
    float         phaseHpThresholds[4]; // % máu chuyển phase, [0]=1.0
    int           skillPerPhase[4];  // id skill boss dùng mỗi phase
    void        (*drawVisual)(const Agent *self, float phaseT); // metaball/FBM/emitter — chỉ nửa này include core VFX .h
} BossDef;

// ---- Nửa ENGINE (boss/boss_system.c — viết 1 lần, không sửa khi thêm boss) ----
int  Boss_Spawn(const BossDef *def, Vector3 pos, AgentTeam team); // trả agentId
void Boss_Update(float dt);   // state machine: chọn mục tiêu (Entity_GetNearbyTargetsTeam),
                              // cast skill theo phase, chuyển phase theo % HP,
                              // <30% HP → yêu cầu Thái Cực (Module 6)
void Boss_Draw(void);         // gọi def->drawVisual
```

Luật: `boss_system.c` KHÔNG include VFX header — chỉ `_def.c` được include (nguyên tắc §1.4 nới riêng cho hàm draw của def, vì boss visual = 100% VFX theo thiết kế).

**AI sáng tạo:** 10 boss trong thiết kế §V.2 = 10 file `_def.c` (Vatu sinf() phập phồng, Zephyrus xoắn ốc, Tiamat sứa khúc xạ...). Phase 0 chỉ cần 1 boss (Hắc Diện Tôn Giả); mục tiêu 1.0 cần 3.

**DoD:** đánh boss thắng/thua được trong game mode; boss đổi hệ theo phase (rãnh hoa văn đổi màu — visual cue); boss dùng skill bắn qua combat registry; boss bị knockback rớt vực chết đúng vật lý.

---

### Module 6 — Thái Cực State (Phong / Lôi)

**Mục tiêu:** Cảnh Giới Thái Cực (Trụ cột 3). Trải trên 3 module có sẵn — KHÔNG tạo thư mục mới:

| Phần | Chủ sở hữu | Nội dung |
|---|---|---|
| Trigger + state | `entities/` | `bool taijiActive` trên Agent; điều kiện: build 2 Âm + 2 Дương trong `equippedSkills` (check trong `Entity_RecomputeElement`) hoặc boss <30% HP (Boss_Update gọi). Miễn nhiễm khắc chế: combat đọc flag này khi tra Clash Matrix |
| Shader đơn sắc | `core/post_fx` | `PostFX_SetMonochrome(float intensity)` — áp lên Canvas tổng, KHÔNG render target mới |
| 2 tuyệt học | `skills/taiji/` | **PHONG**: force field hút (core/force_field có sẵn) gom agent + đạn (combat cần hàm `Combat_DeflectProjectilesInRadius`); **LÔI**: sét tím trắng dùng `core/vfx_proc_ray` (đã có, xem memory Thunder Orb) giáng vào tâm Phong |

Nhược điểm "Vô Sát" (rủi ro §XVII): Lôi tụt mana cực nhanh — enforce qua `Entity_SpendMana`.

**DoD:** vào Thái Cực → toàn màn hình trắng đen, 4 skill cũ khóa, Phong hút được cả minion + đạn (registry), Lôi đánh diện rộng; hết mana → thoát trạng thái.

---

### Module 7 — Game Mode `game/` (MỚI)

**Mục tiêu:** vòng lặp trận đấu Phase 0 hoàn chỉnh (thiết kế §X) — module "nhạc trưởng" duy nhất được include mọi `.h`. `main.c` gọn lại chỉ còn init/loop/unload; sandbox giữ nguyên làm dev harness (chuyển chế độ bằng flag/phím).

```c
typedef enum { GAME_MENU, GAME_ARENA_INTRO, GAME_FIGHTING, GAME_VICTORY, GAME_DEFEAT } GameState;

void Game_Init(void);      // load map, spawn player (control), spawn boss (BossDef), setup env đêm
void Game_Update(float dt);// thứ tự tick CHUẨN: Control → Entities → Boss/AI → Combat → Formations → Skills(VFX) → Env
void Game_Draw(void);      // 1 canvas tổng: map → entities/boss → skills VFX → post fx
void Game_Unload(void);
```

Đây cũng là nơi đặt **bảng luật zone modifier** (Module 2) — luật gameplay tập trung một chỗ.

**DoD = Definition of Done Phase 0 (thiết kế §X):** điều khiển mượt, thắng/thua boss, ring-out chuẩn, Thái Cực hoạt động, ≥60FPS Android.

---

### Module 8 — Minion Pool + AI `ai/` (MỚI) — Phase 1

Minion = agent `ARCH_MINION` trong agentPool (không pool riêng — theo memory MAX_AGENTS=256 trộn hero+minion+boss). Module `ai/` chỉ chứa **brain**:

```c
void AI_Init(void);
void AI_Update(float dt);  // duyệt agentPool: ARCH_MINION → steering lầm lũi về boss địch,
                           // tới gần → tự nổ (Entity_ApplyAoEDamage + event cho VFX);
                           // sau này thêm brain cho ARCH_HERO (4 AI địch)
int  AI_SpawnMinionWave(int bossAgentId, int count); // boss sinh minion quanh mình
```

Không include VFX — nổ/di chuyển phát event kiểu `Entity_OnDash` cho skills vẽ.

**DoD:** boss 2 phe sinh minion, minion bò đúng hướng, tự nổ gây damage đúng phe, 40+ minion không tụt frame.

---

### Module 9 — HUD + Auto-Targeting `ui/` (MỚI) — Phase 1

```c
void UI_Init(void); void UI_Update(float dt); void UI_DrawOverlay(void); // 2D pass sau canvas tổng
// Auto-target (thiết kế §XI): ưu tiên 1 = đạn địch đang bay tới (đọc Combat registry) để đối đòn;
// ưu tiên 2 = boss địch. Kết quả ghi vào PlayerIntent.aimPoint (control/ tiêu thụ).
Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget);
```

Tối giản theo triết lý No Tutorial: chỉ HP/Mana bar + nút ảo mobile, không text hướng dẫn.

---

### Module 10 — Trận Pháp `formations/` (MỚI) — Phase 2

```c
typedef struct {
    const char *name;
    CombatElement elem;
    float radius, duration, manaCost;
    NatureZoneType resonantZone;  // đè lên zone này → mạnh hơn (Lôi Động Trận + Sông)
    void (*onTick)(Vector3 center, float dt);   // logic: buff/debuff/stun qua Entity_* API
    void (*drawGround)(Vector3 center, float t);// decal_system + emitter — chỉ data file include VFX
} FormationDef;   // AI sáng tạo trận mới = 1 file def, giống BossDef

#define MAX_FORMATIONS 4   // formationPool[4] theo thiết kế §VI
int  Formation_Deploy(const FormationDef *def, Vector3 center, int ownerAgentId);
void Formation_Update(float dt);  // tick modifier lên agent trong bán kính (Tầng 4)
```

5 trận thiết kế (Cửu Thiên Lôi Động, Hàn Băng Thủy Tuyệt...) = 5 file def.

---

### Module 11 — Networking `net/` (MỚI) — Phase 1→2

ENet luồng phụ, peer-hosted, host resolve toàn bộ combat (thiết kế §XI). Chỉ bắt đầu khi Game Mode offline ổn định. API sẽ thiết kế chi tiết khi tới lượt — nguyên tắc: `net/` serialize **PlayerIntent** (Module 4 đã tách intent sẵn cho việc này) gửi lên host, host chạy simulation, gửi snapshot agentPool về. Không module nào khác biết đến network.

---

## 4. Cập nhật bảng phân quyền agent (khi module mới ra đời)

Thêm vào bảng Module Agents trong `CLAUDE.md` root khi tạo module:

| Agent mới | Owns | Extra read |
|---|---|---|
| **Combat Agent** | `combat/` | `entities/entities.h`, `core/map_manager.h` |
| **Control Agent** | `control/` | `entities/entities.h`, `core/skill_manager.h`, `combat/combat.h` |
| **Boss Agent** | `boss/` | `entities/entities.h`, `combat/combat.h`, core VFX `.h` (chỉ trong `_def.c`) |
| **Game Agent** | `game/` | mọi `.h` public |
| **AI Agent** | `ai/` | `entities/entities.h`, `combat/combat.h` |
| **UI Agent** | `ui/` | `entities/entities.h`, `combat/combat.h`, `control/control.h` |
| **Formations Agent** | `formations/` | `entities/entities.h`, `core/decal_system.h`, `core/map_manager.h` |

---

## 5. Checklist khởi động một module (quy trình chuẩn)

1. Đọc section tương ứng trong file này + `nguhanhtyvo_kehoach.md` phần liên quan.
2. Viết `<MODULE>_API.md` đầy đủ (mẫu: `ENTITIES_API.md` — có mục "Explicitly NOT in this version").
3. Viết `CLAUDE.md` cho module (scope, forbidden dirs, token-efficiency rules).
4. Implement `.h` trước, `.c` sau; static arrays; build sạch bằng `make`.
5. Thêm autotest vào `sandbox/auto_test` cho DoD của module.
6. Cập nhật bảng agent ở `CLAUDE.md` root + đánh dấu hoàn thành trong file này.
