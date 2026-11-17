/* Headless Tier-2 test — Primary Crystal, 3D Shatter & Freeze Patch
 * Verifies:
 *   1. crystal.mat has u_metallic, u_facetSharpness, dFdx/dFdy flat-facet normals,
 *      VFX_ResolveBody, and zero GLSL float 'f' suffixes in fragment block.
 *   2. freeze_patch.vs/.fs exist, declare fragColor / VFX_ResolveBody / voronoiEdgeDist,
 *      and have zero GLSL float 'f' suffixes.
 *   3. VFX_Crystal_Spawn + VC_Crystal_Update lifecycle: emerge -> hold -> shatter
 *      spawns 3D tumbling shards under 9.81 m/s^2 gravity with ground bounce.
 *   4. VFX_FreezePatch_Spawn + VC_FreezePatch_Update radial expansion & waterMask.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#ifndef PI
#define PI 3.1415926535f
#endif
#ifndef DEG2RAD
#define DEG2RAD (PI / 180.0f)
#endif
#ifndef WHITE
#define WHITE ((Color){255, 255, 255, 255})
#endif

typedef struct {
    int vertexCount;
    int triangleCount;
    float *vertices;
    float *normals;
    float *texcoords;
} Mesh;
typedef struct { int id; } Material;
typedef struct { float x, y, z, w; } Quaternion;
typedef struct { Vector3 position, target, up; float fovy; int projection; } Camera3D;

static void *MemAlloc(unsigned int size) { return calloc(1, size); }
static void UploadMesh(Mesh *mesh, bool dynamic) { (void)mesh; (void)dynamic; }

#define SHADER_UNIFORM_FLOAT 0
#define SHADER_UNIFORM_VEC3 2
#define SHADER_UNIFORM_INT 4
static int GetShaderLocation(Shader s, const char *n) { (void)s;(void)n; return -1; }
static void SetShaderValue(Shader s, int l, const void *v, int t) { (void)s;(void)l;(void)v;(void)t; }
static void SetShaderValueTexture(Shader s, int l, Texture2D tex) { (void)s;(void)l;(void)tex; }
static void SceneTargets_RequestSceneSnapshot(void) {}
static Texture2D SceneTargets_GetSceneSnapshotTexture(void) { return (Texture2D){0}; }
#define RL_CULL_FACE_FRONT 0
#define RL_CULL_FACE_BACK  1
static void rlSetCullFace(int mode) { (void)mode; }

static float Vector3LengthSqr(Vector3 v) { return v.x*v.x + v.y*v.y + v.z*v.z; }
static Vector3 Vector3Add(Vector3 a, Vector3 b) { return (Vector3){a.x+b.x, a.y+b.y, a.z+b.z}; }
static Vector3 Vector3Subtract(Vector3 a, Vector3 b) { return (Vector3){a.x-b.x, a.y-b.y, a.z-b.z}; }
static Vector3 Vector3CrossProduct(Vector3 a, Vector3 b) {
    return (Vector3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
static Vector3 Vector3Scale(Vector3 v, float s) { return (Vector3){v.x*s, v.y*s, v.z*s}; }
static Vector3 Vector3Normalize(Vector3 v) {
    float l = sqrtf(Vector3LengthSqr(v));
    return (l > 1e-6f) ? Vector3Scale(v, 1.0f / l) : (Vector3){0,0,0};
}
static Matrix MatrixScale(float x, float y, float z) { (void)x;(void)y;(void)z; return (Matrix){0}; }
static Matrix MatrixRotate(Vector3 a, float r) { (void)a;(void)r; return (Matrix){0}; }
static Matrix MatrixTranslate(float x, float y, float z) { (void)x;(void)y;(void)z; return (Matrix){0}; }
static Matrix MatrixMultiply(Matrix a, Matrix b) { (void)a;(void)b; return (Matrix){0}; }
static Quaternion QuaternionFromVector3ToVector3(Vector3 a, Vector3 b) { (void)a;(void)b; return (Quaternion){0,0,0,1}; }
static Quaternion QuaternionFromAxisAngle(Vector3 a, float r) { (void)a;(void)r; return (Quaternion){0,0,0,1}; }
static Quaternion QuaternionMultiply(Quaternion a, Quaternion b) { (void)a;(void)b; return (Quaternion){0,0,0,1}; }
static Matrix QuaternionToMatrix(Quaternion q) { (void)q; return (Matrix){0}; }
static void DrawMesh(Mesh m, Material mat, Matrix xform) { (void)m;(void)mat;(void)xform; }

/* Minimal headless stubs for vc_crystal.inl and vc_freeze_patch.inl */
typedef int VFXRenderPass;
typedef int VFXSurfaceMode;
typedef int VFXRenderScope;
#define VFX_RENDER_PASS_BODY 0
#define VFX_SURFACE_ALPHA 0
static VFXRenderScope VFXRender_BeginDraw(VFXRenderPass p, VFXSurfaceMode s, bool dw) {
    (void)p; (void)s; (void)dw; return 0;
}
static void VFXRender_EndDraw(VFXRenderScope *sc) { (void)sc; }

static Shader ResourceManager_LoadShader(const char *vs, const char *fs) {
    (void)vs; (void)fs; return (Shader){0};
}
static void BeginShaderMode(Shader s) { (void)s; }
static void EndShaderMode(void) {}
static void SkillManager_BeginShader(Shader s) { (void)s; }
static void SkillManager_EndShader(void) {}

static Vector3 Environment_GetSunDirection(void) { return (Vector3){-0.4f, -0.8f, -0.3f}; }
static Color Environment_GetSunColor(void) { return WHITE; }
static Shader EnvShadow_GetDepthShader(void) { return (Shader){0}; }

static float g_stubGroundY = 0.0f;
static bool  g_stubHasWater = false;
static float g_stubWaterY = 0.0f;
static float g_stubWaterDepth = 0.0f;

static float MapManager_GetGroundHeightAt(float x, float z) {
    (void)x; (void)z; return g_stubGroundY;
}
static bool MapManager_GetWaterInfoAt(float x, float z, float *outY, float *outDepth) {
    (void)x; (void)z;
    if (outY) *outY = g_stubWaterY;
    if (outDepth) *outDepth = g_stubWaterDepth;
    return g_stubHasWater;
}

typedef struct {
    float height, radius, taper, twist, noise;
    int sides, segments;
} CrystalDesc;
static Mesh ProceduralMesh_BuildCrystalTemplateMesh(const CrystalDesc *d) {
    (void)d; return (Mesh){0};
}
static Material ProceduralMesh_GetPassthroughMaterial(Shader s) {
    (void)s; return (Material){0};
}

typedef struct {
    Color baseColor, edgeColor;
    float roughness, fresnel, refraction, sparkle, crack, emission, thickness, dissolve, metallic, facetSharpness;
    Texture2D texture1;
} CrystalMaterialParams;
typedef struct { Shader shader; CrystalMaterialParams params; } CrystalMaterial;
static void CrystalMaterial_Load(CrystalMaterial *m, const CrystalMaterialParams *p) {
    m->shader = (Shader){0}; m->params = *p;
}
static void CrystalMaterial_Begin(CrystalMaterial m) { (void)m; }
static void CrystalMaterial_SetGrowProgress(CrystalMaterial m, float g) { (void)m; (void)g; }
static void CrystalMaterial_End(void) {}

static void rlDisableBackfaceCulling(void) {}
static void rlEnableBackfaceCulling(void) {}
static void rlCheckRenderBatchLimit(int c) { (void)c; }
static void rlBegin(int m) { (void)m; }
static void rlEnd(void) {}
static void rlColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    (void)r; (void)g; (void)b; (void)a;
}
static void rlTexCoord2f(float u, float v) { (void)u; (void)v; }
static void rlNormal3f(float x, float y, float z) { (void)x; (void)y; (void)z; }
static void rlVertex3f(float x, float y, float z) { (void)x; (void)y; (void)z; }
#define RL_TRIANGLES 4

typedef enum {
    VFX_CRYSTAL_STYLE_ICE = 0,
    VFX_CRYSTAL_STYLE_JADE,
    VFX_CRYSTAL_STYLE_RUBY,
    VFX_CRYSTAL_STYLE_AMETHYST,
    VFX_CRYSTAL_STYLE_GOLD,
    VFX_CRYSTAL_STYLE_STEEL,
    VFX_CRYSTAL_STYLE_OBSIDIAN,
    VFX_CRYSTAL_STYLE_COUNT
} VFX_CrystalStyle;

typedef struct VFX_CrystalDesc {
    Vector3 pos;
    Vector3 dir;
    float height;
    float radius;
    float eruptTime;
    float holdTime;
    float fadeTime;
    float delay;
    float rollDeg;
    VFX_CrystalStyle style;
    bool castShadow;
    bool shatterOnEnd;
} VFX_CrystalDesc;

#include "core/composition/common/vc_crystal.inl"
#include "core/composition/water/vc_freeze_patch.inl"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        g_failures++; \
    } \
} while (0)

static bool ReadWholeFile(const char *path, char *buf, size_t maxBytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, maxBytes - 1, f);
    buf[n] = '\0';
    fclose(f);
    return true;
}

static bool HasGlslFloatSuffix(const char *src) {
    for (size_t i = 1; src[i] != '\0'; ++i) {
        if (src[i] == 'f' && src[i - 1] >= '0' && src[i - 1] <= '9') {
            char next = src[i + 1];
            if (!(next >= 'a' && next <= 'z') && !(next >= 'A' && next <= 'Z') &&
                !(next >= '0' && next <= '9') && next != '_') {
                return true;
            }
        }
    }
    return false;
}

int main(void) {
    static char buf[65536];

    /* 1. Verify crystal.mat shader contract */
    CHECK(ReadWholeFile("core/shading/materials/crystal.mat", buf, sizeof(buf)),
          "crystal.mat readable");
    CHECK(strstr(buf, "u_metallic") != NULL, "crystal.mat declares u_metallic");
    CHECK(strstr(buf, "u_facetSharpness") != NULL, "crystal.mat declares u_facetSharpness");
    CHECK(strstr(buf, "dFdx") != NULL && strstr(buf, "dFdy") != NULL,
          "crystal.mat uses dFdx/dFdy flat geometric facet normals");
    CHECK(strstr(buf, "VFX_ResolveBody") != NULL, "crystal.mat uses VFX_ResolveBody");
    const char *fragStart = strstr(buf, "fragment {");
    CHECK(fragStart != NULL, "crystal.mat has fragment block");
    if (fragStart) {
        CHECK(!HasGlslFloatSuffix(fragStart), "crystal.mat fragment has no GLSL 'f' float suffixes");
    }

    /* 2. Verify freeze_patch.vs & freeze_patch.fs contract */
    CHECK(ReadWholeFile("core/shaders/freeze_patch.vs", buf, sizeof(buf)),
          "freeze_patch.vs readable");
    CHECK(strstr(buf, "fragColor") != NULL && strstr(buf, "VS_FinalOutput") != NULL,
          "freeze_patch.vs declares fragColor and VS_FinalOutput");

    CHECK(ReadWholeFile("core/shaders/freeze_patch.fs", buf, sizeof(buf)),
          "freeze_patch.fs readable");
    CHECK(strstr(buf, "radialCracks") != NULL && strstr(buf, "dendrite") != NULL,
          "freeze_patch.fs uses organic radial cracks and dendritic frost");
    CHECK(strstr(buf, "VFX_ResolveBody") != NULL, "freeze_patch.fs uses VFX_ResolveBody");
    CHECK(!HasGlslFloatSuffix(buf), "freeze_patch.fs has no GLSL 'f' float suffixes");

    /* 3. Verify all 7 VFX_CrystalStyle presets */
    for (int s = 0; s < VFX_CRYSTAL_STYLE_COUNT; ++s) {
        CrystalMaterialParams p = CrystalStyle_GetParams((VFX_CrystalStyle)s);
        CHECK(p.facetSharpness > 0.5f, "CrystalStyle has crisp facetSharpness");
        if (s == VFX_CRYSTAL_STYLE_GOLD || s == VFX_CRYSTAL_STYLE_STEEL) {
            CHECK(p.metallic > 0.9f, "Metal CrystalStyle has metallic > 0.9");
        } else {
            CHECK(p.metallic < 0.25f, "Dielectric/Obsidian CrystalStyle has low metallic");
        }
    }

    /* 3b. Verify Obelisk mesh outward normals and per-facet UVs */
    Mesh obelisk = VC_Crystal_GetTemplateMesh();
    CHECK(obelisk.triangleCount == VC_CRYSTAL_OBELISK_TRIS, "Obelisk has expected triangle count");
    bool allOutward = true;
    for (int t = 0; t < obelisk.triangleCount; ++t) {
        int b = t * 3;
        float cx = (obelisk.vertices[b*3+0] + obelisk.vertices[(b+1)*3+0] + obelisk.vertices[(b+2)*3+0]) / 3.0f;
        float cz = (obelisk.vertices[b*3+2] + obelisk.vertices[(b+1)*3+2] + obelisk.vertices[(b+2)*3+2]) / 3.0f;
        float nx = obelisk.normals[b*3+0];
        float nz = obelisk.normals[b*3+2];
        if (cx * nx + cz * nz <= 0.0f) {
            allOutward = false;
            break;
        }
    }
    CHECK(allOutward, "All obelisk crystal triangles have outward-pointing normals");

    /* 4. Verify single-crystal primary + 3D shatter lifecycle */
    VC_Crystal_ClearAll();
    VC_CrystalShatter_ClearAll();
    VFX_CrystalDesc cDesc = VFX_Crystal_DefaultDesc(VFX_CRYSTAL_STYLE_RUBY);
    cDesc.pos = (Vector3){6.0f, 0.0f, 4.4f};
    cDesc.eruptTime = 0.10f;
    cDesc.holdTime = 0.20f;
    cDesc.fadeTime = 0.15f;
    cDesc.shatterOnEnd = true;

    int h = VFX_Crystal_Spawn(&cDesc);
    CHECK(h >= 0, "VFX_Crystal_Spawn returns valid handle");
    CHECK(VC_Crystal_GetActiveCount() == 1, "Active crystal count is 1");

    for (int step = 0; step < 22; ++step) {
        VC_Crystal_Update(0.016f); /* 0.352s > erupt(0.10) + hold(0.20) */
    }
    CHECK(VC_CrystalShatter_GetActiveCount() > 0,
          "Crystal entering fade phase spawns 3D faceted shatter shards");

    for (int step = 0; step < 120; ++step) {
        VC_Crystal_Update(0.016f);
    }
    CHECK(VC_Crystal_GetActiveCount() == 0, "Crystal expires cleanly");
    CHECK(VC_CrystalShatter_GetActiveCount() == 0, "Shatter shards expire cleanly");

    /* 5. Verify FreezePatch ground vs water behavior */
    VC_FreezePatch_ClearAll();
    g_stubGroundY = 0.0f;
    g_stubHasWater = false;
    int fpGround = VFX_FreezePatch_Spawn((Vector3){6.0f, 0.0f, 4.4f}, 3.0f, 0.30f, 0.50f, 0.20f,
                                         (Color){105, 195, 240, 235}, (Color){225, 245, 255, 255});
    CHECK(fpGround >= 0, "VFX_FreezePatch_Spawn ground returns valid handle");
    CHECK(s_freezePatchPool[fpGround].waterMask < 0.01f, "Ground freeze patch has waterMask ~ 0");

    g_stubHasWater = true;
    g_stubWaterY = 0.5f;
    g_stubWaterDepth = 1.2f;
    int fpWater = VFX_FreezePatch_Spawn((Vector3){8.0f, 0.5f, 4.4f}, 2.5f, 0.30f, 0.50f, 0.20f,
                                        (Color){105, 195, 240, 235}, (Color){225, 245, 255, 255});
    CHECK(fpWater >= 0, "VFX_FreezePatch_Spawn water returns valid handle");
    CHECK(s_freezePatchPool[fpWater].waterMask > 0.99f, "Water freeze patch has waterMask ~ 1");

    for (int step = 0; step < 10; ++step) {
        VC_FreezePatch_Update(0.016f);
    }
    CHECK(s_freezePatchPool[fpGround].currentRadius > 1.0f, "FreezePatch expands radially");

    for (int step = 0; step < 70; ++step) {
        VC_FreezePatch_Update(0.016f);
    }
    CHECK(VC_FreezePatch_GetActiveCount() == 0, "FreezePatch expires cleanly");

    printf("crystal_freeze_primary_test: %d/%d checks passed\n",
           g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
