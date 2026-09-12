# Wind & Vorticles System (Ghost of Tsushima Style)

Hệ thống khí động học và gió định hướng phỏng theo kiến trúc của **Ghost of Tsushima (Sucker Punch - Bill Rockenbeck, GDC 2021)**.

---

## 1. Triết Lý Thiết Kế

Thay vì giải phương trình vi phân Navier-Stokes tốn kém tài nguyên không cần thiết cho góc nhìn thế giới game, hệ thống sử dụng **mô hình xấp xỉ phân tầng**:
- **Cấp độ Vĩ mô (Macro Wind):** Vector gió nền tảng quét qua toàn bộ sàn đấu, được biến điệu bởi gradient noise 3D để tạo cảm giác gió rít từng đợt. CPU và GPU dùng cùng công thức trường nhiễu.
- **Xấp xỉ Khí động Địa hình (Terrain-Aware Lift):** CPU bake một tile 32x32 quanh vùng gameplay và GPU đọc cùng dữ liệu để lấy mẫu độ dốc phía trước dọc theo hướng gió. Mỗi mẫu có cờ validity riêng, vì vậy vùng ngoài mesh không bị hiểu nhầm thành mặt đất Y=0 hoặc một vách giả.
- **Cấp độ Vi mô (Vorticles):** Mảng phẳng tuyến tính $\le 256$ hạt gió vô hình mô hình hóa 4 dạng tác động khí quyển:
  1. `VORTICLE_LINEAR_GUST`: Luồng gió thẳng định hướng (vệt chém kiếm, đạn phi lướt).
  2. `VORTICLE_RADIAL_BLAST`: Sóng xung kích đẩy tỏa tròn (chưởng nổ, tiếp đất chấn động).
  3. `VORTICLE_VORTEX`: Vòng lốc xoáy quanh trục (đối lưu bốc nhiệt từ ngọn lửa, lốc xoáy).
  4. `VORTICLE_TURBULENCE`: Nhiễu hash-gradient 3D cục bộ (va chạm skill, vụ nổ).

---

## 2. API & Cách Sử Dụng

### Khởi tạo & Cập nhật
```c
#include "core/wind/wind_system.h"

// Trong Init()
Wind_Init();

// Mỗi frame trong Update()
Wind_Update(dt);

// Khi thoát
Wind_Unload();
```

### Emitters (Nguồn phát sinh gió)
```c
// 1. Khi nhân vật vung kiếm hoặc tung vệt chém kiếm khí
Wind_SpawnGust(bladePos, slashDir, 3.5f, 15.0f, 0.4f);

// 2. Khi chưởng nổ hoặc minion/boss giậm chân chấn động
Wind_SpawnRadialBlast(impactPoint, 6.0f, 25.0f, 0.6f);

// 3. Tại gốc ngọn lửa trại hoặc vùng cháy lớn (đối lưu nhiệt)
Wind_SpawnVortex(firePos, (Vector3){0, 1, 0}, 2.5f, 8.0f, 2.0f, 2.0f);
```

### Receivers (Đối tượng tiếp nhận gió)
```c
// Hạt môi trường hoặc tro than lấy vận tốc gió tại vị trí hạt:
Vector3 windVel = Wind_EvaluateVelocity(particlePos, GetTime());
particlePos = Vector3Add(particlePos, Vector3Scale(windVel, dt));
```

---

## 3. Đặc tả Kỹ thuật & Tối ưu Hóa
- **Bộ nhớ:** Mảng tĩnh cố định 256 phần tử (`sizeof(VorticleData) * 256` $\approx$ 12 KB).
- **Zero-Allocation:** Tuyệt đối không gọi `malloc`/`free` trong frame loop.
- **Cache L1 Friendly:** Mảng được dồn liên tục (`compact`) trong `Wind_Update(dt)`, đảm bảo CPU duyệt mảng tuần tự không gián đoạn.
- **CPU/GPU Parity:** Compute SSBO nhận đủ 256 Vorticle; không cắt ngầm xuống 16 nguồn.
- **Terrain Tile:** Lưới 32x32 được nội suy song tuyến tính trên cả CPU/GPU, rebake khi đổi map hoặc khi tâm gameplay dịch chuyển 8 m. Tile 48x48 m giữ bước mẫu xấp xỉ 1.55 m, gần với khoảng nhìn trước 1.5 m của terrain lift.
- **Versioned Upload:** Terrain SSBO chỉ upload lại khi tile được rebake; không copy 8 KiB mỗi frame.
