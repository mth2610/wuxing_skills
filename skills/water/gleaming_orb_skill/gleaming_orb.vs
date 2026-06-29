#version 330
#include "core/shaders/common/vs_header.glsl"

void main() {
    // Thao tác biến đổi đỉnh hoặc tính toán nâng cao có thể can thiệp tại đây
    // Trước khi chuyển giao hoàn toàn cho bộ khung xử lý cuối của Engine
    
    // Bắt buộc gọi hàm này ở bước cuối cùng theo đặc tả hệ thống
    VS_FinalOutput();
}