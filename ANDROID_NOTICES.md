# Android Build & Porting Notices (Wuxing Skills)

# clean android
make -f Makefile.Android clean

# build android
make -f Makefile.Android
make -f Makefile.Android USE_VULKAN=1


# pair
adb pair <IP>:<pairing_port>

# connect
adb connect <IP>:<main_port>

# install
adb install -r wuxing_skills.apk

# crash log
adb logcat -c
adb logcat raylib:V "*:S"
adb logcat -b crash | ~/Library/Android/sdk/ndk/28.2.13676358/ndk-stack -sym android.wuxing_skills/obj

This document records the full process, configuration, and hardware/driver-specific bugs
encountered while porting this C/Raylib project to Android.

## 1. Environment & Core Architecture
- **App architecture:** Uses pure `NativeActivity` (plain C) to run Raylib, entirely dropping the
  Java layer (`NativeLoader`, `MainActivity.java`) to minimize overhead and avoid convoluted init
  bugs.
- **Build tooling:** The build is automated via `Makefile.Android`. Requires:
  - **Android NDK** (provides the `aarch64-linux-android31-clang` compiler, sysroot, and
    `libandroid`, `liblog`, `libEGL`, `libGLESv2`, `libOpenSLES` libraries).
  - **Android SDK Build-Tools** (provides `aapt` for APK packaging, `zipalign` for RAM
    optimization, and `apksigner` for signing).

## 2. Build Process
Run the following from the project root:
```bash
# Clean old build/object files to avoid conflicts
make -f Makefile.Android clean

# Compile all .c sources into libmain.so, package resources, and produce the .apk
make -f Makefile.Android
```
*(Note: the Makefile automatically handles directory creation, asset copying, shader conversion,
dynamic-library linking, and signing the APK with a local keystore.)*

**No need to `clean` on every build (since 2026-07-18):** the object rule now has `-MMD -MP` +
`-include $(OBJS:.o=.d)`, so editing a `.h` file (without touching any `.c` directly) now correctly
triggers a recompile of every `.c` that includes that header — previously, incremental builds only
tracked the mtime of the `.c` file itself, so editing a `.h` and rebuilding silently having "no
effect" was a real bug, not a false impression. `clean` (or manually clearing the cache) is still
required in cases Make can't auto-detect:
- Toggling `USE_VULKAN`: `compile_raylib_android` and `compile_shaderc_android` only build if the
  target archive **doesn't already exist** — flipping the flag doesn't invalidate the old cache
  (see §D2 and `third_party/vulkan/docs/HANDOFF.md` §7.18).
- Editing `Makefile.Android` itself (new CFLAGS/LDLIBS) — old object files don't know the compile
  flags changed.

## 3. Critical Graphics Notes on Android (GLES 2.0 / Mali / Adreno)

Bringing Wuxing Skills to Android ran into extremely strict mobile hardware limits. Strictly follow
these rules when adding any new effect:

### A. Geometry Batch Overflow
- **Symptom:** water surfaces get clipped at right angles, spheres/cylinders lose half their
  geometry, or details break apart randomly.
- **Cause:** Raylib's `rlBegin()`/`rlEnd()` uses a default 8192-vertex buffer. On PC, if the buffer
  fills mid-Quad/Triangle, OpenGL is fairly lenient. On mobile GPUs, the driver will **shred and
  discard** the entire buffer once it has a leftover/odd vertex, corrupting the geometry.
- **Mandatory fix:** whenever manually drawing meshes in a loop, you **MUST** call
  `rlCheckRenderBatchLimit(vertex_count)` right BEFORE `rlBegin()`.
  - *Example:* `rlCheckRenderBatchLimit(rings * slices * 4);`

### B. Math Fault Causing a Hard GPU Crash
- **Symptom:** the game crashes immediately when casting a skill (e.g. Hoa Long Phong Ba).
- **Cause:** the phone's GPU triggers a hardware fault (Segmentation Fault / SIGSEGV) if you call
  `normalize(vec3(0.0))` in GLSL. In TBN (Tangent-Bitangent-Normal) mesh-deformation code, using
  `cross(vec3(0,1,0), fragNormal)` at a sphere's pole (where `fragNormal` also points `(0,1,0)`)
  produces `cross == vec3(0)`.
- **Mandatory fix:** always compute into a temporary and check the length via `length()` before
  calling `normalize()`.
  ```glsl
  vec3 tangent = cross(vec3(0.0, 1.0, 0.0), fragNormal);
  if (length(tangent) < 0.1) tangent = cross(vec3(1.0, 0.0, 0.0), fragNormal);
  tangent = normalize(tangent); // Safe
  ```

### C. Shader Conversion (PC to Android)
The project's shaders were originally written for PC (`#version 330`). Android uses OpenGL ES 2.0
(`#version 100`).
- The project uses `scripts/convert_shaders_to_gles.py` to automatically convert them during the
  build (turns `in/out` into `attribute/varying`, adds `precision highp float`, etc.).
- **Texture function note:** GLSL 330 uses `texture()`, but GLSL 100 requires `texture2D()`. The
  Python script handles this automatically (regex-replaces `texture(` with `texture2D(`). **DO
  NOT** manually edit files under `android.wuxing_skills/assets/` and type `texture()` by hand —
  doing so makes the shader fail to compile silently, causing Raylib to fall back to its default
  shader (the visible symptom: a skill's color is wrong, e.g. a yellow Metal skill turning
  orange-red).

### D2. raylib-CMake landmine (2026-07-14 — cause of the BLACK SCREEN + game crash)
Two CMake flags used when building `libraylib.a` for Android caused 2 fatal bugs, both fixed in
`Makefile.Android` (the `compile_raylib_android` target) — **DO NOT re-add them**:
1. **`-DCUSTOMIZE_BUILD=ON` = entire app renders a black screen.** On raylib 6.0 this flag flips a
   `SUPPORT_*` default so `EndDrawing()` no longer swaps the buffer and no longer caps FPS: the app
   initializes GL normally, the loop runs at ~1000fps, but no frame is ever presented
   (SurfaceFlinger sees the layer as `buffer=0x0, 0.00Hz`). Same bug previously seen on desktop
   (`core/docs/PROGRESS.md` Item 41).
2. **`-DGRAPHICS=GRAPHICS_API_OPENGL_ES3` has NO effect** — raylib's CMake unconditionally
   overrides `GRAPHICS=ES2` when `PLATFORM=Android`. This causes an ES2 build where instancing uses
   extension function pointers (EXT/ANGLE) that the Mali GLES 3.2 driver doesn't advertise → NULL
   pointer → **SIGSEGV at pc 0x0 inside `DrawMeshInstanced`**, right when the first instanced VFX
   draws (e.g. entering Game, GlacialCannon). Correct flag: `-DOPENGL_VERSION="ES 3.0"`. Logcat
   symptom of an accidental ES2 build: `GL: VAO extension detected` instead of using the core VAO
   path.

**After changing raylib flags, the cache must be cleared** (the Makefile only builds raylib if
`libraylib.a` is missing):
```bash
rm -rf android.wuxing_skills/raylib_build android.wuxing_skills/lib/arm64-v8a/libraylib.a
```

Quick debug when suspecting "the app is running but not showing anything":
`adb shell dumpsys SurfaceFlinger` — the app's layer must show a buffer + frameRate > 0;
`buffer=0x0` means `eglSwapBuffers` never queued a frame (a platform-layer bug, not a shader bug).

### D3. Online (EOS) on Android = stub
`Makefile.Android` links `net/net_eos_stub.c` → `Net_OnlineAvailable() == false`, so the "HOST
ONLINE" / "JOIN CODE" buttons are disabled **by design**. `third_party/eos-sdk` currently only ships
Win/Mac/Linux binaries. To get real online play on Android: download "EOS SDK for Android" (Epic
portal) — includes `EOSSDK.aar` (libEOSSDK arm64 + required Java classes) — then add a Java/dex
layer to the APK packaging process (the app currently has `hasCode=false`, pure C) + initialize
`EOS_Android_InitializeOptions` with the JavaVM from NativeActivity. This is a separate, not-yet-done
item. LAN (ENet `--host/--join`) is already linked but has no IP-entry UI on Android yet.

### D. No Compute Shaders
- Older Android versions (and GLES 2.0) don't support Compute Shaders.
- All of the project's Particle/Vortex/Force Field logic is currently **CPU-based**. The C loop
  (capped at 2000 particles) runs in under 0.1ms and runs smoothly on Android without needing
  Compute Shaders.
