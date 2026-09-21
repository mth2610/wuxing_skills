// ── vc_silhouette_glow.inl ───────────────────────────────────────────────────
//
// VFX 2: Generic 3D Mesh & Character Silhouette Glow Aura.
// Renders ANY Raylib Model, Mesh, or animated Character in a high-contrast
// unlit white incandescent silhouette with Fresnel edge bloom and surface-hugging
// energy motes. Completely generic: accepts any mesh/model without fake geometry!
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "core/particles/particle_system.h"
#include "core/material/material_system.h"
#include "core/resource_manager.h"
#include "core/vfx_light.h"
#include "character/character_model.h"
#include <math.h>

// ── Active Global Animation State Tracking ───────────────────────────────────
static const CharacterAnimState *s_activeGlobalCharAnim = NULL;
static bool s_meshAuraSuppressLegacyMotes = false;

void VFX_SetActiveCharacterAnimState(const struct CharacterAnimState *animState)
{
    s_activeGlobalCharAnim = animState;
}

const struct CharacterAnimState *VFX_GetActiveCharacterAnimState(void)
{
    return s_activeGlobalCharAnim;
}

// ── Dedicated Unlit Silhouette Shader Setup ──────────────────────────────────
static Shader s_silhShader = {0};
static int s_locGlowColor = -1;
static int s_locRimPower = -1;
static int s_locRimStrength = -1;
static bool s_silhShaderLoaded = false;

static void Silhouette_EnsureShader(void)
{
    if (!s_silhShaderLoaded)
    {
        s_silhShader = ResourceManager_LoadShader("core/shaders/silhouette_glow.vs",
                                                  "core/shaders/silhouette_glow.fs");
        s_locGlowColor = GetShaderLocation(s_silhShader, "u_glowColor");
        s_locRimPower = GetShaderLocation(s_silhShader, "u_rimPower");
        s_locRimStrength = GetShaderLocation(s_silhShader, "u_rimStrength");
        s_silhShaderLoaded = true;
    }
}

static void Silhouette_ApplyUniforms(Color glowColor, float intensity)
{
    Silhouette_EnsureShader();
    if (s_silhShader.id == 0) return;

    Vector4 colNorm = ColorNormalize(glowColor);
    colNorm.w *= Clamp(intensity, 0.0f, 1.0f);

    float rimPower = 2.2f;
    float rimStrength = 2.5f * intensity;

    if (s_locGlowColor >= 0)
        SetShaderValue(s_silhShader, s_locGlowColor, &colNorm, SHADER_UNIFORM_VEC4);
    if (s_locRimPower >= 0)
        SetShaderValue(s_silhShader, s_locRimPower, &rimPower, SHADER_UNIFORM_FLOAT);
    if (s_locRimStrength >= 0)
        SetShaderValue(s_silhShader, s_locRimStrength, &rimStrength, SHADER_UNIFORM_FLOAT);
}

// ── Generic Surface Point & Normal Sampler for ANY Raylib Mesh ───────────────
bool VFX_SampleMeshSurfacePoint(const Mesh *mesh, Matrix transform,
                                Vector3 *outWorldPos, Vector3 *outWorldNormal)
{
    if (!mesh || mesh->vertexCount <= 0) return false;

    const float *vBuf = (mesh->animVertices != NULL) ? mesh->animVertices : mesh->vertices;
    const float *nBuf = (mesh->animNormals != NULL) ? mesh->animNormals : mesh->normals;
    if (!vBuf) return false;

    Vector3 localPos = { 0 };
    Vector3 localNorm = (Vector3){ 0.0f, 1.0f, 0.0f };

    if (mesh->indices != NULL && mesh->triangleCount > 0)
    {
        int tri = GetRandomValue(0, mesh->triangleCount - 1);
        int i0 = mesh->indices[tri * 3 + 0];
        int i1 = mesh->indices[tri * 3 + 1];
        int i2 = mesh->indices[tri * 3 + 2];

        float r1 = (float)GetRandomValue(0, 10000) / 10000.0f;
        float r2 = (float)GetRandomValue(0, 10000) / 10000.0f;
        if (r1 + r2 > 1.0f)
        {
            r1 = 1.0f - r1;
            r2 = 1.0f - r2;
        }
        float r3 = 1.0f - r1 - r2;

        localPos.x = r1 * vBuf[i0 * 3 + 0] + r2 * vBuf[i1 * 3 + 0] + r3 * vBuf[i2 * 3 + 0];
        localPos.y = r1 * vBuf[i0 * 3 + 1] + r2 * vBuf[i1 * 3 + 1] + r3 * vBuf[i2 * 3 + 1];
        localPos.z = r1 * vBuf[i0 * 3 + 2] + r2 * vBuf[i1 * 3 + 2] + r3 * vBuf[i2 * 3 + 2];

        if (nBuf)
        {
            localNorm.x = r1 * nBuf[i0 * 3 + 0] + r2 * nBuf[i1 * 3 + 0] + r3 * nBuf[i2 * 3 + 0];
            localNorm.y = r1 * nBuf[i0 * 3 + 1] + r2 * nBuf[i1 * 3 + 1] + r3 * nBuf[i2 * 3 + 1];
            localNorm.z = r1 * nBuf[i0 * 3 + 2] + r2 * nBuf[i1 * 3 + 2] + r3 * nBuf[i2 * 3 + 2];
            localNorm = Vector3Normalize(localNorm);
        }
    }
    else
    {
        int idx = GetRandomValue(0, mesh->vertexCount - 1);
        localPos = (Vector3){ vBuf[idx * 3 + 0], vBuf[idx * 3 + 1], vBuf[idx * 3 + 2] };
        if (nBuf)
        {
            localNorm = Vector3Normalize((Vector3){ nBuf[idx * 3 + 0], nBuf[idx * 3 + 1], nBuf[idx * 3 + 2] });
        }
    }

    if (outWorldPos) *outWorldPos = Vector3Transform(localPos, transform);
    if (outWorldNormal)
    {
        Vector3 normWorld = {
            transform.m0 * localNorm.x + transform.m4 * localNorm.y + transform.m8 * localNorm.z,
            transform.m1 * localNorm.x + transform.m5 * localNorm.y + transform.m9 * localNorm.z,
            transform.m2 * localNorm.x + transform.m6 * localNorm.y + transform.m10 * localNorm.z
        };
        *outWorldNormal = Vector3Normalize(normWorld);
    }
    return true;
}

// ── Surface Mote Emission Helper ─────────────────────────────────────────────
static void Silhouette_EmitMote(Vector3 surfPos, Vector3 surfNorm, Color auraColor)
{
    ParticleConfig p = { 0 };
    p.position = surfPos;
    p.physics.position = surfPos;

    float spd = 0.22f + (float)GetRandomValue(0, 18) / 100.0f;
    Vector3 vel = Vector3Scale(surfNorm, spd);
    vel.y += 0.35f + (float)GetRandomValue(0, 25) / 100.0f;
    p.velocity = vel;

    p.radius = 0.045f + (float)GetRandomValue(0, 30) / 1000.0f;
    p.lifetime = 0.35f + (float)GetRandomValue(0, 15) / 100.0f;
    p.colorStart = auraColor;
    p.colorEnd = ColorAlpha(auraColor, 0);
    p.render.blendMode = VFX_BLEND_ADDITIVE;
    p.render.unlit = 1;
    p.render.emissiveBoost = 2.2f;
    SpawnParticle(p);
}

static Texture2D s_meshAuraPlasmaTex = {0};
static SpriteAnim s_meshAuraPlasmaAnim = {0};
static bool s_meshAuraPlasmaReady = false;

static void MeshAura_EnsurePlasma(void)
{
    if (s_meshAuraPlasmaReady) return;
    const VFX_SurfaceProfile *profile =
        VFX_SurfaceRegistry_Get(VFX_SURFACE_PLASMA_WISPS_NIAGARA);
    if (profile != NULL && profile->body.id != 0) {
        s_meshAuraPlasmaTex = profile->body;
        SpriteAnim_Init(&s_meshAuraPlasmaAnim, profile->flipbookColumns,
                        profile->flipbookRows, profile->flipbookFrames,
                        (float)profile->flipbookFrames / 1.60f, ANIM_ONCE);
    }
    s_meshAuraPlasmaReady = true;
}

static void MeshAura_EmitPlasma(const Mesh *mesh, Matrix transform,
                                const VFX_ElementMaterial *material, float intensity)
{
    MeshAura_EnsurePlasma();
    if (s_meshAuraPlasmaTex.id == 0) return;
    int count = (int)(6.0f * Clamp(intensity, 0.0f, 1.0f));
    for (int i = 0; i < count; ++i) {
        Vector3 p, n;
        if (!VFX_SampleMeshSurfacePoint(mesh, transform, &p, &n)) continue;
        SpawnParticle((ParticleConfig){
            .position = p, .velocity = (Vector3){0.0f, 0.0f, 0.0f},
            .radius = Math_Mix(0.055f, 0.13f, Random01()),
            .lifetime = Math_Mix(0.75f, 1.35f, Random01()),
            .colorStart = VC_WithAlpha(material->glow, (unsigned char)(170.0f * intensity)),
            .colorEnd = VC_WithAlpha(material->soft, 0),
            .render.texture = s_meshAuraPlasmaTex, .render.blendMode = VFX_BLEND_ADDITIVE,
            .render.unlit = 1, .render.emissiveBoost = 1.35f,
            .spriteAnim = &s_meshAuraPlasmaAnim, .spriteAnimPhase = Random01() * 0.30f,
            .spriteAnimRate = Math_Mix(0.82f, 1.0f, Random01()),
            .rotation = Random01() * 2.0f * PI,
        });
    }
}

void VFX_DrawMeshAura(Mesh mesh, Matrix transform, VC_MaterialId matId,
                      float intensity, VFX_MeshAuraParticleMode particleMode)
{
    const VFX_ElementMaterial *material = VFX_Material(matId);
    s_meshAuraSuppressLegacyMotes = true;
    VFX_DrawMeshSilhouetteGlow(mesh, transform, material->glow, intensity);
    s_meshAuraSuppressLegacyMotes = false;
    if (particleMode == VFX_MESH_AURA_PARTICLES_STATIC_FLIPBOOK)
        MeshAura_EmitPlasma(&mesh, transform, material, intensity);
}

void VFX_DrawMeshAuraModel(Model model, Matrix transform, VC_MaterialId matId,
                           float intensity, VFX_MeshAuraParticleMode particleMode)
{
    const VFX_ElementMaterial *material = VFX_Material(matId);
    s_meshAuraSuppressLegacyMotes = true;
    VFX_DrawModelSilhouetteGlow(model, transform, material->glow, intensity);
    s_meshAuraSuppressLegacyMotes = false;
    if (particleMode != VFX_MESH_AURA_PARTICLES_STATIC_FLIPBOOK || model.meshCount <= 0) return;
    int index = GetRandomValue(0, model.meshCount - 1);
    MeshAura_EmitPlasma(&model.meshes[index], MatrixMultiply(model.transform, transform), material, intensity);
}

// ── 1. Draw Silhouette Glow on ANY Raylib Mesh ────────────────────────────────
void VFX_DrawMeshSilhouetteGlow(Mesh mesh, Matrix transform, Color glowColor, float intensity)
{
    if (intensity <= 0.001f || mesh.vertexCount <= 0) return;
    Silhouette_ApplyUniforms(glowColor, intensity);

    Material mat = LoadMaterialDefault();
    if (s_silhShader.id != 0) mat.shader = s_silhShader;

    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlEnableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    DrawMesh(mesh, mat, transform);

    rlDrawRenderBatchActive();
    EndBlendMode();

    int moteCount = s_meshAuraSuppressLegacyMotes ? 0 : (int)(6.0f * intensity);
    for (int i = 0; i < moteCount; i++)
    {
        Vector3 surfPos, surfNorm;
        if (VFX_SampleMeshSurfacePoint(&mesh, transform, &surfPos, &surfNorm))
            Silhouette_EmitMote(surfPos, surfNorm, glowColor);
    }
}

// ── 2. Draw Silhouette Glow on ANY Raylib Model ───────────────────────────────
void VFX_DrawModelSilhouetteGlow(Model model, Matrix transform, Color glowColor, float intensity)
{
    if (intensity <= 0.001f || model.meshCount <= 0) return;
    Silhouette_ApplyUniforms(glowColor, intensity);

    Shader origShaders[32];
    int count = model.materialCount > 32 ? 32 : model.materialCount;
    for (int i = 0; i < count; i++) origShaders[i] = model.materials[i].shader;
    for (int i = 0; i < count; i++)
    {
        if (s_silhShader.id != 0) model.materials[i].shader = s_silhShader;
    }

    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlEnableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    for (int m = 0; m < model.meshCount; m++)
    {
        int matIdx = (model.meshMaterial != NULL) ? model.meshMaterial[m] : 0;
        Matrix meshXf = MatrixMultiply(model.transform, transform);
        DrawMesh(model.meshes[m], model.materials[matIdx], meshXf);
    }

    rlDrawRenderBatchActive();
    EndBlendMode();

    for (int i = 0; i < count; i++) model.materials[i].shader = origShaders[i];

    int moteCount = s_meshAuraSuppressLegacyMotes ? 0 : (int)(6.0f * intensity);
    for (int i = 0; i < moteCount; i++)
    {
        int mIdx = GetRandomValue(0, model.meshCount - 1);
        Matrix meshXf = MatrixMultiply(model.transform, transform);
        Vector3 surfPos, surfNorm;
        if (VFX_SampleMeshSurfacePoint(&model.meshes[mIdx], meshXf, &surfPos, &surfNorm))
            Silhouette_EmitMote(surfPos, surfNorm, glowColor);
    }
}

void VFX_DrawModelSilhouetteGlowEx(Model model, Vector3 position, float yaw, float scale,
                                  Color glowColor, float intensity)
{
    Matrix matScale = MatrixScale(scale, scale, scale);
    Matrix matRot = MatrixRotateY(yaw);
    Matrix matTrans = MatrixTranslate(position.x, position.y, position.z);
    Matrix transform = MatrixMultiply(MatrixMultiply(matScale, matRot), matTrans);
    VFX_DrawModelSilhouetteGlow(model, transform, glowColor, intensity);
}

// ── 3. Draw Silhouette Glow on Animated Character Model ──────────────────────
void VFX_DrawCharacterSilhouetteGlowEx(Vector3 position, float yaw, float scale,
                                      const struct CharacterAnimState *animState,
                                      Color glowColor, float intensity)
{
    if (intensity <= 0.001f || !CharacterModel_IsLoaded()) return;
    Silhouette_ApplyUniforms(glowColor, intensity);

    Model charModel = CharacterModel_GetModel();
    if (charModel.meshCount <= 0) return;

    // Preserve original shaders to prevent clobbering normal scene rendering
    Shader origShaders[32];
    int count = charModel.materialCount > 32 ? 32 : charModel.materialCount;
    for (int i = 0; i < count; i++) origShaders[i] = charModel.materials[i].shader;
    for (int i = 0; i < count; i++)
    {
        if (s_silhShader.id != 0)
            charModel.materials[i].shader = s_silhShader;
    }

    rlDrawRenderBatchActive();
    rlEnableDepthTest();
    rlEnableDepthMask();
    BeginBlendMode(BLEND_ALPHA);

    if (animState != NULL)
    {
        CharacterModel_Draw(animState, position, yaw, scale, WHITE);
    }
    else
    {
        float yawDeg = yaw * RAD2DEG;
        DrawModelEx(charModel, position, (Vector3){ 0.0f, 1.0f, 0.0f }, yawDeg,
                    (Vector3){ scale, scale, scale }, WHITE);
    }

    rlDrawRenderBatchActive();
    EndBlendMode();

    // Restore original shaders
    for (int i = 0; i < count; i++) charModel.materials[i].shader = origShaders[i];

    // High-density surface envelope motes hugging the animated mesh
    int moteCount = (int)(7.0f * intensity);
    for (int i = 0; i < moteCount; i++)
    {
        Vector3 surfPos = { 0 };
        Vector3 surfNorm = { 0 };
        if (CharacterModel_SampleSurfacePoint(position, yaw, scale, &surfPos, &surfNorm))
            Silhouette_EmitMote(surfPos, surfNorm, glowColor);
    }

    // Dynamic point light at chest radiating pure white aura
    Vector3 chestPos = Vector3Add(position, (Vector3){ 0.0f, 1.10f * scale, 0.0f });
    VFXLight_Spawn(chestPos, glowColor, 3.2f * intensity, 0.05f, VFX_PRIORITY_HIGH_ULTIMATE);
}

// ── 4. Unified Universal Entry Point ─────────────────────────────────────────
void VFX_DrawCharacterSilhouetteGlow(Vector3 playerPos, float yaw, Color auraColor,
                                     float intensity, const void *targetMeshOrAnim)
{
    if (intensity <= 0.001f) return;

    // 1. Auto-detect: if target is NULL, use active character animation or loaded model
    if (targetMeshOrAnim == NULL)
    {
        const CharacterAnimState *activeAnim = s_activeGlobalCharAnim;
        VFX_DrawCharacterSilhouetteGlowEx(playerPos, yaw, 1.0f, activeAnim, auraColor, intensity);
        return;
    }

    // 2. Check if a generic target descriptor was passed
    const VFX_SilhouetteTarget *targetDesc = (const VFX_SilhouetteTarget*)targetMeshOrAnim;
    if (targetDesc->type == VFX_SILHOUETTE_MODEL && targetDesc->model)
    {
        VFX_DrawModelSilhouetteGlow(*targetDesc->model, targetDesc->transform, auraColor, intensity);
        return;
    }
    else if (targetDesc->type == VFX_SILHOUETTE_MESH && targetDesc->mesh)
    {
        VFX_DrawMeshSilhouetteGlow(*targetDesc->mesh, targetDesc->transform, auraColor, intensity);
        return;
    }

    // 3. Fallback: treat targetMeshOrAnim as a CharacterAnimState*
    const CharacterAnimState *animState = (const CharacterAnimState*)targetMeshOrAnim;
    VFX_DrawCharacterSilhouetteGlowEx(playerPos, yaw, 1.0f, animState, auraColor, intensity);
}

// ── 5. Standalone Preset Composition Call ────────────────────────────────────
void VFX_ComposeSilhouetteGlow(Vector3 pos, float yaw, float intensity, Camera3D camera)
{
    (void)camera;
    // Compatibility fixture for the former Silhouette Glow name. New gameplay
    // calls VFX_DrawMeshAura(Model/Mesh,...); this route proves the exact same
    // generic mesh path on the player model, including Plasma Wisp flipbooks.
    if (!CharacterModel_IsLoaded()) return;
    Model model = CharacterModel_GetModel();
    Matrix transform = MatrixMultiply(MatrixMultiply(MatrixScale(1.0f, 1.0f, 1.0f),
                                                       MatrixRotateY(yaw)),
                                      MatrixTranslate(pos.x, pos.y, pos.z));
    VFX_DrawMeshAuraModel(model, transform, VC_MAT_LIGHTNING, intensity,
                          VFX_MESH_AURA_PARTICLES_STATIC_FLIPBOOK);
}
