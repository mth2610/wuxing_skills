#include "maps/toolkit/ground_shadow.h"
#include "core/resource_manager.h"
#include "core/vfx_light.h"
#include "environment/env_shadow.h"
#include "raylib.h"
#include "raymath.h" // MatrixInvert/MatrixMultiply
#include "rlgl.h"    // rlSetTexture, rlGetMatrixTransform
#include <stddef.h>

static Shader s_shader;
static bool   s_ready = false;
static int    s_locLightVP, s_locShadowEnabled, s_locShadowTexel;

// Cache các giá trị ít thay đổi để tránh tính toán lại mỗi frame
static int    s_lastShadowMapWidth = -1;
static float  s_cachedTexel = 1.0f / 1024.0f;

static inline void EnsureLoaded(void) {
    if (s_ready) return;
    s_shader = ResourceManager_LoadShader("maps/toolkit/shaders/ground_shadow.vs",
                                           "maps/toolkit/shaders/ground_shadow.fs");
    if (s_shader.id == 0) return;

    s_locLightVP       = GetShaderLocation(s_shader, "u_lightVP");
    s_locShadowEnabled = GetShaderLocation(s_shader, "u_shadowEnabled");
    s_locShadowTexel   = GetShaderLocation(s_shader, "u_shadowTexel");
    // Đợt E / E2 — opt into the VFX point-light pool (main.c binds each frame).
    // This is what puts a spell's glow on default_arena's floor plate and on
    // both maps' zone discs — raw immediate-mode draws that reach no
    // Model/Material shader able to carry the lights for them.
    VFXLight_RegisterShader(s_shader);
    s_ready = true;
}

void GroundShadow_UpdateFrame(void) {
    // No-op: the shadow map is bound as texture0 via rlSetTexture inside
    // GroundShadow_Begin (see the note there), and the matrix/float uniforms
    // are pushed there too, per-draw-block.
    EnsureLoaded();
}

void GroundShadow_Begin(void) {
    EnsureLoaded();
    if (!s_ready) return;
    
    BeginShaderMode(s_shader);

    // TỐI ƯU 1: Gọi hàm 1 lần và cache kết quả thay vì gọi 2 lần
    bool isShadowEnabled = EnvShadow_IsEnabled();
    float enabledVal = isShadowEnabled ? 1.0f : 0.0f;
    SetShaderValue(s_shader, s_locShadowEnabled, &enabledVal, SHADER_UNIFORM_FLOAT);

    if (isShadowEnabled) {
        // TỐI ƯU 2: Tính toán ma trận
        // The floor draws this wraps are IMMEDIATE-MODE inside main.c's MyBeginMode3D...
        // Fold inverse(view) into the uploaded matrix so `u_lightVP * fragWorldPos` == lightVP*world.
        Matrix currentTransform = rlGetMatrixTransform();
        Matrix worldFromVert = MatrixInvert(currentTransform); 
        Matrix lightVP = EnvShadow_GetLightVP();
        
        SetShaderValueMatrix(s_shader, s_locLightVP, MatrixMultiply(worldFromVert, lightVP));
        
        // TỐI ƯU 3: Tránh phép chia (chia số thực rất tốn kém) bằng cách cache lại texel
        Texture2D map = EnvShadow_GetShadowMap();
        if (map.width != s_lastShadowMapWidth) {
            s_lastShadowMapWidth = map.width;
            s_cachedTexel = (map.width > 0) ? (1.0f / (float)map.width) : (1.0f / 1024.0f);
        }
        
        SetShaderValue(s_shader, s_locShadowTexel, &s_cachedTexel, SHADER_UNIFORM_FLOAT);
        
        // Bind the shadow map as texture0...
        rlSetTexture(map.id);
    }
}

void GroundShadow_End(void) {
    if (!s_ready) return;
    rlSetTexture(0); // restore default texture0 for subsequent map draws
    EndShaderMode();
}