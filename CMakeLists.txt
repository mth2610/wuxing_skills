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
    core/emitter_system.c
    core/flow_map.c
    core/force_field.c
    core/camera_fx.c
    core/particle_system.c
    core/gpu_particle_system.c
    core/path_spline.c
    core/post_fx.c
    core/ribbon_strip.c
    core/screen_distort.c
    core/skill_manager.c
    core/sprite_anim.c
    core/trail_system.c
    core/utils_math.c
    core/vfx_light.c
    core/decal_system.c
    core/procedural_mesh_utils.c
    core/skill_helper.c
    core/particle_radial_burst.c
    core/impact_burst.c
    core/shader_preprocessor.c

    # --- Sandbox & Test Simulator ---
    sandbox/sandbox_core.c
    sandbox/ui_panel.c
    sandbox/vfx_test.c
    sandbox/skill_debugger.c

    # --- Skills ---
    ${SKILL_SOURCES}
    # --- Maps ---
    ${MAP_SOURCES}
)

# Thêm root path để resolve includes có tiền tố core/ và skills/
target_include_directories(wuxing PRIVATE .)

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

# --- gpu_particle_system shaders ---
configure_file(core/shaders/gpu_particles.comp     ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/gpu_particles.comp     COPYONLY)
configure_file(core/shaders/gpu_particles_ssbo.vs  ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/gpu_particles_ssbo.vs  COPYONLY)
configure_file(core/shaders/gpu_particles_vbo.vs   ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/gpu_particles_vbo.vs   COPYONLY)
configure_file(core/shaders/gpu_particles.fs       ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/gpu_particles.fs       COPYONLY)
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


configure_file(core/shaders/common/vs_header.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/vs_header.glsl COPYONLY)
configure_file(core/shaders/common/fs_header.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/fs_header.glsl COPYONLY)
configure_file(core/shaders/common/lighting.glsl ${CMAKE_CURRENT_BINARY_DIR}/core/shaders/common/lighting.glsl COPYONLY)


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