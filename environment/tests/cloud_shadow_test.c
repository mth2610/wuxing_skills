/* Standalone arithmetic/lifecycle test using production environment code.
 * cc -std=c99 -I. -Icore/tests/stubs environment/tests/cloud_shadow_test.c
 *    -lm -o /tmp/wuxing_cloud_test && /tmp/wuxing_cloud_test
 */
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "raylib.h"
#include "core/wind/wind_system.h"

static WindMacroConfig testWind;
static unsigned int textureLoads;
static bool failTexture;
WindMacroConfig Wind_GetMacro(void) { return testWind; }
#define RESOURCE_MANAGER_H
Texture2D ResourceManager_LoadTexture(const char *path) {
    (void)path;
    textureLoads++;
    return (Texture2D){ .id = failTexture ? 0 : 1, .width = 64, .height = 64 };
}
#define TEXTURE_FILTER_BILINEAR 1
#define TEXTURE_WRAP_REPEAT 0
static void SetTextureFilter(Texture2D texture, int filter) { (void)texture; (void)filter; }
static void SetTextureWrap(Texture2D texture, int wrap) { (void)texture; (void)wrap; }
#define SHADER_UNIFORM_VEC4 0
#define SHADER_UNIFORM_VEC2 1
#define SHADER_UNIFORM_INT 2
static unsigned int locationQueries;
static int GetShaderLocation(Shader shader, const char *name) { (void)shader; (void)name; locationQueries++; return 0; }
static void SetShaderValue(Shader shader, int location, const void *value, int type) { (void)shader; (void)location; (void)value; (void)type; }
static Vector3 Vector3Normalize(Vector3 v) {
    float length = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (length > 0) { v.x /= length; v.y /= length; v.z /= length; }
    return v;
}
static Vector3 Vector3Lerp(Vector3 a, Vector3 b, float t) {
    return (Vector3){ a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t };
}
float MapManager_GetGroundHeightAt(float x, float z) { (void)x; (void)z; return 0; }
bool MapManager_GetWaterInfoAt(float x, float z, float *surface, float *depth) {
    (void)x; (void)z; (void)surface; (void)depth;
    return false;
}
#define RL_TRIANGLES 0
#define RL_QUADS 1
#define rlBegin(...) ((void)0)
#define rlEnd(...) ((void)0)
#define rlSetTexture(...) ((void)0)
#define rlDrawRenderBatchActive(...) ((void)0)
#define rlDisableDepthTest(...) ((void)0)
#define rlDisableDepthMask(...) ((void)0)
#define rlDisableBackfaceCulling(...) ((void)0)
#define rlEnableDepthTest(...) ((void)0)
#define rlEnableDepthMask(...) ((void)0)
#define rlEnableBackfaceCulling(...) ((void)0)
#define rlColor4ub(...) ((void)0)
#define rlVertex3f(...) ((void)0)
#define rlActiveTextureSlot(...) ((void)0)
#define rlEnableTexture(...) ((void)0)
#include "environment/environment_system.c"

static bool near(float a, float b) { return fabsf(a-b) < 0.00001f; }

int main(void) {
    Environment_Init();
    // Hemisphere lighting must preserve night darkness and configured color,
    // including a bright sky clamp without lifting the ground hemisphere.
    Environment_SetAmbientColor((Color){ 0, 0, 0, 255 });
    Color sky = Environment_GetSkyAmbient();
    Color ground = Environment_GetGroundAmbient();
    assert(sky.r == 0 && sky.g == 0 && sky.b == 0);
    assert(ground.r == 0 && ground.g == 0 && ground.b == 0);
    Environment_SetAmbientColor((Color){ 80, 100, 120, 255 });
    sky = Environment_GetSkyAmbient();
    ground = Environment_GetGroundAmbient();
    assert(sky.r == 100 && sky.g == 125 && sky.b == 162);
    assert(ground.r == 44 && ground.g == 45 && ground.b == 48);
    Environment_SetAmbientColor((Color){ 240, 240, 240, 255 });
    sky = Environment_GetSkyAmbient();
    ground = Environment_GetGroundAmbient();
    assert(sky.r == 255 && sky.g == 255 && sky.b == 255);
    assert(ground.r < 140 && ground.g < 120 && ground.b < 110);
    assert(Environment_GetCloudShadowFrame().uvTransform.w == 0);
    assert(textureLoads == 0);
    EnvCloudShadowConfig config = {
        .enabled = true, .strength = .12f, .worldSize = 96,
        .planeHeight = 80, .coverage = .48f, .softness = .16f, .windSpeedScale = .55f
    };
    Environment_SetCloudShadowConfig(&config);
    Environment_SetSunDirection((Vector3){ -.6f, -.7f, .6f });
    assert(near(Environment_GetCloudShadowFrame().uvTransform.w, .12f));
    testWind.baseDirection = (Vector3){ 2, 0, -1 };
    unsigned int lightVersion = Environment_GetFrameLighting().version;
    unsigned int cloudVersion = Environment_GetCloudShadowFrame().version;
    Environment_Update(1);
    EnvCloudShadowFrame frame = Environment_GetCloudShadowFrame();
    assert(near(frame.uvTransform.y, 1.1f / 96));
    assert(near(frame.uvTransform.z, 1 - .55f / 96));
    assert(frame.version == cloudVersion);
    assert(Environment_GetFrameLighting().version == lightVersion);
    Environment_Update(NAN);
    assert(near(Environment_GetCloudShadowFrame().uvTransform.y, frame.uvTransform.y));
    Environment_SetCloudShadowConfig(NULL);
    Environment_SetCloudShadowConfig(&config);
    for (int i=0; i<120; i++) Environment_Update(1.0f / 120);
    assert(near(Environment_GetCloudShadowFrame().uvTransform.y, frame.uvTransform.y));
    assert(near(Environment_GetCloudShadowFrame().uvTransform.z, frame.uvTransform.z));
    assert(textureLoads == 1);
    Environment_BindCloudShadowShader((Shader){ .id = 7 });
    Environment_BindCloudShadowShader((Shader){ .id = 7 });
    assert(locationQueries == 4);
    Environment_SetSunDirection((Vector3){ 1, .1f, 0 });
    assert(Environment_GetCloudShadowFrame().uvTransform.w == 0);
    config.worldSize = NAN; config.softness = -1; config.strength = 3;
    Environment_SetCloudShadowConfig(&config);
    config = Environment_GetCloudShadowConfig();
    assert(config.worldSize == 96 && config.softness == .01f && config.strength == 1);
    Environment_SetCloudShadowConfig(NULL);
    assert(Environment_GetCloudShadowFrame().uvTransform.w == 0);
    assert(Environment_GetCloudShadowFrame().uvTransform.y == 0);
    Environment_Init();
    failTexture = true;
    config.enabled = true;
    Environment_SetCloudShadowConfig(&config);
    Environment_SetSunDirection((Vector3){ 0, -1, 0 });
    assert(Environment_GetCloudShadowFrame().uvTransform.w == 0);
    puts("cloud_shadow_test: passed default, transport, timestep, versions, lifecycle, invalid input, horizon, load failure");
    return 0;
}
