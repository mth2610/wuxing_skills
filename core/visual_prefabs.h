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

#include "core/geometry/procedural_mesh_utils.h"

#define Prefab_DrawStonePillar ProceduralMesh_DrawStonePillar
#define Prefab_DrawRoundBoulder ProceduralMesh_DrawRoundBoulder
#define Prefab_DrawBoulder ProceduralMesh_DrawBoulder
#define Prefab_DrawIceCrystal ProceduralMesh_DrawIceCrystal
#define Prefab_DrawMagicPuddle ProceduralMesh_DrawMagicPuddle
#define Prefab_DrawFireball ProceduralMesh_DrawFireball

#include "core/composition/visual_composer.h"

#define Prefab_SpawnSmokePuff VFX_ComposeSmokePuff
#define Prefab_SpawnSmokeTrail VFX_ComposeSmokeTrail
#define Prefab_SpawnLongFissure VFX_ComposeFissure
#define Prefab_SpawnLightningBeam VFX_ComposeLightningBeam

#endif // VISUAL_PREFABS_H
