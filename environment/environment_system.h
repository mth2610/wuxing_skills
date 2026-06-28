#ifndef ENVIRONMENT_SYSTEM_H
#define ENVIRONMENT_SYSTEM_H

#include "raylib.h"

typedef enum {
    ENV_SHAPE_SPHERE,
    ENV_SHAPE_CYLINDER,
    ENV_SHAPE_BOX
} EnvShadowShapeType;

typedef struct {
    Color color;
    float start;
    float end;
    float density;
    bool enabled;
} EnvFogConfig;

// Khởi tạo và cập nhật môi trường
void Environment_Init(void);
void Environment_Update(float dt);

// Vẽ bóng giả thông minh (Smart Fake Shadow)
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);

// Các hàm Getter / Setter cấu hình ánh sáng mặt trời
Vector3 Environment_GetSunDirection(void);
void Environment_SetSunDirection(Vector3 dir);

Color Environment_GetSunColor(void);
void Environment_SetSunColor(Color col);

Color Environment_GetAmbientColor(void);
void Environment_SetAmbientColor(Color col);

Color Environment_GetShadowColor(void);
void Environment_SetShadowColor(Color col);

EnvFogConfig Environment_GetFogConfig(void);
void Environment_SetFogConfig(EnvFogConfig config);

#endif // ENVIRONMENT_SYSTEM_H
