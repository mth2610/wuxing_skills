// Lớp Backend Vulkan cho Wuxing Skills
// File này chỉ được biên dịch khi cấu hình CMake bật cờ WUXING_USE_VULKAN=ON

#if defined(WUXING_USE_VULKAN)

// KHÔNG #undef GRAPHICS_API_OPENGL_33/ES2/ES3 ở đây: rlvk.h's integration model (third_party/
// vulkan/rlvk.h) yêu cầu GRAPHICS_API_OPENGL_33 (hoặc ES3 trên Android) VẪN phải được định
// nghĩa dưới backend Vulkan — chỉ để giữ đúng layout struct rlVertexBuffer của rlgl.h, không
// liên quan gì tới việc thực sự dùng OpenGL. #undef nó (như code cũ ở đây từng làm) sẽ làm sai
// struct layout và corrupt state ngay khi rlvk.h include rlgl.h qua RLVK_IMPLEMENTATION.

// Việc khởi tạo Vulkan và móc nối surface (GLFW desktop lẫn NativeActivity Android) đã được
// thực hiện trực tiếp bên trong mã nguồn Raylib thông qua script scripts/rlvk_patch_raylib.py
// (patch rcore.c + rcore_desktop_glfw.c / rcore_android.c). RLVK_IMPLEMENTATION cũng đã được
// định nghĩa trong rcore.c. Do đó, platform layer đã hoàn tất, không cần biên dịch lại rlvk.h
// ở đây để tránh lỗi duplicate symbol.

void WuxingVulkan_InitBackend() {
    // Không làm gì cả vì đã được xử lý tự động trong raylib InitWindow (WindowAttachVulkanSurface).
}

#endif // WUXING_USE_VULKAN
