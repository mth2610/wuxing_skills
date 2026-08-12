cmake_minimum_required(VERSION 3.12)
project(wuxing_skills C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Thiết lập chế độ Build mặc định là Release cho mượt
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Cấu hình FetchContent để tự động tải Raylib
include(FetchContent)

# Tối ưu hóa build Raylib bằng cách tắt các module không cần thiết
set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "Build raylib games" FORCE)
# CUSTOMIZE_BUILD đã BỊ BỎ (2026-07, CORE_ISSUES.md Item 41): trên raylib 6.0 +
# macOS/Intel HD, build raylib với CUSTOMIZE_BUILD ON khiến cửa sổ init "thành
# công" nhưng vô hình (Dock icon có, không có cửa sổ) — một SUPPORT_* mặc định
# đổi giá trị so với 5.5. Dự án chưa từng set cờ SUPPORT_* tùy biến nào nên cờ
# này vô dụng; bỏ đi để raylib build theo default chuẩn (đã kiểm chứng hiện
# window bằng mini app cô lập). ĐỪNG bật lại mà không set kèm SUPPORT_* đúng.

# -----------------------------------------------------------------
# VULKAN BACKEND SWITCH (Lớp phân tách Renderer)
# -----------------------------------------------------------------
option(WUXING_USE_VULKAN "Build with Vulkan 1.1 backend (rlvk) instead of OpenGL" ON)

if(WUXING_USE_VULKAN)
    message(STATUS "[WUXING] Vulkan Backend ENABLED. Disabling Raylib's OpenGL backend (rlgl.c)...")
    add_compile_definitions(WUXING_USE_VULKAN)
    add_compile_definitions(GRAPHICS_API_VULKAN)
else()
    message(STATUS "[WUXING] OpenGL Backend ENABLED. Using native Raylib rlgl.")
endif()

# -----------------------------------------------------------------
# FRAME-TIME MEASUREMENT BUILD
# -----------------------------------------------------------------
# With vsync (FIFO present) + SetTargetFPS(60), FPS is QUANTIZED: a frame at 17 ms and a frame at
# 33 ms both report 30. Every partial optimization therefore looks like it "did nothing" until the
# very last one crosses 16.6 ms. This option switches the swapchain to IMMEDIATE present and drops
# the FPS cap, so the HUD shows the real frame cost and improvements are visible as they land.
#   cmake -S . -B build -DWUXING_PERF_CAPTURE=ON && cmake --build build -j4
# Turn it back OFF for normal play (uncapped present burns GPU and can tear).
option(WUXING_PERF_CAPTURE "Uncapped present + no FPS cap: measure real frame time, not vsync buckets" OFF)
if(WUXING_PERF_CAPTURE)
    message(STATUS "[WUXING] PERF CAPTURE build: vsync OFF, FPS cap OFF (measurement only)")
    add_compile_definitions(PERFORMANCE_CAPTURE)
endif()

# Tải Raylib phiên bản 6.0
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG        6.0
)
FetchContent_MakeAvailable(raylib)

if(WUXING_USE_VULKAN)
    # Tự động vá raylib source tree để sử dụng rlvk thay vì rlgl
    execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/rlvk_patch_raylib.py ${raylib_SOURCE_DIR})
endif()

if(WUXING_USE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_include_directories(raylib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan ${Vulkan_INCLUDE_DIRS})
endif()

if(WUXING_USE_VULKAN)
    # Ép CMake coi rlgl.c (chứa code OpenGL của Raylib) như một file Header, 
    # từ đó ngăn chặn trình biên dịch dịch file này thành mã máy.
    set_source_files_properties(${raylib_SOURCE_DIR}/src/rlgl.c PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

# ENet (Module 11 net transport — peer-hosted PvP). Small C89 lib, no deps.
FetchContent_Declare(
  enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG        v1.3.18
)
FetchContent_MakeAvailable(enet)

# -----------------------------------------------------------------
# BIÊN DỊCH TRỰC TIẾP FILE MAIN.C Ở THƯ MỤC GỐC
# -----------------------------------------------------------------

# Tự động quét và sinh registry cho skill mới trước khi build
execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_registry.py)
execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_map_registry.py)
execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_vfx_surface_registry.py)

# Texture packing is a HARD rule, not a convention: assets/TEXTURE_PACKING.md is
# enforced here, at configure time, so a non-conforming sheet cannot reach a
# build. The validator existed before this line and was wired to nothing — it
# had been failing on five profiles for an unknown length of time with nobody
# looking. Do not demote this to a warning.
execute_process(
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_vfx_surface_registry.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE WUXING_VFX_SURFACE_VALID)
if(NOT WUXING_VFX_SURFACE_VALID EQUAL 0)
    message(FATAL_ERROR
        "VFX surface registry failed validation — see the FAIL lines above and "
        "assets/TEXTURE_PACKING.md")
endif()

# Tự động quét toàn bộ mã nguồn của kỹ năng trong thư mục skills/
file(GLOB_RECURSE SKILL_SOURCES CONFIGURE_DEPENDS "skills/**/*.c")
# Tự động quét toàn bộ mã nguồn của map trong thư mục maps/
file(GLOB_RECURSE MAP_SOURCES CONFIGURE_DEPENDS "maps/**/*.c")

# Khai báo mục tiêu build ứng dụng chạy trực tiếp trên máy tính
add_executable(wuxing 
    main.c 
    # --- Environment System ---
    environment/environment_system.c
    environment/env_shadow.c
    # --- Core Systems ---
    core/map_manager.c
    core/resource_manager.c
    core/color_gradient.c
    core/float_curve.c
    core/skill_curve.c
    core/tuning.c
    core/emitter_system.c
    core/uv/flow_map.c
    core/deform/mesh_deform.c
    core/uv/uv_deform.c
    core/uv/surface_flow.c
    core/force_field.c
    core/camera_fx.c
    core/audio_system.c
    core/particles/particle_system.c
    core/particles/particle_manager.c
    core/particles/gpu/particle_gpu_backend.c
    core/fluid/fluid_impact.c
    core/fluid/fluid_pbd.c
    core/fluid/fluid_pbd_gpu.c
    core/fluid/fluid_surface.c
    core/path_spline.c
    core/post_fx.c
    core/ribbon_strip.c
    core/screen_distort.c
    core/surface_material.c
    core/gfx_quality.c
    core/atmosphere.c
    core/skill_manager.c
    core/sprite_anim.c
    core/trails/trail_system.c
    core/utils_math.c
    core/vfx_light.c
    core/vfx_contrast.c
    core/decals/decal_system.c
    core/geometry/procedural_mesh_utils.c
    core/geometry/mesh_cache.c
    core/material/material_system.c
    core/composition/visual_composer.c
    core/composition/vfx_sequence.c
    core/presets/vfx_presets.c
    core/skill_helper.c
    core/vfx_surface_registry.c
    core/time_fx.c
    core/motion_controller.c
    core/status_vfx.c
    core/afterimage.c
    core/mesh_adjacency.c
    core/shader_preprocessor.c
    core/metaball_fx.c
    core/debug_draw.c
    core/vulkan/wuxing_vulkan.c

    # --- Entities ---
    entities/entities.c
    ai/ai.c

    # --- Combat (Đấu Pháp: projectile registry + clash matrix) ---
    combat/combat.c

    # --- Control (player controller: intent layer) ---
    control/control.c

    # --- Boss (engine + one _def.c per boss) ---
    boss/boss_system.c
    boss/hac_dien_ton_gia_def.c

    # --- AI (minion brain — Module 8) ---
    ai/ai.c

    # --- UI (HUD + auto-targeting — Module 9) ---
    ui/ui.c
    ui/ui_lobby.c

    # --- Formations (Trận Pháp engine + one _def.c per formation — Module 10) ---
    formations/formation_system.c
    formations/cuu_thien_loi_dong_def.c
    formations/han_bang_thuy_tuyet_def.c

    # --- Net (wire-format core — Module 11; ENet transport gated on PvP) ---
    net/net.c
    net/net_transport.c

    # --- Character (model + animation rendering, counterpart to entities/'s
    # pure gameplay logic) ---
    character/character_model.c

    # --- Game Screen (production-bound; sandbox/ stays the dev harness) ---
    game/game_screen.c
    game/game_rules.c

    # --- Sandbox & Test Simulator ---
    sandbox/sandbox_core.c
    sandbox/ui_panel.c
    sandbox/vfx_test.c
    sandbox/skill_debugger.c
    sandbox/auto_test.c
    sandbox/colour_probe.c
    sandbox/fresnel_probe.c
    sandbox/auto_test_cases.c
    sandbox/visual_verify.c
    sandbox/pool_stats.c

    # --- Skills ---
    ${SKILL_SOURCES}
    # --- Maps ---
    ${MAP_SOURCES}
)

# Thêm root path để resolve includes có tiền tố core/ và skills/
target_include_directories(wuxing PRIVATE . core/geometry core/material core/composition core/presets)

if(WUXING_USE_VULKAN)
    target_include_directories(wuxing PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

# Tự động quét toàn bộ tài nguyên (Shaders/Textures) của kỹ năng và copy vào thư mục build
file(GLOB_RECURSE SKILL_ASSETS CONFIGURE_DEPENDS 
    "skills/**/*.fs"
    "skills/**/*.vs"
    "skills/**/*.png"
)
foreach(asset_path ${SKILL_ASSETS})
    file(RELATIVE_PATH rel_path ${CMAKE_CURRENT_SOURCE_DIR} ${asset_path})
    configure_file(${asset_path} ${CMAKE_CURRENT_BINARY_DIR}/${rel_path} COPYONLY)
endforeach()

# Tự động quét toàn bộ tài nguyên (Shaders/Textures) của map và copy vào thư mục build
file(GLOB_RECURSE MAP_ASSETS CONFIGURE_DEPENDS 
    "maps/**/*.fs"
    "maps/**/*.vs"
    "maps/**/*.png"
)
foreach(asset_path ${MAP_ASSETS})
    file(RELATIVE_PATH rel_path ${CMAKE_CURRENT_SOURCE_DIR} ${asset_path})
    configure_file(${asset_path} ${CMAKE_CURRENT_BINARY_DIR}/${rel_path} COPYONLY)
endforeach()

# --- compute module shaders ---
configure_file(core/particles/shaders/gpu/particle_gpu.comp    ${CMAKE_CURRENT_BINARY_DIR}/core/particles/shaders/gpu/particle_gpu.comp    COPYONLY)
configure_file(core/particles/shaders/gpu/particle_gpu_ssbo.vs ${CMAKE_CURRENT_BINARY_DIR}/core/particles/shaders/gpu/particle_gpu_ssbo.vs COPYONLY)
configure_file(core/particles/shaders/gpu/fluid_surface_capture.vs ${CMAKE_CURRENT_BINARY_DIR}/core/particles/shaders/gpu/fluid_surface_capture.vs COPYONLY)
configure_file(core/fluid/shaders/fluid_pbd_gpu.comp ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_pbd_gpu.comp COPYONLY)
configure_file(core/fluid/shaders/fluid_pbd_surface.vs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_pbd_surface.vs COPYONLY)
configure_file(core/particles/shaders/gpu/particle_gpu_vbo.vs  ${CMAKE_CURRENT_BINARY_DIR}/core/particles/shaders/gpu/particle_gpu_vbo.vs  COPYONLY)
configure_file(core/particles/shaders/gpu/particle_gpu.fs      ${CMAKE_CURRENT_BINARY_DIR}/core/particles/shaders/gpu/particle_gpu.fs      COPYONLY)
configure_file(core/shaders/taiji.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/taiji.fs COPYONLY)

# --- Cập nhật: Sao chép thư viện Shaders chuyên dụng Phase 1 và 2 vào thư mục shaders/ ---
configure_file(core/shaders/additive_soft.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/additive_soft.fs COPYONLY)
configure_file(core/shaders/rim_glow.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/rim_glow.fs COPYONLY)
configure_file(core/shaders/dissolve.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/dissolve.fs COPYONLY)
configure_file(core/shaders/flow_map.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/flow_map.fs COPYONLY)
configure_file(core/shaders/distortion.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/distortion.fs COPYONLY)
configure_file(core/shaders/bloom_bright.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/bloom_bright.fs COPYONLY)
configure_file(core/shaders/bloom_blur.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/bloom_blur.fs COPYONLY)
configure_file(core/shaders/post_process.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/post_process.fs COPYONLY)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/core/trails/shaders)
configure_file(core/trails/shaders/trail_glow.fs ${CMAKE_CURRENT_BINARY_DIR}/core/trails/shaders/trail_glow.fs COPYONLY)
# trail_body/trail_deform were missing here. A missing shader does NOT announce
# itself as a shader problem: TrailUsesDeformShader() falls back to the classic
# pipeline, so the trail keeps rendering — as the OLD effect — and every edit to
# trail_deform.fs appears to do nothing at all.
configure_file(core/trails/shaders/trail_body.fs ${CMAKE_CURRENT_BINARY_DIR}/core/trails/shaders/trail_body.fs COPYONLY)
configure_file(core/trails/shaders/trail_deform.vs ${CMAKE_CURRENT_BINARY_DIR}/core/trails/shaders/trail_deform.vs COPYONLY)
configure_file(core/trails/shaders/trail_deform.fs ${CMAKE_CURRENT_BINARY_DIR}/core/trails/shaders/trail_deform.fs COPYONLY)
configure_file(core/trails/shaders/trail_volume.vs ${CMAKE_CURRENT_BINARY_DIR}/core/trails/shaders/trail_volume.vs COPYONLY)
configure_file(core/trails/shaders/trail_volume.fs ${CMAKE_CURRENT_BINARY_DIR}/core/trails/shaders/trail_volume.fs COPYONLY)
configure_file(core/shaders/water_splash.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/water_splash.fs COPYONLY)
configure_file(core/shaders/water_splash.vs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/water_splash.vs COPYONLY)
configure_file(core/shaders/ground_wave.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/ground_wave.fs COPYONLY)
configure_file(core/shaders/ground_wave.vs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/ground_wave.vs COPYONLY)
configure_file(core/fluid/shaders/fluid_depth_smooth.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_depth_smooth.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_capture.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_capture.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_capture_particle.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_capture_particle.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_capture_back.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_capture_back.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_capture_particle_back.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_capture_particle_back.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_thickness_resolve.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_thickness_resolve.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_thickness_blur.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_thickness_blur.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_depth_narrow_range.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_depth_narrow_range.fs COPYONLY)
configure_file(core/fluid/shaders/fluid_surface.fs ${CMAKE_CURRENT_BINARY_DIR}/core/fluid/shaders/fluid_surface.fs COPYONLY)

configure_file(core/shaders/common/vs_header.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/vs_header.glsl COPYONLY)
configure_file(core/shaders/common/fs_header.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/fs_header.glsl COPYONLY)
configure_file(core/shaders/common/lighting.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/lighting.glsl COPYONLY)
configure_file(core/shaders/common/noise.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/noise.glsl COPYONLY)

# core/uv shared includes. Same trap as the trails block above: these are
# #include'd by trail_deform.fs, shield_shell.fs and aura_shell.fs, and a
# missing include does NOT announce itself as a shader problem — the shader
# fails to compile, raylib hands back the DEFAULT shader with a nonzero id, and
# the consumer's capability check passes while the effect renders as something
# else entirely. flow_map.glsl moved here from core/shaders/common/.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/core/uv/shaders)
configure_file(core/uv/shaders/uv_deform.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/uv/shaders/uv_deform.glsl COPYONLY)
configure_file(core/uv/shaders/surface_flow.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/uv/shaders/surface_flow.glsl COPYONLY)
configure_file(core/uv/shaders/uv_field.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/uv/shaders/uv_field.glsl COPYONLY)
configure_file(core/uv/shaders/flow_map.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/uv/shaders/flow_map.glsl COPYONLY)

# core/deform's own GLSL mirror (05/08/2026, first one — see that file's own
# header). Same missing-shader-file trap as the core/uv includes above.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/core/deform/shaders)
configure_file(core/deform/shaders/mesh_deform.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/deform/shaders/mesh_deform.glsl COPYONLY)


# Liên kết tĩnh với thư viện Raylib vừa tải về
target_link_libraries(wuxing PRIVATE raylib enet)
if(WUXING_USE_VULKAN)
    target_link_libraries(wuxing PRIVATE Vulkan::Vulkan)
endif()

target_include_directories(wuxing PRIVATE ${enet_SOURCE_DIR}/include)

# EOS online backend (internet P2P qua ha tang free cua Epic — EOS_SETUP.md).
# Mac dinh OFF: build voi stub. Bat khi SDK da nam o third_party/eos-sdk:
#   cmake -S . -B build -DWUXING_EOS=ON
option(WUXING_EOS "Build the EOS online backend (needs third_party/eos-sdk)" OFF)
if (WUXING_EOS)
    target_sources(wuxing PRIVATE net/net_eos.c)
    target_include_directories(wuxing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eos-sdk/SDK/Include)
    if (APPLE)
        target_link_libraries(wuxing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eos-sdk/SDK/Bin/libEOSSDK-Mac-Shipping.dylib)
    elseif (UNIX)
        target_link_libraries(wuxing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eos-sdk/SDK/Bin/libEOSSDK-Linux-Shipping.so)
    endif()
    # Dylib install name la @rpath/... — tro rpath ve SDK/Bin de dyld tim thay
    set_target_properties(wuxing PROPERTIES
        BUILD_RPATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party/eos-sdk/SDK/Bin)
else()
    target_sources(wuxing PRIVATE net/net_eos_stub.c)
endif()

# Cấu hình các frameworks bắt buộc dành riêng cho macOS
if(APPLE)
    target_link_libraries(wuxing PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()

# make lint — runs lint_skill.py over all skills (exit non-zero on any FAIL)
add_custom_target(lint
    COMMAND ${CMAKE_COMMAND} -E env python3
        ${CMAKE_SOURCE_DIR}/scripts/lint_skill.py --all
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Linting all skills..."
)
