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
set(CUSTOMIZE_BUILD ON CACHE BOOL "Customize raylib build features" FORCE)

# Tải Raylib phiên bản 5.5
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG        5.5
)
FetchContent_MakeAvailable(raylib)

# -----------------------------------------------------------------
# BIÊN DỊCH TRỰC TIẾP FILE MAIN.C Ở THƯ MỤC GỐC
# -----------------------------------------------------------------

# Khai báo mục tiêu build ứng dụng chạy trực tiếp trên máy tính
add_executable(wuxing 
    main.c 
    # --- Core Systems ---
    core/color_gradient.c
    core/emitter_system.c
    core/flow_map.c
    core/force_field.c
    core/camera_fx.c
    core/particle_system.c
    core/path_spline.c
    core/post_fx.c
    core/ribbon_strip.c
    core/sandbox_core.c
    core/screen_distort.c
    core/skill_manager.c
    core/sprite_anim.c
    core/trail_system.c
    core/ui_panel.c
    core/utils_math.c
    core/vfx_light.c
    core/decal_system.c
    core/vfx_test.c

    # --- Skills ---
    # Metal (Kim)
    skills/metal/metal_projectile/metal_skill.c
    skills/metal/sword_rain/sword_rain_skill.c
    
    # Wood (Mộc)
    skills/wood/wood_roots/wood_skill.c
    
    # Water (Thủy)
    skills/water/water_projectile/fluid_skill.c
    skills/water/water_stream/tube_skill.c
    skills/water/water_shield/shield_skill.c
    
    # Fire (Hỏa)
    skills/fire/fire_ball/fire_skill.c
    
    # Taiji (Thái Cực)
    skills/taiji/wind_storm/wind_skill.c
    skills/taiji/lightning/electric_skill.c
)

# Thêm root path để resolve includes có tiền tố core/ và skills/
target_include_directories(wuxing PRIVATE .)

# Tự động copy file Shader và Texture vào thư mục build để game không bị lỗi load
configure_file(skills/water/water_projectile/fluid.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/water/water_projectile/fluid.fs COPYONLY)
configure_file(skills/water/water_projectile/fluid.vs ${CMAKE_CURRENT_BINARY_DIR}/skills/water/water_projectile/fluid.vs COPYONLY)
configure_file(skills/water/water_stream/tube.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/water/water_stream/tube.fs COPYONLY)
configure_file(skills/water/water_stream/tube.vs ${CMAKE_CURRENT_BINARY_DIR}/skills/water/water_stream/tube.vs COPYONLY)
configure_file(skills/fire/fire_ball/fire.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/fire/fire_ball/fire.fs COPYONLY)
configure_file(skills/metal/metal_projectile/metal.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/metal/metal_projectile/metal.fs COPYONLY)
configure_file(skills/wood/wood_roots/wood.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/wood/wood_roots/wood.fs COPYONLY)
configure_file(skills/taiji/lightning/electric.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/taiji/lightning/electric.fs COPYONLY)
configure_file(skills/taiji/wind_storm/wind.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/taiji/wind_storm/wind.fs COPYONLY)
configure_file(skills/water/water_shield/shield.fs ${CMAKE_CURRENT_BINARY_DIR}/skills/water/water_shield/shield.fs COPYONLY)
configure_file(skills/water/water_shield/shield.vs ${CMAKE_CURRENT_BINARY_DIR}/skills/water/water_shield/shield.vs COPYONLY)

# --- Copy Shaders của hệ thống hạt cũ ---
# GPU mode (Linux/Windows - OpenGL 4.3+): Compute Shader + SSBO
configure_file(core/shaders/particles.comp ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/particles.comp COPYONLY)
configure_file(core/shaders/particles.vs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/particles.vs COPYONLY)
configure_file(core/shaders/particles.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/particles.fs COPYONLY)
# CPU mode (macOS - OpenGL 4.1): Vertex attributes thay SSBO
configure_file(core/shaders/particles_cpu.vs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/particles_cpu.vs COPYONLY)
configure_file(core/shaders/particles_cpu.fs ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/particles_cpu.fs COPYONLY)
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

# --- Copy thêm Texture của skill ---
configure_file(skills/fire/fire_ball/dragon_head.png ${CMAKE_CURRENT_BINARY_DIR}/skills/fire/fire_ball/dragon_head.png COPYONLY)
configure_file(skills/metal/metal_projectile/sword.png ${CMAKE_CURRENT_BINARY_DIR}/skills/metal/metal_projectile/sword.png COPYONLY)
configure_file(skills/water/water_shield/water_caustics.png ${CMAKE_CURRENT_BINARY_DIR}/skills/water/water_shield/water_caustics.png COPYONLY)


# Liên kết tĩnh với thư viện Raylib vừa tải về
target_link_libraries(wuxing PRIVATE raylib)

# Cấu hình các frameworks bắt buộc dành riêng cho macOS
if(APPLE)
    target_link_libraries(wuxing PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()