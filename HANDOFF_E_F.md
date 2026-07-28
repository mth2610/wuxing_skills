# Handoff — Đợt E/F, phần còn lại (28/07/2026)

Dán nguyên file này (hoặc trỏ tới nó) cho session mới.

---

## Đọc gì — ĐÚNG những thứ này, không đọc thêm

`core/docs/PROGRESS.md` giờ **2459 dòng**; đọc cả file là lãng phí lớn nhất có
thể mắc ở đây. Thứ tự:

1. `core/CLAUDE.md` — luật của module (bắt buộc).
2. **File này**, hết.
3. `core/docs/PROGRESS.md` **chỉ dòng 2186 → cuối** (các mục PERF + soft
   particle + energy burst mới nhất). Phần trên đó là lịch sử đã đóng.
4. `ENGINE_LANDMINES.md` — **mục cuối cùng** (rlvk: sampler thứ hai). Bắt buộc
   trước khi động vào bất kỳ shader nào.
5. `core/docs/ELDEN_VFX_SPEC.md` — **chỉ §0.2 (dòng ~100-127) và đúng section
   của task đang làm**. Mỗi task tự chứa.

Không đọc: `scripts/flipbook/*` (trừ khi bake sheet mới — lúc đó đọc
`scripts/flipbook/README.md`, đã cập nhật đầy đủ), `build_cache/`, các mục
PROGRESS trước dòng 2186.

---

## Trạng thái

**Xong:** Part A (F1 lit particles, F2 smoke, F3 fire, F4 aura) · E1 post-FX ·
E2 VFX light · E3 `VFX_Sequence` · E5 batch 1 (4/4) · E4 3/4 flipbook
(`fire_puff_8x8`, `smoke_puff_8x8`, `dust_puff_4x4` — tất cả có mục trong
`assets/INDEX.md`) · E6 #6 `VFX_ComposeImpactPackage` + `VFX_ComposeEnergyBurst`.

**F0 (thanh lọc) — ĐÃ THỰC HIỆN 28/07.** Xoá 34 `.inl`, 6 thư mục nguyên tố,
`core/vfx_proc_ray.*`, cả hai thành phần spirit. Bộ sống sót đúng 11 cái:
SmokePuff, FlameVolume, CharacterAura, GlintSparkle, RuneCircle, ChargeConverge,
DissolveExit, SweepSlash, EnergyBurst, ImpactPackage, LightShaft — trên nền
VFX_Sequence / vfx_light / post-FX / particle / trail / decal.

**Skill hiện đang TRỌC, và đó là trạng thái cố ý.** Logic/timing/damage/clash
không đổi, chỉ phần vẽ bị gỡ. Chỗ nào có kế thừa thật thì đã thay
(`VFX_ComposeImpact` → `VFX_ComposeImpactPackage`, 8 chỗ); chỗ nào không có kế
thừa MỘT-PHÁT (cast, bolt, projectile trail, ice spike, orbital) thì để trống
kèm comment nêu lý do — thay bằng thứ CONTINUOUS bắn một phát chỉ ra một frame,
đọc thành giật, và che mất đúng cái mà E7 phải đo. Chi tiết: PROGRESS mục cuối.

**Còn lại, theo thứ tự đề xuất:**

| # | Việc | Ghi chú |
|---|---|---|
| ~~1~~ | ~~**E6 #5 `VFX_ComposeSweepSlash`**~~ | **XONG 28/07** — mask tự sinh (`SweepSlash_BuildMask`), chưa ai nhìn bằng mắt. `core/docs/PROGRESS.md` mục cuối |
| ~~2~~ | ~~**E6 #7 `VFX_ComposeLightShaft`**~~ | **XONG 28/07** — không có soft-particle depth fade (sampler thứ hai bị chặn), fade theo chiều dài tia thay thế |
| ~~3~~ | ~~**E6 #8 `VFX_ComposeSpiritSwarm`**~~ | **XONG 28/07** — E6 đóng hoàn toàn. Pool 6x24, archetype tự wire qua sync script |
| 4 | **E7 retrofit** | **chốt kiểm**: 3 skill mẫu, A/B với E0. Spec nói rõ đây là chỗ kế hoạch được chứng minh hoặc phải hoạch định lại |
| ~~5~~ | ~~E8 platform/perf~~ | **Phần làm được đã XONG 28/07** — tier gate (LOW/MED/HIGH) + `postfx_perf_log` + `gfx_tier_test`. Còn lại là việc của chủ project: rlvk visual tier, 60fps PC bật hết, A33. **GLES đã bị bỏ khỏi E8** (Android chạy rlvk từ 17/07) |

**Ba mục OPEN đã ghi trong PROGRESS:**

- **SpiritStream chưa ra "linh hồn"** — `VFX_ComposeSpiritStream` chạy tốt, không
  xấu, nhưng chủ project nói không giống linh hồn (chưa diễn đạt được vì sao).
  PROGRESS mục cuối liệt kê 5 giả thuyết đã xếp theo giá rẻ→đắt: màu (thử trước),
  rồi cho cả ĐOẠN thân biến mất/hiện lại, rồi chuyển động trôi thay vì đạn đạo,
  rồi lớp trong, cuối cùng là "sai nguyên thể" (cần hình có bóng dáng, không phải
  ống). Đừng bắt đầu lại từ con số 0.


- **Soft particle** — vết cắt hạt/nền vẫn còn. Nguyên nhân đã xác định:
  `particle_lit.fs` chỉ được có **một** sampler dưới rlvk. Hướng: tách shader
  biến thể, hoặc sửa binding trong rlvk (module `third_party/vulkan/`, agent
  khác). Máy móc phía C vẫn còn và đang trơ (`s_locSoftFade == -1`).
- **Hiệu năng ImpactPackage** — đã từ 4.70× xuống 1.91× chi phí so với burst
  rời, vẫn tụt khi bắn liên tục ở severity 1.0. Cần gạt: `severity01`
  (0.5 ⇒ ngang bench). Công tắc đo từng beat: `impact_light`,
  `impact_distort`, `impact_decal`, `impact_hitstop`.

---

## Landmine sẽ dẫm lại nếu không biết

1. **rlvk: sampler thứ hai làm mất binding của `texture0`** → mọi hạt thành ô
   vuông trắng phẳng. Shader vẫn báo biên dịch **thành công**. Cách phân biệt:
   đặt uniform của tính năng mới về 0 (sampler không bao giờ được đọc) — vẫn
   vuông = binding, chỉ vuông khi bật = toán lấy mẫu.
2. **Shader biên dịch hỏng KHÔNG trả về id 0.** raylib đưa lại shader mặc định,
   rlvk ghi lỗi rồi chạy tiếp. `id != 0` trả lời "có gì đó được bind", không
   trả lời "shader của tôi dịch được". Kiểm bằng một uniform mà shader chắc
   chắn khai báo có phân giải được không — mẫu ở `ParticleLighting_Begin`.
3. **`SpriteAnim` chạy theo tuổi TUYỆT ĐỐI, không có lệch pha.** Hạt sinh cùng
   frame giữ cùng khung suốt đời → đọc thành "một tấm ảnh di chuyển". Dùng
   `ParticleConfig.spriteAnimPhase`. **Pha ăn vào quỹ sheet**: đời dài nhất +
   pha lớn nhất phải nằm trong số khung, không thì sprite lao vào đuôi rỗng và
   biến mất trong khi alpha vẫn bảo còn hiện.
4. **`VFX_Sequence` nhân `a` không-gian của mọi beat với scale của sequence.**
   Ramp theo severity ở cả hai chỗ = nhân đôi (đã tốn một vòng: 1.69× scale =
   4.7× fill). Severity chỉ được sống ở **một** chỗ.
5. **Phát hạt phải là TỐC ĐỘ, không phải số lượng mỗi lần gọi.** Một
   `VFX_Compose*` được gọi mỗi frame mà spawn số cố định thì mật độ **đổi theo
   fps**. Suy tốc độ từ số hạt-sống mong muốn: `rate = live / tuổi_thọ_TB`, có
   bộ tích luỹ giữ phần lẻ.
6. **Không tự ý thêm camera shake.** Chỉ khi chủ project yêu cầu. Ngưỡng chặn
   *không phải* là sự cho phép.
7. **Sheet khói phải tự đổ bóng.** Mask phẳng không bao giờ đọc ra khối — chồng
   lên nhau thành các tấm bìa. Đo: spread giá trị p10..p90; 0.00 = hỏng,
   0.69 = đạt.

---

## Cách làm việc mà phiên này trả giá mới rút ra

- **Chi phí không nằm ở hệ hạt.** Đã hai lần đoán vào đó, không lần nào ăn.
  Burst chạy rời giữ 60 fps. Trước khi tối ưu, hãy **trừ**: cái gì nhanh, cái
  bao trùm nó chậm, khác nhau ở đâu.
- **Bật `particle_perf_log 1`** → `live / quads / batches / vfxLights / fps`.
  Mỗi số giết một giả thuyết khác nhau.
- **Chủ project không đọc log trừ khi được nhờ.** Khi một câu hỏi chỉ log mới
  trả lời được, hãy nhờ thẳng — dòng `GLSL compile failed` đã nằm đó suốt hai
  vòng debug bằng mắt.
- **Không tự chạy game được** (đã thu hẹp 28/07): `./build/wuxing` chết ở `FATAL:
  RLVK: instance creation failed`, NHƯNG test rlvk headless tạo instance và chạy
  20/20 bình thường — hỏng là **đường windowed/surface**, không phải Vulkan. Cái gì
  headless thì agent đo được; cái gì cần surface thì không. Mọi câu hỏi runtime phải
  đi qua log/tunable, hoặc trả lời bằng đọc code + số học.
- **Có hiệu ứng đang chạy tốt làm mẫu thì CHÉP SỐ của nó trước.** EnergyBurst
  mất một vòng vì tự nghĩ ra kích thước/tốc độ thay vì lấy của
  `vc_smoke_puff.inl`.
- **Tunable thay vì build lại** (`core/tuning.h`), đăng ký **lười** ở lần dùng
  đầu, không bao giờ từ `Init`.
- **Mọi VFX mới phải vào bench**: thêm entry tay vào
  `scripts/vfx_test_manifest.json` rồi chạy `python3 scripts/sync_vfx_test.py`.
- Trả lời **tiếng Việt**, ngắn, dẫn `path:line`.

## Lệnh

```bash
cmake --build build -j4          # build
./scripts/run_core_tests.sh      # 10/10 phải xanh trước khi báo xong
```
