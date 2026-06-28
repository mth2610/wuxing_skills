# Android Build & Porting Notices (Wuxing Skills)

Tài liệu này ghi chú lại toàn bộ quy trình, cấu hình và các lỗi phần cứng/trình điều khiển đặc thù đã gặp phải trong quá trình port dự án C/Raylib này sang nền tảng Android.

## 1. Môi trường & Kiến trúc cốt lõi
- **Kiến trúc ứng dụng:** Sử dụng hoàn toàn `NativeActivity` (C thuần) để chạy Raylib, loại bỏ hoàn toàn tầng Java (`NativeLoader`, `MainActivity.java`) nhằm tối giản overhead và tránh lỗi khởi tạo rườm rà.
- **Công cụ biên dịch:** Quá trình build được tự động hóa qua `Makefile.Android`. Yêu cầu có:
  - **Android NDK** (Cung cấp Clang compiler `aarch64-linux-android31-clang`, sysroot, và thư viện `libandroid`, `liblog`, `libEGL`, `libGLESv2`, `libOpenSLES`).
  - **Android SDK Build-Tools** (Cung cấp `aapt` để đóng gói APK, `zipalign` để tối ưu hóa bộ nhớ RAM, và `apksigner` để ký ứng dụng).

## 2. Quy trình Build
Thực thi các lệnh sau tại thư mục gốc của dự án:
```bash
# Dọn dẹp các tệp build và object cũ để tránh xung đột
make -f Makefile.Android clean

# Biên dịch toàn bộ mã nguồn (.c) thành libmain.so, đóng gói tài nguyên, và xuất file .apk
make -f Makefile.Android build
```
*(Ghi chú: Makefile tự động lo việc tạo thư mục, sao chép assets, chuyển đổi shader, biên dịch liên kết thư viện động, và ký APK với keystore cục bộ).*

## 3. Các lưu ý Sống còn về Đồ họa trên Android (GLES 2.0 / Mali / Adreno)

Quá trình đưa Wuxing Skills lên Android đã vấp phải các giới hạn cực kỳ khắt khe của phần cứng di động. Tuyệt đối tuân thủ các quy tắc sau khi tạo thêm hiệu ứng mới:

### A. Lỗi Tràn Bộ Đệm Hình Học (Geometry Batch Limit)
- **Triệu chứng:** Mặt nước bị cắt vuông góc, các khối cầu/trụ bị mất một nửa hình, hoặc các chi tiết bị gãy vụn ngẫu nhiên.
- **Nguyên nhân:** Raylib dùng `rlBegin()`/`rlEnd()` với bộ đệm mặc định là 8192 đỉnh. Trên PC, nếu bộ đệm đầy ngang chừng một Quad/Triangle, OpenGL xử lý khá xề xòa. Nhưng trên GPU Mobile, trình điều khiển sẽ **cắt nát và vứt bỏ** toàn bộ buffer bị lẻ đỉnh, gây hỏng hình học.
- **Giải pháp bắt buộc:** Bất cứ khi nào vẽ các khối lưới (Mesh) thủ công bằng vòng lặp, **BẮT BUỘC** phải gọi hàm `rlCheckRenderBatchLimit(số_đỉnh)` ngay TRƯỚC `rlBegin()`. 
  - *Ví dụ:* `rlCheckRenderBatchLimit(rings * slices * 4);`

### B. Lỗi Toán Học (Math Fault) Gây Crash Cứng GPU
- **Triệu chứng:** Game crash văng ra ngoài ngay lập tức khi tung chiêu (ví dụ: Hoa Long Phong Ba).
- **Nguyên nhân:** GPU trên điện thoại sẽ gây lỗi phần cứng (Hardware Segmentation Fault / SIGSEGV) nếu bạn gọi hàm `normalize(vec3(0.0))` trong GLSL. Trong kỹ thuật làm biến dạng mesh bằng TBN (Tangent-Bitangent-Normal), nếu dùng `cross(vec3(0,1,0), fragNormal)` tại đỉnh khối cầu (nơi `fragNormal` cũng hướng `(0,1,0)`), kết quả của `cross` sẽ là `vec3(0)`.
- **Giải pháp bắt buộc:** Luôn tính toán biến tạm, kiểm tra độ dài bằng `length()` trước khi `normalize()`.
  ```glsl
  vec3 tangent = cross(vec3(0.0, 1.0, 0.0), fragNormal);
  if (length(tangent) < 0.1) tangent = cross(vec3(1.0, 0.0, 0.0), fragNormal);
  tangent = normalize(tangent); // An toàn
  ```

### C. Chuyển đổi Shader (PC sang Android)
Shader gốc của dự án được viết cho PC (`#version 330`). Android sử dụng OpenGL ES 2.0 (`#version 100`).
- Dự án sử dụng script `scripts/convert_shaders_to_gles.py` tự động chuyển đổi trong quá trình build (đổi `in/out` thành `attribute/varying`, thêm `precision highp float`, v.v...).
- **Lưu ý Hàm Texture:** GLSL 330 dùng `texture()`, nhưng GLSL 100 bắt buộc phải dùng `texture2D()`. Script Python đã tự động xử lý việc này (Regex replace `texture(` thành `texture2D(`). **KHÔNG ĐƯỢC** tự ý vào thư mục `android.wuxing_skills/assets/` chỉnh sửa thủ công và gõ hàm `texture()` — thao tác này sẽ khiến shader biên dịch thất bại trong im lặng, khiến Raylib lùi về dùng shader mặc định (hậu quả là chiêu thức bị sai màu, VD: chiêu Metal màu vàng biến thành màu cam đỏ).

### D. Không có Compute Shaders
- Android phiên bản cũ (và GLES 2.0) không hỗ trợ Compute Shaders.
- Mọi logic Particle, Vortex, Force Fields hiện tại của dự án đều là **CPU-based**. Vòng lặp C (giới hạn 2000 hạt) có tốc độ xử lý dưới 0.1ms và hoàn toàn có thể chạy mượt trên Android mà không cần đến Compute Shaders.
