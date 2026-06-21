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
add_executable(wuxing main.c fluid_skill.c metal_skill.c fire_skill.c wood_skill.c electric_skill.c skill_manager.c sword_rain_skill.c)

# Tự động copy file Shader vào thư mục build để game không bị lỗi load shader
configure_file(fluid.fs ${CMAKE_CURRENT_BINARY_DIR}/fluid.fs COPYONLY)
configure_file(fire.fs ${CMAKE_CURRENT_BINARY_DIR}/fire.fs COPYONLY)
configure_file(metal.fs ${CMAKE_CURRENT_BINARY_DIR}/metal.fs COPYONLY)
configure_file(wood.fs ${CMAKE_CURRENT_BINARY_DIR}/wood.fs COPYONLY)
configure_file(electric.fs ${CMAKE_CURRENT_BINARY_DIR}/electric.fs COPYONLY)



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