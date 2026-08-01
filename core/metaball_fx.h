#ifndef METABALL_FX_H
#define METABALL_FX_H

#include "raylib.h"

/* ============================================================================
 * METABALLS / SCREEN-SPACE FLUID (MỚI)
 * --------------------------------------------------------------------------
 * Gộp các "blob" rời rạc (đầu đạn nước, giọt dung nham...) thành 1 khối lỏng
 * liền mạch: vẽ blob world-space thành đĩa tròn screen-space vào 1 buffer
 * mask độ phân giải thấp, blur Gaussian 2-pass, rồi threshold (smoothstep)
 * để biên trở nên mượt và các blob gần nhau hoà vào nhau.
 *
 * Đây là hiệu ứng SCREEN-SPACE THUẦN 2D (không depth-test với scene 3D —
 * blob luôn vẽ đè lên trên cùng).
 *
 * QUAN TRỌNG — pattern Register/Draw (KHÔNG gọi GL trực tiếp trong skill):
 * Draw[Name]Skill() của skill chạy BÊN TRONG render-pass 3D của
 * core/screen_distort.c (raylib's BeginTextureMode/EndTextureMode không
 * nest được — gọi MetaballFX compositing trực tiếp trong đó sẽ phá binding
 * của render target 3D đang dùng). Vì vậy:
 *   1) Trong Draw[Name]Skill(): chỉ gọi MetaballFX_RegisterBlob() (không có
 *      lệnh GL nào, chỉ ghi vào mảng tĩnh) cho mỗi blob muốn hiện frame này.
 *   2) main.c gọi MetaballFX_Prepare() đúng 1 lần/frame, NGOÀI
 *      BeginMode3D/EndMode3D, rồi MetaballFX_Composite() vào VFX body layer.
 *      Tách pass giúp mask/blur không lồng render target, nhưng body vẫn được
 *      compositor HDR xử lý như mọi VFX khác.
 * Blob chỉ tồn tại 1 frame — skill phải RegisterBlob lại mỗi frame.
 * ==========================================================================*/

#define METABALL_MAX_BLOBS 32

void MetaballFX_Init(int width, int height);
void MetaballFX_Unload(void);

// Gọi từ skill Draw (an toàn gọi trong 3D pass — không có lệnh GL).
void MetaballFX_RegisterBlob(Vector3 worldPos, float radius);
/* True only until Prepare consumes this frame's registrations. */
bool MetaballFX_HasRegisteredBlobs(void);

// Gọi từ main.c, NGOÀI BeginMode3D/EndMode3D, 1 lần/frame. Chuẩn bị mask và
// blur, đồng thời tự xoá registry. Ngay sau đó gọi Composite trong VFX body.
void MetaballFX_Prepare(Camera3D camera, Color tint, float threshold,
                        float smoothness);
void MetaballFX_Composite(void);

// Compatibility helper: Prepare + Composite onto the current target.
void MetaballFX_DrawRegistered(Camera3D camera, Color tint, float threshold,
                               float smoothness);

#endif // METABALL_FX_H
