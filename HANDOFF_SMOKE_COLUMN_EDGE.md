# Bàn giao — biên hai bên của `VFX_ComposeSmokeColumn`

Dán toàn bộ file này vào phiên mới.

---

## Việc cần làm

Cột khói (`VFX_ComposeSmokeColumn`, fixture 22 trong VFX tester) **đã đúng về hình
dạng, chuyển động và texture**. Còn đúng một khuyết điểm: **hai biên trái/phải là
một nét cắt cứng**, không tan dần như khói thật.

Biên trên/dưới thì đã mềm — chúng là **biên hình học thật** của lưới, xử lý bằng
vertex alpha trong `PMTube_DrawFaded`. Hai biên bên **không phải biên**: mặt ống
là một mặt **liên tục**, nó chỉ *trông* như có biên vì bị chiếu xuống 2D. Đó là
lý do bài toán khó hơn vẻ ngoài.

---

## Trạng thái hiện tại — ĐÃ KHÔI PHỤC, đừng tưởng là dở dang

Phiên trước sửa 7 thứ liên tiếp rồi **lui gần hết**, vì 4 trong số đó dựa trên
các ảnh debug **bị nhiễm** (xem §"Đã bác bỏ"). Trạng thái bây giờ = đúng lúc
người dùng nói *"khá giống rồi, chỉ còn biên chưa đẹp"*.

`core/trails/shaders/trail_volume.fs`:
```glsl
vec3 N = normalize(fragNormal);
vec3 V = normalize(viewPos - fragPosition);
float d = abs(dot(N, V));
float depth = pow(clamp(1.0 - d, 0.0, 1.0), max(u_volMask.y, 0.001));
float rim   = smoothstep(0.0, max(u_volMask.z, 0.001), d);
float edge  = depth * rim;
```
`core/trails/trail_system.c`: `float mask[4] = {1.63f, 0.85f, 0.34f, 1.75f};`
(`.x` lát dọc sheet 2 · `.y` luỹ thừa depth · `.z` độ mềm biên · `.w` mật độ)

`core/composition/common/vc_smoke_column.inl`: `tubeRadialSegs = 16`,
`tubeMaxRings = 40`, `tubeSingleSided = false`.

Build sạch. `./scripts/run_core_tests.sh` → **32/35**; ba suite hỏng
(`swept_trail`, `tube_frame`, `volume_trail`) **đã hỏng từ trước**, không liên quan.

---

## ĐÃ CHỨNG MINH — đo được, đừng đo lại

`core/tests/silhouette_test.c` là rasterizer phần mềm (không GPU) dựng hình trụ
và hình lập phương rồi **đo bước nhảy alpha lớn nhất tại biên**. Chạy:
`cc -std=c11 -O1 -o /tmp/sil core/tests/silhouette_test.c -lm && /tmp/sil`

| | |
|---|---|
| Không xử lý gì | **0.47** (mốc so sánh) |
| Hai mặt, `p = 0.75` (đang chạy) | 0.365 |
| Chỉ cull mặt sau, `p = 0.75` | 0.274 — **vẫn hỏng** |
| Cull + `p = 2` | **0.119** |

**Hai phát hiện cốt lõi:**

1. **Một số hạng per-fragment KHÔNG THỂ làm tan đường bao khi vẽ hai mặt.** Càng
   gần biên, tia nhìn càng lướt dọc bề mặt nên xuyên qua càng nhiều mặt; số lần
   cắt tăng **nhanh hơn** mức số hạng giảm, nên alpha tích luỹ **cao ở rìa hơn ở
   giữa**. Không hằng số nào cứu được.

2. **Luỹ thừa phải ≥ 2.** Gần rìa `|N·V| = √(1−(x/R)²) ≈ √(2R)·√(R−x)`, tức rời
   đường bao với **đạo hàm vô hạn**. Bình phương triệt tiêu căn và làm độ dốc
   **tuyến tính theo khoảng cách trên màn hình**. `0.75 < 1` làm chỗ dốc **tệ
   hơn**.

3. **Tessellation KHÔNG phải câu trả lời** — 6 → 48 vành làm metric *xấu đi*
   (0.467 → 0.552) khi hai lỗi trên còn nguyên.

4. **Fresnel không thể làm mềm biên hình lập phương** — cạnh gấp không phải tiếp
   tuyến. Giữ trong suite làm ca đối chứng.

**Chưa áp vào shader**, vì lần áp trước đi kèm mấy thay đổi dựa trên số liệu
nhiễm, và kết quả trên màn hình **tệ đi**. Áp lại cần một phép đo sạch từ app.

---

## ĐÃ BÁC BỎ — đừng thử lại

| Giả thuyết | Vì sao sai |
|---|---|
| **Dùng `dFdx/dFdy` thay pháp tuyến đỉnh** (Gemini đề xuất) | Đã thử. Cho pháp tuyến **phẳng theo từng tam giác** → **lỗi ô vuông** nhìn thấy rõ. Và tiền đề của nó sai: biến dạng làm ở **CPU**, và `pm_tube.inl:322` **đã dựng lại pháp tuyến** từ lưới đã bẻ, **mỗi khung hình**. |
| **Sàn `sin(π/N)` do mặt cắt là đa giác** | Đo ra `\|N·V\|` tại đỉnh đường bao = 0.000 (lý tưởng) / 0.033 (biến dạng). Nó **có** về 0. |
| **Thuộc tính `vertexNormal` không tới shader** | Probe đo `length(fragNormal)` = **233 phẳng khắp mặt** → tới nơi, độ dài không đổi. |
| **Có post-process đổi màu** | `colour_probe` đo shader mặc định: xám (128,128,128) → (128,128,128) **OK**, đỏ → **OK**. Đường ống trung thực. |
| **Bỏ `rlPushMatrix()` rỗng trong `pm_tube.inl`** | Vô hại nhưng **không phải nguyên nhân**: `MyBeginMode3D` tự nhân `matView` vào transform, nên mọi draw immediate-mode trong pass 3D đều có `matModel = model × view`. |

---

## MANH MỐI CÒN MỞ — đáng theo tiếp

Probe fresnel (phím **I**) vẽ hình trụ trơn của engine, tô bằng hai cách đọc, rồi
**tự quét pixel in ra số**. Lần chạy cuối:

```
viewPos-frag  188 193 192 190 185 179 171 160 146 129 108  80  42   3   0  45 133
-frag (view)  153 188 197 201 203 202 200 197 191 185 176 165 147 124  91  42   0
|fragNormal|  233 233 233 ... 233   (phẳng)
```

Hai hình trụ đặt **lệch sang hai bên** điểm nhìn, nên với camera phối cảnh **đỉnh
sáng phải lệch về phía tâm màn hình** — đó là hình học đúng, không phải lỗi.

- Cột **`-fragPosition`** (view space): đỉnh ở ~25% tính từ trái, tức **lệch về
  tâm màn hình** → **đúng chiều**.
- Cột **`viewPos - fragPosition`**: đỉnh ở ~6%, **lệch ra ngoài** → **sai chiều**.

Khớp với `ENGINE_LANDMINES §9`: `matModel = model × view` trong pass 3D, nên
`fragPosition` là **view space**, `viewPos` là **world space**, và phép trừ đó
trộn hai hệ — ở **mọi shader trong dự án**, không riêng cột khói.

**Chưa kết luận được**, vì logic chấm điểm của probe ("đỉnh trong vòng 10% quanh
tâm") **sai với hình trụ đặt lệch trục**. Việc đầu tiên nên làm:

> Đặt **một** hình trụ **đúng trên trục nhìn** (`cam.target`), chạy cả hai cách
> đọc, và đòi hỏi một **vòm đối xứng**. Đó là ca duy nhất mà tiêu chí "đỉnh ở
> giữa" đúng nghĩa.

Nếu `-fragPosition` thắng thì **cả 5 fresnel đang chạy** (`plasma_shell`,
`crystal`, `aura_shell`, `effect_material`, `water_splash`) đều đang sai — một
phát hiện cấp engine, cần ghi vào `ENGINE_LANDMINES.md`.

---

## Ý tưởng chưa thử, và nó có triển vọng nhất

**Phá biên bằng noise (alpha erosion).** Đây là hướng duy nhất **đi vòng qua**
điều đã chứng minh ở §"Đã chứng minh" mục 1: nếu không cần *làm tan* biên mà làm
nó **rách ngẫu nhiên** thì bài toán tích luỹ biến mất — không còn một đường liền
để mắt bắt. Khói thật đúng như vậy: nó tơi ra, không có biên mờ hoàn hảo.

Rẻ: dùng luôn sheet đang sample (`s1.a`), không thêm texture.

```glsl
float organicEdge = smoothstep(0.1, 0.8, edge * s1.a);
```

**CẢNH BÁO VỀ CÁCH ĐO.** Metric hiện tại (`bước nhảy alpha lớn nhất tại biên`)
sẽ **xấu đi** khi biên gồ ghề, dù mắt thấy **mềm hơn**. Phải định nghĩa metric
mới trước — ví dụ **độ biến thiên vị trí đường bao dọc thân** — rồi mới thêm
`EDGE_NOISE` vào `silhouette_test.c`.

---

## Công cụ có sẵn — dùng, đừng dựng lại

**Máy này KHÔNG chạy được game** (`FATAL: RLVK: instance creation failed` — không
có display session). Nhưng người dùng chạy được, và **ảnh/log thì agent tự đọc**.
Đừng bắt người dùng mô tả bằng lời.

| | |
|---|---|
| `core/tests/silhouette_test.c` | Rasterizer CPU, đo độ gắt của biên. Không GPU. |
| `sandbox/colour_probe.c` — phím **O** | Ghi 4 ô màu hằng số qua shader mặc định và qua `trail_volume.fs`, **đọc pixel in ra số**. |
| `sandbox/fresnel_probe.c` — phím **I** | Hình trụ trơn + fresnel, tự quét và tự chấm. |
| `AutoTest_SaveScreenshotWorld` — phím **P** | Ảnh **cắt sát vùng hiệu ứng**. `[` `]` thu/phóng. |
| `volume_debug` trong `tuning.cfg` | 1 = `edge` · 2 = `\|N·V\|` · 3 = `pattern` · 4 = fade dọc · 5 = `length(fragNormal)` · 7 = `\|fragPosition\|/40` · 8/9 = màu hằng số |
| `smokecolumn_freeze = 1` | Đóng băng **riêng lưới**, sheet vẫn trượt — tách hai chuyển động. |

---

## LUẬT LÀM VIỆC — học bằng máu trong phiên trước

Phiên trước mất gần trọn buổi vì **bốn lần cái thước sai trước cái nó đo**. Đọc
kỹ, đây là phần giá trị nhất của bản bàn giao:

1. **KIỂM CÔNG CỤ TRƯỚC KHI TIN SỐ ĐO.** Rasterizer đầu tiên có răng cưa lấn át
   phép đo. Metric quét cả hàng nên báo một đường nối tam giác ở **giữa thân**
   khi biên đã sửa xong. Debug view nằm **dưới hai lệnh `discard`** nên chỉ vẽ
   những fragment sống sót. Probe đầu tiên bắt trúng **phông** thay vì hình trụ.
   Bốn lần.

2. **DEBUG VIEW PHẢI CẮT NGANG SHADER.** Hằng số `return` ngay đầu `main()`, và
   **mọi `discard` bị chặn** khi đang debug. Nếu không, nó trả lời một câu hỏi
   khác với câu đang hỏi.

3. **ĐỔI MỘT BIẾN MỖI LẦN.** Có lúc đổi ba thứ cùng lúc (ngưỡng + advection + hạ
   tần số) rồi không biết cái nào gây hại. `silhouette_test` cho thấy `cull` và
   `p=2` **không cái nào chạy được nếu thiếu cái kia** — nên tìm kiếm một-biến
   -một-lần sẽ **không bao giờ tìm ra**. Phải suy ra, không mò.

4. **MỘT UNIFORM CHƯA SET KHÔNG BÁO LỖI.** Nó đọc ra `(0,0,0)`, cho ra một vector
   đơn vị hợp lệ, một gradient trông hợp lý — chỉ nằm sai chỗ. `viewPos` **chỉ**
   được bind bởi `SkillManager_BeginShader()`; `core/trails/` dùng
   `BeginShaderMode()` thô.

5. **rlgl GOM LÔ.** Đặt uniform giữa các lệnh vẽ mà không
   `rlDrawRenderBatchActive()` thì **chỉ giá trị cuối cùng** được dùng cho tất
   cả. Ba hình trụ từng đọc ra cùng một số vì lý do này.

6. **TOẠ ĐỘ CỬA SỔ ≠ PIXEL FRAMEBUFFER.** `GetWorldToScreen` trả về toạ độ cửa
   sổ, `LoadImageFromScreen` trả về pixel framebuffer — Retina chênh **2 lần**.

7. **PASS 3D CÓ TONEMAP.** Trắng 1.0 đọc về **233**, không phải 255. Trị tuyệt
   đối từ probe vẽ trong pass 3D không đáng tin; chỉ **biến thiên** mới đọc được.

8. **ĐỪNG ĐỔ LỖI CHO BACKEND SỚM.** Đã đổ cho rlvk hai lần, cả hai lần đều là lỗi
   trong code của chính mình.

---

## Đọc gì trước

1. `core/trails/shaders/trail_volume.fs` — đọc hết phần comment đầu file.
2. `core/tests/silhouette_test.c` — comment đầu file giải thích metric và vì sao.
3. `ENGINE_LANDMINES.md` §1 (flush lô), §8 (UBO theo instance), §9 (`matModel`).
4. `core/CLAUDE.md` §"Debug & testing workflow" — bảng "câu hỏi này thuộc loại
   nào" ở §1 đúng ra đã tiết kiệm được cả buổi.
