# Hệ thống ForceField — Hướng dẫn sử dụng

> **Tóm gọn:** ForceField là cách duy nhất để điều khiển chuyển động của particle ngoài `velocity` ban đầu và `drag`. Mỗi particle gắn **một** ForceField, ForceField đó chứa **nhiều lớp lực** (gravity, noise, vortex, viscosity...) chồng lên nhau.

---

## 1. Khái niệm cốt lõi

```
ParticleConfig
├── velocity        → vận tốc ban đầu (bắn ra theo hướng nào)
├── drag            → giảm tốc tuyến tính mỗi frame (phanh đơn giản)
└── forceField ──→  ForceField
                    ├── ForceLayer 0   (ví dụ: GRAVITY_DIR ↓)
                    ├── ForceLayer 1   (ví dụ: NOISE_CURL)
                    └── ForceLayer 2   (ví dụ: VISCOSITY)
```

- **ForceField** là container tĩnh (`static` variable trong skill file). Nhiều particle có thể trỏ vào cùng một ForceField.
- Mỗi frame, `UpdateParticles()` tự động gọi `ForceField_Evaluate()` và `ForceField_GetViscosityDamping()` cho từng particle đang sống.
- Skill file **sở hữu** bộ nhớ ForceField. Particle system chỉ giữ con trỏ, không copy.

---

## 2. Các loại lực (ForceType)

### Additive acceleration — cộng vào velocity

| ForceType | Tác dụng | Trường quan trọng |
|---|---|---|
| `FORCE_GRAVITY_DIR` | Gia tốc cố định theo hướng bất kỳ. Trọng lực, lực đẩy lên. | `direction` (normalize sẵn), `strength` (m/s²) |
| `FORCE_GRAVITY_POINT` | Hút hoặc đẩy hạt về một điểm. `strength < 0` = đẩy ra. | `origin`, `strength`, `radius`, `falloff` |
| `FORCE_VORTEX` | Xoáy quanh một trục. Tạo lốc xoáy, tornado. | `origin` (điểm trên trục), `direction` (hướng trục), `strength`, `radius` |
| `FORCE_WIND` | Gió theo hướng + nhiễu Perlin làm gió rung tự nhiên. | `direction`, `strength`, `noiseScale`, `noiseSpeed` |
| `FORCE_NOISE_PERLIN` | Đẩy hạt theo Perlin noise. Tạo jitter, bụi, khói loạn. | `strength`, `noiseScale`, `noiseSpeed` |
| `FORCE_NOISE_CURL` | Curl noise — divergence-free. Swirling mượt, không dồn cụm. | `strength`, `noiseScale`, `noiseSpeed` |
| `FORCE_DRAG` | Lực cản tỉ lệ vận tốc (lực ngược chiều vel). | `strength` |

### Multiplicative damping — nhân vào velocity

| ForceType | Tác dụng | Trường quan trọng |
|---|---|---|
| `FORCE_VISCOSITY` | `vel *= exp(-strength * dt)`. Hạt đặc quánh dần. Dùng cho nước, chất lỏng. | `strength` (~1–10) |

> ⚠️ `FORCE_VISCOSITY` **không sinh acceleration** — chỉ có tác dụng qua `ForceField_GetViscosityDamping()`. Particle phải có `forceField != NULL` thì viscosity mới chạy.

---

## 3. Tham số của ForceLayer

```c
typedef struct {
    ForceType type;

    Vector3 origin;     // tâm vortex/hút — KHÔNG dùng cho GRAVITY_DIR, DRAG, VISCOSITY
    Vector3 direction;  // hướng lực / trục vortex (normalize sẵn)

    float strength;     // cường độ: m/s² cho accel, hằng số giảm chấn cho VISCOSITY
    float radius;       // 0.0 = vô cực; > 0 = chỉ trong sphere bán kính này
    float falloff;      // 0.0 = hằng số; 1.0 = tuyến tính; 2.0 = bình phương

    float noiseScale;   // tần số không gian: nhỏ = nhiễu to/chậm; lớn = nhiễu nhỏ/nhanh
    float noiseSpeed;   // tốc độ cuộn noise theo thời gian
} ForceLayer;
```

**Quy tắc radius + falloff:**
- `radius = 0` → lực đều lên mọi particle, không phụ thuộc vị trí
- `radius > 0, falloff = 0` → hằng số trong sphere, bằng 0 ngoài
- `radius > 0, falloff = 1` → mạnh nhất ở tâm, giảm tuyến tính về 0 ở biên
- `radius > 0, falloff = 2` → giảm bình phương (giống lực điện từ)

---

## 4. Quy trình tạo ForceField mới trong một skill

### Bước 1 — Khai báo static ở đầu file

```c
static ForceField s_myField;
```

### Bước 2 — Khởi tạo trong InitXxxSkill()

```c
void InitMySkill(...) {
    ForceField_Clear(&s_myField);  // luôn Clear trước

    ForceField_AddLayer(&s_myField, (ForceLayer){
        .type      = FORCE_GRAVITY_DIR,
        .direction = {0, -1, 0},
        .strength  = 500.0f,
    });

    ForceField_AddLayer(&s_myField, (ForceLayer){
        .type       = FORCE_NOISE_CURL,
        .strength   = 40.0f,
        .noiseScale = 0.015f,
        .noiseSpeed = 0.6f,
    });
}
```

### Bước 3 — Gắn vào particle khi spawn

```c
ParticleConfig cfg = {0};
cfg.position     = spawnPos;
cfg.velocity     = vel;
cfg.drag         = 2.0f;
cfg.radius       = 5.0f;
cfg.lifetime     = 1.0f;
cfg.colorStart   = RED;
cfg.colorEnd     = (Color){0, 0, 0, 0};
cfg.physicsFlags = P_PHYSICS_DRAG;
cfg.forceField   = &s_myField;   // ← gắn ở đây
SpawnParticle(cfg);
```

### Bước 4 (tuỳ chọn) — Vortex origin theo emitter di chuyển

```c
// Trong UpdateXxxSkill(), trước khi spawn:
s_myField.layers[0].origin = emitters[e].currentPos;
```

---

## 5. Công thức cho các hiệu ứng phổ biến

### 🔥 Lửa bốc lên, uốn lượn
```c
{ .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = 200.0f }
{ .type = FORCE_NOISE_CURL,  .strength = 80.0f, .noiseScale = 0.02f, .noiseSpeed = 1.2f }
```

### 🌪️ Lốc xoáy (Tornado)
```c
{ .type = FORCE_VORTEX,      .origin = center, .direction = {0,1,0}, .strength = 300.0f, .radius = 150.0f }
{ .type = FORCE_NOISE_CURL,  .strength = 30.0f, .noiseScale = 0.01f, .noiseSpeed = 0.8f }
```

### 💧 Nước văng (Splash)
```c
{ .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 845.0f }
{ .type = FORCE_NOISE_PERLIN,.strength = 20.0f, .noiseScale = 0.008f, .noiseSpeed = 0.3f }
{ .type = FORCE_VISCOSITY,   .strength = 4.8f }   // giọt nước gom cụm, đặc quánh
```

### ⚡ Tia điện / Spark giật
```c
{ .type = FORCE_NOISE_PERLIN,.strength = 120.0f, .noiseScale = 0.05f, .noiseSpeed = 3.0f }
```

### 🌿 Lá bay lướt
```c
{ .type = FORCE_NOISE_CURL,  .strength = 25.0f, .noiseScale = 0.012f, .noiseSpeed = 0.4f }
{ .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = 65.0f }
```

---

## 6. Giới hạn & lưu ý

| | Giá trị | Ghi chú |
|---|---|---|
| Layer tối đa / ForceField | **8** | Đủ cho hầu hết hiệu ứng |
| Số ForceField | Không giới hạn | Mỗi skill tự quản lý |
| Nhiều particle dùng chung 1 field | ✅ | Trỏ cùng con trỏ |
| ForceField trên GPU (compute shader) | ❌ | GPU chỉ dùng drag + velocity |
| FORCE_VISCOSITY cần forceField != NULL | ✅ | Không có field = không có viscosity |

**Curl vs Perlin:**
- **Perlin** — rẻ hơn, nhưng có divergence (hạt có thể dồn cụm)
- **Curl** — đắt hơn (6 sample Perlin), divergence-free, xoáy tự nhiên hơn, hạt không dồn

---

## 7. Noise primitives — dùng độc lập

```c
float   n    = Noise_Perlin3D(x, y, z);          // [-1, 1]
float   v    = Noise_Value3D(x, y, z);            // [0, 1], rẻ hơn ~2x
Vector3 curl = Noise_Curl3D(x, y, z, scale);      // divergence-free Vector3
```

---

## 8. Sơ đồ luồng mỗi frame

```
UpdateParticles(dt)
│
└─ for each particle i:
    ├─ lifetime -= dt  →  chết thì xoá khỏi mảng
    ├─ drag:  vel *= (1 - drag*dt)
    │
    └─ if forceField != NULL:
        ├─ acc  = ForceField_Evaluate(ff, pos, vel, time)
        │         └─ tổng tất cả layer additive (gravity/noise/vortex/wind/drag)
        ├─ vel += acc * dt
        │
        ├─ damp = ForceField_GetViscosityDamping(ff, dt)
        │         └─ tích exp(-s*dt) của tất cả VISCOSITY layer
        └─ vel *= damp
    
    pos += vel * dt
```
