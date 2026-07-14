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

# Tải Raylib phiên bản 6.0
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG        6.0
)
FetchContent_MakeAvailable(raylib)

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

# Tự động quét toàn bộ mã nguồn của kỹ năng trong thư mục skills/
file(GLOB_RECURSE SKILL_SOURCES CONFIGURE_DEPENDS "skills/**/*.c")
# Tự động quét toàn bộ mã nguồn của map trong thư mục maps/
file(GLOB_RECURSE MAP_SOURCES CONFIGURE_DEPENDS "maps/**/*.c")

# Khai báo mục tiêu build ứng dụng chạy trực tiếp trên máy tính
add_executable(wuxing 
    main.c 
    # --- Environment System ---
    environment/environment_system.c
    # --- Core Systems ---
    core/map_manager.c
    core/resource_manager.c
    core/color_gradient.c
    core/float_curve.c
    core/skill_curve.c
    core/tuning.c
    core/emitter_system.c
    core/flow_map.c
    core/force_field.c
    core/camera_fx.c
    core/audio_system.c
    core/particle_system.c
    compute/gpu_particle_system.c
    core/path_spline.c
    core/post_fx.c
    core/ribbon_strip.c
    core/screen_distort.c
    core/surface_material.c
    core/atmosphere.c
    core/skill_manager.c
    core/sprite_anim.c
    core/trail_system.c
    core/utils_math.c
    core/vfx_light.c
    core/vfx_proc_ray.c
    core/decal_system.c
    core/geometry/procedural_mesh_utils.c
    core/geometry/mesh_cache.c
    core/material/material_system.c
    core/composition/visual_composer.c
    core/presets/vfx_presets.c
    core/skill_helper.c
    core/time_fx.c
    core/motion_controller.c
    core/status_vfx.c
    core/afterimage.c
    core/shader_preprocessor.c
    core/metaball_fx.c
    core/debug_draw.c

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
    sandbox/visual_verify.c
    sandbox/pool_stats.c

    # --- Skills ---
    ${SKILL_SOURCES}
    # --- Maps ---
    ${MAP_SOURCES}
)

# Thêm root path để resolve includes có tiền tố core/ và skills/
target_include_directories(wuxing PRIVATE . core/geometry core/material core/composition core/presets)

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
configure_file(compute/shaders/gpu_particles.comp     ${CMAKE_CURRENT_BINARY_DIR}/compute/shaders/gpu_particles.comp     COPYONLY)
configure_file(compute/shaders/gpu_particles_ssbo.vs  ${CMAKE_CURRENT_BINARY_DIR}/compute/shaders/gpu_particles_ssbo.vs  COPYONLY)
configure_file(compute/shaders/gpu_particles_vbo.vs   ${CMAKE_CURRENT_BINARY_DIR}/compute/shaders/gpu_particles_vbo.vs   COPYONLY)
configure_file(compute/shaders/gpu_particles.fs       ${CMAKE_CURRENT_BINARY_DIR}/compute/shaders/gpu_particles.fs       COPYONLY)
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
configure_file(core/shaders/trail_glow.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/trail_glow.fs COPYONLY)
configure_file(core/shaders/water_splash.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/water_splash.fs COPYONLY)
configure_file(core/shaders/water_splash.vs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/water_splash.vs COPYONLY)

configure_file(core/shaders/common/vs_header.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/vs_header.glsl COPYONLY)
configure_file(core/shaders/common/fs_header.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/fs_header.glsl COPYONLY)
configure_file(core/shaders/common/lighting.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/lighting.glsl COPYONLY)


# Liên kết tĩnh với thư viện Raylib vừa tải về
target_link_libraries(wuxing PRIVATE raylib enet)
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