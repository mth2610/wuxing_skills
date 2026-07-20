#include "maps/toolkit/ground_shadow.h"
#include "core/resource_manager.h"
#include "environment/env_shadow.h"
#include "raylib.h"
#include "rlgl.h" // rlSetTexture
#include <stddef.h>

static Shader s_shader;
static bool   s_ready = false;
static int    s_locLightVP, s_locShadowEnabled, s_locShadowTexel;

static void EnsureLoaded(void) {
    if (s_ready) return;
    s_shader = ResourceManager_LoadShader("maps/toolkit/shaders/ground_shadow.vs",
                                           "maps/toolkit/shaders/ground_shadow.fs");
    if (s_shader.id == 0) return;

    s_locLightVP       = GetShaderLocation(s_shader, "u_lightVP");
    s_locShadowEnabled = GetShaderLocation(s_shader, "u_shadowEnabled");
    s_locShadowTexel   = GetShaderLocation(s_shader, "u_shadowTexel");
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
    if (!s_ready) { return; }
    BeginShaderMode(s_shader);

    float enabled = EnvShadow_IsEnabled() ? 1.0f : 0.0f;
    SetShaderValue(s_shader, s_locShadowEnabled, &enabled, SHADER_UNIFORM_FLOAT);
    if (EnvShadow_IsEnabled()) {
        Matrix lightVP = EnvShadow_GetLightVP();
        SetShaderValueMatrix(s_shader, s_locLightVP, lightVP);
        Texture2D map = EnvShadow_GetShadowMap();
        float texel = (map.width > 0) ? (1.0f / (float)map.width) : (1.0f / 1024.0f);
        SetShaderValue(s_shader, s_locShadowTexel, &texel, SHADER_UNIFORM_FLOAT);
        // Bind the shadow map as texture0 (the default diffuse sampler), the
        // one texture-binding path proven to work under rlvk — the on-screen
        // debug preview samples the same texture fine via DrawTexturePro,
        // which also goes through texture0. A custom `sampler2D shadowMap`
        // uniform via SetShaderValueTexture read as unbound/white here (~1.0 =
        // far -> nothing ever occluded). See ground_shadow.fs.
        rlSetTexture(map.id);
    }
}

void GroundShadow_End(void) {
    if (!s_ready) return;
    rlSetTexture(0); // restore default texture0 for subsequent map draws (grid lines, etc.)
    EndShaderMode();
}
