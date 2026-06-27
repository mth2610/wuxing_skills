#ifndef RIBBON_STRIP_H
#define RIBBON_STRIP_H

#include "raylib.h"

// =============================================================================
// RIBBON STRIP - Module dùng chung cho mọi chiêu cần vẽ "thân dài" liên tục
// (rồng lửa, dây leo gỗ, tia điện, dòng nước...) thay cho chuỗi billboard
// xếp chồng lên nhau (gây overdraw nặng + sai silhouette khi nhìn dọc path).
//
// Kỹ thuật: camera-facing ribbon. Tại mỗi điểm trên path, offset trái/phải
// theo 1 vector vuông góc với cả tangent của path VÀ hướng nhìn camera, tạo
// thành 1 dải triangle-strip liên tục (vẽ bằng rlgl immediate-mode, không
// cần VBO, không malloc). Đây đúng kỹ thuật Trail Renderer của Unity/Unreal
// dùng cho hiệu ứng dạng dải.
//
// Module này KHÔNG tự quản lý bộ nhớ - người gọi tự cấp 1 mảng RibbonPoint
// tĩnh (static array), module chỉ đọc và vẽ, đúng nguyên tắc no-malloc của
// project.
// =============================================================================

typedef struct {
  Vector3 position; // Vị trí điểm trên path (thế giới 3D)
  float halfWidth;  // Bề rộng NỬA thân tại điểm này (world units)
  Color tint;       // Màu + alpha tại điểm này
  float v;          // UV dọc theo chiều dài dải - người gọi tự tính (ví dụ
                     // normDist 0..1), cho phép cuộn texture theo thời gian
                     // sau này nếu cần (hiệu ứng "chảy" dọc thân) mà không
                     // phải sửa lại module này.
} RibbonPoint;

// Vẽ 1 dải ribbon camera-facing liên tục qua danh sách điểm.
// points[0] và points[count-1] là 2 đầu mút của dải. Cần count >= 2.
//
// Texture được bind bên trong hàm này (giống DrawBillboard) - shader và
// blend mode (BeginShaderMode/BeginBlendMode) vẫn phải được set từ NGOÀI
// trước khi gọi, hàm này chỉ submit hình học, không đổi shader/blend state,
// để không phá batch hiện tại (tương thích với cách DrawBillboard đang
// được gọi xen kẽ trong cùng 1 block BeginShaderMode/BeginBlendMode).
void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture,
                     Camera3D camera);

#endif // RIBBON_STRIP_H
