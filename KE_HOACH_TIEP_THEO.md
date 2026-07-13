# KẾ HOẠCH TIẾP THEO — Sau đợt Net/EOS (12/07/2026)

> Nguồn đối chiếu: `nguhanhtyvo_kehoach.md` §IX (chiến lược giai đoạn) + §XVIII
> (mục tiêu v1.0), `MODULES_ROADMAP.md` (Module 1–11 ✅), `NET_API.md` §3b/3c.
> Tài liệu này là **thứ tự ưu tiên cho các đợt làm việc kế tiếp** — mỗi đợt kết
> thúc bằng build sạch + autotest pass + cập nhật API docs (không cập nhật giữa chừng).

---

## 0. HIỆN TRẠNG — đã xong, KHÔNG làm lại

| Mảng | Trạng thái |
|---|---|
| Gameplay core | Module 1–10: entities v2, map zones, combat clash, control intent, boss Hắc Diện, Thái Cực, game mode, minion AI, HUD auto-target, 2 trận pháp — autotest 14/14 |
| Match thật | VERDANT_PATH island, ring-out, minion wave theo phase boss, Loadout UI (TAB) |
| Net LAN | ENet peer-hosted (`--host` / `--join <ip>`), protocol v2 (melee + match-state sync) |
| Net INTERNET | **EOS backend hoàn chỉnh**: Device ID auth, lobby mã phòng 5 ký tự, P2P xuyên NAT, fragmentation; UI menu TAO PHONG / NHAP MA + HUD mã phòng; verify end-to-end qua Epic thật |
| Assets chờ | `ANIMATION_ASSETS_PLAN.md` — user đang chuẩn bị GLB (player dash/jump/hit/die, minion, boss) |

## Khoảng cách so với mục tiêu v1.0 (thiết kế §XVIII)

| Mục tiêu v1.0 | Hiện có | Thiếu |
|---|---|---|
| PvP đối kháng hai phe (tầm nhìn: tới **4v4**, sảnh chờ, bot bù người) | Invasion duel 1v1 (khách = TEAM_ENEMY đánh chung trận boss), transport 1 peer | Multi-peer (8 người), sảnh chờ, team battle 1v1→4v4, hero-bot + buff bù chênh lệch |
| 20 skill / 5 hệ | ~11 skill + 2 Thái Cực | ~7–9 skill mới, phủ đủ phân cấp cự ly mỗi hệ |
| ≥3 Boss Đại Tinh Linh | 1 (Hắc Diện Tôn Giả) | +2 BossDef (engine data-driven đã sẵn) |
| 5 Trận Pháp | 2 | +3 FormationDef |
| Campaign vượt sóng minion | Minion wave phụ trợ trong trận boss | Mode riêng: sóng tăng dần → boss chốt |
| 3 map địa hình có zone | VERDANT_PATH (+ map sandbox) | Map Sa mạc đêm trăng (heightmap) + 1 map nữa, đủ bộ Sông/Rừng/Cát |
| Android + PC ≥60FPS | PC chạy tốt | Build Android lại, profiling FPS, touch controls, EOS Android |

---

## ĐỢT A — Trận đội online 1v1 → 4v4 (ưu tiên cao nhất)

**TẦM NHÌN (chốt 13/07/2026):** trận lớn nhất là **4v4**. Mở phòng = vào
**sảnh chờ**; chưa đủ người vẫn bắt đầu được (1v1/2v2/3v3/4v4); hai phe lệch
số lượng thì **bù bằng người chơi ảo (hero-bot AI)** + **buff bù chênh lệch**
cho phe ít người. Chế độ "xâm nhập trận boss" hiện tại giữ làm đường dev/test,
không phải mode ship.

Toàn bộ nằm trong `net/` + `game/` + `ai/` (hero-bot) + `ui/` (sảnh chờ),
không đụng engine VFX. Thứ tự bắt buộc: A1 → A2 → A3 → A4 → A5 (mỗi bước
build sạch + autotest rồi mới sang bước sau).

### A1. Multi-peer transport — nền móng mọi thứ *(L — 1-2 phiên)* ✅ 13/07
- Hiện transport chỉ 1 peer (`enet_host_create(..., 1 peer)`, EOS
  `MaxLobbyMembers = 2`, một `s_remotePuid`). Nâng lên **tối đa 7 khách + host = 8**:
  - `net_transport.c`: mảng peer tĩnh `NetPeer[NET_MAX_PLAYERS-1]` (ENet peer
    hoặc EOS PUID + trạng thái + agentId của hero từng người). Host nhận
    intent **theo từng peer**, snapshot broadcast cho mọi peer.
  - Protocol **v3**: `NET_CTRL_HELLO` mang thêm `slot/team`; thêm
    `NET_CTRL_ROSTER` (danh sách người chơi trong phòng — đổi là gửi lại).
  - EOS: `MaxLobbyMembers = 8`; backend seam giữ nguyên, `EosSend` thêm tham
    số đích (broadcast = lặp qua peer list).
- **DoD:** 3 instance (1 host + 2 khách, dùng `WUXING_EOS_FRESH_DEVICE`) cùng
  vào phòng, cả 2 khách điều khiển hero riêng, snapshot đồng bộ cả 3 màn hình;
  autotest wire-format v3 (roster round-trip).

### A2. Sảnh chờ (lobby screen) *(M–L — 1-2 phiên)* ✅ 13/07 (kèm heartbeat 1Hz + host timeout 8s — EOS không tự báo peer chết)
- Màn hình mới giữa menu và trận: **8 ô slot chia 2 cột phe** (Thanh Long /
  Bạch Hổ...). Người vào phòng được xếp slot tự cân bằng, host có thể kéo/đổi
  phe. Hiện mã phòng to + danh sách tên (Device ID → tên "P1..P8" trước,
  display name sau).
- Slot trống hiển thị "BOT" (bật/tắt từng slot bởi host) — quyết định cấu
  hình bot NGAY ở sảnh, không phải lúc vào trận.
- Host bấm **BẮT ĐẦU** → gửi `NET_CTRL_START` kèm roster chốt; mọi client
  chuyển vào trận cùng lúc. Phòng đóng khi trận bắt đầu (late-join = Giai đoạn 3).
- **DoD:** 3 người thật vào sảnh thấy nhau + đổi phe; host start cả 3 vào
  trận đúng đội hình; khách thoát sảnh → slot mở lại.

### A3. Team battle mode 1v1 → 4v4 *(M — 1 phiên)* ✅ 13/07 (verify online trọn vòng thắng-thua-rematch qua Epic)
- `game/`: `GAME_MODE_TEAM_BATTLE` — spawn 2 phe đối diện theo roster
  (2 cụm spawn point trên VERDANT_PATH), KHÔNG boss/minion wave.
- Luật thắng: **team elimination** — phe nào hết hero (chết/rớt đài) thua;
  đồng đội chết KHÔNG respawn trong trận (trận ngắn, đúng nhịp đấu trường).
  ENTER = rematch cả phòng (host reset + `NET_CTRL_STATE`).
- HUD: thanh máu đồng đội/địch mini theo phe, đếm số hero còn lại mỗi phe.
- **DoD:** autotest luật elimination offline (giả lập 2v2, giết dần từng
  hero → đúng phe thắng); trận 1v1 online trọn vòng thắng-thua-rematch.

### A4. Hero-bot AI + buff bù chênh lệch *(L — 1-2 phiên)* ✅ 13/07 (verify online 1v2-bot: handicap +1 đúng cả 2 vòng; bot tự hạ host đứng im)
- **Hero-bot** (`ai/` mở rộng — brain RIÊNG, không dùng minion brain):
  điều khiển agent ARCH_HERO phe thiếu người. Hành vi tối thiểu đáng chơi:
  giữ cự ly theo bộ skill trang bị, cast theo cooldown + mana, né ra khỏi
  rìa đài, ưu tiên target yếu máu / gần nhất, dash né khi đạn bay vào
  (đọc combat snapshot — auto-target đã có sẵn logic bắt đạn để tái dùng).
  Bot chạy HOÀN TOÀN trên host — với client, bot không khác gì người thật
  (cùng đường snapshot), zero việc thêm ở tầng net.
- **Buff bù chênh lệch** (bảng tra tĩnh trong `game/game_rules.c` — nơi duy
  nhất chứa luật): phe ít hơn N người nhận buff theo bậc, ví dụ khởi điểm
  `+15% damage, +15% max HP, +25% mana regen` mỗi đầu người chênh — số liệu
  tinh chỉnh qua sandbox tunables (`RegisterSkillTunables` pattern).
- Bot cũng đếm là "người" khi tính chênh lệch (bot thay người thì KHÔNG buff
  — buff chỉ dành cho phe chấp nhận đánh thiếu).
- **DoD:** autotest — 1v2 có bot: bot cast được skill + không tự rớt đài;
  1v2 không bot: phe 1 người nhận đúng buff theo bảng; 2v2 đủ người: không buff.

### A5. Chất lượng đường truyền + đồng bộ còn thiếu *(M — 1 phiên)* ✅ 13/07 (autotest 16/16 + verify online: người chơi hiện đúng model, render nhiều hero không clobber). Kèm fix môi trường: self-join guard (cùng máy quên FRESH_DEVICE), ForceRelays mặc định (NAT-proof cho 2 máy thật), log chẩn đoán NAT/relay

> **ĐỢT A HOÀN TẤT 13/07/2026** — PvP online 1v1→4v4 chơi được thật: sảnh chờ, team battle elimination, hero-bot + buff bù người, mirror chiêu + nội suy, đối thủ render bằng model thật. autotest 16/16.
- **VFX event mirroring:** host phát `NET_EVT_CAST {agentId, skillIndex,
  aimPoint}` (reliable) khi CastSkill thành công; client cast "visual-only"
  (client không tick Combat_Update nên projectile chỉ là VFX — damage vẫn từ
  snapshot). Kèm minion explosion + boss phase (cho mode xâm nhập/campaign).
- **Snapshot interpolation:** client giữ 2 snapshot gần nhất, lerp ~100ms
  buffer (không prediction).
- **Loadout sync:** client đổi TAB → gửi 4 slot lên host →
  `Entity_SetEquippedSkill` + `RecomputeElement` (Vô Hệ đúng cho mọi người chơi).
- **Zone rule cho remote hero:** `HostApplyRemoteEdges` áp cooldown mult theo zone.
- **DoD:** trận 2v2 hai máy thật: thấy chiêu của nhau bay đúng hướng, chuyển
  động mượt, đổi loadout ở sảnh/trận phản ánh đúng element.

---

## ĐỢT B — Nội dung (song song được với A bằng agent riêng)

### B1. Thêm 2 Boss Đại Tinh Linh *(M — 1 phiên/boss)*
- 2 `boss/*_def.c` mới theo mẫu `hac_dien_ton_gia_def.c` (BossDef data-driven,
  skill theo NAME). Đề xuất theo thiết kế: 1 boss thiên về khống chế (Thổ/Kim),
  1 boss cơ động đánh nhanh (Hỏa/Phong) — metaball shader đổi rãnh hoa văn chỉ thị hệ.
- **DoD:** chọn boss khi vào trận (hoặc random); autotest phase-shift cho boss mới.

### B2. Đủ 5 Trận Pháp *(M — 1 phiên)*
- +3 `formations/*_def.c` (Cửu Thiên Lôi Động... theo danh sách thiết kế §VI),
  tận dụng zone resonance đã có. VFX dùng compose sẵn (`vfx_proc_ray`, ribbon).
- **DoD:** 5/5 trận deploy được, autotest formation mở rộng.

### B3. Phủ 20 skill / 5 hệ *(L — 2-3 phiên, chia nhỏ theo hệ)*
- Rà `SKILL_RECIPE.md`: mỗi hệ đủ 4 skill phân cấp cự ly (cận/trung/xa/đặc thù).
  Skill mới BẮT BUỘC: meter-scale từ đầu, nộp collider vào combat registry,
  `_params.inl` + `_tunables.inl`, wire vào `sandbox/vfx_test.c` NEW FX tab.
- Kèm cân bằng hệ Thổ (thiết kế §XI): đạn chậm ×0.5 Hỏa, cast 1.5s đứng yên — 
  kiểm tra đã áp dụng đúng trong combat/ chưa, chưa thì bổ sung.
- **DoD:** bảng 5 hệ × 4 skill đủ ô; autotest combat registry đếm đủ skill mới.

### B4. Campaign mode vượt sóng *(M — 1 phiên)*
- `game/`: `GAME_MODE_CAMPAIGN` — sóng minion tăng dần (dùng ai/ pool sẵn),
  xen 1 trận boss chốt; chết = hết, thắng boss = xong màn.
- **DoD:** chơi được từ menu ("6. CAMPAIGN"), autotest wave-tăng-dần headless.

### B5. Map mới *(M — 1 phiên/map)*
- Map Sa mạc đêm trăng (heightmap tĩnh + `NAT_DESERT_ZONE`) — mục tiêu v1.0 nêu đích danh.
- Mỗi map: visual cue zone rõ (No Tutorial), khai báo `MapZone[]`, arena bounds riêng
  (`Entity_SetArenaBounds` per-map đã có).
- **DoD:** K-cycle qua map mới không leak bounds; zone modifier đúng bảng luật.

---

## ĐỢT C — Mobile / Android (sau khi A xong, B có thể dở dang)

### C1. Build Android lại + profiling *(M — 1 phiên, cần user chạy device thật)*
- Áp Rules A–E shader GLES (memory `project_android_shader_pipeline`).
  User đo FPS trên máy thật — mục tiêu ≥60FPS, nghi phạm chính: GPU particle + post FX.
- **DoD:** APK chạy, số FPS đo thật được ghi lại để tuning.

### C2. Touch controls *(M–L — 1-2 phiên)*
- Virtual joystick trái (move) + nút phải (skill 1-4, melee, dash/jump),
  auto-target đã có sẵn lo phần aim (đúng thiết kế Mobile Auto-Targeting §XI).
- `control/` đọc touch → cùng `PlayerIntent` (không đổi wire format).
- **DoD:** chơi trọn trận boss bằng cảm ứng, không cần chuột/phím.

### C3. EOS Android *(S–M — 1 phiên)*
- SDK có sẵn `Bin/Android`; CMake nhánh Android link `.so` + JNI init
  (EOS Android cần `EOS_Android_InitializeOptions` — xem `Android/eos_android.h`).
- **DoD:** 2 máy (PC + Android) vào chung phòng bằng mã.

---

## ĐỢT D — Feel & polish (chạy nền, chen giữa các đợt)

| # | Việc | Cỡ | Ghi chú |
|---|---|---|---|
| D1 | Wire animation GLB khi user giao asset | S/asset | Theo `ANIMATION_ASSETS_PLAN.md`; slot anim đã chừa sẵn trong `character/` |
| D2 | Boss pattern variety | S–M | Hắc Diện hiện drift/strafe đơn giản — thêm telegraph + combo theo phase |
| D3 | Âm thanh (NGOÀI thiết kế — đề xuất) | M | raylib audio; tối thiểu: cast, hit, clash Đấu Pháp, sting Thái Cực, ambient đêm. Cần user duyệt trước khi làm |
| D4 | Tuning pass cân bằng | M | Playtest: Thổ penalty, Thái Cực "Vô Sát" (thiết kế §XVII), mana economy — dùng sandbox tunables sẵn |
| D5 | Net side-thread + reconnection | M | Thiết kế §XI (ENet đa luồng) — hoãn được vì main-thread polling chưa nghẽn |

## NGOÀI PHẠM VI hiện tại (Giai đoạn 3 — chưa đụng)

Ranking server, trang phục, sự kiện mùa, late-join giữa trận, Firebase.
Host migration: theo thiết kế đã chốt "trận tự kết thúc khi host rời" — áp
dụng nguyên xi cho team battle (host rời sảnh = giải tán phòng, rời trận =
trận kết thúc, không chuyển host).

---

## Thứ tự đề xuất & quy trình

```
A1 (multi-peer) → A2 (sảnh chờ) → A3 (team battle) → A4 (bot+buff) → A5 (mượt+sync)
                                  ↘ B1, B2, B3 (agent nội dung chạy song song từ sau A3)
sau A5:  C1 → C2 → C3 (Android)  →  B4 → B5...
D1 chen bất kỳ lúc nào có asset; D4 sau mỗi đợt nội dung (bảng buff A4 cần D4 sớm).
```

Quy trình mỗi hạng mục (giữ nguyên nếp cũ):
1. Đọc section thiết kế liên quan + API doc của module đụng tới.
2. Sửa `.h` + API doc TRƯỚC khi đổi API (breaking change).
3. Static arrays, C99, meter-scale, không malloc; VFX mới → wire vfx_test.
4. Thêm/ mở rộng autotest cho DoD; build sạch; **14/14+ pass** rồi mới sang mục kế.
5. Cập nhật docs MỘT LẦN cuối đợt (không giữa chừng).
