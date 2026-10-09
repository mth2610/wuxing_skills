/* Production shadow lifecycle with GPU allocation/state mocked.
 * cc -std=c99 -I. -Icore/tests/stubs environment/tests/shadow_quality_test.c
 *    -lm -o /tmp/wuxing_shadow_quality_test && /tmp/wuxing_shadow_quality_test
 * This checks policy and rollback, not shadow pixels or GPU performance.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "raylib.h"

static unsigned int nextId, allocationCount, failAllocation, liveCount;
static bool live[256], incomplete;
static unsigned int TestAllocate(void) {
    allocationCount++;
    if (allocationCount == failAllocation) return 0;
    unsigned int id = ++nextId;
    assert(id < 256);
    live[id] = true;
    liveCount++;
    return id;
}
static void TestDestroy(unsigned int id) {
    assert(id < 256 && live[id]);
    live[id] = false;
    liveCount--;
}
#define RESOURCE_MANAGER_H
#define ResourceManager_LoadShader(...) ((Shader){ .id = 900 })
#define RL_ATTACHMENT_COLOR_CHANNEL0 0
#define RL_ATTACHMENT_DEPTH 1
#define RL_ATTACHMENT_TEXTURE2D 2
#define RL_PIXELFORMAT_UNCOMPRESSED_R32 8
#define RL_MODELVIEW 0
#define RL_PROJECTION 1
#define TEXTURE_FILTER_POINT 0
#define TEXTURE_WRAP_CLAMP 0
#define LOG_INFO 0
#define LOG_WARNING 1
#define rlLoadTexture(...) TestAllocate()
#define rlLoadTextureDepth(...) TestAllocate()
#define rlLoadFramebuffer(...) TestAllocate()
#define rlUnloadFramebuffer(id) TestDestroy(id)
#define rlUnloadTexture(id) TestDestroy(id)
#define rlFramebufferComplete(...) (!incomplete)
#define rlFramebufferAttach(...) ((void)0)
#define rlEnableFramebuffer(...) ((void)0)
#define rlDisableFramebuffer(...) ((void)0)
#define rlDrawRenderBatchActive(...) ((void)0)
#define rlEnableDepthTest(...) ((void)0)
#define rlEnableDepthMask(...) ((void)0)
#define rlViewport(...) ((void)0)
#define rlClearColor(...) ((void)0)
#define rlClearScreenBuffers(...) ((void)0)
#define rlMatrixMode(...) ((void)0)
#define rlPushMatrix(...) ((void)0)
#define rlLoadIdentity(...) ((void)0)
#define rlOrtho(...) ((void)0)
#define rlMultMatrixf(...) ((void)0)
#define rlPopMatrix(...) ((void)0)
#define rlReadTexturePixels(...) NULL
#define SetTextureFilter(...) ((void)0)
#define SetTextureWrap(...) ((void)0)
#define BeginShaderMode(...) ((void)0)
#define EndShaderMode(...) ((void)0)
#define TraceLog(...) ((void)0)
#define MemFree(...) ((void)0)
#define MatrixLookAt(...) ((Matrix){0})
#define MatrixOrtho(...) ((Matrix){0})
#define MatrixMultiply(...) ((Matrix){0})
static Vector3 Vector3Normalize(Vector3 v) { return v; }
#define Vector3Subtract(...) ((Vector3){0})
#define Vector3Scale(...) ((Vector3){0})
#define Vector3Add(...) ((Vector3){0})
#define Vector3CrossProduct(...) ((Vector3){0})
#define Vector3DotProduct(...) 0.0f
#define Vector3DistanceSqr(...) 0.0f
static Vector3 Environment_GetSunDirection(void) { return (Vector3){0,-1,0}; }
#include "environment/env_shadow.c"

int main(void) {
    assert(EnvShadow_SetQuality(0));
    EnvShadow_Init();
    assert(EnvShadow_IsEnabled());
    assert(EnvShadow_GetShadowMap().width == 1024);
    assert(EnvShadow_GetStaticShadowMap().width == 512);
    assert(liveCount == 6);
    for (int tier = 0; tier < 4; tier++) {
        assert(EnvShadow_SetQuality(tier));
        assert(EnvShadow_GetQuality() == tier && EnvShadow_IsEnabled());
        assert(EnvShadow_GetShadowMap().width == (tier < 2 ? 1024 : 2048));
        assert(EnvShadow_GetStaticShadowMap().width == (tier < 2 ? 512 : 1024));
        assert(liveCount == 6);
    }
    unsigned int oldDynamic = EnvShadow_GetShadowMap().id;
    unsigned int oldStatic = EnvShadow_GetStaticShadowMap().id;
    failAllocation = allocationCount + 4; // Static replacement fails after dynamic succeeds.
    assert(!EnvShadow_SetQuality(0));
    assert(EnvShadow_GetQuality() == 3 && EnvShadow_IsEnabled());
    assert(EnvShadow_GetShadowMap().id == oldDynamic);
    assert(EnvShadow_GetStaticShadowMap().id == oldStatic && liveCount == 6);
    incomplete = true;
    assert(!EnvShadow_SetQuality(0));
    assert(EnvShadow_GetShadowMap().id == oldDynamic && liveCount == 6);
    incomplete = false;
    s_capturing = true;
    assert(!EnvShadow_SetQuality(0));
    assert(EnvShadow_GetQuality() == 3 && liveCount == 6);
    s_capturing = false;
    s_staticCacheValid = true;
    assert(EnvShadow_SetQuality(0));
    assert(!s_staticCacheValid && liveCount == 6);
    EnvShadow_SetEnabled(false);
    assert(EnvShadow_SetQuality(2));
    assert(!EnvShadow_IsEnabled()); // Explicit diagnostic toggle stays authoritative.
    puts("shadow_quality_test: passed all tiers, enabled default, rollback, cleanup, capture guard, cache invalidation");
    return 0;
}
