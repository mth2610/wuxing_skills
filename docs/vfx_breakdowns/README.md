# Wuxing Skills - Unreal Engine Niagara VFX Re-engineering Guide
## Mục Lục & Cẩm Nang Tái Cấu Trúc Toàn Diện 50+ VFX Mẫu Sang C Raylib Engine

Thư mục này chứa toàn bộ các tài liệu kỹ thuật bóc tách chi tiết cấu trúc, logic chuyển động, bản đồ toán học vi phân và cách ánh xạ sang engine C Raylib của dự án cho hơn 50 VFX mẫu trích xuất từ Unreal Engine (`unreal-engine-starter-content-main`).

---

## Danh Mục Tài Liệu Chi Tiết

### 1. [01_fx_explosions.md](file:///Users/mth2610/Desktop/c_games/wuxing_skills/docs/vfx_breakdowns/01_fx_explosions.md) - Hệ Thống Nổ & Xung Kích Điện Ảnh
- **Bao gồm các asset**: `NS_Explosion`, `NS_Dirt_Explosion` (Large, Medium, Small), `NE_Core`, `NE_Explosion`, `NE_Debris`, `NE_DustExplosion`, `NE_GroundDust`, `NE_SparkDebris`, `NE_PostProcess`.
- **Toán học & Vật lý**:
  - Phân rã 6 tầng thị giác (Core Flash -> Fireball Roil -> Spark Debris -> Rock Shrapnel -> Ground Dust Ring -> Dissipating Smoke).
  - Phương trình cản khí động học phi tuyến: $\frac{d\mathbf{v}}{dt} = -D \|\mathbf{v}\| \mathbf{v}$.
  - Nhiễu dòng xoáy không phân kỳ 3D Curl Noise ($\nabla \times \mathbf{\Psi}$).
  - Động học va chạm nảy sàn ($e = 0.45$) và kéo dãn theo tốc độ (Velocity Stretch).
- **Mã nguồn C Engine**: Cấu hình hoàn chỉnh hàm `SpawnExplosionVFX()` dùng `ParticleConfig`, `SpriteAnim 8x8` và `ForceField`.

---

### 2. [02_fx_weapons_and_impacts.md](file:///Users/mth2610/Desktop/c_games/wuxing_skills/docs/vfx_breakdowns/02_fx_weapons_and_impacts.md) - Vũ Khí, Đầu Nòng & Va Chạm Đa Bề Mặt
- **Bao gồm các asset**: `NS_MuzzleFlash`, `NE_MuzzleFlash_Base`, `NE_MuzzleFlash_Smoke`, `NE_MuzzleFlash_Sparks_Base`, `NE_BulletShells`, `NS_BulletTracer`, `NS_RocketTrail`, `NS_Impact_Concrete`, `NS_Impact_Metal`, `NS_Impact_Wood`, `NS_Impact_Glass`.
- **Toán học & Vật lý**:
  - Cấu trúc lửa chữ thập trực giao 3D (`Cross-Billboard Alignment`).
  - Động học văng và lộn vòng tự do của vỏ đạn (`Tumbling shell physics`).
  - Đường đạn neon (`Stretched Capsule`) và đuôi khói tên lửa nở theo căn bậc hai ($r(t) \propto \sqrt{t}$).
  - Ma trận phản xạ góc nón theo pháp tuyến bề mặt ($\hat{\mathbf{n}}$) cho 4 loại chất liệu (Kim loại, Bê tông, Gỗ, Kính).
- **Mã nguồn C Engine**: Triển khai `SpawnMuzzleFlashVFX()` và `SpawnImpactVFX(surfaceType)`.

---

### 3. [03_fx_sparks_smoke_fire.md](file:///Users/mth2610/Desktop/c_games/wuxing_skills/docs/vfx_breakdowns/03_fx_sparks_smoke_fire.md) - Tia Lửa, Cột Khói & Lửa Môi Trường
- **Bao gồm các asset**: `NS_Spark_Burst`, `NS_Spark_Continuous`, `NS_Spark_Impact_Looping`, `NE_Sparks`, `NE_SecondarySparks`, `NS_Smoke_Plume`, `NS_Chimney_Smoke`, `NS_Fire`, `NE_Smoke`.
- **Toán học & Vật lý**:
  - Mô hình phát xạ tia lửa 2 cấp (Primary Sparks -> Secondary Bounce).
  - Quy luật cột đối lưu nhiệt Morton-Taylor-Turner cho khói ống khói bốc cao và giãn nở thể tích ($R(y) = R_0 + \alpha_{\text{entrain}} y$).
  - Trường lực xoáy trục thẳng đứng `FORCE_VORTEX` tạo cột lửa hình xoắn ốc (tornado flame).
- **Mã nguồn C Engine**: Triển khai `UpdateChimneySmokeEmitter()` và `EmitWeldingSparks()`.

---

### 4. [04_fx_player_pickups_ribbons.md](file:///Users/mth2610/Desktop/c_games/wuxing_skills/docs/vfx_breakdowns/04_fx_player_pickups_ribbons.md) - Người Chơi, Bước Chân, Vật Phẩm & Tia Sét
- **Bao gồm các asset**: `NS_Pickup_Spawn`, `NS_Pickup_Idle`, `NS_Pickup_Success`, `NS_Pickup_Timeout`, `NS_Footstep_Gravel`, `NS_Footstep_Bubbles`, `NS_Footstep_Fire`, `NS_Footstep_LW`, `NS_Player_Buff_Looping`, `NS_Player_Teleport_In/Out`, `NS_TeslaCoil`, `NS_Boundary_*`.
- **Toán học & Vật lý**:
  - Máy trạng thái thị giác 4 pha của vật phẩm tương tác.
  - Quỹ đạo hút tâm xoắn ốc kết hợp Point Attractor và Vortex.
  - Giải thuật chia đôi dịch chuyển điểm giữa đệ quy (Midpoint Displacement) tạo tia sét hồ quang Tesla Coil đa giác.
  - Tích hợp bước chân `AnimNotify_Footstep` in Decal và sinh hạt theo chất liệu nền.
- **Mã nguồn C Engine**: Triển khai `UpdatePickupIdleEffect()` và hàm tạo điểm sét `GenerateLightningPoints()`.

---

## Bộ Công Cụ Hỗ Trợ Đã Xây Dựng
1. `tools/niagara_deep_parser.py`: Trích xuất tự động siêu tốc cấu trúc của bất kỳ tệp Niagara `.uasset` nào.
2. `tools/extract_ue_textures.py`: Trích xuất texture/flipbook từ `.uasset` sang ảnh PNG.
3. `tools/flip_extracted_flipbooks.py`: Lật dọc trục Y (Y-up flip) chuẩn hóa chiều trọng lực cho mọi frame flipbook.
4. `tools/slice_flipbook.py`: Cắt atlas thành từng frame ảnh đơn lẻ.
