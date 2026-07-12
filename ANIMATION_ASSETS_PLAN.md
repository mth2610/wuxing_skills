# KẾ HOẠCH ANIMATION ASSETS — Người chơi / Quái (Minion) / Boss

> File chuẩn bị asset cho anh Thang. Engine đã sẵn sàng nhận thêm clip —
> chỉ cần xuất đúng quy ước bên dưới rồi báo Claude wire slot mới.

## Quy ước kỹ thuật (bắt buộc — theo `character/character_model.c`)

- **Định dạng**: GLB, tất cả animation nằm CHUNG 1 file model
  (như `assets/characters/player.glb` hiện tại — dùng
  `scripts/combine_character_glb.py` để gộp nếu xuất rời).
- **Sample rate**: engine phát ở `ANIM_FPS = 60` — xuất bake 60fps
  (30fps vẫn chạy nhưng sẽ bị nhanh gấp đôi, tránh).
- **Đặt tên clip**: match theo **substring, không phân hoa thường**
  (vd slot "punch" nhận cả `Punching_Fast`). Tên gợi ý bên dưới đã an toàn.
- Clip one-shot (đòn đánh, né, chết) nên có pose đầu ≈ pose idle để đỡ giật
  khi cắt vào; engine tự co giãn tốc độ clip theo thời lượng gameplay
  (`CharacterModel_TriggerAttackTimed`), cứ làm đúng "nhịp đẹp" của clip.

## 1. NGƯỜI CHƠI (player.glb — đã có: idle, walking, running*, punching, kicking, palming, casting)

(*`running` đã nằm trong file nhưng engine chưa dùng — sẽ wire khi có dash/run thật.)

| Ưu tiên | Tên clip | Dùng cho | Ghi chú nhịp |
|---|---|---|---|
| ★★★ | `dashing` | Lướt khinh công (Shift) | 0.3–0.4s, người lao về trước, tà áo/khăn bay — engine phát kèm afterimage |
| ★★★ | `jumping` | Bật nhảy + bay (Space) | 3 đoạn trong 1 clip: đạp đất → lơ lửng (giữa clip loop được càng tốt) → tiếp đất |
| ★★★ | `hitreact` | Trúng đòn | 0.2–0.3s giật người, nhẹ thôi vì bị đánh liên tục |
| ★★★ | `dying` | Hết máu / rơi vực | 1–1.5s gục xuống; rơi vực sẽ dùng đoạn đầu |
| ★★ | `meditating` | Thiền định (G) | Ngồi kiết già, loop 2–3s, có nhịp thở |
| ★★ | `casting_heavy` | Chiêu lớn / Thái Cực Lôi | Vung 2 tay chậm & nặng hơn `casting` thường |
| ★★ | `victory` | Thắng trận (màn CHIEN THANG) | Loop ngắn, chắp tay/vuốt râu kiểu tu tiên |
| ★ | `strafe_left` / `strafe_right` | Đi ngang khi khóa mục tiêu | Chuẩn bị cho auto-target lock đi vòng |
| ★ | `taiji_enter` | Vào Cảnh Giới Thái Cực | 1s dang tay, khớp lúc màn hình chuyển trắng đen |

## 2. QUÁI / MINION (chưa có model — đang vẽ procedural cầu + vòng xoay)

Một model chung `minion.glb`, đổi màu theo ngũ hành bằng tint (engine tự làm):

| Ưu tiên | Tên clip | Dùng cho | Ghi chú |
|---|---|---|---|
| ★★★ | `idle` | Lơ lửng tại chỗ | Loop, phập phồng như tinh linh |
| ★★★ | `walk` | Lầm lũi bò về boss địch | Loop, tốc độ khớp 2 m/s |
| ★★★ | `windup` | 0.3s trước khi tự nổ | Phồng to + rung — tín hiệu cho người chơi né (No Tutorial) |
| ★★ | `dying` | Bị giết trước khi kịp nổ | Xẹp/tan 0.4s |
| ★ | `spawn` | Chui ra từ boss | 0.5s tụ hình từ khói |

Kiểu dáng gợi ý: tinh linh nhỏ đầu to không chân (bay là đỡ phải làm chân),
đúng khí chất "đèn lồng ma trơi" ban đêm của art direction.

## 3. BOSS HẮC DIỆN TÔN GIẢ (chưa có model — đang vẽ procedural)

Nếu làm model `boss_hac_dien.glb` (1 model, đổi hệ bằng tint + VFX rune có sẵn):

| Ưu tiên | Tên clip | Dùng cho | Ghi chú |
|---|---|---|---|
| ★★★ | `idle` | Lơ lửng thở | Loop 3–4s, áo choàng/khí bay |
| ★★★ | `casting` | Bắn skill theo phase (3s/phát) | 0.8–1s vươn tay — cho người chơi thấy trước mà né |
| ★★★ | `phaseshift` | Chuyển phase / biến hệ | 1.5s gồng + bung năng lượng, khớp lúc rune đổi màu |
| ★★ | `hitreact` | Trúng đòn nặng / mất khiên | Ngắn 0.2s, boss không nên giật nhiều |
| ★★ | `summon` | Gọi bầy minion | 1s dang tay triệu hồi |
| ★★ | `dying` | Gục — màn CHIEN THANG | 2–3s tan rã hoành tráng, đáng công người chơi |
| ★ | `taiji_rage` | Dưới 30% máu vào Thái Cực | Loop idle dữ tợn hơn, dùng thay `idle` ở phase cuối |

Kiểu dáng gợi ý: nhân dạng cao gầy, mặt nạ đen trống (đúng tên Hắc Diện),
tay dài — phần thân dưới có thể là khói (đỡ rig chân, hợp levitate).

## Thứ tự làm đề xuất

1. Player: `dashing`, `jumping`, `hitreact`, `dying` — 4 clip này nâng cảm giác chiến đấu nhiều nhất.
2. Minion model + `idle/walk/windup` — quái hiện là quả cầu, thay sớm là arena sống hẳn.
3. Boss model + bộ ★★★ — làm cuối vì procedural hiện tại vẫn tạm đạt.

Xuất xong clip nào cứ đưa vào `assets/characters/` rồi nhắn — wire vào engine
(slot mới trong `character_model.h` + trigger đúng chỗ) là việc của Claude.
