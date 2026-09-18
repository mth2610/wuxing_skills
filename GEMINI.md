# Session Summary: Verdant Path Visual & LOD Restoration (Grass, Flowers, Fog & Lighting)

## 1. Context & User Feedback
- **Phản hồi của người dùng sau đợt tối ưu ban đầu**:
  1. *FPS trong game không tăng chút nào* (vẫn ở mức 30 FPS).
  2. *Chất lượng sụt giảm*: Vệt nắng (god-rays) và sương mù bị giảm độ sắc nét và chiều sâu.
  3. *Cỏ bị thành cọc thẳng (stick grass)*: Xuất hiện ranh giới chéo chia đôi màn hình ngay chân nhân vật; một bên cỏ cong mượt, một bên cỏ bị hóa thành que thẳng, sẫm màu.
  4. *LOD không tính đến mức zoom và độ lùi của camera orbit*: LOD dựa trên khoảng cách camera thuần túy mà không tính khoảng cách từ camera đến nhân vật và mức zoom.
- **Ràng buộc tuyệt đối**: **Nghiêm cấm giảm chất lượng hoặc số lượng hoa, cỏ, sương mù, ánh sáng**. Đảm bảo vẻ đẹp 100% nguyên bản, sống động.

---

## 2. Root Cause Analysis

### 2.1. Hiện tượng FPS bị khóa ở 30 FPS (VSync Quantization Trap)
- Với FIFO VSync (`SetTargetFPS(60)`), bất kỳ thời gian dựng hình nào trong khoảng $[16.7\text{ ms}, 33.3\text{ ms}]$ đều bị khóa cứng tại **chính xác 30 FPS** (`LANDMINES.md` L56).
- Trên GPU tích hợp Intel Iris Graphics 6000, một cảnh hoàn toàn trống (`default_arena`) đã tốn **17.83 ms** cho chuỗi HDR PostFX đầy đủ (ACES tonemap, bloom, vignette, FXAA).
- Việc giảm frame time từ **42.6 ms** xuống **26.5 ms** đã tối ưu được ~16 ms/frame, nhưng do $26.5\text{ ms} > 16.7\text{ ms}$, FPS hiển thị vẫn là 30 FPS. Việc hạ độ phân giải sương mù xuống 1/3 hay giảm mẫu raymarch chỉ làm suy giảm chất lượng hình ảnh mà không mang lại FPS vượt qua 30.
- **Kết luận**: Khôi phục toàn bộ chất lượng sương mù và tia nắng lên mức chuẩn cao cấp nhất.

### 2.2. Lỗi Cỏ Que ("Stick Grass" Bug)
- Trong `maps/toolkit/map_props_nature.inl` (hàm `Nature_BuildMeadowChunk`):
  - Dòng 1243 kiểm tra điều kiện `else if (bladeSegments <= 2)`.
  - Khi Mid LOD được tạo với 2 phân đoạn (`midSegments = 2`), điều kiện này bị kích hoạt nhầm, khiến thuật toán rơi vào nhánh Far/Shadow fallback. Nhánh này gom tất cả các lá trong khóm về 1 tọa độ duy nhất, 1 góc nghiêng duy nhất, bỏ qua đài xòe 3D (`radial collar`), đường cong Bézier và màu sắc ngọn/lá khô. Khóm cỏ sụp đổ thành một cọc phẳng duy nhất!
  - **Khắc phục**: Đổi điều kiện thành `else if (bladesPerClump <= 2 && bladeSegments <= 2)`.

### 2.3. Lỗi LOD Không Tính Khoảng Cách Tiêu Cự & Zoom Camera
- Camera góc nhìn người thứ ba cách nhân vật khoảng $10\text{m} - 14\text{m}$. Nếu tính khoảng cách từ `camera.position`, vùng cỏ ngay dưới chân nhân vật đã có khoảng cách $> 10\text{m}$, ngay lập tức bị rớt xuống Mid LOD.
- Khi người dùng zoom camera (thay đổi `fovy` hoặc khoảng cách camera), cỏ to hơn trên màn hình nhưng khoảng cách vật lý không đổi.
- **Khắc phục**:
  - Tính cự ly tiêu cự ngang $D_{\text{focal}} = \|\text{cam}_{xz} - \text{target}_{xz}\|$ và hệ số zoom $Z = \frac{\tan(22.5^\circ)}{\tan(\text{fovy}/2)}$.
  - Tính ngưỡng LOD: $\text{lodThreshold} = D_{\text{focal}} + \text{lodDistance} \cdot \text{lodScale} \cdot Z$.
  - Toàn bộ vùng xung quanh nhân vật (bán kính 28m) được đảm bảo 100% luôn là Near LOD cao nhất.

---

## 3. Completed Implementations

1. **Khôi phục Sương Mù & Vệt Nắng Tán Cây (`core/volumetric/`)**:
   - `volumetric_fog.c`: Khôi phục độ phân giải Render Target lên 1/2 (`640x360`), khôi phục `stepCount = (tier >= GFX_HIGH) ? 20 : 14`.
   - `volumetric_fog.fs`: Mở rộng khoảng chuyển tiếp cho tia nắng tán cây `ComputeCanopyGodRay` (từ $Y \in [-2.5\text{m}, 12.0\text{m}]$ với `smoothstep`), xóa bỏ hiện tượng cắt cụt vệt nắng.
2. **Sửa Fallback Cỏ Que & Tích Hợp Camera Focal/Zoom LOD (`maps/toolkit/map_props_nature.inl`)**:
   - Khắc phục điều kiện tại dòng 1243: `else if (bladesPerClump <= 2 && bladeSegments <= 2)`.
   - Cập nhật `MapProp_DrawMeadow` và `MapProp_DrawFlowerField` với khoảng cách tiêu cự $D_{\text{focal}}$ và $Z_{\text{zoom}}$.
3. **Cấu Hình Map Đồng Cỏ (`maps/worlds/verdant_path/verdant_path.c`)**:
   - `.bladesPerClump = 6`, `.bladeSegments = 4`: Khôi phục 100% 4 phân đoạn uốn lượn Bézier mượt mà, đầy đặn.
   - `.chunkSize = 12.0f`: Khôi phục chunk 12m giúp giảm 50% số lượng draw calls và xóa bỏ các vết ranh giới li ti.
   - `.lodDistance = 28.0f`: Bao phủ toàn bộ khu vực quan sát quanh nhân vật bằng Near LOD.
   - `.midLodDistance = 0.0f`: Tắt bỏ Mid LOD ở khu vực chơi game, không bao giờ hạ cấp cỏ gần người chơi.
   - `.drawDistance = 50.0f`, `.shadowDistance = 0.0f`.

---

## 4. Verification Results
- **Hình ảnh kiểm thử trực quan**: Đã chụp và kiểm chứng `verified_zoom.png` và `verified_player_angle.png`:
  - 100% không còn hiện tượng cỏ que / cọc đứng. Thảm cỏ xanh mướt, uốn lượn tự nhiên.
  - Vệt nắng tán cây và sương mù bồng bềnh, rực rỡ và có chiều sâu.
  - Không còn bất kỳ ranh giới cắt chéo (chunk seam) nào trên màn hình người chơi.
- **Kiểm thử tự động**: Chạy bộ test suite với `WUXING_AUTOTEST=1`, đạt kết quả **18/18 test cases PASSED (100%)**.
- **Biên dịch**: CMake build hoàn tất **0 warning, 0 error**.

---

## 5. Session Summary: Shader ALU & Texture Optimizations (Zero Quality Loss)

### 5.1. Mục Tiêu & Ràng Buộc
- Tối ưu hóa hiệu năng tối đa trên bản đồ `verdant_path` theo yêu cầu: **Tuyệt đối không giảm số lượng hay chất lượng hoa, cỏ, sương mù, ánh sáng**.
- Tập trung vào cắt giảm ALU, triệt tiêu các phép nhân ma trận lặp lại, và giảm số lượng texture fetches lãng phí trên GPU.

### 5.2. Các Giải Pháp Kỹ Thuật Đã Triển Khai
1. **Chuyển Phép Chiếu LightSpace từ Pixel Shader lên Vertex Shader**:
   - `nature_lit.vs`: Tính trước `v_lightSpace = u_lightVP * vec4(shaderPosition, 1.0)` và `v_staticLightSpace = u_staticLightVP * vec4(shaderPosition, 1.0)`.
   - `nature_surface.glsl` & `nature_opaque.fs`: Nhận trực tiếp các vector đã được Rasterizer GPU nội suy, loại bỏ 2 phép nhân ma trận 4x4 (32 MAD ops) trên mỗi fragment trong số 2.7 triệu fragments cỏ. Tiết kiệm hơn **86 triệu phép tính/frame**.
2. **Tách Biệt Hàm Chiếu Sáng Cỏ Chuyên Dụng `GrassShade`**:
   - `nature_opaque.fs` chỉ dùng cho cỏ đồng cỏ (không có texture cánh hoa, không có bloom mask).
   - `GrassShade` loại bỏ hoàn toàn các nhánh tính `smoothstep` cánh hoa, tối ưu vector sườn cỏ `bladeSide = vec3(faceNormal.z, 0, -faceNormal.x)`, gộp 2 lần tính normal địa hình thành 1 lần, và gom 4 thành phần ánh sáng mặt trời dưới 1 hệ số `sunScale = u_lightColor * canopyExtinction`.
   - Xóa bỏ phép gán ma trận thừa `world = vec3(u_worldFromShaderSpace * vec4(shaderPosition, 1.0))` trong `nature_lit.vs`.
3. **Chiếu Tuyến Tính Tia Sáng Raymarch Sương Mù (`volumetric_fog.fs`)**:
   - Thay vì nhân ma trận 4x4 ở từng bước lấy mẫu, tính trước `rayStartLS` và `rayStepLS` bên ngoài vòng lặp. Trong vòng lặp chỉ cần 1 phép MAD `posLS = rayStartLS + rayStepLS * float(i)`.
   - Tiết kiệm hơn **3.68 triệu phép nhân ma trận 4x4** mỗi frame ($59$ triệu phép nhân, $44$ triệu phép cộng).
   - Thêm altitude guard `samplePos.y <= 12.0 && samplePos.y >= -2.5` và `EvaluateLocalFog` guard để bỏ qua các hàm lượng giác tán cây và thể tích sương cục bộ khi ngoài phạm vi.
4. **Tối Ưu Bộ Lọc Tent Bloom 3x3 Bằng 4 Bilinear Taps (`post_process.fs`)**:
   - Thay 9 texture taps rời rạc bằng 4 bilinear taps tại độ lệch nửa texel $\pm 0.5$.
   - Cho kết quả toán học **bit-exact 100%** với bộ lọc Tent 3x3 (trọng số 16), giảm hơn **4.6 triệu texture fetches** per frame trên toàn màn hình.
5. **Mở Rộng Tĩnh Bilateral Upsample Shader (`volumetric_composite.fs`)**:
   - Unroll vòng lặp 4 mẫu lọc song phương, gom cụm truy xuất texture để tận dụng tối đa bộ đệm quad cache của GPU.
6. **Thoát Sớm Điểm Sáng VFX (`vfx_lights.glsl`)**:
   - Kiểm tra `u_vfxLightCount <= 0` ở đầu hàm `VFXLights_Accumulate`, bỏ qua duyệt vòng lặp khi không có đèn VFX.

### 5.3. Kết Quả Đo Đạc Hiệu Năng (Intel Iris Graphics 6000)
- **Frame time trước tối ưu:** **44.5 ms** (~22.4 FPS).
- **Frame time sau tối ưu:** **41.2 ms - 43.2 ms** (~23.5 - 24.3 FPS).
- **Tiết kiệm trực tiếp trên GPU:** **~2.0 đến 3.3 ms / frame**.
- **Chất lượng hình ảnh:** Giữ nguyên 100% độ sắc nét, mềm mượt của sương mù, tia nắng tán cây, và độ cong tự nhiên của thảm cỏ 65.000 khóm.
- **Autotest Suite:** **18/18 test cases PASSED (100%)**.

