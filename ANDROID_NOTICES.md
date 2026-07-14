# Android Build & Porting Notices (Wuxing Skills)

# clean android
make -f Makefile.Android clean

# build android 
make -f Makefile.Android

# pair
adb pair <IP>:<Port_ghép_nối>

# connect 
adb connect <IP>:<Port_chính>b 

# intasll
adb install -r wuxing_skills.apk

# log crash
adb logcat -c
adb logcat raylib:V "*:S"
adb logcat -b crash | ~/Library/Android/sdk/ndk/28.2.13676358/ndk-stack -sym android.wuxing_skills/obj

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
make -f Makefile.Android
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

### D2. Landmine raylib-CMake (14/07/2026 — nguyên nhân MÀN HÌNH ĐEN + crash Game)
Hai cờ CMake khi build `libraylib.a` cho Android đã gây 2 lỗi chí mạng, đều đã
fix trong `Makefile.Android` (mục `compile_raylib_android`) — **ĐỪNG thêm lại**:
1. **`-DCUSTOMIZE_BUILD=ON` = màn hình đen toàn app.** Trên raylib 6.0 cờ này
   lật default một `SUPPORT_*` khiến `EndDrawing()` không swap buffer + không
   giới hạn FPS: app init GL bình thường, loop chạy ~1000fps, nhưng không có
   frame nào được present (SurfaceFlinger thấy layer `buffer=0x0, 0.00Hz`).
   Cùng bug từng gặp ở desktop (CORE_ISSUES.md Item 41).
2. **`-DGRAPHICS=GRAPHICS_API_OPENGL_ES3` KHÔNG có tác dụng** — raylib CMake
   ghi đè GRAPHICS=ES2 vô điều kiện khi PLATFORM=Android. Hệ quả build ES2:
   instancing dùng con trỏ hàm extension (EXT/ANGLE) mà driver Mali GLES 3.2
   không quảng cáo → con trỏ NULL → **SIGSEGV pc 0x0 trong `DrawMeshInstanced`**
   ngay khi VFX instanced đầu tiên vẽ (vd vào Game, GlacialCannon). Cờ đúng:
   `-DOPENGL_VERSION="ES 3.0"`. Dấu hiệu build ES2 nhầm trong logcat:
   `GL: VAO extension detected` thay vì dùng VAO core.

**Sau khi đổi cờ raylib phải xoá cache** (Makefile chỉ build raylib khi thiếu
`libraylib.a`):
```bash
rm -rf android.wuxing_skills/raylib_build android.wuxing_skills/lib/arm64-v8a/libraylib.a
```

Debug nhanh khi nghi "app chạy mà không hiện": `adb shell dumpsys SurfaceFlinger`
— layer của app phải có buffer + frameRate > 0; `buffer=0x0` nghĩa là
eglSwapBuffers chưa bao giờ queue frame (lỗi tầng platform, không phải shader).

### D3. Online (EOS) trên Android = stub
`Makefile.Android` link `net/net_eos_stub.c` → `Net_OnlineAvailable()==false`,
nút "TAO PHONG ONLINE"/"NHAP MA VAO PHONG" bị vô hiệu **by design**.
`third_party/eos-sdk` hiện chỉ có binary Win/Mac/Linux. Muốn online thật trên
Android: tải "EOS SDK for Android" (Epic portal) — gồm `EOSSDK.aar`
(libEOSSDK arm64 + Java classes bắt buộc) — rồi thêm tầng Java/dex vào quy
trình đóng gói APK (app hiện `hasCode=false`, C thuần) + init
`EOS_Android_InitializeOptions` với JavaVM từ NativeActivity. Đây là hạng mục
riêng, chưa làm. LAN (ENet `--host/--join`) đã link sẵn nhưng chưa có UI nhập
IP trên Android.

### D. Không có Compute Shaders
- Android phiên bản cũ (và GLES 2.0) không hỗ trợ Compute Shaders.
- Mọi logic Particle, Vortex, Force Fields hiện tại của dự án đều là **CPU-based**. Vòng lặp C (giới hạn 2000 hạt) có tốc độ xử lý dưới 0.1ms và hoàn toàn có thể chạy mượt trên Android mà không cần đến Compute Shaders.
