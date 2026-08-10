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

## 8. Đợt hợp nhất trail — ĐÃ CHẠY, test về 43/47

Gộp ribbon + strand thành MỘT trail mô tả bằng dữ liệu
(`core/trails/trail_recipe.h`), giữ nguyên nửa mô phỏng của `trail_system.c`.
Kế hoạch: `~/.claude/plans/inherited-hugging-kettle.md`.
Mốc an toàn NGAY TRƯỚC đợt này: commit `d372823` — bỏ đợt gộp =
`git reset --hard d372823`, không cần stash theo thư mục.

### 8.1. Đã xong

- `trail_recipe.h` — `TrailRecipe` = geometry + `UVDeformField` + `SurfaceFlow`
  + surface + mask + colour + pass policy, dựng từ `core/uv/` đúng như
  `core/uv/README.md` tuyên bố (`mesh + UVDeformField + SurfaceFlow = effect`).
- `vc_ribbon_trail.inl` → `vc_trail.inl`: `k_trailPresets[]` (6 preset) +
  `k_trailMotion[]` (thay 5 switch theo kind — một bảng thì không quên cột).
- Xoá `vc_strand_trail.inl`, `vc_smoke_ribbon_trail.inl` (392 dòng code chết,
  không được include ở đâu), nhánh `SweptTrail_BuildAssetSheet` +
  `swept_sheet` (đóng luôn mục 4.2), tầng alias `VFX_ComposeSweptTrail`.
- API mới: `VFX_ComposeTrail` / `VFX_ComposeTrailEx` / `VFX_TrailSetWidth` /
  `VFX_KillTrail` / `VFX_Trail_Stop`. `vc_projectile.inl` đã migrate.
- Test: 43/47 — bằng mức trước đợt gộp. 4 suite đỏ còn lại
  (`energy_burst_semantic_layers`, `tube_frame`, `vfx_layered_field_contract`,
  `volume_trail`) đã đỏ từ trước, không liên quan.

### 8.2. Năm lỗi "preset strand bị chạy qua đường của swept"

`TRAIL_PRESET_ENERGY` từng render ra rỗng. Tìm ra bằng **probe nhị phân**, không
phải đọc code: ép mode 2 trả magenta → không pixel nào; ép ngay đầu `main()` trả
xanh → đúng 4 pixel. Tức shader CÓ chạy, nhưng dải gần như không có diện tích.

| # | Lỗi | Sửa |
|---|---|---|
| 1 | `s_sweptWidthCurve/AlphaCurve` chỉ nạp cho 4 preset swept; **FloatCurve rỗng eval ra 0** → bề rộng 0, alpha 0 | cờ `motion.curves` |
| 2 | `aspectCap=false` vẫn bị chia đôi bề rộng | radius LÀ nửa-bề-rộng |
| 3 | `t->tint = WHITE` mỗi frame tẩy trắng preset mang màu trong tint | chỉ áp khi có gradient |
| 4 | `MaxNodes` hardcode 60 Hz cho trail 30 Hz → giữ gấp đôi lịch sử, cắt đuôi vuông | `MaxNodesFor(kind, ...)` |
| 5 | `nodeHomeSpring/MaxDev/OrderFrac` áp cho preset không cloth | gate theo `motion.cloth` |

Cộng thêm **cùng lớp lỗi với §1**: `tuning.cfg` có `swept_width = 3.0` và
`swept_alpha = 1.5` — knob GLOBAL của họ swept nhân vào preset strand vốn chưa
bao giờ có knob đó (0.45 m → 1.35 m, thành cục phình). Nay `TrailMotion` có cờ
`sweptKnobs`; **một knob global trong composer dùng chung PHẢI nêu rõ nó áp cho
preset nào.**

Và `cfg.deform.envHead/envTail/phase` bị bỏ sót — vertex deform tắt nhưng
FRAGMENT vẫn đọc chúng (ramp disorder + phase mỗi lần spawn). `envHead = 0` làm
cửa sổ head-weld sập, ramp bão hoà ngay segment đầu, dissolve cắn từ ĐẦU thay vì
từ đuôi.

**Đo được (chroma):** ENERGY 0.658 (bản duyệt 0.647) · MAIN 0.641 (0.656).
Màu khớp; hình dạng đã thuôn hai đầu, hết vết cắt vuông.

### 8.3a. Shader — mode 1 (packed wisp) ĐÃ XOÁ

`trail_deform.fs` từng có 3 mode. **Không composer nào đặt `material.mode = 1`**
kể từ khi strand trail thay thế nó — 30 dòng shader + 2 uniform
(`u_turbStrength`, `u_edgeTear`) không đường nào chạm tới, nằm trong đúng file
mà mọi trail phải debug qua. Đã xoá cùng với loc + upload phía C, và hai ngưỡng
`< 0.5` / `>= 1.5` gộp thành MỘT: dưới 1.5 là passthrough, trên là strand.

Test đã trỏ lại theo đúng kỷ luật cũ: assertion nay khẳng định **nhánh đó phải ở
trạng thái đã xoá**, không phải im lặng bỏ đi. Render không đổi (chroma 27:
0.658 = 0.658; 18: 0.878 vs 0.879).

Còn lại của bước shader: đưa phần LẤY MẪU qua `SurfaceFlow_FieldSample`
(`uv_field.glsl` đã có sẵn), để bỏ nốt `u_panSpeed`/`u_tiling` khỏi cầu tạm.

### 8.3. Cầu tạm — `TrailRecipe_ToLegacyMaterial`

`trail_deform.fs` VẪN là 3 mode viết tay. Cầu tạm dịch recipe → uniform cũ ở
ĐÚNG MỘT chỗ. Cố ý không gộp shader cùng lượt: công thức mode 2 là ~150 dòng
người dùng đã duyệt bằng mắt, viết lại cùng lúc với composer thì khi hỏng sẽ
không phân biệt được lỗi composer hay lỗi shader. **Xoá cầu tạm = định nghĩa
"xong" của bước shader.**

Cầu tạm hiện suy ra `wispMix/strandGain/flowStrength/bundleWeight/bundleWidth`
từ recipe thay vì hardcode — hardcode từng đưa số của ENERGY cho cả SMOKE.

### 8.3b. Bench — một nút MỖI PRESET (đã xong)

`scripts/sync_vfx_test.py` sinh lại toàn bộ manifest, khoá theo `.inl`, một file
một entry — nên hàng thêm tay bị bỏ, và entry cũ quay ra spawn `0` = BLADE, tức
**ENERGY không còn vào được từ bench**. Đã thêm `FIXTURE_PRESET_VARIANTS` vào
script: một composition mà chủ đề chính là "cùng một máy, nhiều look được viết
sẵn" thì các look phải nằm cạnh nhau trên bench. Đây là ngoại lệ DUY NHẤT của
bất biến một-entry-một-`.inl`, và lý do là §1: giấu lựa chọn sau một tuning value
chính là thứ khiến hai phiên render nhầm style mà không ai thấy được.

Bench nay có 6 nút: 27 MAIN · 28 ENERGY · 29 BLADE · 30 WISP · 31 BACKDROP ·
32 SMOKE.

### 8.3c. SMOKE — ĐÍNH CHÍNH, và đổi sang trắng

**Tôi đã ghi sai ở commit `c8cf913`.** Tôi viết "SMOKE HỎNG — dải đỏ phẳng,
không có sợi". Sai: sợi, chuyển động và texture đều đúng. Kết luận đó dựa trên
một render ở pha xấu, ở kích thước nhỏ, và tôi đã không kiểm lại trước khi ghi
vào doc lẫn commit message.

Màu đỏ cũng **không phải lỗi**: bench truyền `VC_MAT_FIRE`, preset lấy tint từ
material. Preset làm đúng điều nó được bảo.

Cái ĐÚNG là có, nhưng nhỏ hơn nhiều: `tailColor = lerp(base, m->body, tailDarken)`
trở thành **vô nghĩa** khi `base` đã là `m->body` — đầu và đuôi cùng màu, ramp
dọc trail bằng 0. Không phải hồi quy: `vc_strand_trail.inl` cũ có đúng công thức
đó. Và `TrailColorConfig.tail` là trường khai báo cho đúng việc này nhưng **không
ai đọc** — API chết.

Đã sửa (theo yêu cầu của người dùng, khói → trắng):
- `bool useGlowTint` → `TrailTintSource {GLOW, BODY, NEUTRAL}`. Một bool không
  nói được "trung tính", mà khói cần đúng thế: khói là SẢN PHẨM cháy, không phải
  nguyên tố — khói của Fire không nên đỏ, của Lightning không nên xanh.
- Cầu tạm nay đọc `colour.tail` khi được khai báo, rơi về phép trộn cũ khi không.
- SMOKE: trắng ở đầu, nguội về xám trung tính.

### 8.3d. Chuỗi chữ V ở BACKDROP — là `tuning.cfg`, lần thứ BA

| khoá | tuning.cfg | mặc định code |
|---|---|---|
| `swept_tile` | 0.5 | 1.10 |
| `swept_flow` | +1.0 | **−1.0** |

Tile nửa lại ⇒ 16 vệt của mask procedural lặp gấp đôi mật độ ⇒ đúng "cái lược"
mà chú thích trong `SweptTrail_BuildBladeMask` nói nó cố ý tránh. `swept_flow`
đảo dấu ⇒ hoa văn trôi về phía đầu, vật chất trông như chảy NGƯỢC vào nguồn.
Cùng build, chỉ khác tuning.cfg → khác hẳn. Cả 4 preset swept dùng chung hai
knob này; BACKDROP rộng nhất nên lộ trước.

CHƯA sửa tuning.cfg — có thể là lựa chọn cũ của người dùng.

### 8.3e. QUYẾT ĐỊNH: swept chuyển sang vật liệu STRAND

Nguyên nhân gốc của "texture xấu": preset swept chạy nhánh **passthrough**
(`texture(texture0, vSegUV) * vColor`) và đang mặc **mask procedural sinh lúc
chạy** — thứ mà code tự gọi là *fallback*. Đường art thật (`energy_flow.png`) đã
bị xoá vì nó đã trỏ sai ảnh. Nên 4 preset swept đang mặc đồ dự phòng vĩnh viễn.

`energy_wisp.png` KHÔNG dùng thẳng được cho passthrough: nó là sheet STRAND
(`R:pattern1 | G:pattern2 | B:distort | A:dissolve`), đưa vào passthrough thì RGB
bị hiểu là màu và A bị hiểu là độ đục — đúng loại nhầm mà
`assets/TEXTURE_PACKING.md` được máy kiểm để chặn.

**Hướng đã chốt:** bỏ nhánh passthrough cho preset swept, cho cả 6 preset dùng
CHUNG vật liệu strand, chỉ khác nhau ở deform/flow layer. Khi đó:
- `energy_wisp.png` dùng được ngay, đúng grammar;
- `trail_deform.fs` còn đúng MỘT công thức (passthrough chỉ còn cho trail không
  có recipe) — hoàn tất việc #3;
- tune bằng layer, không bằng ảnh.

Việc cần làm: cho 4 preset swept `topology = PARALLEL` + deform layer riêng (cloth
vẫn giữ, nó là chuyển động chứ không phải bề mặt), `surface = ENERGY_RIBBON`, bỏ
`sheetOverride` + `SweptTrail_BuildBladeMask`. Nếu cần gu sợi khác (dày/mảnh),
sinh biến thể sheet bằng script như `scripts/gen_energy_wisp_texture.py`.

### 8.4. Còn lại

- Gộp `trail_deform.fs` về một công thức đọc `u_uvField`/`u_flowLayer`
  (`uv_field.glsl` đã sẵn cả hai lối SUMMED và PARALLEL).
- Manifest bench: một entry mỗi preset (sửa tay `scripts/vfx_test_manifest.json`
  trước, rồi `scripts/sync_vfx_test.py`). Hiện bench vẫn 2 entry cũ.
- SMOKE preset (`--render-vfx 25` vẫn là puff tube cũ) chưa được nhìn bằng mắt.
- `trail_glow.fs` KHÔNG chết (có load lúc chạy) — bỏ khỏi danh sách xoá.
