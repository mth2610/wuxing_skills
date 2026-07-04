#ifndef VISUAL_PREFABS_H
#define VISUAL_PREFABS_H

#include "raylib.h"

// ============================================================================
// VISUAL PREFABS SYSTEM
// Cung cấp các hàm vẽ/sinh hiệu ứng 3D "chuẩn game AAA" đã được căn chỉnh sẵn 
// độ cong, góc cạnh, và ánh sáng. Khác với các hàm ProceduralMesh thô, Prefab
// đảm bảo kết quả hình ảnh luôn đẹp và tự nhiên.
// ============================================================================

// ----------------------------------------------------------------------------
// Nhóm 1: Mesh & Hình khối tĩnh (Gọi liên tục trong hàm Vẽ - Draw)
// ----------------------------------------------------------------------------

// Vẽ một cột đá từ dưới đất đâm lên.
// - sharpness: 0.0 = đầu bằng phẳng hình trụ, 1.0 = chóp nhọn như măng đá.
// - progress: 0.0 -> 1.0 (hiệu ứng trồi lên từ mặt đất).
void Prefab_DrawStonePillar(Vector3 basePos, float radius, float height, float sharpness, float progress);

void Prefab_DrawRoundBoulder(Vector3 pos, float radius);

// Vẽ một cục đá tảng góc cạnh lởm chởm.
// - jaggedness: 0.0 = nhẵn thín (như quả bóng), 1.0 = gồ ghề sắc cạnh.
// - seed: Cố định hình dạng đá cho mỗi tảng riêng biệt.
void Prefab_DrawBoulder(Vector3 pos, float radius, float jaggedness, int seed);

// Vẽ một cụm tinh thể băng mọc tủa ra.
// - sharpness: 0.0 = đầu tà tà, 1.0 = mũi nhọn sắc bén.
void Prefab_DrawIceCrystal(Vector3 basePos, float radius, float height, float sharpness, int seed);

// Vẽ một vũng nước ma thuật dính sát mặt đất.
void Prefab_DrawMagicPuddle(Vector3 pos, float radius);

// Vẽ một quả cầu lửa bùng cháy, biến dạng liên tục theo thời gian.
void Prefab_DrawFireball(Vector3 pos, float radius, float time);

// ----------------------------------------------------------------------------
// Nhóm 2: Effect & Particle (Gọi 1 lần trong hàm Khởi tạo chiêu - Cast/Update)
// ----------------------------------------------------------------------------

// Bùng ra một đám khói đặc tại một điểm.
void Prefab_SpawnSmokePuff(Vector3 pos, float size);

// Rải một vệt khói dọc theo đường thẳng (bay lững lờ đứt đoạn).
void Prefab_SpawnSmokeTrail(Vector3 start, Vector3 end, float duration);

// Rạch một vết nứt đất dài.
void Prefab_SpawnLongFissure(Vector3 start, Vector3 end, float width);

// Bắn một tia điện sét dọc theo đường thẳng.
void Prefab_SpawnLightningBeam(Vector3 start, Vector3 end, float duration);

#endif // VISUAL_PREFABS_H
