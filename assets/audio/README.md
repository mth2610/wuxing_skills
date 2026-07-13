# Âm thanh — bỏ file vào đây là nghe

Khung âm thanh (`core/audio_system.h/.c`) đã wire sẵn vào game. Mỗi sự kiện
map tới MỘT đường dẫn cố định bên dưới — **thả đúng file đúng tên là tự
phát**, không cần sửa code. Thiếu file = im lặng (game vẫn chạy bình thường).

Đổi âm lượng tổng: `Audio_SetMasterVolume(0..1)` (mặc định 0.8).

## SFX — `assets/audio/sfx/` (khuyến nghị `.wav`, one-shot ngắn)

| File | Khi nào phát |
|---|---|
| `ui_click.wav`     | Bấm nút menu / sảnh (chưa wire, để dành) |
| `cast_water.wav`   | Tung chiêu hệ Thủy |
| `cast_wood.wav`    | Tung chiêu hệ Mộc |
| `cast_fire.wav`    | Tung chiêu hệ Hỏa |
| `cast_earth.wav`   | Tung chiêu hệ Thổ |
| `cast_metal.wav`   | Tung chiêu hệ Kim |
| `cast_taiji.wav`   | Tung Phong / Lôi (Thái Cực) |
| `melee_hit.wav`    | Đấm / đá / chưởng |
| `skill_hit.wav`    | Chiêu trúng đối thủ |
| `clash.wav`        | Đấu Pháp — hai chiêu khắc chế va nhau |
| `explosion.wav`    | Minion tự nổ |
| `taiji_enter.wav`  | Vào Cảnh Giới Thái Cực (sting) |
| `ringout.wav`      | Rơi khỏi đài (chưa wire, để dành) |
| `victory.wav`      | Thắng trận |
| `defeat.wav`       | Thua trận |

- Cast/hit tự **random cao độ nhẹ (±6%)** mỗi lần phát nên không bị máy móc.
- SFX trong đấu trường phát **theo vị trí**: nhỏ dần theo khoảng cách tới
  người chơi (tắt hẳn quá 26 m) + pan trái/phải. UI/stinger phát full 2D.

## Nhạc nền — `assets/audio/music/` (khuyến nghị `.ogg`, loop được)

| File | Khi nào |
|---|---|
| `arena_night.ogg` | Nền đêm, loop suốt khi trong trận (SCREEN_GAME) |

## Ghi chú (giới hạn hiện tại, tinh chỉnh sau)

- Cùng một SFX bắn dồn dập (8 người spam) hiện dùng `PlaySound` đơn — tiếng
  sau cắt tiếng trước. Nếu cần chồng tiếng, sau này thêm pool `LoadSoundAlias`.
- Client trong trận online mới nghe **cast** (qua mirror) + nhạc nền; tiếng
  **hit/clash/explosion** hiện chỉ phát ở máy chủ (nơi combat resolve). Sẽ
  đồng bộ về client trong đợt hoàn thiện net sau.
- File mẫu miễn phn: freesound.org, Kenney (kenney.nl/assets → audio),
  sonniss.com (GDC bundle). Xuất WAV 44.1kHz mono cho SFX.
