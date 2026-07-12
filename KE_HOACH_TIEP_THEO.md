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
| PvP 1v1 **đối kháng hai phe** | Invasion duel (khách = TEAM_ENEMY đánh chung trận boss) | Chế độ duel đối xứng thật, kết thúc trận khi 1 hero chết |
| 20 skill / 5 hệ | ~11 skill + 2 Thái Cực | ~7–9 skill mới, phủ đủ phân cấp cự ly mỗi hệ |
| ≥3 Boss Đại Tinh Linh | 1 (Hắc Diện Tôn Giả) | +2 BossDef (engine data-driven đã sẵn) |
| 5 Trận Pháp | 2 | +3 FormationDef |
| Campaign vượt sóng minion | Minion wave phụ trợ trong trận boss | Mode riêng: sóng tăng dần → boss chốt |
| 3 map địa hình có zone | VERDANT_PATH (+ map sandbox) | Map Sa mạc đêm trăng (heightmap) + 1 map nữa, đủ bộ Sông/Rừng/Cát |
| Android + PC ≥60FPS | PC chạy tốt | Build Android lại, profiling FPS, touch controls, EOS Android |

---

## ĐỢT A — PvP Online hoàn chỉnh (ưu tiên cao nhất)

Mục tiêu: từ "kết nối được" → "chơi được thật với bạn bè". Toàn bộ nằm trong
`net/` + `game/` + `entities/` (event hooks), không đụng engine VFX.

### A1. Duel mode đối xứng — trận PvP thật *(M — 1 phiên)*
- `game/`: thêm `GAME_MODE_DUEL` — 2 hero 2 phe spawn đối diện, KHÔNG boss,
  không minion wave; thắng/thua khi 1 hero chết hoặc rớt đài (ring-out).
- Host chọn mode khi tạo phòng (menu: TAO PHONG → chọn "SONG DAU" / "XAM NHAP").
- Invader chết = kết thúc trận (fix limitation hiện tại), ENTER = rematch
  (reset cả 2 hero + đồng bộ qua `NET_CTRL_STATE`).
- **DoD:** 2 instance đấu 1 trận trọn vẹn thắng-thua-rematch; autotest cho
  luật thắng/thua duel (offline giả lập 2 hero).

### A2. VFX event mirroring — client thấy skill của host *(M–L — 1-2 phiên)*
- Hiện client chỉ thấy vị trí/HP, KHÔNG thấy hiệu ứng skill (host-local).
- Thêm kênh event: host phát `NET_EVT_CAST {agentId, skillIndex, aimPoint}`
  (reliable) khi CastSkill thành công; client gọi CastSkill local ở chế độ
  "visual-only" (combat registry đã tách — client không tick Combat_Update
  nên projectile chỉ là VFX, damage vẫn do host resolve qua snapshot HP).
- Cùng cơ chế cho minion explosion + boss phase-shift (2 event nhiều tính đọc trận nhất).
- **DoD:** client nhìn thấy fireball của host bay đúng hướng; HP chỉ đổi theo snapshot.

### A3. Snapshot interpolation *(S–M — 1 phiên)*
- 20Hz snapshot hiện snap thẳng → giật khi ping cao. Client giữ 2 snapshot
  gần nhất, lerp position với buffer ~100ms (`Entity_NetSyncAgent` thêm
  đường mượt, KHÔNG prediction — giữ đơn giản).
- **DoD:** chạy 2 máy qua internet, chuyển động đối thủ mượt mắt thường.

### A4. Loadout sync + zone rule cho remote hero *(S — ghép chung phiên A3)*
- Client đổi skill bằng TAB → gửi loadout lên host (reliable, 4 byte slot);
  host `Entity_SetEquippedSkill` + `Entity_RecomputeElement` (Vô Hệ đúng).
- `HostApplyRemoteEdges` áp cooldown mult theo zone (hiện bỏ qua — ghi chú sẵn trong code).
- **DoD:** khách đổi sang bộ skill Hỏa, host thấy element đổi; đứng sông cooldown giảm.

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

Ranking server, trang phục, sự kiện mùa, PvP 2v2 (host migration — thiết kế đã
chốt "trận tự kết thúc khi host rời"), Firebase.

---

## Thứ tự đề xuất & quy trình

```
A1 → A2 → (A3+A4)  →  C1 → C2 → C3  →  B4 → B5...
        ↘ B1, B2, B3 (agent nội dung chạy song song từ sau A1)
D1 chen bất kỳ lúc nào có asset; D4 sau mỗi đợt nội dung.
```

Quy trình mỗi hạng mục (giữ nguyên nếp cũ):
1. Đọc section thiết kế liên quan + API doc của module đụng tới.
2. Sửa `.h` + API doc TRƯỚC khi đổi API (breaking change).
3. Static arrays, C99, meter-scale, không malloc; VFX mới → wire vfx_test.
4. Thêm/ mở rộng autotest cho DoD; build sạch; **14/14+ pass** rồi mới sang mục kế.
5. Cập nhật docs MỘT LẦN cuối đợt (không giữa chừng).
