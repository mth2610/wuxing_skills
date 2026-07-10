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
                     // phải sửa lại module này. Xem Ribbon_ComputeArcLengthUV
                     // nếu muốn UV theo độ dài thật thay vì tự tính index/count.
} RibbonPoint;

// Cách tính vector "right" (offset trái/phải tạo bề rộng ribbon) tại mỗi
// điểm - xem DrawRibbonStripEx.
typedef enum {
  RIBBON_CAMERA_FACING, // right = tangent × hướng nhìn camera (mặc định,
                        // đúng kiểu Trail Renderer - silhouette luôn đúng
                        // hướng camera). Dùng cho tia sét, beam, projectile
                        // trail - bất kỳ dải nào cần luôn "quay mặt" camera.
  RIBBON_WORLD_UP,      // right = tangent × world-up (0,1,0). Dải KHÔNG
                        // billboard theo camera - dùng cho dải nằm sát mặt
                        // đất/mặt phẳng cố định (sông, dây leo bò trên đất)
                        // mà xoay camera không được lật ngược silhouette.
  RIBBON_FIXED_NORMAL,  // right = tangent × fixedNormal do người gọi cấp -
                        // dải nằm cố định trên 1 mặt phẳng bất kỳ (không
                        // nhất thiết world-up), ví dụ dải áp lên tường
                        // nghiêng hoặc mặt phẳng skill tự định nghĩa.
} RibbonMode;

// Vẽ 1 dải ribbon liên tục qua danh sách điểm, hướng "right" tại mỗi điểm
// quyết định bởi `mode` (xem RibbonMode). points[0] và points[count-1] là 2
// đầu mút của dải. Cần count >= 2. `fixedNormal` chỉ dùng khi
// mode == RIBBON_FIXED_NORMAL (bỏ qua ở 2 mode còn lại).
//
// Texture được bind bên trong hàm này (giống DrawBillboard) - shader và
// blend mode (BeginShaderMode/BeginBlendMode) vẫn phải được set từ NGOÀI
// trước khi gọi, hàm này chỉ submit hình học, không đổi shader/blend state,
// để không phá batch hiện tại (tương thích với cách DrawBillboard đang
// được gọi xen kẽ trong cùng 1 block BeginShaderMode/BeginBlendMode).
void DrawRibbonStripEx(const RibbonPoint *points, int count, Texture2D texture,
                       Camera3D camera, RibbonMode mode, Vector3 fixedNormal);

// Tiện ích: DrawRibbonStripEx với mode = RIBBON_CAMERA_FACING (hành vi cũ,
// đa số ribbon trong project - tia sét, beam, trail - đều camera-facing).
void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture,
                     Camera3D camera);

// Điền points[i].v = độ dài cộng dồn từ points[0] đến points[i], chuẩn hoá
// về [0,1] (points[0].v = 0, points[count-1].v = 1). Dùng thay cho việc tự
// tính v = index/count trong caller - v theo index/count làm texture bị
// kéo dãn không đều trên path cong/jagged (đoạn thẳng dài giữa 2 waypoint xa
// nhau bị nén y hệt đoạn ngắn giữa 2 waypoint gần nhau). Gọi hàm này ngay
// trước Draw*, sau khi đã điền position cho toàn bộ points[]. count >= 2.
void Ribbon_ComputeArcLengthUV(RibbonPoint *points, int count);

#endif // RIBBON_STRIP_H
