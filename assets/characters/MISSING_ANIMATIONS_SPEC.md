# Đặc Tả Kỹ Thuật & Mỹ Thuật Các Animation Cần Bổ Sung (`humanoid_anims.glb`)

Tài liệu này mô tả chi tiết các animation còn thiếu để thay thế toàn bộ clip dùng tạm trong [player_anims.json](file:///Users/mth2610/Desktop/c_games/wuxing_skills/assets/characters/player_anims.json) và 21 chiêu Võ Học (`TCDK_*`, `TTQP_*`, `BTBC_*`), theo phong cách kiếm hiệp / võ hiệp (*Cửu Âm Chân Kinh*, *Where Winds Meet*).

---

## 1. Quy Chuẩn Kỹ Thuật Chung (Rig & Export Spec)

| Thông số | Quy chuẩn bắt buộc | Ghi chú tích hợp engine |
|---|---|---|
| **Skeleton / Rig** | Khớp 100% bone hierarchy `mixamorig` của [player.glb](file:///Users/mth2610/Desktop/c_games/wuxing_skills/assets/characters/player.glb) | Nạp chung vào [humanoid_anims.glb](file:///Users/mth2610/Desktop/c_games/wuxing_skills/assets/characters/animations/humanoid_anims.glb) |
| **Frame Rate & Định dạng** | **60 fps**, `.glb` (NLA Action đặt đúng **Tên clip chuẩn**) | `CharacterModel_FindAnimIndex` tìm theo tên Action chính xác |
| **Root Motion** | **In-Place** (khóa dịch chuyển X/Z trên `Hips`/Root, giữ chuyển động Y tự nhiên) | Lướt tiến (`lunge`), knockback và vận tốc chạy do [entities.c](file:///Users/mth2610/Desktop/c_games/wuxing_skills/entities/entities.c) xử lý |
| **Socket Vũ Khí** | Kiếm gắn tại xương bàn tay phải (`RightHand` / `mixamorig:RightHand`) | Mũi kiếm hướng theo trục dọc thanh kiếm để `ModularInstance_GetBladeKinematics` bám đúng đỉnh VFX |
| **Thủ Thế Tay Trái (Kiếm Quyết)** | Ngón trỏ + ngón giữa duỗi thẳng, ngón cái đè ngón áp út & út | Tạo dáng kiếm hiệp đặc trưng cho toàn bộ clip `sword_*` / `wuxia_sword_*` |
| **Blend Cross-Fade** | Giữ tư thế đầu/cuối tự nhiên (không giật trục Hips 180°) | Engine tự động slerp `0.12s–0.18s` khi chuyển slot |

---

## 2. Nhóm P0 — Cấp Bách (Thay Thế Clip Dùng Tạm Sai Ngữ Cảnh)

### 2.1. `sword_idle_wuxia`
- **Loại & Thời lượng:** `Loop` — **2.00s (120 frames @60fps)**
- **Thay thế cho:** `withdrawing_sword` đang bị lặp ở slot `sword_idle`
- **Tư thế & Chuyển động:**
  - Chân trước (trái) bước nhẹ nửa bàn chân, gối hơi chùng giữ trọng tâm thấp; chân sau mở góc ~35°.
  - Thân trên hơi xoay nghiêng 20° sang phải để thu hẹp diện tích trúng đòn.
  - Tay phải cầm kiếm thả tự nhiên chếch xuống và ra sau góc ~35° so với mặt đất, cổ tay hơi ngửa khoe sống kiếm.
  - Tay trái giơ ngang ngực/thượng đan điền kết **Kiếm Quyết** hướng mũi ngón tay về phía trước.
  - Nhịp thở: Ngực và vai nhấp nhô rất nhẹ theo chu kỳ 2 giây, mũi kiếm khẽ đung đưa ~2–3 cm.
- **Liên kết code:** Slot `CHAR_ANIM_SWORD_IDLE` trong [player_anims.json](file:///Users/mth2610/Desktop/c_games/wuxing_skills/assets/characters/player_anims.json).

### 2.2. `sword_run_wuxia`
- **Loại & Thời lượng:** `Loop` — **0.70s (42 frames @60fps)**
- **Thay thế cho:** `running` tay không (hiện làm kiếm xuyên vào đùi/bụng)
- **Tư thế & Chuyển động:**
  - Thân trên đổ về trước ~22°, bước chạy sải dài nhẹ nhàng kiểu khinh công.
  - Tay phải duỗi chếch ra sau hông phải, giữ lưỡi kiếm xuôi thẳng về phía sau (tuyệt đối không vung tay phải ra trước).
  - Tay trái đánh nhịp nhẹ phía trước ngực/sườn trái để giữ thăng bằng.
  - Chu kỳ chân đối xứng trái–phải mượt tại frame 0 và frame 42.
- **Liên kết code:** Slot mới `CHAR_ANIM_SWORD_RUN` trong [game_screen.c](file:///Users/mth2610/Desktop/c_games/wuxing_skills/game/game_screen.c).

### 2.3. `sword_parry_guard`
- **Loại & Thời lượng:** `One-Shot + Hold` — **0.45s (27 frames @60fps)**
- **Thay thế cho:** `sword_and_shield_casting` ở slot `sword_parry` / `sword_block`
- **Keyframes:**
  - **0–6f (0.00s–0.10s):** Thu chân phải về nửa bước, hạ trọng tâm, đưa tay phải nâng kiếm dựng chéo trước mặt/ngực (góc ~60° so với mặt đất), tay trái đặt sát cổ tay phải hoặc chạm nhẹ sống kiếm trợ lực.
  - **6–27f (0.10s–0.45s):** Giữ vững tư thế thủ kiếm (micro-tension rung nhẹ vai), sẵn sàng chuyển sang phản kiếm hoặc thả về `sword_idle_wuxia`.
- **Liên kết code:** Nhấn `F` (`Entity_StartParry`) khi đang ở `COMBAT_STANCE_SWORD`.

### 2.4. `sword_parry_deflect`
- **Loại & Thời lượng:** `One-Shot` — **0.35s (21 frames @60fps)**
- **Mục đích:** Phát khi đỡ trúng đòn cận chiến trong cửa sổ Perfect Deflect (`0.22s`) hoặc gạt đạn (`Combat_DeflectProjectilesInRadius`).
- **Keyframes:**
  - **0–4f (0.00s–0.07s):** Từ thế `sword_parry_guard`, bật mạnh cổ tay và vai phải hất lưỡi kiếm chéo lên trên sang phải (tạo lực gạt lệch đòn đánh đối phương).
  - **4–12f (0.07s–0.20s — Peak):** Kiếm mở rộng hết biên độ sang phải trên, thân trên hơi vặn theo quán tính mở toang thế công phản kích.
  - **12–21f (0.20s–0.35s):** Thu kiếm nhanh về ngang hông sẵn sàng chém phản công.

### 2.5. `unarmed_parry_guard`
- **Loại & Thời lượng:** `One-Shot + Hold` — **0.45s (27 frames @60fps)**
- **Thay thế cho:** `taiji_cloud_hands` / `crouch_idle` ở slot `parry` / `block` tay không
- **Keyframes:**
  - **0–6f:** Hạ tấn trung bình, hai cẳng tay dựng chéo bắt chữ X (hoặc tay trái gạt ngang trước trán, tay phải hộ tâm trước ngực), bàn tay mở chưởng cứng cáp.
  - **6–27f:** Giữ thế thủ vững chắc.

### 2.6. `knockdown_back_fall`
- **Loại & Thời lượng:** `One-Shot` — **0.85s (51 frames @60fps)**
- **Thay thế cho:** `jumping_down` ở slot `knockdown`
- **Keyframes:**
  - **0–12f (Impact):** Ngực bật ngửa ra sau, hai tay vung mất thăng bằng, hai chân nhấc khỏi đất.
  - **12–34f (Ground Hit @30f):** Lưng và hông đập xuống mặt đất (Y=0), đầu hơi gập lên tránh va chạm, hai tay chống nhẹ xuống đất.
  - **34–51f (Settle):** Nằm sấp/ngửa ổn định để nối sang clip `getting_up` (#22 đã có).

### 2.7. `hit_react_light`
- **Loại & Thời lượng:** `One-Shot` — **0.28s (17 frames @60fps)**
- **Mục đích:** Bổ sung biến thể cho slot `hit` (hiện chỉ có `receive_stomach_uppercut`).
- **Keyframes:**
  - **0–5f:** Vai trái và đầu giật mạnh ra sau-phải như trúng đòn tạt ngang, chân trụ giữ nguyên không trượt.
  - **5–17f:** Xoay vai trả lại hướng nhìn đối thủ cực nhanh để không cản tầm nhìn người chơi.

### 2.8. `hit_guard_break`
- **Loại & Thời lượng:** `One-Shot` — **0.60s (36 frames @60fps)**
- **Mục đích:** Phát khi người chơi bị phá thủ bởi Beat 4 Finisher / Hư Chiêu (`BasicAttackHitEvent.guardBroken == true`).
- **Keyframes:**
  - **0–10f:** Hai tay đang thủ bị bật tung rộng sang hai bên, ngực ưỡn ngửa ra sau lộ sơ hở.
  - **10–26f:** Loạng choạng lùi nửa bước tại chỗ, đầu cúi xuống rồi lắc nhẹ.
  - **26–36f:** Gượng kéo tay về tư thế thủ.

### 2.9. `stun_dizzy_loop`
- **Loại & Thời lượng:** `Loop` — **1.20s (72 frames @60fps)**
- **Mục đích:** Phát khi bị `Entity_IsStunned` (ví dụ bị Perfect Deflect phản choáng `0.40s` hoặc trúng khống chế).
- **Tư thế & Chuyển động:** Đứng hơi khom lưng, hai gối lỏng, tay trái đưa lên ôm nhẹ thái dương/trán, thân trên lắc lư vòng tròn chậm kiểu choáng váng.

---

## 3. Nhóm P1 — Bộ Đòn Đánh Thường 4-Beat & Quyền Chưởng Võ Hiệp

### 3.1. `wuxia_sword_slash_1` (Beat 1 — Hoành Tảo)
- **Loại & Thời lượng:** `One-Shot` — **0.46s (28 frames @60fps)** | **Hit-Apex:** **0.12s (frame 7)**
- **Keyframes:**
  - **0–3f (0.00s–0.05s):** Vặn hông sang phải, kéo mũi kiếm về ngang hông phải chuẩn bị vung.
  - **3–9f (0.05s–0.15s, Apex @7f):** Bước nhẹ chân trái, vung kiếm chém ngang từ **phải sang trái** ngang tầm ngực (góc nghiêng nhẹ 10°), lưỡi kiếm mở hết biên độ phía trước ở frame 7–8 để khớp `VFX_MetalSlash`.
  - **9–28f (0.15s–0.46s):** Hãm kiếm bên trái, tay trái kết Kiếm Quyết giữ thăng bằng sau lưng; từ frame 16 có thể ngắt sang Beat 2 (`cancelWindowTimer`).

### 3.2. `wuxia_sword_slash_2` (Beat 2 — Nghịch Liêu)
- **Loại & Thời lượng:** `One-Shot` — **0.50s (30 frames @60fps)** | **Hit-Apex:** **0.14s (frame 8)**
- **Keyframes:**
  - **0–4f (0.00s–0.07s):** Tiếp nối từ cuối Beat 1 bên trái, hạ thấp mũi kiếm xuống ngang đầu gối trái, cổ tay xoay ngửa lưỡi kiếm.
  - **4–11f (0.07s–0.18s, Apex @8f):** Đẩy hông lên, chém vát chéo từ **trái-dưới lên phải-trên** (góc 45°), tạo đường vòng cung ngược sáng rực.
  - **11–30f (0.18s–0.50s):** Kiếm vút qua vai phải rồi thu nhẹ về thế chém xuống.

### 3.3. `wuxia_sword_thrust_3` (Beat 3 — Trường Hồng Quán Nhật)
- **Loại & Thời lượng:** `One-Shot` — **0.54s (32 frames @60fps)** | **Hit-Apex:** **0.16s (frame 10)**
- **Keyframes:**
  - **0–5f (0.00s–0.08s):** Co chân phải lùi nhẹ lấy đà, thu chuôi kiếm sát bên sườn phải, mũi kiếm hướng thẳng tâm mục tiêu.
  - **5–12f (0.08s–0.20s, Apex @10f):** Lao vai phải về trước, duỗi thẳng tắp cánh tay phải đâm mũi kiếm xuyên thẳng phía trước, tay trái duỗi ngược ra sau tạo đường thẳng cân bằng (đồng bộ `VFX_MetalLance`).
  - **12–32f (0.20s–0.54s):** Rút kiếm thu về hông chuẩn bị xoay người Beat 4.

### 3.4. `wuxia_sword_finisher_4` (Beat 4 — Toàn Phong Trảm / Hư Chiêu Phá Thủ)
- **Loại & Thời lượng:** `One-Shot` — **0.66s (40 frames @60fps)** | **Hit-Apex:** **0.22s (frame 13)**
- **Keyframes:**
  - **0–7f (0.00s–0.12s):** Xoay gót chân trái quay lưng 180° tích lực toàn thân, giơ kiếm cao qua đầu.
  - **7–16f (0.12s–0.27s, Apex @13f):** Hoàn tất xoay 360°, vung kiếm chém vòng cung 270° cực rộng từ phải sang trái ngang tầm vai, tạo quầng trăng khuyết `VFX_MetalCrescentWave` và hiệu ứng phá thủ.
  - **16–40f (0.27s–0.66s):** Hạ kiếm dừng ở thế đứng tấn vững chãi, thu kiếm về `sword_idle_wuxia`.

### 3.5. `baji_shoulder_ram` (Thiết Sơn Kháo — `btbc_thiet_son_khao`)
- **Loại & Thời lượng:** `One-Shot` — **0.42s (25 frames @60fps)** | **Hit-Apex:** **0.15s (frame 9)**
- **Thay thế cho:** `crouch_idle` + `mutant_jump_attack`
- **Keyframes:**
  - **0–6f:** Hạ thấp trọng tâm tấn Mã Bộ, xoay vai phải ra sau, hai tay nắm chặt thu sát hông.
  - **6–12f (Apex @9f):** Đạp mạnh chân sau húc thẳng **vai phải + cùi chỏ phải** về phía trước như ngọn núi va đập, cằm thu sát vai bảo vệ cổ.
  - **12–25f:** Hãm lực húc, giữ chân tấn vững và thu vai về.

### 3.6. `palm_twin_push` (Song Chưởng Thôi Sơn — `btbc_bang_son_toai_chuong`)
- **Loại & Thời lượng:** `One-Shot` — **0.50s (30 frames @60fps)** | **Hit-Apex:** **0.18s (frame 11)**
- **Keyframes:**
  - **0–7f:** Thu hai lòng bàn tay về ngang sườn, khuỷu tay ép sát lưng, hít hơi ưỡn nhẹ ngực.
  - **7–14f (Apex @11f):** Đẩy bật hai lòng bàn tay (gốc chưởng hướng ra ngoài, ngón tay dựng đứng) song song về phía trước cùng nhịp bước tấn.
  - **14–30f:** Rung nhẹ đầu ngón tay xả kình lực rồi thu về.

### 3.7. `palm_combo_rapid` (Liên Hoàn Chưởng — `ttqp_pha_lang_quyen`, Beat 4 Unarmed)
- **Loại & Thời lượng:** `One-Shot` — **0.55s (33 frames @60fps)** | **Hit-Apex:** **0.16s & 0.28s (frame 10 & 17)**
- **Keyframes:**
  - **0–12f:** Phóng chưởng trái thẳng ngực rồi thu ngay về hông.
  - **12–22f:** Xoay hông tung chưởng phải mạnh hơn nối tiếp liên hoàn không ngừng nghỉ.
  - **22–33f:** Thu tay về thế thủ quyền.

### 3.8. `martial_ground_stomp` (Chấn Địa Cước — `btbc_dia_liet_chan_ba`, `earth_pillar_prison`)
- **Loại & Thời lượng:** `One-Shot` — **0.55s (33 frames @60fps)** | **Hit-Apex:** **0.22s (frame 13)**
- **Thay thế cho:** `hell_slammer_a` (clip quái vật đập tay)
- **Keyframes:**
  - **0–9f:** Nhấc cao đầu gối phải lên ngang thắt lưng, hai tay giơ ngang vai giữ khí thế.
  - **9–15f (Apex @13f):** Dậm mạnh gót chân phải xuống đất (khớp đúng thời điểm nổ vòng địa chấn VFX), đồng thời hai nắm đấm kéo giật mạnh xuống ngang hông.
  - **15–33f:** Giữ dáng tấn uy nghiêm rồi đứng thẳng lại.

---

## 4. Nhóm P2 — Chiêu Thức Đặc Trưng (Signature Võ Học)

### 4.1. `iaido_sheath_hold` (`tcdk_nhat_kiem_doan_thien` — Pha tụ kình)
- **Loại & Thời lượng:** `Loop / Hold` — **0.60s (36 frames @60fps)**
- **Thay thế cho:** `sheath_sword_1` (hiện đang cất kiếm ra sau lưng)
- **Tư thế:** Chân trái bước trước hơi khuỵu; tay trái giữ vỏ kiếm tưởng tượng ngang hông trái; tay phải đặt lên chuôi kiếm hơi kéo hé lưỡi kiếm ~5 cm, đầu hơi cúi, ánh mắt khóa chặt mục tiêu.

### 4.2. `iaido_draw_slash` (`tcdk_nhat_kiem_doan_thien` — Pha bạt kiếm)
- **Loại & Thời lượng:** `One-Shot` — **0.45s (27 frames @60fps)** | **Hit-Apex:** **0.08s (frame 5)**
- **Keyframes:**
  - **0–6f (Apex @5f):** Từ tư thế `iaido_sheath_hold`, xoay bật hông rút kiếm chém ngang cực nhanh từ trái sang phải (1 đường thẳng sắc lẹm), đồng bộ `VFX_MetalSeverLine_Spawn`.
  - **6–18f:** Dừng kiếm chếch sang phải, thân giữ thăng bằng thấp.
  - **18–27f:** Vẩy nhẹ giọt sương/kiếm khí trên lưỡi kiếm rồi thu về thế thủ.

### 4.3. `sword_finger_command` (`tcdk_chan_am_kiem_tran` — Ngự kiếm)
- **Loại & Thời lượng:** `One-Shot` — **0.50s (30 frames @60fps)** | **Peak:** **0.18s (frame 11)**
- **Keyframes:**
  - **0–8f:** Tay phải cầm kiếm dựng dọc bên sườn; tay trái kết Kiếm Quyết thu về trước ngực.
  - **8–18f (Peak @11f):** Đưa thẳng cánh tay trái chỉ Kiếm Quyết về phía trước (hoặc vạch vòng tròn chỉ huy phi kiếm hạ xuống trận pháp).
  - **18–30f:** Thu tay về hông.

### 4.4. `sword_whirlwind_spin` (`tcdk_canh_kim_kiem_vuc`, `metal_blade_storm`)
- **Loại & Thời lượng:** `One-Shot` — **0.60s (36 frames @60fps)** | **Peak:** **0.18s (frame 11)**
- **Keyframes:**
  - **0–6f:** Khuỵu gối lấy đà, mở kiếm sang trái.
  - **6–22f:** Xoay tròn cơ thể 360° liên tục trên mũi chân, kiếm duỗi ngang tầm thắt lưng tạo vòng tròn kiếm khí quanh người.
  - **22–36f:** Hãm vòng xoay, cắm nhẹ mũi kiếm chếch xuống đất.

### 4.5. `grappling_pull_palm` (`ttqp_nghich_luu_cam_na` — Cầm Nã kéo giật)
- **Loại & Thời lượng:** `One-Shot` — **0.45s (27 frames @60fps)** | **Peak:** **0.18s (frame 11)**
- **Keyframes:**
  - **0–8f:** Vươn dài tay phải ra trước, các ngón tay mở rộng thành trảo (bàn tay hổ trảo).
  - **8–16f (Peak @11f):** Nắm chặt bàn tay và kéo giật mạnh khuỷu tay về sát sườn phải đồng thời xoay hông tạo lực hút xoáy nước kéo địch lại gần.
  - **16–27f:** Chuyển sang thế đánh bồi.

### 4.6. `taiji_deflect_circle` (`ttqp_thuy_kinh_quyen_thuan` — Vẽ vòng Thái Cực)
- **Loại & Thời lượng:** `One-Shot` — **0.55s (33 frames @60fps)** | **Peak:** **0.20s (frame 12)**
- **Keyframes:**
  - **0–18f:** Hai bàn tay mở mềm mại, tay phải vẽ nửa vòng cung phía trên sang phải, tay trái vẽ nửa vòng cung phía dưới sang trái tạo thành vòng tròn Âm Dương liên hoàn trước ngực (đẩy bật phản lực Thủy Kính).
  - **18–33f:** Thu hai tay về đan điền.

### 4.7. `qi_armor_burst` (`btbc_ban_thach_kim_cang_giap` — Vận công hộ thể)
- **Loại & Thời lượng:** `One-Shot` — **0.55s (33 frames @60fps)** | **Peak:** **0.22s (frame 13)**
- **Thay thế cho:** `mutant_roaring` (dáng quái vật gầm)
- **Keyframes:**
  - **0–10f:** Đứng tấn Mã Bộ vững vàng, hai nắm đấm đặt ngửa ép sát bên hông, vai hạ, hơi cúi cằm tụ nội lực.
  - **10–20f (Peak @13f):** Thẳng lưng, hai nắm đấm xoay úp đẩy mạnh sang hai bên hông, ngực mở rộng bộc phát sóng giáp Thổ hộ thể xung quanh.
  - **20–33f:** Thu tay về tư thế sẵn sàng chiến đấu.

---

## 5. Nhóm P3 — Khinh Công (Qinggong) & Môi Trường

### 5.1. `qinggong_glide_loop`
- **Loại & Thời lượng:** `Loop` — **1.00s (60 frames @60fps)**
- **Tư thế & Chuyển động:** Lướt nhẹ trên không kiểu *Where Winds Meet*: thân trên hơi ngả trước 15°, hai chân khép hờ hơi co nhẹ ở gối với mũi chân duỗi thẳng, hai cánh tay mở nhẹ ra sau như cánh chim nhạn, tà áo phất nhẹ.

### 5.2. `air_dash_spin`
- **Loại & Thời lượng:** `One-Shot` — **0.35s (21 frames @60fps)**
- **Tư thế & Chuyển động:** Lộn xoắn trục dọc cơ thể 360° trên không khi kích hoạt `dash` giữa không trung, tay ép gọn sát thân để tạo cảm giác bứt tốc.

### 5.3. `swim_forward_loop`
- **Loại & Thời lượng:** `Loop` — **1.10s (66 frames @60fps)**
- **Tư thế & Chuyển động:** Bơi sải/bơi ếch nhịp nhàng ở mặt nước (`Y` khớp mực nước sông map `song_quao`), bổ sung cùng clip `treading_water` (#39 đã có trong `humanoid_anims.glb`).

---

## 6. Bảng Đối Chiếu Đồng Bộ Hit-Apex Với Code (`entities.c`)

Để cảm giác đánh trúng đòn (**hitstop**, **damage**, **VFX_MetalSlash**) khớp 100% với đỉnh vung kiếm/chưởng của animation:

| Đòn đánh / Chiêu thức | Clip sử dụng | Hit-Apex trong `entities.c` | Frame đỉnh cần đạt (@60fps) |
|---|---|---|---|
| Basic Beat 1 (Sword) | `wuxia_sword_slash_1` | `0.12s` | Frame `7` |
| Basic Beat 2 (Sword) | `wuxia_sword_slash_2` | `0.14s` | Frame `8` |
| Basic Beat 3 (Sword) | `wuxia_sword_thrust_3` | `0.16s` | Frame `10` |
| Basic Beat 4 (Finisher) | `wuxia_sword_finisher_4` | `0.22s` | Frame `13` |
| Active Parry Deflect | `sword_parry_deflect` | `0.00s–0.22s` | Frame `4–12` |
| Thiết Sơn Kháo | `baji_shoulder_ram` | `0.15s` | Frame `9` |
| Nhất Kiếm Đoạn Thiên | `iaido_draw_slash` | `0.08s` | Frame `5` |
