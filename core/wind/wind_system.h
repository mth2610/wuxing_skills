#ifndef WUXING_WIND_SYSTEM_H
#define WUXING_WIND_SYSTEM_H

#include "core/wind/wind_types.h"

// =============================================================================
// WIND SYSTEM API (Ghost of Tsushima Style)
//
// Quản lý gió toàn cục (Macro Wind) và mảng hạt gió cục bộ (Vorticles).
// An toàn trên mọi nền tảng: không malloc trong game loop, static ring-buffer O(N)
// với N <= 256.
// =============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Vòng đời hệ thống
// -----------------------------------------------------------------------------
void Wind_Init(void);
void Wind_Update(float dt);
void Wind_Unload(void);
void Wind_Clear(void); // Xóa toàn bộ Vorticle đang hoạt động

// -----------------------------------------------------------------------------
// Cấu hình Gió vĩ mô (Macro Wind) & Địa hình
// -----------------------------------------------------------------------------
void Wind_SetMacro(const WindMacroConfig *cfg);
WindMacroConfig Wind_GetMacro(void);

// Thiết lập hàm truy vấn độ cao địa hình để tính toán dòng khí lướt qua dốc (Terrain Lift)
void Wind_SetTerrainHeightQuery(TerrainHeightQueryFn queryFn, void *userData);

// -----------------------------------------------------------------------------
// Emitters: Kích phát luồng gió cục bộ (Vorticles)
// Trả về index của Vorticle trong pool, hoặc -1 nếu pool đầy (ring-buffer tự ghi đè
// phần tử già nhất).
// -----------------------------------------------------------------------------

// 1. Luồng gió thẳng có hướng (vệt chém kiếm, đạn lướt, phi đao)
int Wind_SpawnGust(Vector3 pos, Vector3 dir, float radius, float strength, float duration);

// 2. Vụ nổ xung kích tỏa tròn (chưởng nổ, tiếp đất chấn động, dậm chân)
int Wind_SpawnRadialBlast(Vector3 pos, float radius, float strength, float duration);

// 3. Lốc xoáy quanh trục (dòng đối lưu nhiệt ngọn lửa, lốc xoáy)
int Wind_SpawnVortex(Vector3 pos, Vector3 axis, float radius, float strength, float inwardPull, float duration);

// -----------------------------------------------------------------------------
// Receivers: Đánh giá vận tốc và gia tốc gió tại một điểm trong không gian
// -----------------------------------------------------------------------------

// Lấy vector vận tốc gió tức thời tại điểm pos (kết hợp Macro Wind + Vorticles + Terrain Lift)
Vector3 Wind_EvaluateVelocity(Vector3 pos, float time);

// Tính gia tốc tác động lên một hạt tại pos đang di chuyển với vận tốc currentVel
Vector3 Wind_EvaluateAcceleration(Vector3 pos, float time, Vector3 currentVel);

// Lấy riêng thành phần gió vĩ mô tại pos
Vector3 Wind_GetMacroAt(Vector3 pos, float time);

// -----------------------------------------------------------------------------
// Buffer & Inspection
// -----------------------------------------------------------------------------
const VorticleData* Wind_GetActiveVorticles(int *outCount);
int Wind_GetActiveCount(void);

// -----------------------------------------------------------------------------
// Debug Visualization (Gizmo trực quan hóa hướng gió và mảng quả cầu Vorticle)
// -----------------------------------------------------------------------------
void Wind_DrawDebug(Vector3 anchorPos);

#ifdef __cplusplus
}
#endif

#endif // WUXING_WIND_SYSTEM_H
