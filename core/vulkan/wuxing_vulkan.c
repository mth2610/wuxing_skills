// Lớp Backend Vulkan cho Wuxing Skills
// File này chỉ được biên dịch khi cấu hình CMake bật cờ WUXING_USE_VULKAN=ON

#if defined(WUXING_USE_VULKAN)

// Chặn Raylib không được sử dụng backend OpenGL
#undef GRAPHICS_API_OPENGL_33
#undef GRAPHICS_API_OPENGL_ES2
#undef GRAPHICS_API_OPENGL_ES3

// Việc khởi tạo Vulkan và móc nối surface với GLFW đã được thực hiện trực tiếp bên trong 
// mã nguồn của Raylib thông qua script scripts/rlvk_patch_raylib.py (can thiệp vào rcore_desktop_glfw.c).
// RLVK_IMPLEMENTATION cũng đã được định nghĩa trong rcore.c.
// Do đó, platform layer đã hoàn tất, không cần biên dịch lại rlvk.h ở đây để tránh lỗi duplicate symbol.

void WuxingVulkan_InitBackend() {
    // Không làm gì cả vì đã được xử lý tự động trong raylib InitWindow (WindowAttachVulkanSurface).
}

#endif // WUXING_USE_VULKAN
