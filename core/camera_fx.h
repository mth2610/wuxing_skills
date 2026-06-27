#ifndef CAMERA_FX_H
#define CAMERA_FX_H

#include "raylib.h"

// Thêm chấn động rung lắc camera (trauma: [0.0 .. 1.0])
// Mức chấn động sẽ cộng dồn và giới hạn tối đa ở 1.0.
void CameraFX_Shake(float trauma);

// Cập nhật trạng thái rung lắc camera và áp dụng sai số vào vị trí/hướng camera
void CameraFX_Update(Camera3D *camera, float dt);

#endif // CAMERA_FX_H
