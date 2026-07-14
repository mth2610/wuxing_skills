// Lớp Backend Vulkan cho Wuxing Skills
// File này chỉ được biên dịch khi cấu hình CMake bật cờ WUXING_USE_VULKAN=ON

#if defined(WUXING_USE_VULKAN)

// Chặn Raylib không được sử dụng backend OpenGL
#undef GRAPHICS_API_OPENGL_33
#undef GRAPHICS_API_OPENGL_ES2
#undef GRAPHICS_API_OPENGL_ES3

// Kích hoạt việc sinh mã nguồn thực thi cho rlvk (Vulkan backend)
#define RLVK_IMPLEMENTATION
#include "../../third_party/vulkan/rlvk.h"

// TODO: Thêm logic móc nối Window/GLFW ở đây (rlvkAttachSurface)
// vì Raylib mặc định sẽ khởi tạo OpenGL Context.
void WuxingVulkan_InitBackend() {
    // Tạm thời để trống. Sẽ được triển khai khi có thiết bị Vulkan thật.
    // Các hàm cần móc nối:
    // rlvkSetMsaaSamples(4);
    // rlvkAttachSurface(glfw_surface);
}

#endif // WUXING_USE_VULKAN
