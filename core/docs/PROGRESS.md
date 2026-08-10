# PROGRESS — VFX contrast và strand trail

**Cập nhật:** 10/08/2026
**Trạng thái:** **CHƯA SỬA ĐƯỢC.**

Người dùng đã chạy bản build sau thay đổi cuối cùng và xác nhận:
**strand trail vẫn không đạt yêu cầu**. Không được coi các thay đổi dưới đây
là bản sửa hoàn tất.

## 1. Yêu cầu và triệu chứng hiện tại

- Cần một giải pháp contrast tổng hợp ở tầng gốc cho particle, ribbon/trail và
  decal, thay vì từng VFX tự chữa màu.
- Energy ribbon/strand trail với vật liệu Fire vẫn đọc như một dải đỏ đặc; lõi
  vàng mong muốn không hiện đúng.
- Trên nền sáng trail bị bệt/mất sắc độ rõ hơn nền tối.
- Trong một vòng sửa trước, smoke trail và smoke column bị lộ biên rõ. Nguyên
  nhân của hồi quy đó là coverage phi tuyến dùng chung; phần này đã được gỡ,
  nhưng smoke vẫn cần xác nhận thị giác lại trên đúng scene.

Ảnh tham chiếu người dùng cung cấp ngoài repo:

- `Screen Shot 2026-08-09 at 20.36.46.png`
- `Screen Shot 2026-08-09 at 20.37.01.png`
- `Screen Shot 2026-08-09 at 21.04.48.png`

## 2. Các thay đổi đã thực hiện trong phiên

### 2.1. Shared contrast profile

- Mở rộng `VFXContrastProfile` và đường áp dụng contrast dùng chung.
- ENERGY có body structure/core/emission rõ hơn.
- SMOKE và DUST được giữ alpha/edge trung tính để không làm lộ biên.
- Trail body/deform shader nhận `u_contrastParams`.
- Các contract test của render layer và bright-background được cập nhật.

File chính:

- `core/vfx_contrast.h`
- `core/vfx_contrast.c`
- `core/shaders/common/vfx_contrast.glsl`
- `core/trails/shaders/trail_body.fs`
- `core/trails/shaders/trail_deform.fs`
- `core/trails/trail_system.c`

### 2.2. Tách trail BODY và EMISSION

- `main.c` hiện gọi cả:
  - `DrawTrailEntitiesBody(camera)`
  - `DrawTrailEntitiesEmission(camera)`
- BODY dùng alpha blend để giữ hue/coverage trên nền sáng.
- EMISSION dùng additive cho HDR và bloom.
- `trail_deform.fs::ResolvePass()`:
  - BODY trả straight RGB + coverage.
  - EMISSION trả HDR RGB + intensity trong alpha.

Đây là kiến trúc đúng về blend: additive đơn thuần không thể giữ hue khi
destination đã sáng. Tuy vậy, kiến trúc đúng này chưa làm ảnh thực tế hết đỏ.

### 2.3. Gỡ coverage expansion gây lộ biên smoke

`core/shaders/distortion.fs` từng dùng:

```glsl
1.0 - pow(1.0 - body.a, 6.0)
```

Alpha 0.1 vì thế thành khoảng 0.47, khiến biên mềm của smoke, particle và decal
hiện thành đường rõ. Hiện compositor đã trở lại tuyến tính:

```glsl
float bodyCoverage = body.a;
```

Không được khôi phục nonlinear alpha toàn cục để chữa riêng energy trail.

### 2.4. Đổi nguồn màu lõi strand

Trong cả spawn và live update của `vc_strand_trail.inl`:

- Bỏ cách whitening trực tiếp từ glow về trắng. Với Fire, cách cũ biến đỏ
  thành hồng chứ không thành vàng.
- Màu lõi hiện lấy từ
  `ColorGradient_Sample(material->hotGrad, 0.20f)`, sau đó blend theo
  `hotWhiten`.
- Với material Fire, phép tính CPU cho kết quả gold/yellow.
- `ApplyDeformUniforms()` đã được kiểm tra và có upload `u_colHot`.

### 2.5. Tạo geometric hot core độc lập với strand texture

`trail_deform.fs` mode 2 hiện tạo `core0/core1/core2` từ khoảng cách tới ba
centreline, không nhân lõi với density `s0/s1/s2` của texture.

Lý do: texel R/G tại đúng tâm bundle có thể tối, làm hot core bằng 0 trong khi
support đỏ vẫn hiện. BODY nhận lõi hẹp ở 65%; EMISSION nhận toàn bộ
`hotSignal`.

### 2.6. Sửa điều kiện discard

Sau mục 2.5, shader vẫn từng discard theo `inten` của texture body trước khi
xét lõi độc lập, nên thay đổi geometric core bị vô hiệu.

Điều kiện hiện tại:

```glsl
if (max(inten, hotSignal) < 0.004)
    discard;
```

Regression test đã khóa điều kiện này. **Sau chính thay đổi này, người dùng vẫn
xác nhận “không được”.** Vì vậy không tiếp tục tuning màu/hotMix bằng suy đoán.

## 3. Những phần core đã kiểm tra

- `VFX_ComposeStrandTrail()` đặt `material.mode = 2.0f`.
- Energy style dùng ENERGY_RIBBON, additive, `bodyOpacity = 0.90f`, profile
  ENERGY và material hot gradient.
- `StrandTrail_OnUpdate()` đẩy lại style, tint, hotColor và material mỗi frame.
- `DrawTrailEntitiesLayer(camera, 0)` không loại energy trail additive; pass
  BODY ép blend sang `BLEND_ALPHA`.
- `DrawTrailEntitiesLayer(camera, 1)` vẽ energy trail ở EMISSION.
- `ApplyDeformUniforms()` đẩy `u_renderPass`, `u_bodyOpacity`,
  `u_contrastParams`, `u_colHot`, `u_colTail`, band shape và UV field.
- Group collection và group draw dùng cùng deform shader; chưa thấy mode 2 bị
  rơi khỏi render group ở code core đã đọc.

Giới hạn của kết luận: chưa có bằng chứng runtime rằng nút/scene trong ảnh thật
sự gọi `VFX_ComposeStrandTrail()` hoặc nạp đúng shader/binary vừa build.

## 4. Kiểm thử và build

- `trail_deform.fs` preprocess và compile thành công bằng GLSL 330 với
  `glslangValidator`.
- `cmake --build build -j2`: thành công, target `wuxing` đạt 100%.
- `vfx_render_layers_contract_test`: PASS ở bản compile mới.
- `bright_vfx_isolation_test`: PASS; washout xuất hiện tại additive
  composition trước post-processing.
- `vfx_contrast_test`: binary test hiện có báo PASS.
- `trail_deform_test`: các regression mới về hot gradient, geometric core,
  BODY/EMISSION và discard đều PASS. Còn 1 failure không thuộc lỗi màu:
  assertion cũ yêu cầu `VFX_ComposeSmokeTrail` biến mất khỏi public API,
  nhưng symbol vẫn tồn tại.
- `swept_trail_test`: còn 1 failure không thuộc strand mode 2, là assertion
  cũ về body sheet `assets/textures/energy_flow.png`.
- `git diff --check`: không có whitespace error khi tắt fsmonitor.

## 5. Việc phải làm đầu tiên ở phiên sau

### 5.1. Xác định đúng call-site trước khi sửa shader thêm

Core skill không cho đọc `sandbox/` và `assets/` nếu người dùng chưa cho
phép rõ ràng. Phiên này vì thế chưa kiểm tra được nút VFX bench/call-site và
sheet thật.

Phiên sau cần xin phép đọc hai thư mục đó cho lỗi này, rồi:

1. Truy đúng nút/scene tạo trail trong ảnh.
2. Xác nhận nó gọi
   `VFX_ComposeStrandTrail(..., VFX_STRAND_ENERGY)`, không phải ribbon cũ,
   swept trail hoặc một composer khác có hình gần giống.
3. Xác nhận executable, working directory và shader path thực tế. Shader được
   load lúc khởi động; tuning hot-reload không thay mã GLSL.

### 5.2. Instrument runtime có giới hạn

Chỉ log một lần khi material mode 2 xuất hiện, gồm:

- trail id và shader id;
- material mode;
- pass BODY/EMISSION;
- source blend mode;
- texture id;
- `hotColor.rgb`;
- `bodyOpacity`;
- location của `u_colHot` và `u_renderPass`.

Gỡ log sau khi xác định đường đi.

### 5.3. Debug shader theo thứ tự nhị phân

Không tuning tiếp. Dùng override tạm thời:

1. Mode 2 trả thẳng `vec4(u_colHot, 1)` để chứng minh đúng shader đang chạy.
2. Hiển thị `centreCore` trắng/đen.
3. Hiển thị `hotSignal` vàng/đen.
4. Hiển thị BODY và EMISSION riêng.
5. Chỉ khi call-site và uniform đều đúng mới kiểm tra channel R/G/A của sheet.

## 6. Những hướng không được lặp lại

- Không tăng additive/HDR để chữa nền sáng; cách đó đẩy màu gần trắng hơn.
- Không dùng nonlinear alpha expansion toàn cục; nó làm lộ biên smoke,
  particle và decal.
- Không tiếp tục đổi `hotWhiten`, gradient stop hoặc ngưỡng `hotMix` trước
  khi chứng minh mode-2 shader này thật sự chạy trong ảnh người dùng.
- Không viết workaround riêng trong VFX composition nếu lỗi nằm ở core.
- Comment ở hàng ENERGY trong `vc_strand_trail.inl` vẫn nói trail chỉ chạy
  BODY. Comment này đã lỗi thời sau khi `main.c` bật EMISSION; cần sửa khi
  tiếp tục, nhưng không phải nguyên nhân hình ảnh.

## 7. Working tree hiện tại

Toàn bộ thay đổi đang **uncommitted**. Không revert hoặc xóa thay đổi ngoài
phạm vi nếu chưa đọc `git diff`.

Các file đang thay đổi trong đợt này:

- `ENGINE_LANDMINES.md`
- `main.c`
- `core/vfx_contrast.h`
- `core/vfx_contrast.c`
- `core/shaders/common/vfx_contrast.glsl`
- `core/shaders/distortion.fs`
- `core/trails/trail_system.c`
- `core/trails/shaders/trail_body.fs`
- `core/trails/shaders/trail_deform.fs`
- `core/composition/common/vc_strand_trail.inl`
- `core/composition/common/vc_ribbon_trail.inl`
- `core/tests/bright_vfx_isolation_test.c`
- `core/tests/vfx_render_layers_contract_test.c`
- `core/tests/vfx_contrast_test.c`
- `core/tests/trail_deform_test.c`
- `core/tests/swept_trail_test.c`
- `core/docs/API.md`
- `core/docs/API_GUIDE.md`
- `core/docs/LANDMINES.md`
- `core/docs/PROGRESS.md`

Đọc diff của từng file trước khi hành động; không giả định mọi thay đổi đều chỉ
phục vụ strand trail.
