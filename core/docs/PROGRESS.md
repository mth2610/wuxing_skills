# PROGRESS — VFX contrast và strand trail

**Cập nhật:** 10/08/2026
**Trạng thái:** **ĐÃ SỬA** — nguyên nhân gốc đã tìm ra và xác nhận bằng ảnh render.
Nguyên nhân **không nằm ở rlvk**, không nằm ở shader, và không nằm ở màu.

## 1. Nguyên nhân gốc

`tuning.cfg` còn giữ `strandtrail_style = 1.0` từ một lần A/B trước đó. Đó không
phải là knob chỉnh giá trị — nó **thay nguyên hàng style** cho mọi strand trail
đang sống, mỗi frame, trong `StrandTrail_OnUpdate`:

| | ENERGY (được yêu cầu) | SMOKE (bị ép) |
|---|---|---|
| `hotWhiten` | 0.72 | **0.0** — không có lõi nóng để tô |
| tint | `glow` (nóng) | `body` (tối) |
| blend | additive | `BLEND_ALPHA` |
| sheet | `energy_wisp.png` | `smoke_strand.png` |

Nên "dải đỏ đặc, không có lõi vàng" chính là style SMOKE của material Fire, chạy
đúng như thiết kế. Mọi thay đổi shader ở phiên trước đều **đúng** nhưng không thể
nhìn thấy, vì nhánh đang sửa chạy bằng tham số của style kia.

**Bằng chứng.** `./build/wuxing --render-vfx 27` (bench fixture 27 =
`VFX_ComposeStrandTrail(&xf, VC_MAT_FIRE, 0, 2.0f, VFX_STRAND_ENERGY)`):

- với `strandtrail_style = 1.0` → dải đỏ đặc, log `style -> smoketrailfx`;
- với `strandtrail_style = -1.0` → **sợi vàng kim, lõi nóng rõ**, log
  `style -> energytrail`.

Không đổi một dòng shader nào giữa hai lần render đó.

## 2. Đã sửa gì

1. **`tuning.cfg`** — `strandtrail_style` về `-1.0`, kèm chú thích tiếng Việt nói
   rõ tại sao phải giữ ở `-1` khi không đang so sánh.
2. **`core/composition/common/vc_strand_trail.inl`** — khi override ép một style
   **khác** style được yêu cầu, log `LOG_WARNING` (log-on-change), nêu tên file,
   style bị ép **và** style được yêu cầu. Dòng `LOG_INFO` cũ chỉ nói ai thắng, nên
   đọc y hệt nhau dù override có bật hay không.
3. **Chú thích lỗi thời** ở hàng ENERGY ("trail chỉ chạy BODY, main.c không gọi
   emission") đã sửa — `main.c` gọi cả hai pass.
4. **Landmine** — `ENGINE_LANDMINES.md` §13 (cross-cutting) + một mục trong
   `core/docs/LANDMINES.md`.

## 3. Test đã sửa trong đợt này

- **`vfx_contrast_test` chưa từng BUILD được** trong harness (`core/vfx_contrast.h`
  include `raylib.h`, tier headless không có raylib). Báo cáo "PASS" ở phiên trước
  là từ một lần compile tay. Đã thêm `core/tests/stubs/raylib.h` (chỉ plain data,
  không bao giờ khai báo hàm raylib) + include path trong
  `scripts/run_core_tests.sh`, và test nay include thẳng `core/vfx_contrast.c`.
  **PASS thật.**
- **`trail_deform_test`** — assertion cũ đòi `VFX_ComposeSmokeTrail` biến mất khỏi
  public API. Việc xóa đó chưa bao giờ xảy ra (xem §4). Assertion nay chốt hợp
  đồng THỰC TẾ: bản thay thế tồn tại, và file cũ tự khai báo nó đã bị thay thế.
- **`swept_trail_test`** — assertion cũ đòi `#define SWEPT_ASSET_PATH
  "assets/textures/energy_flow.png"`. Literal đó bị bỏ có chủ đích: sheet nay lấy
  qua surface registry. Assertion nay chốt đúng nguồn đó.

`./scripts/run_core_tests.sh`: **43/47 suites PASS.** 4 suite còn đỏ
(`energy_burst_semantic_layers_test`, `tube_frame_test`,
`vfx_layered_field_contract_test`, `volume_trail_test`) đã đỏ từ trước đợt này,
không thuộc phạm vi strand trail — chưa đụng tới.

## 4. Quyết định còn treo (cần người dùng chốt)

### 4.1. `VFX_ComposeSmokeTrail` — xóa hay giữ?

Được đánh dấu xóa từ 03/08/2026, một test còn assert là nó đã biến mất, nhưng nó
vẫn còn: khai báo trong `visual_composer.h`, cài đặt trong `vc_smoke_trail.inl`
(485 dòng), và nối vào bench entry 25. Bản thay thế là
`VFX_ComposeStrandTrail(..., VFX_STRAND_SMOKE)`. Rủi ro là **hai nút bench trông
giống nhau** — đánh giá/tune nhầm cái không ship. Phiên này chỉ dán cảnh báo lên
đầu file cũ; xóa thật đụng cả `sandbox/`, nên chờ chốt.

### 4.2. Swept trail đang dựng sheet từ ảnh sai

`SweptTrail_BuildAssetSheet` (`vc_ribbon_trail.inl`) được migrate sang surface
registry, nhưng `VFX_SURFACE_ENERGY_RIBBON` trỏ tới `energy_wisp.png`
(512x512, sheet STRAND tiling, chủ sở hữu là strand trail), trong khi mọi hằng số
crop/rotate trong hàm đó được đo trên `energy_flow.png` (1792x896, vẽ nằm ngang,
nội dung chỉ ở 40% giữa chiều cao). `energy_flow.png` **chưa được đăng ký** trong
`assets/vfx_surface_profiles.json`.

Không ảnh hưởng hình ảnh đang ship: nhánh này là opt-in (`swept_sheet`, mặc định
0 = procedural). Cách sửa đúng là đăng ký `energy_flow.png` thành profile riêng —
việc đó cần khai báo channel grammar theo `assets/TEXTURE_PACKING.md`, tức là một
quyết định về asset, nên chưa tự làm. **Đừng chỉnh lại các hằng số crop để chiều
theo sheet sai.** Hiện đã ghi cảnh báo ngay tại chỗ trong code.

## 5. Ribbon trail bệt màu trên nền sáng — ĐÃ SỬA

Người dùng báo tiếp: ribbon trail (`VFX_ComposeRibbonTrail`, bench 19) vẫn bệt
màu trên nền sáng. Đây là lỗi **khác** lỗi §1 và đã tái hiện + đo được.

**Nguyên nhân.** `k_sweptLayers` viết cho trail ADDITIVE, nên `alphaMul`
(MAIN: 0.10 / 0.36 / 0.30) là **trọng số phát sáng** — chúng cộng lại thành ánh
sáng. `DrawLayeredRibbon`/`DrawLayeredTube` đưa thẳng con số đó vào pass BODY
(BLEND_ALPHA) làm **coverage**, nên body bị chặn ở 0.36. Compositor tính
`scene*(1-0.36) + bodyColor*0.36` → 64% nền sáng còn nguyên, màu của trail bị
pha loãng ngay từ đầu. `TrailMaterialConfig::bodyOpacity` chính là coverage
được viết riêng cho việc này, nhưng chỉ đường DEFORM đọc nó; đường layered cổ
điển bỏ qua hoàn toàn. Cộng thêm: `TrailLayer::whiten` cũng được áp trong pass
BODY, làm nhạt đúng cái lớp có nhiệm vụ giữ màu.

**Đo được** (peak chroma trên nền sáng, fixture 19):

| `swept_body` | chroma |
|---|---|
| 0.0 (hành vi cũ) | 0.31 |
| 0.55 | 0.40 |
| **1.0 (mặc định mới)** | **0.72** |
| nền đêm, trước khi sửa | 0.61 |

**Đã sửa.**
- `trail_system.c`: thêm `TrailLayerPassAlphaMul()` + `TrailLayerWhitensThisPass()`,
  cả hai đường layered (ribbon + tube) đi qua đó. Chỉ đổi hành vi khi trail
  additive CÓ đặt `bodyOpacity`, nên mọi trail cũ giữ nguyên.
- `vc_ribbon_trail.inl`: `s_sweptBodyOpacity = 1.0f` + tunable `swept_body`,
  đẩy lại mỗi frame (không bake lúc spawn).
- `bright_vfx_isolation_test`: thêm regression cho cả số học lẫn call-site.
- Landmine trong `core/docs/LANDMINES.md`.

1.0 **không phải** "đục hoàn toàn": chỉ layer 1 (layer mang sheet) vẽ trong pass
BODY, và coverage vẫn bị nhân bởi alpha mềm của sheet, taper bề rộng và đường
cong vòng đời.

## 6. Còn cần mắt người xác nhận

Smoke trail / smoke column sau khi gỡ coverage phi tuyến (`distortion.fs` đã trở
lại `float bodyCoverage = body.a;`) cần một lần nhìn lại. Và giá trị `swept_body`
là quyết định thẩm mỹ — sweep bằng `tuning.cfg` nếu 1.0 quá đặc cho gu của bạn.

**Cách tái hiện nền sáng khi cần lại:** clear nền thôi là chưa đủ (skybox vẽ đè —
phải bỏ luôn `MapManager_DrawActive`), và body/emission của VFX đi vào render
target riêng nên phép trộn thật sự nằm ở `distortion.fs`, không phải lúc draw.

## 7. Những hướng không được lặp lại

- **Không sửa shader trước khi chứng minh nhánh đó đang chạy.**
  `./build/wuxing --render-vfx <index>` render một fixture bench ra PNG trong vài
  giây và in kèm log chọn style. Nó trả lời "có phải effect mình đang sửa không"
  trước khi sửa, thay vì sau lần sửa thứ mười.
- Không tăng additive/HDR để chữa nền sáng; cách đó đẩy màu gần trắng hơn.
- Không dùng nonlinear alpha expansion toàn cục; nó làm lộ biên smoke, particle
  và decal.
- Không viết workaround riêng trong VFX composition nếu lỗi nằm ở core.
- Trước mọi A/B thị giác: đọc cả block `tuning.cfg` của effect đang xét. Knob
  **chỉnh giá trị** thì nêu hệ số trong báo cáo; knob **chọn biến thể** thì phải
  tự hét lên (nay đã có).

## 8. Đợt hợp nhất trail — ĐANG NẰM TRONG `stash@{0}`, KHÔNG ở trong cây

10/08/2026: đã thử gộp ribbon + strand thành một trail mô tả bằng dữ liệu
(`core/trails/trail_recipe.h` — file này vẫn còn vì untracked). Build sạch,
`TRAIL_PRESET_MAIN` render đẹp hơn trước, nhưng **`TRAIL_PRESET_ENERGY` render
ra rỗng**, nên đợt đó đã được `git stash` lại.

Kế hoạch đầy đủ: `~/.claude/plans/inherited-hugging-kettle.md`.
Lấy lại: `git stash pop` (hoặc `git stash show -p stash@{0}` để xem trước).

### 8.1. Trong stash có gì

- `trail_recipe.h` + `vc_trail.inl` (`k_trailPresets[]` 6 preset +
  `k_trailMotion[]` thay 5 switch theo kind).
- Xoá `vc_strand_trail.inl`, `vc_smoke_ribbon_trail.inl` (392 dòng, KHÔNG được
  include ở đâu — code chết, phát hiện này vẫn đúng), nhánh
  `SweptTrail_BuildAssetSheet` + `swept_sheet` (đóng mục 4.2), tầng alias
  `VFX_ComposeSweptTrail`.
- API mới `VFX_ComposeTrail` / `VFX_ComposeTrailEx` / `VFX_KillTrail`.
- `TrailRecipe_ToLegacyMaterial` — cầu tạm dịch recipe → uniform cũ ở ĐÚNG một
  chỗ, để không phải viết lại shader cùng lượt.

### 8.2. Lỗi phải sửa đầu tiên khi lấy lại

`TRAIL_PRESET_ENERGY` spawn đúng (log `style 4 ... width 0.45 m`) nhưng không có
gì trên màn hình. **Đã loại trừ:** sheet (nay resolve qua registry, có fallback +
cảnh báo); bề rộng (`aspectCap=false` đã được tôn trọng trong
`SweptTrail_HalfWidth` — aspectK 0 từng ép nửa-bề-rộng về 0); `material.mode`
(cầu tạm đặt 2.0 cho topology PARALLEL).

**Nghi tiếp theo, chưa kiểm:** đường update của composer swept ghi đè thứ gì đó
mà strand cần — nó vốn viết cho follower có cloth, còn strand thì không.
Kiểm theo thứ tự: trail có lay node không (`historyCount`), `cfg.shape`, và
những trường mode-2 mà `ApplyDeformUniforms` THỰC SỰ đọc so với những gì cầu tạm
điền vào.

### 8.3. Hai điều đã học, giữ lại kể cả khi bỏ đợt gộp

- `vc_projectile.inl` CÓ gọi ribbon trail. Khảo sát "0 consumer" ban đầu chỉ quét
  module gameplay, không quét `core/composition/` — tiền đề "gộp rẻ" là nói quá.
- `trail_glow.fs` KHÔNG chết (có load lúc chạy). Chỉ `smoke_trail.fs` là chưa
  thấy ai load.
