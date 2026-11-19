// game/horde_renderer.c
// Zero-lag instanced renderer for Open-World Vampire-Survivors hordes,
// boss-wave minions, minion bolts, and batched ground shadow discs.
// Supports both:
//   1. GPU-Instanced Skeletal Animation (baked RGBA32F bone-matrix texture + vertexBoneIds/Weights)
//   2. Legacy/Static Mesh Instancing (for rigid/floating constructs)
#include "game/horde_renderer.h"
#include "ai/ai.h"
#include "entities/entities.h"
#include "environment/environment_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HORDE_MAX_INSTANCES 256

// -----------------------------------------------------------------------------
// 1. GPU-Instanced Skeletal Shader (100+ skinned monsters in 1 draw call!)
// -----------------------------------------------------------------------------
static const char *HORDE_SKINNED_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexBoneIds;\n"
    "in vec4 vertexBoneWeights;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "uniform sampler2D u_boneTex;\n"
    "uniform int u_boneCount;\n"
    "out vec3 fragNormal;\n"
    "out vec2 fragTexCoord;\n"
    "out vec3 fragWorldPos;\n"
    "out float fragTelegraph;\n"
    "out float fragHitFlash;\n"
    "\n"
    "mat4 FetchBoneMat(int boneIdx, int row) {\n"
    "    int bx = clamp(boneIdx, 0, u_boneCount - 1) * 4;\n"
    "    return mat4(\n"
    "        texelFetch(u_boneTex, ivec2(bx + 0, row), 0),\n"
    "        texelFetch(u_boneTex, ivec2(bx + 1, row), 0),\n"
    "        texelFetch(u_boneTex, ivec2(bx + 2, row), 0),\n"
    "        texelFetch(u_boneTex, ivec2(bx + 3, row), 0)\n"
    "    );\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    float frameRow      = instanceTransform[0][3];\n"
    "    float telegraphFrac = instanceTransform[1][3];\n"
    "    float hitFlash      = instanceTransform[2][3];\n"
    "    mat4 instMat = instanceTransform;\n"
    "    instMat[0][3] = 0.0;\n"
    "    instMat[1][3] = 0.0;\n"
    "    instMat[2][3] = 0.0;\n"
    "    instMat[3][3] = 1.0;\n"
    "\n"
    "    int r0 = int(floor(frameRow));\n"
    "    int r1 = r0 + 1;\n"
    "    float frac = frameRow - float(r0);\n"
    "    // Clamp r1 within same clip if caller passed exact end, or let caller wrap frameRow\n"
    "    vec4 localPos = vec4(0.0);\n"
    "    vec3 localNrm = vec3(0.0);\n"
    "    float totalW = 0.0;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float w = vertexBoneWeights[i];\n"
    "        if (w > 0.0001) {\n"
    "            int b = int(vertexBoneIds[i] + 0.5);\n"
    "            mat4 m0 = FetchBoneMat(b, r0);\n"
    "            mat4 m1 = FetchBoneMat(b, r1);\n"
    "            mat4 bm = m0 * (1.0 - frac) + m1 * frac;\n"
    "            localPos += (bm * vec4(vertexPosition, 1.0)) * w;\n"
    "            localNrm += (mat3(bm) * vertexNormal) * w;\n"
    "            totalW += w;\n"
    "        }\n"
    "    }\n"
    "    if (totalW < 0.001) {\n"
    "        localPos = vec4(vertexPosition, 1.0);\n"
    "        localNrm = vertexNormal;\n"
    "    }\n"
    "\n"
    "    vec4 worldPos = instMat * localPos;\n"
    "    fragNormal = normalize(mat3(instMat) * localNrm);\n"
    "    fragTexCoord = vertexTexCoord;\n"
    "    fragWorldPos = worldPos.xyz;\n"
    "    fragTelegraph = telegraphFrac;\n"
    "    fragHitFlash = hitFlash;\n"
    "    gl_Position = mvp * worldPos;\n"
    "}\n";

static const char *HORDE_SKINNED_FS =
    "#version 330\n"
    "in vec3 fragNormal;\n"
    "in vec2 fragTexCoord;\n"
    "in vec3 fragWorldPos;\n"
    "in float fragTelegraph;\n"
    "in float fragHitFlash;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D u_emissiveTex;\n"
    "uniform float u_hasEmissiveTex;\n"
    "uniform vec3 u_sunToLight;\n"
    "uniform vec3 u_sunColor;\n"
    "uniform vec3 u_skyColor;\n"
    "uniform vec3 u_groundColor;\n"
    "uniform vec3 u_viewPos;\n"
    "uniform vec3 u_fogColor;\n"
    "uniform float u_fogStart;\n"
    "uniform float u_fogEnd;\n"
    "uniform float u_fogEnabled;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(fragNormal);\n"
    "    vec3 L = normalize(u_sunToLight);\n"
    "    vec3 V = normalize(u_viewPos - fragWorldPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "\n"
    "    vec3 albedo = texture(texture0, fragTexCoord).rgb;\n"
    "    vec3 emissive = (u_hasEmissiveTex > 0.5) ? texture(u_emissiveTex, fragTexCoord).rgb * 2.2 : vec3(0.0);\n"
    "\n"
    "    float ndl = dot(N, L);\n"
    "    float wrap = ndl * 0.5 + 0.5;\n"
    "    wrap *= wrap;\n"
    "    vec3 diffuse = albedo * u_sunColor * wrap * 0.78;\n"
    "\n"
    "    float hemi = N.y * 0.5 + 0.5;\n"
    "    vec3 ambient = albedo * mix(u_groundColor * 1.15, u_skyColor * 0.95, hemi);\n"
    "\n"
    "    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.28 * smoothstep(-0.1, 0.25, ndl);\n"
    "    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);\n"
    "    vec3 rim = mix(u_skyColor, u_sunColor, 0.35) * (fres * 0.36);\n"
    "\n"
    "    vec3 col = ambient + diffuse + u_sunColor * spec + rim + emissive;\n"
    "\n"
    "    if (fragTelegraph > 0.05) {\n"
    "        float tGlow = smoothstep(0.05, 0.85, fragTelegraph);\n"
    "        col = mix(col, vec3(1.15, 0.24, 0.10), tGlow * 0.45);\n"
    "        col += vec3(0.55, 0.10, 0.03) * (tGlow * (0.5 + 0.5 * fres));\n"
    "    }\n"
    "    if (fragHitFlash > 0.05) {\n"
    "        col = mix(col, vec3(1.45, 1.42, 1.38), clamp(fragHitFlash, 0.0, 1.0) * 0.85);\n"
    "    }\n"
    "\n"
    "    if (u_fogEnabled > 0.5) {\n"
    "        float dist = length(u_viewPos - fragWorldPos);\n"
    "        float f = clamp((dist - u_fogStart) / max(u_fogEnd - u_fogStart, 0.001), 0.0, 1.0);\n"
    "        col = mix(col, u_fogColor, f * 0.78);\n"
    "    }\n"
    "\n"
    "    finalColor = vec4(col, 1.0);\n"
    "}\n";

// -----------------------------------------------------------------------------
// 2. Non-Skinned Fallback / Construct Shader (Caster Idol, Shadows, Bolts)
// -----------------------------------------------------------------------------
static const char *HORDE_VS =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec2 vertexTexCoord;\n"
    "in vec3 vertexNormal;\n"
    "in vec4 vertexColor;\n"
    "in vec2 vertexTexCoord2;\n"
    "in mat4 instanceTransform;\n"
    "uniform mat4 mvp;\n"
    "uniform float u_time;\n"
    "uniform int u_kind;\n"
    "out vec3 fragNormal;\n"
    "out vec3 fragLocalPos;\n"
    "out vec3 fragWorldPos;\n"
    "out vec4 fragColor;\n"
    "out float fragTelegraph;\n"
    "out float fragHitFlash;\n"
    "out float fragAnimPhase;\n"
    "\n"
    "vec3 RotX(vec3 v, float a) { float c = cos(a), s = sin(a); return vec3(v.x, c*v.y - s*v.z, s*v.y + c*v.z); }\n"
    "vec3 RotY(vec3 v, float a) { float c = cos(a), s = sin(a); return vec3(c*v.x + s*v.z, v.y, -s*v.x + c*v.z); }\n"
    "\n"
    "void main() {\n"
    "    float animPhase     = instanceTransform[0][3];\n"
    "    float telegraphFrac = instanceTransform[1][3];\n"
    "    float hitFlash      = instanceTransform[2][3];\n"
    "    float instState     = instanceTransform[3][3];\n"
    "    mat4 instMat = instanceTransform;\n"
    "    instMat[0][3] = 0.0; instMat[1][3] = 0.0; instMat[2][3] = 0.0; instMat[3][3] = 1.0;\n"
    "\n"
    "    vec3 pos = vertexPosition;\n"
    "    vec3 nrm = vertexNormal;\n"
    "    float grp = vertexTexCoord2.x;\n"
    "    float w   = clamp(vertexTexCoord2.y, 0.0, 1.0);\n"
    "    float isStrike = step(3.5, instState) * step(instState, 4.5);\n"
    "\n"
    "    if (u_kind == 1) {\n"
    "        float hoverY = sin(u_time * 2.8 + animPhase) * 0.085;\n"
    "        if (grp > 0.88) {\n"
    "            float ang = u_time * 2.4 + animPhase * 0.35;\n"
    "            pos = RotY(pos, ang);\n"
    "            nrm = RotY(nrm, ang);\n"
    "            pos.y += sin(u_time * 3.4 + animPhase + pos.x * 4.0) * 0.07 * w;\n"
    "        } else if (grp > 0.62) {\n"
    "            float pulse = 1.0 + 0.22 * sin(u_time * 5.2 + animPhase) + 0.35 * telegraphFrac;\n"
    "            vec3 c = vec3(0.0, 0.74, 0.0);\n"
    "            pos = c + (pos - c) * pulse;\n"
    "            pos.z += (0.24 * isStrike - 0.10 * telegraphFrac) * w;\n"
    "        } else if (grp > 0.12) {\n"
    "            float sideSign = (grp < 0.38) ? 1.0 : -1.0;\n"
    "            pos.y += sin(u_time * 3.0 + animPhase + sideSign * 1.4) * 0.11 * w;\n"
    "            pos.x += sideSign * (0.08 * telegraphFrac) * w;\n"
    "        }\n"
    "        pos = RotX(pos, 0.14 - 0.22 * telegraphFrac + 0.25 * isStrike);\n"
    "        pos.y += hoverY;\n"
    "    }\n"
    "\n"
    "    vec4 worldPos = instMat * vec4(pos, 1.0);\n"
    "    fragNormal = normalize(mat3(instMat) * nrm);\n"
    "    fragLocalPos = pos;\n"
    "    fragWorldPos = worldPos.xyz;\n"
    "    fragColor = vertexColor;\n"
    "    fragTelegraph = telegraphFrac;\n"
    "    fragHitFlash = hitFlash;\n"
    "    fragAnimPhase = animPhase;\n"
    "    gl_Position = mvp * worldPos;\n"
    "}\n";

static const char *HORDE_FS =
    "#version 330\n"
    "in vec3 fragNormal;\n"
    "in vec3 fragLocalPos;\n"
    "in vec3 fragWorldPos;\n"
    "in vec4 fragColor;\n"
    "in float fragTelegraph;\n"
    "in float fragHitFlash;\n"
    "in float fragAnimPhase;\n"
    "uniform vec4 u_tint;\n"
    "uniform vec4 u_emissive;\n"
    "uniform vec3 u_sunToLight;\n"
    "uniform vec3 u_sunColor;\n"
    "uniform vec3 u_skyColor;\n"
    "uniform vec3 u_groundColor;\n"
    "uniform vec3 u_viewPos;\n"
    "uniform vec3 u_fogColor;\n"
    "uniform float u_fogStart;\n"
    "uniform float u_fogEnd;\n"
    "uniform float u_fogEnabled;\n"
    "uniform float u_time;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec3 N = normalize(fragNormal);\n"
    "    vec3 L = normalize(u_sunToLight);\n"
    "    vec3 V = normalize(u_viewPos - fragWorldPos);\n"
    "    vec3 H = normalize(L + V);\n"
    "    float matClass = fragColor.a;\n"
    "    float isEmissive = smoothstep(0.80, 0.98, matClass);\n"
    "    float isMetallic = smoothstep(0.35, 0.55, matClass) * (1.0 - isEmissive);\n"
    "    float grain = sin(fragLocalPos.x * 28.0 + fragLocalPos.z * 23.0) * cos(fragLocalPos.y * 31.0);\n"
    "    float cavityAO = clamp(0.68 + 0.34 * fragLocalPos.y, 0.55, 1.05);\n"
    "    vec3 albedo = fragColor.rgb * u_tint.rgb * (1.0 + 0.08 * grain) * cavityAO;\n"
    "    float ndl = dot(N, L);\n"
    "    float wrap = ndl * 0.5 + 0.5; wrap *= wrap;\n"
    "    vec3 diffuse = albedo * u_sunColor * wrap * 0.72;\n"
    "    float hemi = N.y * 0.5 + 0.5;\n"
    "    vec3 ambient = albedo * mix(u_groundColor * 1.15, u_skyColor * 0.95, hemi);\n"
    "    float spec = pow(max(dot(N, H), 0.0), mix(20.0, 44.0, isMetallic)) * mix(0.14, 0.48, isMetallic) * smoothstep(-0.1, 0.25, ndl);\n"
    "    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);\n"
    "    vec3 rim = mix(u_skyColor, u_sunColor, 0.35) * (fres * mix(0.28, 0.50, isMetallic));\n"
    "    float pulse = 0.88 + 0.16 * sin(u_time * 4.5 + fragAnimPhase);\n"
    "    vec3 selfGlow = fragColor.rgb * (isEmissive * 1.85 * pulse) + u_emissive.rgb;\n"
    "    vec3 col = ambient + diffuse + u_sunColor * spec + rim + selfGlow;\n"
    "    if (fragTelegraph > 0.05) {\n"
    "        float tGlow = smoothstep(0.05, 0.85, fragTelegraph);\n"
    "        col = mix(col, vec3(1.15, 0.24, 0.10), tGlow * 0.45);\n"
    "    }\n"
    "    if (fragHitFlash > 0.05) {\n"
    "        col = mix(col, vec3(1.45, 1.42, 1.38), clamp(fragHitFlash, 0.0, 1.0) * 0.85);\n"
    "    }\n"
    "    if (u_fogEnabled > 0.5) {\n"
    "        float dist = length(u_viewPos - fragWorldPos);\n"
    "        float f = clamp((dist - u_fogStart) / max(u_fogEnd - u_fogStart, 0.001), 0.0, 1.0);\n"
    "        col = mix(col, u_fogColor, f * 0.78);\n"
    "    }\n"
    "    finalColor = vec4(col, u_tint.a);\n"
    "}\n";

// -----------------------------------------------------------------------------
// Data Structures
// -----------------------------------------------------------------------------
typedef enum {
    HORDE_CLIP_IDLE = 0,
    HORDE_CLIP_MOVE,
    HORDE_CLIP_TELEGRAPH,
    HORDE_CLIP_STRIKE,
    HORDE_CLIP_DEATH,
    HORDE_CLIP_COUNT
} HordeClipSlot;

typedef struct {
    bool      ready;
    Model     model;
    Material  mat;
    Texture2D boneTex;       // RGBA32F: (boneCount*4) x totalFrames
    Texture2D emissiveTex;
    bool      hasEmissiveTex;
    int       boneCount;
    int       clipStartRow[HORDE_CLIP_COUNT];
    int       clipFrameCount[HORDE_CLIP_COUNT];
} HordeSkinnedArchetype;

static bool     s_ready = false;

static Shader   s_shader = { 0 };
static Material s_mat = { 0 };
static int      s_locTint = -1, s_locEmissive = -1, s_locTime = -1, s_locKind = -1;
static int      s_locSunToLight = -1, s_locSunColor = -1, s_locSkyColor = -1, s_locGroundColor = -1;
static int      s_locViewPos = -1, s_locFogColor = -1, s_locFogStart = -1, s_locFogEnd = -1, s_locFogEnabled = -1;

static Shader   s_skinnedShader = { 0 };
static int      s_skLocBoneTex = -1, s_skLocBoneCount = -1, s_skLocEmissiveTex = -1, s_skLocHasEmissive = -1;
static int      s_skLocSunToLight = -1, s_skLocSunColor = -1, s_skLocSkyColor = -1, s_skLocGroundColor = -1;
static int      s_skLocViewPos = -1, s_skLocFogColor = -1, s_skLocFogStart = -1, s_skLocFogEnd = -1, s_skLocFogEnabled = -1;

static HordeSkinnedArchetype s_skinned[4] = { 0 };
static Mesh s_meshKind[4] = { 0 };
static Mesh s_meshShadow = { 0 };
static Mesh s_meshBolt = { 0 };

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static Matrix TransformToMatrix(Transform t) {
    return MatrixMultiply(
        MatrixMultiply(MatrixScale(t.scale.x, t.scale.y, t.scale.z),
                       QuaternionToMatrix(t.rotation)),
        MatrixTranslate(t.translation.x, t.translation.y, t.translation.z));
}

static int FindAnimBoneIndex(const ModelAnimation *anim, const char *boneName, int hint) {
    (void)boneName;
    return (hint >= 0 && hint < anim->boneCount) ? hint : -1;
}

static HordeClipSlot ClassifyClipName(const char *name) {
    if (!name) return HORDE_CLIP_MOVE;
    if (strcmp(name, "idle") == 0 || strstr(name, "Idle") || strstr(name, "idle")) return HORDE_CLIP_IDLE;
    if (strcmp(name, "running") == 0 || strcmp(name, "walk") == 0 || strstr(name, "Slither")) return HORDE_CLIP_MOVE;
    if (strstr(name, "Telegraph") || strstr(name, "telegraph") || strcmp(name, "casting") == 0) return HORDE_CLIP_TELEGRAPH;
    if (strstr(name, "Strike") || strstr(name, "strike") || strstr(name, "Attack") ||
        strstr(name, "punch") || strstr(name, "swiping") || strstr(name, "slammer")) return HORDE_CLIP_STRIKE;
    if (strstr(name, "Death") || strstr(name, "death") || strcmp(name, "jumping_down") == 0) return HORDE_CLIP_DEATH;
    return HORDE_CLIP_MOVE;
}

static bool LoadSkinnedArchetype(HordeSkinnedArchetype *out, const char *meshGlbPath, const char *animGlbPath) {
    memset(out, 0, sizeof(*out));
    if (!meshGlbPath || !FileExists(meshGlbPath)) return false;
    const char *aPath = (animGlbPath && FileExists(animGlbPath)) ? animGlbPath : meshGlbPath;

    Model mdl = LoadModel(meshGlbPath);
    if (mdl.meshCount <= 0 || mdl.skeleton.boneCount <= 0 || !mdl.skeleton.bindPose) {
        if (mdl.meshCount > 0) UnloadModel(mdl);
        return false;
    }

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(aPath, &animCount);
    if (!anims || animCount <= 0) {
        UnloadModel(mdl);
        return false;
    }

    // Map clips to slots
    int slotAnimIdx[HORDE_CLIP_COUNT];
    for (int s = 0; s < HORDE_CLIP_COUNT; s++) slotAnimIdx[s] = -1;
    for (int a = 0; a < animCount; a++) {
        HordeClipSlot slot = ClassifyClipName(anims[a].name);
        if (slotAnimIdx[slot] < 0) slotAnimIdx[slot] = a;
    }
    int fallbackAnim = (slotAnimIdx[HORDE_CLIP_MOVE] >= 0) ? slotAnimIdx[HORDE_CLIP_MOVE] : 0;
    for (int s = 0; s < HORDE_CLIP_COUNT; s++) {
        if (slotAnimIdx[s] < 0) slotAnimIdx[s] = fallbackAnim;
    }

    // Count total frames (add +1 duplicate end frame per clip so linear interpolation in VS never bleeds into next clip)
    int totalRows = 0;
    for (int s = 0; s < HORDE_CLIP_COUNT; s++) {
        int aIdx = slotAnimIdx[s];
        int kc = anims[aIdx].keyframeCount;
        if (kc < 1) kc = 1;
        out->clipStartRow[s] = totalRows;
        out->clipFrameCount[s] = kc;
        totalRows += kc + 1;
    }

    int boneCount = mdl.skeleton.boneCount;
    int texW = boneCount * 4;
    int texH = totalRows;

    float *pixels = (float *)MemAlloc((size_t)texW * (size_t)texH * 4u * sizeof(float));
    Matrix *invBind = (Matrix *)MemAlloc((size_t)boneCount * sizeof(Matrix));
    if (!pixels || !invBind) {
        MemFree(pixels); MemFree(invBind);
        UnloadModelAnimations(anims, animCount);
        UnloadModel(mdl);
        return false;
    }

    for (int b = 0; b < boneCount; b++) {
        invBind[b] = MatrixInvert(TransformToMatrix(mdl.skeleton.bindPose[b]));
    }

    // Bake skinning matrices: inv(bindPose) * poseMatrix for every clip & frame
    for (int s = 0; s < HORDE_CLIP_COUNT; s++) {
        const ModelAnimation *anim = &anims[slotAnimIdx[s]];
        int kc = out->clipFrameCount[s];
        int startRow = out->clipStartRow[s];

        for (int f = 0; f <= kc; f++) {
            int srcFrame = (f < kc) ? f : ((s == HORDE_CLIP_IDLE || s == HORDE_CLIP_MOVE) ? 0 : (kc - 1));
            int row = startRow + f;
            for (int b = 0; b < boneCount; b++) {
                int ab = FindAnimBoneIndex(anim, mdl.skeleton.bones[b].name, b);
                Matrix poseMat = (ab >= 0 && anim->keyframePoses)
                                     ? TransformToMatrix(anim->keyframePoses[srcFrame][ab])
                                     : TransformToMatrix(mdl.skeleton.bindPose[b]);
                Matrix skin = MatrixMultiply(invBind[b], poseMat);

                int idx = (row * texW + b * 4) * 4;
                pixels[idx + 0]  = skin.m0;  pixels[idx + 1]  = skin.m1;  pixels[idx + 2]  = skin.m2;  pixels[idx + 3]  = skin.m3;
                pixels[idx + 4]  = skin.m4;  pixels[idx + 5]  = skin.m5;  pixels[idx + 6]  = skin.m6;  pixels[idx + 7]  = skin.m7;
                pixels[idx + 8]  = skin.m8;  pixels[idx + 9]  = skin.m9;  pixels[idx + 10] = skin.m10; pixels[idx + 11] = skin.m11;
                pixels[idx + 12] = skin.m12; pixels[idx + 13] = skin.m13; pixels[idx + 14] = skin.m14; pixels[idx + 15] = skin.m15;
            }
        }
    }

    MemFree(invBind);
    UnloadModelAnimations(anims, animCount);

    Image img = {
        .data = pixels,
        .width = texW,
        .height = texH,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32
    };
    out->boneTex = LoadTextureFromImage(img);
    SetTextureFilter(out->boneTex, TEXTURE_FILTER_POINT);
    SetTextureWrap(out->boneTex, TEXTURE_WRAP_CLAMP);
    MemFree(pixels);

    out->model = mdl;
    out->boneCount = boneCount;
    out->mat = mdl.materials[0];
    out->mat.shader = s_skinnedShader;
    if (mdl.materials[0].maps[MATERIAL_MAP_EMISSION].texture.id > 0) {
        out->emissiveTex = mdl.materials[0].maps[MATERIAL_MAP_EMISSION].texture;
        out->hasEmissiveTex = true;
    }
    out->ready = (out->boneTex.id > 0);
    TraceLog(LOG_INFO, "HORDE_RENDERER: Skinned archetype '%s' ready (bones=%d, clips=%d, boneTex=%dx%d)",
             meshGlbPath, boneCount, animCount, texW, texH);
    return out->ready;
}

static bool ExtractGlbTexcoord1(const char *glbPath, const Model *mdl, float *outUv2, int totalVerts) {
    int dataSize = 0;
    unsigned char *fileData = LoadFileData(glbPath, &dataSize);
    if (!fileData || dataSize < 28) {
        if (fileData) UnloadFileData(fileData);
        return false;
    }
    uint32_t magic = 0, jsonLen = 0, jsonType = 0;
    memcpy(&magic, fileData, 4);
    memcpy(&jsonLen, fileData + 12, 4);
    memcpy(&jsonType, fileData + 16, 4);
    if (magic != 0x46546C67u || jsonType != 0x4E4F534Au || 20 + (int)jsonLen + 8 > dataSize) {
        UnloadFileData(fileData);
        return false;
    }
    const char *json = (const char *)(fileData + 20);
    int binOff = 20 + (int)jsonLen;
    uint32_t binLen = 0, binType = 0;
    memcpy(&binLen, fileData + binOff, 4);
    memcpy(&binType, fileData + binOff + 4, 4);
    if (binType != 0x004E4942u || binOff + 8 + (int)binLen > dataSize) {
        UnloadFileData(fileData);
        return false;
    }
    const unsigned char *blob = fileData + binOff + 8;
    const char *accArr = strstr(json, "\"accessors\"");
    const char *bvArr  = strstr(json, "\"bufferViews\"");
    const char *primSearch = strstr(json, "\"primitives\"");
    if (!accArr || !bvArr || !primSearch || jsonLen > 65536) {
        UnloadFileData(fileData);
        return false;
    }
    int bvOffset[64] = { 0 }, bvStride[64] = { 0 }, bvCount = 0;
    const char *p = strchr(bvArr, '[');
    while (p && bvCount < 64) {
        const char *obj = strchr(p, '{');
        const char *arrEnd = strchr(bvArr, ']');
        if (!obj || (arrEnd && obj > arrEnd)) break;
        const char *objEnd = strchr(obj, '}');
        if (!objEnd) break;
        const char *bo = strstr(obj, "\"byteOffset\"");
        if (bo && bo < objEnd) bvOffset[bvCount] = atoi(strchr(bo, ':') + 1);
        const char *bs = strstr(obj, "\"byteStride\"");
        if (bs && bs < objEnd) bvStride[bvCount] = atoi(strchr(bs, ':') + 1);
        bvCount++;
        p = objEnd + 1;
    }
    int accBv[128] = { 0 }, accOff[128] = { 0 }, accCnt[128] = { 0 }, accNum = 0;
    p = strchr(accArr, '[');
    while (p && accNum < 128) {
        const char *obj = strchr(p, '{');
        const char *arrEnd = strchr(accArr, ']');
        if (!obj || (arrEnd && obj > arrEnd)) break;
        int depth = 0;
        const char *cur = obj;
        while (*cur && (cur < json + jsonLen)) {
            if (*cur == '{') depth++;
            else if (*cur == '}') { depth--; if (depth == 0) break; }
            cur++;
        }
        const char *objEnd = cur;
        const char *bv = strstr(obj, "\"bufferView\"");
        if (bv && bv < objEnd) accBv[accNum] = atoi(strchr(bv, ':') + 1);
        const char *bo = strstr(obj, "\"byteOffset\"");
        if (bo && bo < objEnd) accOff[accNum] = atoi(strchr(bo, ':') + 1);
        const char *cn = strstr(obj, "\"count\"");
        if (cn && cn < objEnd) accCnt[accNum] = atoi(strchr(cn, ':') + 1);
        accNum++;
        p = objEnd + 1;
    }
    bool anyLoaded = false;
    int vOff = 0;
    const char *scan = primSearch;
    for (int m = 0; m < mdl->meshCount; m++) {
        int vCount = mdl->meshes[m].vertexCount;
        const char *tc1 = strstr(scan, "\"TEXCOORD_1\"");
        if (tc1 && vOff + vCount <= totalVerts) {
            const char *colon = strchr(tc1, ':');
            int accIdx = colon ? atoi(colon + 1) : -1;
            if (accIdx >= 0 && accIdx < accNum && accCnt[accIdx] == vCount) {
                int bvi = accBv[accIdx];
                if (bvi >= 0 && bvi < bvCount) {
                    int stride = (bvStride[bvi] >= 8) ? bvStride[bvi] : 8;
                    int base = bvOffset[bvi] + accOff[accIdx];
                    if (base + (vCount - 1) * stride + 8 <= (int)binLen) {
                        for (int v = 0; v < vCount; v++) {
                            float u = 0.0f, w = 0.0f;
                            memcpy(&u, blob + base + v * stride + 0, 4);
                            memcpy(&w, blob + base + v * stride + 4, 4);
                            outUv2[(vOff + v) * 2 + 0] = u;
                            outUv2[(vOff + v) * 2 + 1] = w;
                        }
                        anyLoaded = true;
                    }
                }
            }
            scan = tc1 + 12;
        }
        vOff += vCount;
    }
    UnloadFileData(fileData);
    return anyLoaded;
}

static Mesh BuildLowPolySilhouette(float rx, float ry, float rz, float crestH, int sides) {
    int triCount = sides * 4;
    int vertCount = triCount * 3;
    Mesh m = { 0 };
    m.vertexCount = vertCount;
    m.triangleCount = triCount;
    m.vertices   = (float *)MemAlloc((unsigned int)vertCount * 3u * sizeof(float));
    m.normals    = (float *)MemAlloc((unsigned int)vertCount * 3u * sizeof(float));
    m.texcoords  = (float *)MemAlloc((unsigned int)vertCount * 2u * sizeof(float));
    m.texcoords2 = (float *)MemAlloc((unsigned int)vertCount * 2u * sizeof(float));
    m.colors     = (unsigned char *)MemAlloc((unsigned int)vertCount * 4u * sizeof(unsigned char));
    if (!m.vertices || !m.normals || !m.texcoords || !m.texcoords2 || !m.colors) {
        MemFree(m.vertices); MemFree(m.normals); MemFree(m.texcoords);
        MemFree(m.texcoords2); MemFree(m.colors);
        return (Mesh){ 0 };
    }
    Vector3 bot = { 0.0f, 0.02f, 0.0f };
    Vector3 top = { 0.0f, ry + crestH, 0.12f * rz };
    int v = 0;
    for (int i = 0; i < sides; i++) {
        float a0 = ((float)i / (float)sides) * 6.2831853f;
        float a1 = ((float)(i + 1) / (float)sides) * 6.2831853f;
        Vector3 l0 = { cosf(a0) * rx * 0.72f, ry * 0.25f, sinf(a0) * rz * 0.72f };
        Vector3 l1 = { cosf(a1) * rx * 0.72f, ry * 0.25f, sinf(a1) * rz * 0.72f };
        Vector3 u0 = { cosf(a0) * rx, ry * 0.72f, sinf(a0) * rz + 0.08f * rz };
        Vector3 u1 = { cosf(a1) * rx, ry * 0.72f, sinf(a1) * rz + 0.08f * rz };
        Vector3 tris[4][3] = { { bot, l1, l0 }, { l0, l1, u1 }, { l0, u1, u0 }, { u0, u1, top } };
        for (int t = 0; t < 4; t++) {
            Vector3 p0 = tris[t][0], p1 = tris[t][1], p2 = tris[t][2];
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)));
            Vector3 pts[3] = { p0, p1, p2 };
            for (int k = 0; k < 3; k++) {
                m.vertices[v * 3 + 0] = pts[k].x; m.vertices[v * 3 + 1] = pts[k].y; m.vertices[v * 3 + 2] = pts[k].z;
                m.normals[v * 3 + 0] = n.x; m.normals[v * 3 + 1] = n.y; m.normals[v * 3 + 2] = n.z;
                m.texcoords[v * 2 + 0] = 0.5f; m.texcoords[v * 2 + 1] = 0.5f;
                m.texcoords2[v * 2 + 0] = 0.0f; m.texcoords2[v * 2 + 1] = 0.0f;
                m.colors[v * 4 + 0] = 220; m.colors[v * 4 + 1] = 205; m.colors[v * 4 + 2] = 185; m.colors[v * 4 + 3] = 0;
                v++;
            }
        }
    }
    UploadMesh(&m, false);
    return m;
}

static Mesh LoadMergedMonsterMesh(const char *glbPath, const Color *zoneColors, int zoneColorCount,
                                    float fallbackRx, float fallbackRy, float fallbackRz, float fallbackCrest, int fallbackSides) {
    if (!glbPath || !FileExists(glbPath)) {
        return BuildLowPolySilhouette(fallbackRx, fallbackRy, fallbackRz, fallbackCrest, fallbackSides);
    }
    Model mdl = LoadModel(glbPath);
    if (mdl.meshCount <= 0 || !mdl.meshes) {
        if (mdl.meshCount > 0) UnloadModel(mdl);
        return BuildLowPolySilhouette(fallbackRx, fallbackRy, fallbackRz, fallbackCrest, fallbackSides);
    }
    int totalVerts = 0, totalTris = 0;
    for (int i = 0; i < mdl.meshCount; i++) {
        totalVerts += mdl.meshes[i].vertexCount;
        totalTris  += mdl.meshes[i].triangleCount;
    }
    if (totalVerts <= 0 || totalTris <= 0) {
        UnloadModel(mdl);
        return BuildLowPolySilhouette(fallbackRx, fallbackRy, fallbackRz, fallbackCrest, fallbackSides);
    }
    Mesh dst = { 0 };
    dst.vertexCount   = totalVerts;
    dst.triangleCount = totalTris;
    dst.vertices   = (float *)MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    dst.normals    = (float *)MemAlloc((unsigned int)totalVerts * 3u * sizeof(float));
    dst.texcoords  = (float *)MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    dst.texcoords2 = (float *)MemAlloc((unsigned int)totalVerts * 2u * sizeof(float));
    dst.colors     = (unsigned char *)MemAlloc((unsigned int)totalVerts * 4u * sizeof(unsigned char));
    dst.indices    = (unsigned short *)MemAlloc((unsigned int)totalTris * 3u * sizeof(unsigned short));
    if (!dst.vertices || !dst.normals || !dst.texcoords || !dst.texcoords2 || !dst.colors || !dst.indices) {
        MemFree(dst.vertices); MemFree(dst.normals); MemFree(dst.texcoords);
        MemFree(dst.texcoords2); MemFree(dst.colors); MemFree(dst.indices);
        UnloadModel(mdl);
        return BuildLowPolySilhouette(fallbackRx, fallbackRy, fallbackRz, fallbackCrest, fallbackSides);
    }
    int vOff = 0, iOff = 0;
    bool hadTexcoords2 = false;
    for (int m = 0; m < mdl.meshCount; m++) {
        const Mesh *src = &mdl.meshes[m];
        Color zc = (m < zoneColorCount) ? zoneColors[m] : zoneColors[zoneColorCount - 1];
        for (int v = 0; v < src->vertexCount; v++) {
            int dv = vOff + v;
            dst.vertices[dv*3+0] = src->vertices ? src->vertices[v*3+0] : 0.0f;
            dst.vertices[dv*3+1] = src->vertices ? src->vertices[v*3+1] : 0.0f;
            dst.vertices[dv*3+2] = src->vertices ? src->vertices[v*3+2] : 0.0f;
            dst.normals[dv*3+0]  = src->normals  ? src->normals[v*3+0]  : 0.0f;
            dst.normals[dv*3+1]  = src->normals  ? src->normals[v*3+1]  : 1.0f;
            dst.normals[dv*3+2]  = src->normals  ? src->normals[v*3+2]  : 0.0f;
            dst.texcoords[dv*2+0] = src->texcoords ? src->texcoords[v*2+0] : 0.5f;
            dst.texcoords[dv*2+1] = src->texcoords ? src->texcoords[v*2+1] : 0.5f;
            if (src->texcoords2) {
                dst.texcoords2[dv*2+0] = src->texcoords2[v*2+0];
                dst.texcoords2[dv*2+1] = src->texcoords2[v*2+1];
                hadTexcoords2 = true;
            } else {
                dst.texcoords2[dv*2+0] = 0.0f;
                dst.texcoords2[dv*2+1] = 0.0f;
            }
            dst.colors[dv*4+0] = zc.r; dst.colors[dv*4+1] = zc.g;
            dst.colors[dv*4+2] = zc.b; dst.colors[dv*4+3] = zc.a;
        }
        int primIndices = src->triangleCount * 3;
        if (src->indices) {
            for (int k = 0; k < primIndices; k++) dst.indices[iOff + k] = (unsigned short)(vOff + src->indices[k]);
        } else {
            for (int k = 0; k < primIndices; k++) dst.indices[iOff + k] = (unsigned short)(vOff + k);
        }
        vOff += src->vertexCount;
        iOff += primIndices;
    }
    if (!hadTexcoords2) ExtractGlbTexcoord1(glbPath, &mdl, dst.texcoords2, totalVerts);
    UnloadModel(mdl);
    UploadMesh(&dst, false);
    return dst;
}

static Mesh BuildShadowDiscMesh(int sides) {
    Mesh m = { 0 };
    m.vertexCount = sides * 3;
    m.triangleCount = sides;
    m.vertices   = (float *)MemAlloc((unsigned int)m.vertexCount * 3u * sizeof(float));
    m.normals    = (float *)MemAlloc((unsigned int)m.vertexCount * 3u * sizeof(float));
    m.texcoords  = (float *)MemAlloc((unsigned int)m.vertexCount * 2u * sizeof(float));
    m.texcoords2 = (float *)MemAlloc((unsigned int)m.vertexCount * 2u * sizeof(float));
    m.colors     = (unsigned char *)MemAlloc((unsigned int)m.vertexCount * 4u * sizeof(unsigned char));
    if (!m.vertices || !m.normals || !m.texcoords || !m.texcoords2 || !m.colors) {
        MemFree(m.vertices); MemFree(m.normals); MemFree(m.texcoords);
        MemFree(m.texcoords2); MemFree(m.colors);
        return (Mesh){ 0 };
    }
    int v = 0;
    for (int i = 0; i < sides; i++) {
        float a0 = ((float)i / (float)sides) * 6.2831853f;
        float a1 = ((float)(i + 1) / (float)sides) * 6.2831853f;
        Vector3 pts[3] = {
            { 0.0f, 0.0f, 0.0f },
            { cosf(a1) * 0.45f, 0.0f, sinf(a1) * 0.45f },
            { cosf(a0) * 0.45f, 0.0f, sinf(a0) * 0.45f }
        };
        for (int k = 0; k < 3; k++) {
            m.vertices[v*3+0] = pts[k].x; m.vertices[v*3+1] = pts[k].y; m.vertices[v*3+2] = pts[k].z;
            m.normals[v*3+0] = 0.0f; m.normals[v*3+1] = 1.0f; m.normals[v*3+2] = 0.0f;
            m.texcoords[v*2+0] = 0.5f; m.texcoords[v*2+1] = 0.5f;
            m.texcoords2[v*2+0] = 0.0f; m.texcoords2[v*2+1] = 0.0f;
            m.colors[v*4+0] = 255; m.colors[v*4+1] = 255; m.colors[v*4+2] = 255; m.colors[v*4+3] = 0;
            v++;
        }
    }
    UploadMesh(&m, false);
    return m;
}

static Color ElementTint(int elem) {
    switch (elem) {
        case 0:  return (Color){ 80, 185, 255, 255 };
        case 1:  return (Color){ 95, 215, 115, 255 };
        case 2:  return (Color){ 255, 110, 65, 255 };
        case 3:  return (Color){ 220, 170, 90, 255 };
        case 4:  return (Color){ 215, 225, 240, 255 };
        default: return (Color){ 200, 180, 245, 255 };
    }
}

static void DrawInstancedBatch(Mesh mesh, int kind, const Matrix *xf, int count, Vector4 tint, Vector4 emissive) {
    if (count <= 0 || mesh.vaoId == 0) return;
    BeginShaderMode(s_shader);
    SetShaderValue(s_shader, s_locKind, &kind, SHADER_UNIFORM_INT);
    SetShaderValue(s_shader, s_locTint, &tint, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_shader, s_locEmissive, &emissive, SHADER_UNIFORM_VEC4);
    DrawMeshInstanced(mesh, s_mat, xf, count);
    EndShaderMode();
}

static void DrawSkinnedInstancedBatch(HordeSkinnedArchetype *arch, const Matrix *xf, int count) {
    if (!arch || !arch->ready || count <= 0 || arch->model.meshCount <= 0) return;
    BeginShaderMode(s_skinnedShader);
    SetShaderValue(s_skinnedShader, s_skLocBoneCount, &arch->boneCount, SHADER_UNIFORM_INT);
    SetShaderValueTexture(s_skinnedShader, s_skLocBoneTex, arch->boneTex);
    float hasEm = arch->hasEmissiveTex ? 1.0f : 0.0f;
    SetShaderValue(s_skinnedShader, s_skLocHasEmissive, &hasEm, SHADER_UNIFORM_FLOAT);
    if (arch->hasEmissiveTex) {
        SetShaderValueTexture(s_skinnedShader, s_skLocEmissiveTex, arch->emissiveTex);
    }
    DrawMeshInstanced(arch->model.meshes[0], arch->mat, xf, count);
    EndShaderMode();
}

static float ComputeSkinnedFrameRow(const HordeSkinnedArchetype *arch, const AIMonsterRenderInfo *m, float now) {
    HordeClipSlot slot = HORDE_CLIP_MOVE;
    if (m->state == MON_STATE_GRAZE) slot = HORDE_CLIP_IDLE;
    else if (m->state == MON_STATE_TELEGRAPH) slot = HORDE_CLIP_TELEGRAPH;
    else if (m->state == MON_STATE_STRIKE) slot = HORDE_CLIP_STRIKE;

    int start = arch->clipStartRow[slot];
    int count = arch->clipFrameCount[slot];
    if (count <= 1) return (float)start;

    float localFrame = 0.0f;
    if (slot == HORDE_CLIP_TELEGRAPH || slot == HORDE_CLIP_STRIKE) {
        localFrame = Clamp(m->telegraphFrac, 0.0f, 1.0f) * (float)(count - 1);
    } else if (slot == HORDE_CLIP_IDLE) {
        float t = fmodf(now * 24.0f + m->animPhase * 4.0f, (float)(count - 1));
        if (t < 0.0f) t += (float)(count - 1);
        localFrame = t;
    } else {
        // MOVE_SLITHER: driven by locomotion animPhase (plus gentle idle speed)
        float t = fmodf(m->animPhase * 5.0f + now * 6.0f, (float)(count - 1));
        if (t < 0.0f) t += (float)(count - 1);
        localFrame = t;
    }
    return (float)start + localFrame;
}

// -----------------------------------------------------------------------------
// Lifecycle & Render
// -----------------------------------------------------------------------------
void HordeRenderer_Init(void) {
    if (s_ready) return;

    // 1. Non-skinned construct shader
    s_shader = LoadShaderFromMemory(HORDE_VS, HORDE_FS);
    s_shader.locs[SHADER_LOC_MATRIX_MODEL]      = GetShaderLocationAttrib(s_shader, "instanceTransform");
    s_shader.locs[SHADER_LOC_VERTEX_COLOR]      = GetShaderLocationAttrib(s_shader, "vertexColor");
    s_shader.locs[SHADER_LOC_VERTEX_TEXCOORD02] = GetShaderLocationAttrib(s_shader, "vertexTexCoord2");
    s_locTint        = GetShaderLocation(s_shader, "u_tint");
    s_locEmissive    = GetShaderLocation(s_shader, "u_emissive");
    s_locTime        = GetShaderLocation(s_shader, "u_time");
    s_locKind        = GetShaderLocation(s_shader, "u_kind");
    s_locSunToLight  = GetShaderLocation(s_shader, "u_sunToLight");
    s_locSunColor    = GetShaderLocation(s_shader, "u_sunColor");
    s_locSkyColor    = GetShaderLocation(s_shader, "u_skyColor");
    s_locGroundColor = GetShaderLocation(s_shader, "u_groundColor");
    s_locViewPos     = GetShaderLocation(s_shader, "u_viewPos");
    s_locFogColor    = GetShaderLocation(s_shader, "u_fogColor");
    s_locFogStart    = GetShaderLocation(s_shader, "u_fogStart");
    s_locFogEnd      = GetShaderLocation(s_shader, "u_fogEnd");
    s_locFogEnabled  = GetShaderLocation(s_shader, "u_fogEnabled");
    s_mat = LoadMaterialDefault();
    s_mat.shader = s_shader;

    // 2. GPU-instanced skeletal shader
    s_skinnedShader = LoadShaderFromMemory(HORDE_SKINNED_VS, HORDE_SKINNED_FS);
    s_skinnedShader.locs[SHADER_LOC_MATRIX_MODEL]      = GetShaderLocationAttrib(s_skinnedShader, "instanceTransform");
    s_skinnedShader.locs[SHADER_LOC_VERTEX_BONEIDS]    = GetShaderLocationAttrib(s_skinnedShader, "vertexBoneIds");
    s_skinnedShader.locs[SHADER_LOC_VERTEX_BONEWEIGHTS]= GetShaderLocationAttrib(s_skinnedShader, "vertexBoneWeights");
    s_skinnedShader.locs[SHADER_LOC_MAP_ALBEDO]        = GetShaderLocation(s_skinnedShader, "texture0");
    s_skLocBoneTex     = GetShaderLocation(s_skinnedShader, "u_boneTex");
    s_skLocBoneCount   = GetShaderLocation(s_skinnedShader, "u_boneCount");
    s_skLocEmissiveTex = GetShaderLocation(s_skinnedShader, "u_emissiveTex");
    s_skLocHasEmissive = GetShaderLocation(s_skinnedShader, "u_hasEmissiveTex");
    s_skLocSunToLight  = GetShaderLocation(s_skinnedShader, "u_sunToLight");
    s_skLocSunColor    = GetShaderLocation(s_skinnedShader, "u_sunColor");
    s_skLocSkyColor    = GetShaderLocation(s_skinnedShader, "u_skyColor");
    s_skLocGroundColor = GetShaderLocation(s_skinnedShader, "u_groundColor");
    s_skLocViewPos     = GetShaderLocation(s_skinnedShader, "u_viewPos");
    s_skLocFogColor    = GetShaderLocation(s_skinnedShader, "u_fogColor");
    s_skLocFogStart    = GetShaderLocation(s_skinnedShader, "u_fogStart");
    s_skLocFogEnd      = GetShaderLocation(s_skinnedShader, "u_fogEnd");
    s_skLocFogEnabled  = GetShaderLocation(s_skinnedShader, "u_fogEnabled");

    // 3. Load Rigged Skeletal Archetypes (humanoid_anims.glb shared across all humanoid monsters!)
    const char *kHumanoidAnims = "assets/characters/animations/humanoid_anims.glb";
    if (FileExists("assets/models/monsters/nt_human_swarmer.glb")) {
        LoadSkinnedArchetype(&s_skinned[MONSTER_SWARM_MELEE], "assets/models/monsters/nt_human_swarmer.glb", kHumanoidAnims);
    } else {
        LoadSkinnedArchetype(&s_skinned[MONSTER_SWARM_MELEE], "assets/models/monsters/nt_serpent_viper.glb", NULL);
    }
    LoadSkinnedArchetype(&s_skinned[MONSTER_RANGED_CASTER], "assets/models/monsters/nt_human_caster.glb",  kHumanoidAnims);
    LoadSkinnedArchetype(&s_skinned[MONSTER_LEAPER],        "assets/models/monsters/nt_human_leaper.glb",  kHumanoidAnims);
    LoadSkinnedArchetype(&s_skinned[MONSTER_ELITE_GUARD],   "assets/models/monsters/nt_human_brute.glb",   kHumanoidAnims);

    // 4. Fallback / Non-skinned Archetypes
    static const Color kScarabZones[3] = { { 40, 34, 34, 0 }, { 168, 130, 74, 128 }, { 255, 148, 26, 255 } };
    static const Color kCasterZones[4] = { { 48, 46, 52, 0 }, { 162, 126, 70, 128 }, { 42, 232, 212, 255 }, { 110, 255, 242, 255 } };
    static const Color kGolemZones[3]  = { { 54, 48, 46, 0 }, { 170, 132, 72, 128 }, { 255, 142, 26, 255 } };
    static const Color kLeaperZones[4] = { { 44, 32, 30, 0 }, { 126, 98, 66, 128 }, { 255, 126, 22, 255 }, { 230, 216, 188, 64 } };

    if (!s_skinned[MONSTER_SWARM_MELEE].ready) {
        s_meshKind[MONSTER_SWARM_MELEE] = LoadMergedMonsterMesh("assets/models/monsters/nt_swarm_scarab.glb", kScarabZones, 3, 0.28f, 0.55f, 0.34f, 0.16f, 6);
    }
    s_meshKind[MONSTER_RANGED_CASTER] = LoadMergedMonsterMesh("assets/models/monsters/nt_caster_idol.glb", kCasterZones, 4, 0.24f, 0.72f, 0.24f, 0.28f, 5);
    s_meshKind[MONSTER_ELITE_GUARD]   = LoadMergedMonsterMesh("assets/models/monsters/nt_elite_golem.glb", kGolemZones, 3, 0.42f, 0.92f, 0.38f, 0.25f, 8);
    s_meshKind[MONSTER_LEAPER]        = LoadMergedMonsterMesh("assets/models/monsters/nt_leaper_stalker.glb", kLeaperZones, 4, 0.24f, 0.44f, 0.48f, 0.14f, 6);

    s_meshShadow = BuildShadowDiscMesh(8);
    s_meshBolt   = BuildLowPolySilhouette(0.14f, 0.14f, 0.36f, 0.04f, 4);

    s_ready = true;
}

void HordeRenderer_Unload(void) {
    if (!s_ready) return;
    for (int k = 0; k < 4; k++) {
        if (s_skinned[k].ready) {
            UnloadTexture(s_skinned[k].boneTex);
            UnloadModel(s_skinned[k].model);
            s_skinned[k].ready = false;
        }
        if (s_meshKind[k].vaoId > 0) UnloadMesh(s_meshKind[k]);
    }
    UnloadMesh(s_meshShadow);
    UnloadMesh(s_meshBolt);
    UnloadShader(s_shader);
    UnloadShader(s_skinnedShader);
    s_ready = false;
}

void HordeRenderer_Draw3D(Camera3D cam) {
    if (!s_ready) HordeRenderer_Init();

    static AIMonsterRenderInfo monsters[AI_MAX_CAMP_MONSTERS];
    int mCount = AI_QueryMonsterRenderData(monsters, AI_MAX_CAMP_MONSTERS);

    static AIMonsterRenderInfo waveMinions[64];
    int wCount = 0;
    for (int i = 0; i < MAX_AGENTS && wCount < 64; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->archetype != ARCH_MINION || AI_IsCampMonster(i)) continue;
        waveMinions[wCount++] = (AIMonsterRenderInfo){
            .agentId       = i,
            .pos           = a->position,
            .yaw           = 0.0f,
            .scale         = 0.85f,
            .animPhase     = (float)GetTime() * 4.5f + (float)i,
            .telegraphFrac = 0.0f,
            .hitFlash      = (a->hitFlashTimer > 0.0f) ? (a->hitFlashTimer / 0.12f) : 0.0f,
            .element       = a->currentElement,
            .kind          = MONSTER_RANGED_CASTER,
            .state         = MON_STATE_CHASE
        };
    }

    static Matrix xfKind[4][HORDE_MAX_INSTANCES];
    static int    cntKind[4];
    static Matrix xfShadow[HORDE_MAX_INSTANCES];
    static int    cntShadow = 0;
    for (int k = 0; k < 4; k++) cntKind[k] = 0;
    cntShadow = 0;

    float now = (float)GetTime();
    const float maxDrawDistSq = 72.0f * 72.0f;

    for (int pass = 0; pass < 2; pass++) {
        const AIMonsterRenderInfo *arr = (pass == 0) ? monsters : waveMinions;
        int n = (pass == 0) ? mCount : wCount;
        for (int i = 0; i < n; i++) {
            const AIMonsterRenderInfo *m = &arr[i];
            float dx = m->pos.x - cam.position.x;
            float dz = m->pos.z - cam.position.z;
            if (dx * dx + dz * dz > maxDrawDistSq) continue;

            int k = (int)m->kind;
            if (k < 0 || k > 3) k = 0;

            float hop = (k == MONSTER_RANGED_CASTER) ? 0.14f : 0.0f;
            float sc = m->scale;
            Matrix S = MatrixScale(sc, sc, sc);
            Matrix R = MatrixRotateY(m->yaw);
            Matrix T = MatrixTranslate(m->pos.x, m->pos.y + hop, m->pos.z);
            Matrix M = MatrixMultiply(MatrixMultiply(S, R), T);

            if (s_skinned[k].ready) {
                M.m3 = ComputeSkinnedFrameRow(&s_skinned[k], m, now);
            } else {
                M.m3 = m->animPhase;
            }
            M.m7  = m->telegraphFrac;
            M.m11 = m->hitFlash;
            M.m15 = (float)m->state;

            if (cntKind[k] < HORDE_MAX_INSTANCES) {
                xfKind[k][cntKind[k]++] = M;
            }

            if (cntShadow < HORDE_MAX_INSTANCES) {
                float shScale = sc * ((k == MONSTER_ELITE_GUARD) ? 1.25f : 0.95f);
                Matrix SS = MatrixScale(shScale, 1.0f, shScale);
                Matrix TS = MatrixTranslate(m->pos.x, m->pos.y + 0.04f, m->pos.z);
                Matrix MS = MatrixMultiply(SS, TS);
                MS.m3 = 0.0f; MS.m7 = 0.0f; MS.m11 = 0.0f; MS.m15 = 0.0f;
                xfShadow[cntShadow++] = MS;
            }
        }
    }

    Vector3 sunToLight = Vector3Normalize(Vector3Negate(Environment_GetSunDirection()));
    Color scCol = Environment_GetSunColor();
    float sunInt = Environment_GetSunIntensity();
    Vector3 sunColor = { (scCol.r / 255.0f) * sunInt, (scCol.g / 255.0f) * sunInt, (scCol.b / 255.0f) * sunInt };
    Color skyCol = Environment_GetSkyAmbient();
    Vector3 skyColor = { skyCol.r / 255.0f, skyCol.g / 255.0f, skyCol.b / 255.0f };
    Color gndCol = Environment_GetGroundAmbient();
    Vector3 groundColor = { gndCol.r / 255.0f, gndCol.g / 255.0f, gndCol.b / 255.0f };
    EnvFogConfig fog = Environment_GetFogConfig();
    Vector3 fogColor = { fog.color.r / 255.0f, fog.color.g / 255.0f, fog.color.b / 255.0f };
    float fogEnabled = fog.enabled ? 1.0f : 0.0f;

    // Push lighting to both shaders
    SetShaderValue(s_shader, s_locTime,        &now,           SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locSunToLight,  &sunToLight,    SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locSunColor,    &sunColor,      SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locSkyColor,    &skyColor,      SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locGroundColor, &groundColor,   SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locViewPos,     &cam.position,  SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locFogColor,    &fogColor,      SHADER_UNIFORM_VEC3);
    SetShaderValue(s_shader, s_locFogStart,    &fog.start,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locFogEnd,      &fog.end,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_locFogEnabled,  &fogEnabled,    SHADER_UNIFORM_FLOAT);

    SetShaderValue(s_skinnedShader, s_skLocSunToLight,  &sunToLight,    SHADER_UNIFORM_VEC3);
    SetShaderValue(s_skinnedShader, s_skLocSunColor,    &sunColor,      SHADER_UNIFORM_VEC3);
    SetShaderValue(s_skinnedShader, s_skLocSkyColor,    &skyColor,      SHADER_UNIFORM_VEC3);
    SetShaderValue(s_skinnedShader, s_skLocGroundColor, &groundColor,   SHADER_UNIFORM_VEC3);
    SetShaderValue(s_skinnedShader, s_skLocViewPos,     &cam.position,  SHADER_UNIFORM_VEC3);
    SetShaderValue(s_skinnedShader, s_skLocFogColor,    &fogColor,      SHADER_UNIFORM_VEC3);
    SetShaderValue(s_skinnedShader, s_skLocFogStart,    &fog.start,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_skinnedShader, s_skLocFogEnd,      &fog.end,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_skinnedShader, s_skLocFogEnabled,  &fogEnabled,    SHADER_UNIFORM_FLOAT);

    DrawInstancedBatch(s_meshShadow, -1, xfShadow, cntShadow,
                       (Vector4){ 0.05f, 0.04f, 0.03f, 0.52f },
                       (Vector4){ 0.0f, 0.0f, 0.0f, 0.0f });

    for (int k = 0; k < 4; k++) {
        if (s_skinned[k].ready) {
            DrawSkinnedInstancedBatch(&s_skinned[k], xfKind[k], cntKind[k]);
        } else {
            DrawInstancedBatch(s_meshKind[k], k, xfKind[k], cntKind[k],
                               (Vector4){ 1.0f, 1.0f, 1.0f, 1.0f },
                               (Vector4){ 0.0f, 0.0f, 0.0f, 0.0f });
        }
    }

    static AIMinionBoltRenderInfo bolts[AI_MAX_MINION_BOLTS];
    static Matrix xfBolts[AI_MAX_MINION_BOLTS];
    int bCount = AI_QueryMinionBolts(bolts, AI_MAX_MINION_BOLTS);
    if (bCount > 0) {
        int bElem = bolts[0].element;
        for (int i = 0; i < bCount; i++) {
            float yaw = atan2f(bolts[i].vel.x, bolts[i].vel.z);
            Matrix S = MatrixScale(1.0f, 1.0f, 1.4f);
            Matrix R = MatrixRotateY(yaw);
            Matrix T = MatrixTranslate(bolts[i].pos.x, bolts[i].pos.y, bolts[i].pos.z);
            Matrix M = MatrixMultiply(MatrixMultiply(S, R), T);
            M.m3 = 0.0f; M.m7 = 0.0f; M.m11 = 0.0f; M.m15 = 0.0f;
            xfBolts[i] = M;
        }
        Color bc = ElementTint(bElem);
        DrawInstancedBatch(s_meshBolt, -1, xfBolts, bCount,
                           (Vector4){ (float)bc.r / 255.0f, (float)bc.g / 255.0f, (float)bc.b / 255.0f, 1.0f },
                           (Vector4){ (float)bc.r / 300.0f, (float)bc.g / 300.0f, (float)bc.b / 300.0f, 0.0f });
    }
}
