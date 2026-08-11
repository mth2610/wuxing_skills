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

## 9. Texture cho trail — thiết kế và audit (10/08/2026)

Ghi lại để phiên sau chạy thẳng, không phải suy luận lại.

### 9.1. Sheet sợi KHÔNG đúng cho mọi archetype

Sau khi 4 preset swept chuyển sang vật liệu strand (§8), cả 6 preset đều đọc
`energy_wisp.png`. Đó là lý do BACKDROP trông mảnh: nó đang mặc sheet của
archetype khác. Năm archetype, năm nhu cầu khác nhau:

| Archetype | Sheet phải mang gì | Hiện có |
|---|---|---|
| **Filament** (ENERGY, WISP) | sợi mảnh + **khe hở giữa chúng** — khe hở CHÍNH LÀ hiệu ứng | ✅ `energy_wisp.png` |
| **Blade** (BLADE) | **biên ngoài sắc + lõi sáng**; một lưỡi kiếm phải đọc ra VẬT THỂ, sợi biến nó thành năng lượng | ❌ mượn sheet sợi |
| **Cloth** (MAIN) | **nếp gấp rộng, tần số thấp**, liên tục dọc chiều dài | ❌ |
| **Mass** (BACKDROP) | gần như mây: rất mờ, rất rộng, **không chi tiết** — chi tiết đánh nhau với trail phía trước | ❌ |
| **Shape** (SMOKE) | MỘT vệt hoàn chỉnh, taper vẽ sẵn hai đầu, stretch một lần | ✅ `smoke_strand.png` |

### 9.2. KHÔNG cần grammar mới — cần MỘT generator có tham số

Layout `STRAND` (`R:pattern1 | G:pattern2 | B:distort | A:dissolve`) mô tả được
cả bốn loại trên. Khác nhau chỉ là **tần số, tương phản, cách xử lý biên** —
tức là NỘI DUNG, không phải hợp đồng kênh. Nên đây là một script có preset, chứ
không phải bốn script rời (mỗi script rời lại là một bản sao của cùng một ý,
đúng thứ §8 vừa xoá ở tầng composer).

`scripts/gen_energy_wisp_texture.py` hiện hardcode: `SIZE`/`OUT` là hằng số
module, không có argparse, sinh đúng một ảnh. Việc cần làm là tách các hằng số
tạo hình thành preset (mật độ sợi, độ dày, tương phản biên, tần số nền) + CLI
`--preset <name> --out <path>`.

### 9.3. Audit `assets/textures/` — 69 file

```
69 .png  →  19 trong registry
           26 chỉ code tham chiếu  (KHÔNG qua registry)
           24 KHÔNG ai tham chiếu
```

> **ĐÍNH CHÍNH 10/08/2026 — con số 24 SAI, và sai theo hướng nguy hiểm.**
> `rune_glyphs_0..3` bị đếm nhầm là mồ côi. Chúng **đang được dùng**, nạp bằng
> đường dẫn dựng lúc chạy: `vc_rune_circle.inl:92` gọi
> `snprintf(path, ..., "assets/textures/rune_glyphs_%d.png", g)`. `git rm` theo
> danh sách cũ là gãy VFX vòng rune.
>
> **Quy tắc:** một lần grep theo tên file KHÔNG chứng minh được file mồ côi.
> Phải quét luôn các chỗ dựng đường dẫn động (`snprintf`/`TextFormat` +
> `assets/textures`) trước khi xoá bất cứ thứ gì. Trong repo này hiện chỉ có
> đúng một chỗ như vậy — nhưng phải KIỂM, không phải nhớ.
>
> Số đúng: **22 file không có consumer lúc chạy** (không xuất hiện trong bất kỳ
> `.c`/`.h`/`.inl`/`.json` nào). ĐÃ `git rm` (chủ repo chốt xoá thẳng, không qua
> `_unused/`); 5 trong số đó sinh lại được bằng script (`gen_dust_flipbook.py`,
> `sim_fire_flipbook.py`, `gen_volume_trail_textures.py`, `flipbook/pack.py`),
> 17 file còn lại không ai tham chiếu ở đâu cả. `assets/textures/` còn 47 file.
> Kiểm sau khi xoá: cmake configure qua (validator registry chạy ở đó), render
> bench không có cảnh báo thiếu asset, test 43/47.

**24 file mồ côi.** Có một cụm flipbook trùng lặp rõ rệt:
`fire_atlas_8x8`, `fire_puff_8x8`, `fire_puff_8x8_smoke`, `smoke_puff_8x8`,
`smoke_puff_8x8_flame`, `dust_puff_4x4`, `dust_puff_4x4_smoke`,
`dust_puff_8x8`, `flame_tongue_8x8`; cộng `rune_glyphs_0..3`, một cụm
ground/grass PBR (`grass_ground_*`, `ground_composed*`, `dirt_diffuse_soft`,
`grass_detail`), và `gradient_alpha`, `petal_card`, `qi_wisp_soft`.

**26 file "chỉ code tham chiếu" mới là vấn đề kiến trúc thật** — chúng đi vòng
qua registry, nên KHÔNG ai kiểm channel grammar cho chúng. Đó đúng là loại nhầm
đã làm `energy_flow.png` bị đọc sai suốt (§4.2/§8.3e).

### 9.4. Việc phải làm, theo thứ tự

~~1. Tham số hoá `gen_energy_wisp_texture.py`~~ · ~~2. Sinh 3 sheet mới~~ ·
~~3. Trỏ `recipe.surface` sang sheet mới~~ — **HUỶ, xem §9.6.** Đo rồi: sheet
không phải biến số. Còn lại:

~~4. Dọn file mồ côi~~ — **XONG.** 22 file (không phải 24 — xem đính chính ở
   §9.3), `git rm` thẳng theo quyết định của chủ repo.
5. Đưa dần các file code-tham-chiếu vào registry — đây là việc dài, làm theo từng
   consumer, không làm một lượt. **CÒN TREO.**

### 9.5. §9.5 ĐÃ TRẢ LỜI: tune tới nơi, KHÔNG cần sheet mới

Đã sweep bằng 6 tunable tạm (`dbg_bundle/gain/edge/dissolve/wisp/third`) trên
bench 27–32, render headless, so với **mặc định code** (`tuning.cfg` đã trung
hoà — xem cảnh báo §8.3d). Kết quả: **blade, cloth và mass đều lấy được từ
`energy_wisp.png`.** Ba file art trong §9.1 rơi khỏi kế hoạch.

Knob quyết định là **`gain`** — số mũ `pow()` áp lên mật độ lấy mẫu:
- `gain < 1` nâng các KHE HỞ giữa sợi lên cho tới khi chúng dính lại thành khối;
- `gain > 1` đẩy chúng ra thành sợi rời.

Tức archetype nằm ở CÁCH ĐỌC bề mặt, không nằm ở bức ảnh. Đúng tinh thần §9.2
("cần một generator có tham số") nhưng rẻ hơn một bậc: tham số nằm ở phía đọc,
không phải phía sinh ảnh.

### 9.6. `TrailStrandConfig` — archetype là DỮ LIỆU, không phải suy diễn

Năm số điều khiển việc lấy mẫu trước đây được **cầu tạm suy ra** từ những trường
không liên quan gì tới chúng:

| số | suy ra từ (cũ) | vì sao sai |
|---|---|---|
| `bundleWidth` | `waveAmp * 0.85` | buộc bề RỘNG của bó vào biên độ nó ĐUNG ĐƯA — hai thứ độc lập |
| `thirdWeight` | `mask.tailNarrow` | sửa knob đuôi lại đổi cách lấy mẫu bề mặt |
| `flowDistort` | `mask.dissolveSoft` | như trên |
| `gain` | `colour.coreWidth > 0 ? 1.35 : 0.75` | archetype bị suy ra từ việc CÓ lõi nóng hay không |
| `fineMix` | `flow.layerCount > 1 ? 0.6 : 0.7` | |

Những ràng buộc đó được nghĩ ra chỉ để **khỏi phải viết một hằng số ra**, và giá
phải trả rất cụ thể: trường `bundle` đã được khai trong `k_sweptStrand[]` NGAY TỪ
ĐẦU rồi bị `(void)` vứt đi. Nên cả bốn preset swept bị ghim dưới **một phần tư
quad của chính nó** → mảnh như sợi tóc ở mọi bán kính. **Đó chính là "quá mảnh"
mà chủ repo báo** — không phải do sheet.

Nay là `TrailStrandConfig` trên recipe, khai báo tường minh từng preset:

| preset | bundle | gain | edge | ý đồ |
|---|---|---|---|---|
| BLADE | 0.36 | 0.34 | 0.06 | VẬT THỂ: đặc ruột, biên ngoài sắc |
| MAIN | 0.65 | 0.61 | 0.32 | vải: nếp rộng, tần số thấp |
| WISP | 0.22 | 1.35 | 0.14 | giữ nguyên chất sợi (`gain > 1`) |
| BACKDROP | 0.95 | 0.19 | 0.75 | khối: không chi tiết để đánh nhau với trail trước |
| ENERGY | 0.34 | 1.35 | 0.18 | **giữ NGUYÊN số cũ** |
| SMOKE | 0.26 | 0.75 | 0.34 | **giữ NGUYÊN số cũ** |

ENERGY là preset DUY NHẤT chủ repo đã duyệt bằng mắt, nên nó phải không đổi —
render xác nhận không đổi. BACKDROP hạ trọng số layer (0.055/0.14 → 0.025/0.063):
lấp khe hở làm diện tích phát sáng tăng vài lần, trọng số vừa đủ cho một sợi
thưa thì chói khi thành dải đặc.

Test: `swept_trail_test` khẳng định bridge **ĐỌC** archetype thay vì suy ra
(`!FileHas("out->bundleWidth = L0->amplitude")`) — nếu ai đó khôi phục lối suy
diễn, test đỏ. 43/47, đúng bốn suite đỏ cũ.

### 9.7. "Khựng một cái rồi đổi pha" — ĐÃ CHẨN, ĐÃ SỬA

`FollowerCut` (`core/trails/trail_system.c`). Cắt xảy ra khi transform được bám
nhảy xa hơn `teleportSpeed * dt` trong một frame. Nó reset history về 1 node —
**cái khựng đó CHÍNH LÀ định nghĩa của cắt, giữ nguyên**. Nhưng nó còn xoá
`laidDist` = 0, mà `laidDist` là đồng hồ đo quãng đường `nodeUV[]` ghi lại theo
từng node và fragment stage đọc lại qua `u_pathArc.x` để định pha texture strand.
Nên hoa văn nhảy về đầu ĐÚNG frame trail khựng: hai triệu chứng, một nguyên nhân.

Giữ đồng hồ là an toàn — không ai khác đọc `laidDist`, nó KHÔNG phải chiều dài
của aspect cap, và đường vẽ đã tự gấp nó ở 8192 m cho độ chính xác float.

**Kiểm bằng cách ÉP nhánh chạy, không phải bằng suy luận:** hạ tạm ngưỡng xuống
4.6 m/s cho bench cắt liên tục → đồng hồ nay báo **giữ ở 4.32 m** qua các lần
cắt, chỗ trước đây báo 0.00 m. Log của cắt nay in kèm giá trị đó vĩnh viễn.

Chưa đụng vào ngưỡng 45 m/s: mọi lần cắt bench kích hoạt đều là nhảy thật lúc
spawn (3–5 m trong một frame). Trong game có swing nào chạm ngưỡng không thì nay
chỉ cần grep log một dòng.

### 9.8. Quyết định còn treo

- **24 file mồ côi:** chuyển sang `_unused/` hay `git rm` thẳng?
- **`tuning.cfg`:** `swept_width = 3.0` / `swept_alpha = 1.5` / `swept_tile = 0.5`
  / `swept_flow = +1.0` vẫn NGUYÊN, chưa đụng. Lưu ý mới: `swept_width = 3.0` có
  lẽ **không phải rác** — nó đang bù đúng cho cái hairline mà §9.6 vừa sửa tận
  gốc. Sửa xong gốc rồi thì ×3 đó nay là thừa và sẽ làm quad phình. Đề xuất: bỏ
  cả 4 dòng về mặc định. `swept_sheet = 1` thì đã CHẾT hẳn (nhánh bị xoá ở §8.1).
### 9.9. BLADE mỏng — ĐÍNH CHÍNH GIẢ THUYẾT CỦA CHÍNH TÔI, rồi đo ra thủ phạm

Ở bản §9.8 đầu tiên tôi viết "BLADE mỏng vì hình học — `RIBBON_FIXED_NORMAL`
normal `(0,1,0)` nên nhìn nghiêng". **Sai, và tôi viết ra khi chưa đo** — đúng
loại lỗi §8.3c đã tự kiểm điểm. Mặt phẳng của BLADE không cố định `(0,1,0)`: nó
được tính lại mỗi frame từ độ cong đường đi của mũi (`SweptTrail_UpdateNormal`,
dòng 1620 ghi đè `t->fixedNormal = s->normal`). Ribbon nằm TRONG mặt phẳng vung,
không nhìn nghiêng.

Đo bằng cách tách biến trên fixture 29 (mỗi lần chỉ đổi MỘT knob):

| render | knob | kết quả |
|---|---|---|
| `bw_c` | `swept_aspect = 4` một mình | **không đổi gì** → aspect cap KHÔNG chạm ở bề rộng bench |
| `bw_d` | `swept_width = 3` một mình | rộng lên rõ → bề rộng CALLER mới là thứ đang chặn |
| `bw_a` | `swept_width = 6` một mình | vẫn mảnh → tới đây thì cap MỚI chạm |
| `bw_e` | `swept_width = 6` + `aspect = 2.2` | đọc ra lưỡi kiếm có bề bản |

Tức có **HAI** ngưỡng nối tiếp, và trước đó tôi gộp làm một:
1. Ở 0.1 m bench truyền vào → `want = 0.05 m` nửa-bề-rộng thắng. Mỏng vì
   **người gọi xin mỏng**. Đúng thiết kế: bề rộng là việc của caller.
2. Trên ~0.15 m nửa-bề-rộng (sweep 6 m) → **cap 1:20 thắng**, và mọi giá trị lớn
   hơn đều sập về CÙNG một sợi. Tham số bề rộng mất hết ý nghĩa từ đó trở lên.

Chỉ (2) là lỗi. Đã sửa `aspectK` 0.0250 → 0.0550 (1:20 → 1:9 theo bề rộng đầy
đủ). **Nâng TRẦN, không tự nới cái gì**: bench 29 render y hệt, vì ở 0.1 m cap
chưa bao giờ chạm. Vẫn chặt hơn MAIN (0.0715) và BACKDROP (0.1).

Còn treo: `radiusDefault = 0.10f` dùng chung cho cả bốn preset swept — số một
caller nhận khi truyền 0. Chưa đụng, vì bench truyền số riêng nên không có bằng
chứng nào từ đây nói nó sai; muốn biết thì phải xem một skill thật gọi nó.

## SSF nước hết "nhựa" — thickness lấy lại gradient (2026-08-11)

Regression pin được về `0262068` ("update force field", 03/08): dòng gộp
`depthGap` đổi từ `min(kernel, max(0.022, gap*1.25))` sang
`max(kernel, min(0.40, gap*0.90))`. Với vật thể bay trên không, gap > 1 m nên
CỘT NƯỚC bị ghim cứng 0.40 m trên toàn bộ silhouette — vừa dày gấp 2.5x, vừa
mất sạch biến thiên độ dày. Đã trả lại ngữ nghĩa `min` (receiver chỉ CHẶN, không
TẠO cột nước).

Nguyên nhân thứ hai, có trước regression: `DecodeOpticalThickness` bão hoà ở cap
0.16 m với knee 0.11 m, trong khi orb 2.000 splat cộng ra p ≈ 1.3 m → mọi pixel
bên trong đều dính cap. Đã chia bù chồng lấn kernel (`/1.5`) và dời knee lên
0.42 m; **giữ nguyên dải output 0..0.16** nên toàn bộ ngưỡng foam/rim/coverage ở
hạ nguồn không phải chỉnh.

Số đo (mirror trong test): tỉ lệ core/rim 1.31 → 4.28; cột nước của orb bay
0.400 m → 0.152 m.

Chưa làm, xếp theo giá trị:
- **Mobile**: 4 render target đều R32F và pass thickness blend cộng vào R32F.
  ES 3.0/3.1 báo `INVALID_OPERATION` khi blend với draw buffer float 32-bit
  (cần `EXT_float_blend`), và lọc LINEAR trên float 32-bit cần
  `OES_texture_float_linear`. Bộ lọc separable chỉ lấy mẫu đúng tâm texel nên
  NEAREST là đủ → đường ra: linear view distance trong R16F/RGBA8-packed +
  NEAREST, thickness sang unorm additive, capture nửa độ phân giải, 1 vòng lọc.
- `fluid_capture_particle.fs` vừa ghi `gl_FragDepth` vừa `discard` → tắt early-Z
  trên Mali (late-ZS), với overdraw rất nặng.
- `fluid_depth_narrow_range.fs` thực chất đang là bilateral Gaussian, không phải
  narrow-range: nó hạ trọng số mẫu ngoài dải thay vì KẸP vào `[z-r, z+r]` như
  bài báo — đúng cái gây bướu hình hạt còn thấy trên bề mặt.
- `s_materialBody` là static toàn cục → hai chất lỏng khác màu trong một frame
  sẽ giành nhau. Cần material theo từng stream nếu muốn SSF phục vụ lửa/thuỷ
  ngân/độc.

## SSF: hạt GPU mất `v_life` — capture bị discard âm thầm (2026-08-11)

`core/particles/shaders/gpu/fluid_surface_capture.vs` khai báo 3 output, trong khi
CẢ HAI fragment shader ghép với nó (`fluid_capture_particle.fs`,
`fluid_surface_thickness.fs`) mở đầu bằng `if (v_life <= 0.0) discard;`. GL nối
varying theo TÊN và để input không khớp ở trạng thái *undefined* chứ không báo lỗi
link; rlvk cũng vậy (hạ xuống biến Private). Nghĩa là toàn bộ depth + thickness của
hạt chạy trên GPU backend phụ thuộc vào một giá trị rác — không log, không lỗi.
Đây là ứng viên hàng đầu cho việc mặt nước không bao giờ liền khối mà luôn lỗ chỗ
theo từng splat.

Đã thêm `v_life = life_data.x` (life còn lại, khớp ngữ nghĩa của
`fluid_pbd_surface.vs`) và guard `core/tests/shader_stage_interface_test.c` — guard
này FAIL 2 lỗi trên shader trước khi sửa, PASS sau khi sửa.

Chưa xác nhận bằng mắt: cần chạy sandbox NEW FX → WATER ORB. Nếu mặt nước vẫn trắng
bạc sau bản sửa này thì nghi vấn tiếp theo là đường CPU (`FLUID_SURFACE_MAX_PARTICLES
= 384` trong khi water orb phát 2.000 hạt → chỉ 384 hạt đầu vào được capture, phần
còn lại biến mất) chứ không phải phần quang học.

## Đã tìm ra vì sao nước mất độ trong: b03b7b6 (2026-08-11)

Người dùng nhớ đúng — ở `0237c80` (05/08) fluid còn ổn. `b03b7b6` (10/08) rút bỏ hai
lớp VFX tách rời; từ đó `ScreenDistort_BeginVFXBody()` bind thẳng `renderTex`, đúng
cái texture mà `ScreenDistort_GetSceneTexture()` trả về. `FluidSurface_Composite()`
chạy bên trong pass đó, nên nó **lấy mẫu chính colour attachment đang ghi** — undefined
trong GL, read/write hazard trong Vulkan. Tap khúc xạ chết ⇒ chỉ còn các số hạng đục
của shader (in-scatter + specular) ⇒ vỏ nhựa.

Sửa: `FluidSurface_Capture()` chụp một bản sao scene riêng (đúng format, giữ HDR) rồi
composite lấy mẫu bản sao. Không hồi sinh lớp đã bị rút, không đổi thứ tự trong main.c.
Chi phí: một lần copy toàn màn hình, chỉ ở frame có fluid.

Guard: `core/tests/fluid_refraction_source_test.c` — FAIL 4 lỗi trên mã trước khi sửa,
PASS sau khi sửa. Nó kiểm tra ĐIỀU KIỆN hazard trước, nên nếu sau này khôi phục lớp
riêng thì yêu cầu tự hết hiệu lực.

Chưa xác nhận bằng mắt (Core không có tầng visual tự động). Hai chỉnh quang học trước
đó — `min` thay `max` khi gộp depthGap, và knee của `DecodeOpticalThickness` — vẫn giữ;
mỗi cái một dòng, dễ dial lại sau khi nhìn thấy kết quả thật.

## Biên tưa: hai bản sửa ĐÃ THỬ và ĐÃ HOÀN TÁC (2026-08-11)

Trạng thái hiện tại = commit `279a115`. Vân đường-đồng-mức đã hết; còn lại **một
chút sọc ở biên, phải nhìn rất kĩ mới thấy** — đây là mức được chấp nhận, không
phải việc còn dở.

Đã đo được (bằng `WUXING_FLUID_DEBUG`, xem `fluid_surface.c`): capture **sạch**
(chế độ 12 không sọc), và mỗi pass lọc kéo bề mặt **theo đúng trục của nó** (chế
độ 14 chỉ chạy pass ngang → sọc chuyển từ dọc sang ngang). Nguyên nhân đúng như
vậy: ở gần silhouette phía ngoài không còn bề mặt, nên phép gom mẫu **một phía**
kéo kết quả về phía thân khối.

**Hai bản sửa đã thử, cả hai làm SỌC NHIỀU HƠN, đã hoàn tác — đừng thử lại:**

1. Gom theo cặp, `break` khi một phía hết bề mặt. Đúng về mặt đối xứng (guard đo
   được: lệch một phía +0.1176 m → theo cặp +0.0000 m), nhưng trong một trường
   splat **thưa** thì bất kỳ lỗ nhỏ nào bên trong cũng chặn vòng lặp gần như
   ngay lập tức, nên bộ lọc gần như không làm mịn ở đúng những chỗ cần nhất.
2. Hole fill chỉ lấp khi bị bao quanh (`surfaceCount > 13` trên 24 hàng xóm).
   Tách đúng "lỗ" khỏi "rìa" về mặt hình học, nhưng ngưỡng đó quá chặt với thân
   khối thưa: nhiều pixel lỗ **bên trong** cũng không đủ 14 hàng xóm nên thôi
   được lấp, để lại nhiều khoảng trống hơn trước.

Cả hai đều **giảm lượng làm mịn đúng ở nơi cần nó nhất**. Muốn khử nốt phần dư
này thì phải đổi kiểu lọc (lọc 2D thật, hoặc curvature-flow) chứ không vá được
trong khuôn khổ separable — và đó là việc đắt, cân nhắc cùng lúc với đợt hiệu năng.

Thiết bị đo `u_debugView` vẫn còn trong `fluid_surface.fs` và `fluid_surface.c`
(mặc định tắt). Gỡ khi nào chốt là không đụng vào biên nữa.
