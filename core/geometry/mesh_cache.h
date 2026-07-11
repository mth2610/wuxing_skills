#ifndef MESH_CACHE_H
#define MESH_CACHE_H

#include "raylib.h"
#include "procedural_mesh_utils.h"

// Khởi tạo bộ đệm cache
void MeshCache_Init(void);

// Giải phóng bộ đệm cache
void MeshCache_Unload(void);

// Lấy dữ liệu đá tảng đã cache theo seed và độ gồ ghề
RockMeshData* MeshCache_GetRock(int seed, float jaggedness);
RockMeshData* MeshCache_GetRockEx(int seed, float jaggedness, int subdivisions);

// Lấy dữ liệu cụm tinh thể băng đã cache theo seed và độ sắc nhọn
ShardClusterMeshData* MeshCache_GetIce(int seed, float sharpness);

#endif // MESH_CACHE_H
