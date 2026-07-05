#include "visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/particle_system.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/trail_system.h"
#include "core/camera_fx.h"
#include "core/camera_context.h"
#include "environment/environment_system.h"
#include "core/ribbon_strip.h"
#include "core/path_spline.h"
#include "core/screen_distort.h"
#include "core/time_fx.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/geometry/mesh_cache.h"
#include "core/material/material_system.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/vfx_proc_ray.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_CONCURRENT_CAST_EFFECTS 16
static ForceField s_castPullFlds[MAX_CONCURRENT_CAST_EFFECTS];
static int s_castPullFldNextSlot = 0;

#define MAX_CONCURRENT_PROJECTILE_TRAILS 32
static ForceField s_flightFlds[MAX_CONCURRENT_PROJECTILE_TRAILS];
static int s_flightFldNextSlot = 0;

void VFX_ComposeSmokePuff(Vector3 pos, float size)
{
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 15;
    cfg.countMax = 25;
    cfg.speedMin = 2.0f;
    cfg.speedMax = 4.0f;
    cfg.radiusMin = size * 0.3f;
    cfg.radiusMax = size * 0.8f;
    cfg.lifetimeMin = 1.5f;
    cfg.lifetimeMax = 2.5f;
    cfg.pitchRange = PI;
    cfg.upwardBias = 2.0f;

    cfg.colorStart = (Color){100, 100, 100, 200};
    cfg.colorEnd = (Color){50, 50, 50, 0};

    static ForceField f = {0};
    if (f.layerCount == 0)
    {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 5.0f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;

    ParticleSystem_SpawnRadialBurst(pos, size, &cfg);
}

void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration)
{
    Vector3 dir = Vector3Subtract(end, start);
    float len = Vector3Length(dir);
    if (len < 0.1f)
        return;
    dir = Vector3Scale(dir, 1.0f / len);

    int numPuffs = (int)(len / 2.0f) + 1;

    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 2;
    cfg.countMax = 4;
    cfg.speedMin = 0.5f;
    cfg.speedMax = 1.0f;
    cfg.radiusMin = 0.5f;
    cfg.radiusMax = 1.2f;
    cfg.lifetimeMin = duration * 0.8f;
    cfg.lifetimeMax = duration * 1.2f;
    cfg.pitchRange = PI;
    cfg.upwardBias = 1.0f;

    cfg.colorStart = (Color){100, 100, 100, 200};
    cfg.colorEnd = (Color){50, 50, 50, 0};

    static ForceField f = {0};
    if (f.layerCount == 0)
    {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 3.0f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;

    for (int i = 0; i <= numPuffs; i++)
    {
        float t = (float)i / (float)numPuffs;
        Vector3 pos = Vector3Add(start, Vector3Scale(dir, t * len));
        ParticleSystem_SpawnRadialBurst(pos, 1.0f, &cfg);
    }
}

void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width)
{
    float totalDist = Vector3Distance(start, end);
    if (totalDist > 0.001f) {
        Vector3 dir = Vector3Normalize(Vector3Subtract(end, start));
        Vector3 right = (Vector3){ -dir.z, 0.0f, dir.x };
        float halfW = width * 0.5f;
        
        Vector3 p1 = Vector3Add(start, Vector3Scale(right, -halfW));
        Vector3 p2 = Vector3Add(end, Vector3Scale(right, -halfW));
        Vector3 p3 = Vector3Add(end, Vector3Scale(right, halfW));
        Vector3 p4 = Vector3Add(start, Vector3Scale(right, halfW));
        
        float yOffset = 0.02f;
        p1.y += yOffset;
        p2.y += yOffset;
        p3.y += yOffset;
        p4.y += yOffset;
        
        Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
        
        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ALPHA);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        
        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
            rlColor4ub(255, 255, 255, 255);
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p1.x, p1.y, p1.z);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p2.x, p2.y, p2.z);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p3.x, p3.y, p3.z);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p4.x, p4.y, p4.z);
        rlEnd();
        rlSetTexture(0);
        
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
    }
}

int VFX_ComposeLightningBolt(Vector3 start, Vector3 end, float scale)
{
    int id = SpawnProcBolt(ProcRay_BoltLightningConfig(), scale);
    VFXLight_Spawn(end, (Color){0, 200, 255, 255}, 4.0f, 0.2f, 0);
    return id;
}

void VFX_ComposeImpact(Vector3 pos, EffectPresetType preset, float scale)
{
    const VFX_ImpactPreset *p = VFX_Preset_GetImpact(preset);
    if (p == NULL)
        return;

    PlayImpactSound(preset);

    if (scale >= 1.5f)
        TimeFX_Hitstop(0.09f, 0.05f);

    /* 1. Screen Distortion */
    if (p->distortEnabled)
    {
        ScreenDistort_Add(pos, p->distortRadius * scale, p->distortStrength, p->distortLife, p->distortSpeed);
    }

    /* 2. Light Spawn */
    if (p->lightEnabled)
    {
        VFXLight_Spawn(pos, p->lightColor, p->lightRadius * scale, p->lightLife, VFX_PRIORITY_LOW);
    }

    /* 3. Decal Spawn */
    if (p->decalEnabled)
    {
        SpawnGroundDecal(p->decalPreset, pos, p->decalScale * scale, p->decalLife);
    }

    /* 4. Particle Radial Burst */
    if (p->particlesEnabled)
    {
        ParticleSystem_SpawnRadialBurst(pos, scale, &p->particles);
    }
}

void VFX_ComposeCast(Vector3 pos, EffectPresetType preset, float scale)
{
    const VFX_CastPreset *p = VFX_Preset_GetCast(preset);
    if (p == NULL)
        return;

    PlayCastSound(preset);

    /* 1. Light Flash */
    VFXLight_Spawn(pos, p->flashColor, p->lightRadius * scale, p->lightLifetime, VFX_PRIORITY_LOW);

    /* 2. Inward-pulling force field setup */
    ForceField *castPullFld = &s_castPullFlds[s_castPullFldNextSlot];
    s_castPullFldNextSlot = (s_castPullFldNextSlot + 1) % MAX_CONCURRENT_CAST_EFFECTS;
    ForceField_Clear(castPullFld);

    float spawnRadius = p->spawnRadius * scale;
    float pullStrength = p->pullStrength * scale;
    ForceField_AddLayer(castPullFld, (ForceLayer){
                                         .type = FORCE_GRAVITY_POINT,
                                         .origin = pos,
                                         .strength = pullStrength,
                                         .radius = spawnRadius * 1.5f,
                                         .falloff = 1.0f});

    /* 3. Spawn gathering particles */
    int count = (int)((float)p->particleCount * scale);
    for (int i = 0; i < count; i++)
    {
        float a = (float)i / count * 2.0f * PI;
        float r = spawnRadius * (0.6f + 0.4f * ((float)rand() / (float)RAND_MAX));
        Vector3 spawnPos = {
            pos.x + cosf(a) * r,
            pos.y + ((float)rand() / (float)RAND_MAX) * spawnRadius * 0.5f,
            pos.z + sinf(a) * r};
        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = (Vector3){0.0f, 0.0f, 0.0f},
            .radius = ((float)rand() / (float)RAND_MAX * 0.012f + 0.005f) * scale,
            .lifetime = (float)rand() / (float)RAND_MAX * 0.5f + 0.4f,
            .gradient = p->gradient,
            .forceField = castPullFld});
    }
}

int VFX_ComposeProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed)
{
    const VFX_ProjectilePreset *p = VFX_Preset_GetProjectile(preset);
    if (p == NULL)
        return -1;

    /* 1. Setup directional flight force field */
    ForceField *flightFld = &s_flightFlds[s_flightFldNextSlot];
    s_flightFldNextSlot = (s_flightFldNextSlot + 1) % MAX_CONCURRENT_PROJECTILE_TRAILS;
    ForceField_Clear(flightFld);
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, start));
    ForceField_AddLayer(flightFld, (ForceLayer){.type = FORCE_GRAVITY_DIR, .direction = dir, .strength = 3.25f});
    ForceField_AddLayer(flightFld, (ForceLayer){.type = FORCE_NOISE_PERLIN, .strength = 0.2f, .noiseScale = 0.08f, .noiseSpeed = 2.0f});

    /* 2. Setup tail dust config */
    static ParticleConfig s_tailEmit;
    s_tailEmit = (ParticleConfig){
        .radius = 0.3f * scale,
        .lifetime = 0.4f,
        .gradient = p->gradient,
        .forceField = flightFld};

    /* 3. Spawn head particle */
    SpawnParticle((ParticleConfig){
        .position = start,
        .velocity = Vector3Scale(dir, speed),
        .colorStart = p->tint,
        .colorEnd = p->tint,
        .radius = 0.5f * scale,
        .lifetime = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.5f,
        .gradient = p->gradient,
        .forceField = flightFld,
        .onLiveEmit = &s_tailEmit,
        .onLiveEmitRate = 120.0f});

    /* 4. Spawn trail ribbon */
    TrailConfig cfg = {
        .type = TRAIL_TYPE_PROJECTILE,
        .pos = start,
        .vel = Vector3Scale(dir, speed),
        .len = 4.0f * scale,
        .thick = 0.8f * scale,
        .trailLength = 20.0f * scale,
        .life = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.5f,
        .target = target,
        .scale = scale,
        .tint = p->tint,
        .forceField = flightFld,
        .gradient = p->gradient};
    return SpawnTrailEntity(cfg);
}

void VFX_ComposeTriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg)
{
    if (cfg == NULL)
        return;

    /* Step 1: screen distortion */
    if (cfg->distortEnabled)
    {
        ScreenDistort_Add(pos, cfg->distortRadius, cfg->distortStrength,
                          cfg->distortLife, cfg->distortSpeed);
    }

    /* Step 2: ground decal */
    if (cfg->decalEnabled)
    {
        float rotation = cfg->decalRandomRotation
                             ? (float)GetRandomValue(0, 360)
                             : cfg->decalFixedRotation;
        DecalSystem_Add(pos, rotation, cfg->decalScale * sizeScale,
                        cfg->decalTex, cfg->decalLife, cfg->decalTint);
    }

    /* Step 3: point light flash */
    if (cfg->lightEnabled)
    {
        VFXLight_Spawn(pos, cfg->lightColor, cfg->lightRadius * sizeScale, cfg->lightLife, VFX_PRIORITY_LOW);
    }

    /* Step 4: radial particle burst */
    if (cfg->particlesEnabled)
    {
        ParticleSystem_SpawnRadialBurst(pos, sizeScale, &cfg->particles);
    }
}

void VFX_ComposeStonePillar(Vector3 basePos, float progress) {
    if (progress <= 0.0f) return;
    
    rlDisableBackfaceCulling();
    float height = 1.8f;
    float radius = 0.4f;
    float rise = progress * progress * (3.0f - 2.0f * progress);
    float yOffset = -height * (1.0f - rise) - 0.2f;
    Vector3 actualPos = Vector3Add(basePos, (Vector3){0, yOffset, 0});
    float topRadius = radius * (1.0f - 0.4f); // sharpness is 0.4f
    
    EffectMaterial mat = Material_Get(MAT_ROCK);
    Material_Begin(mat);
    ProceduralMesh_DrawOrganicStonePillar(actualPos, height + 0.2f, radius, topRadius);
    Material_End();
}

void VFX_ComposeBoulder(Vector3 pos) {
    // 1. Draw smooth round center boulder using MAT_ROCK
    EffectMaterial mat = Material_Get(MAT_ROCK);
    Material_Begin(mat);
    DrawCoreSphere(pos, 0.5f, 32, 32, WHITE);
    Material_End();

    // 2. Draw outer jagged rock shard using MeshCache_GetRock
    RockMeshData* data = MeshCache_GetRock(1337, 0.4f); // seed 1337, jaggedness 0.4
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    Material_Begin(mat);
    
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y, pos.z);
    rlScalef(0.5f, 0.5f, 0.5f);
    ProceduralMesh_DrawRock(data, WHITE);
    rlPopMatrix();
    
    Material_End();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void VFX_ComposeIceCrystal(Vector3 basePos, int seed) {
    ShardClusterMeshData* data = MeshCache_GetIce(seed, 0.2f); // sharpness 0.2f
    EffectMaterial mat = Material_Get(MAT_ICE);
    
    BeginBlendMode(BLEND_ALPHA);
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    Material_Begin(mat);
    
    rlPushMatrix();
    rlTranslatef(basePos.x, basePos.y, basePos.z);
    rlScalef(0.3f, 1.5f, 0.3f); // radius 0.3f, height 1.5f
    ProceduralMesh_DrawShardCluster(data, WHITE);
    rlPopMatrix();
    
    Material_End();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
}

void VFX_ComposeMagicPuddle(Vector3 pos) {
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    
    float radius = 1.5f;
    rlPushMatrix();
    rlTranslatef(pos.x, pos.y + 0.05f, pos.z);
    
    Shader flowShader = ResourceManager_LoadShader(0, "core/shaders/puddle.fs");
    Texture2D tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    Texture2D flowTex = ResourceManager_LoadTexture("assets/textures/water_flow.png");
    
    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(flowTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(flowTex, TEXTURE_FILTER_BILINEAR);

    int timeLoc = GetShaderLocation(flowShader, "u_time");
    int tex0Loc = GetShaderLocation(flowShader, "causticsTex");
    int tex1Loc = GetShaderLocation(flowShader, "flowTex");
    
    float time = GetTime();
    SetShaderValue(flowShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    
    BeginShaderMode(flowShader);
    SkillManager_BeginShader(flowShader);
    SetShaderValueTexture(flowShader, tex0Loc, tex);
    SetShaderValueTexture(flowShader, tex1Loc, flowTex);
    
    rlDrawRenderBatchActive(); 
    rlActiveTextureSlot(1);
    rlEnableTexture(flowTex.id);
    rlActiveTextureSlot(0);
    rlEnableTexture(tex.id);
    
    ProceduralMesh_DrawOrganicPuddle((Vector3){0, 0, 0}, radius);
    
    rlDrawRenderBatchActive();
    rlActiveTextureSlot(1);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    rlDisableTexture();
    
    SkillManager_EndShader();
    EndShaderMode();
    
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
    rlPopMatrix();
    rlEnableBackfaceCulling();
}

void VFX_ComposeFireball(Vector3 pos, float time) {
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    
    float radius = 0.3f;
    Vector3 actualPos = Vector3Add(pos, (Vector3){0, 0.5f, 0});

    EffectMaterialParams coreParams = {0};
    coreParams.baseColor = (Color){255, 200, 100, 255};
    coreParams.emissiveIntensity = 2.0f;
    EffectMaterial coreMat = Material_LoadCustom(coreParams);
    Material_Begin(coreMat);
    DrawCoreSphere(actualPos, radius * 0.6f, 16, 16, WHITE);
    Material_End();
    
    EffectMaterialParams auraParams = {0};
    auraParams.baseColor = (Color){255, 100, 0, 150};
    auraParams.rimStrength = 2.0f;
    auraParams.fresnelPower = 2.0f;
    auraParams.emissiveIntensity = 1.0f;
    auraParams.distortionStrength = 0.8f;
    auraParams.translucency = 1.0f;
    EffectMaterial auraMat = Material_LoadCustom(auraParams);
    Material_Begin(auraMat);
    DrawCoreSphere(actualPos, radius, 16, 16, WHITE);
    Material_End();
    rlEnableDepthMask();
    EndBlendMode();
}

void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time) {
    static Shader s_tubeShader = {0};
    static int s_timeLoc = -1;
    static int s_viewPosLoc = -1;
    static int s_lightDirLoc = -1;
    static int s_uvLengthLoc = -1;
    
    if (s_tubeShader.id == 0) {
        s_tubeShader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs", "skills/water/water_stream/tube.fs");
        s_timeLoc     = GetShaderLocation(s_tubeShader, "u_time");
        s_viewPosLoc  = GetShaderLocation(s_tubeShader, "viewPos");
        s_lightDirLoc = GetShaderLocation(s_tubeShader, "u_lightDir");
        s_uvLengthLoc = GetShaderLocation(s_tubeShader, "u_uvLength");
    }
    
    if (s_tubeShader.locs == NULL) return;
    
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlEnableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    BeginShaderMode(s_tubeShader);
    
    SkillManager_BeginShader(s_tubeShader);
    
    SetShaderValue(s_tubeShader, s_timeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_tubeShader, s_viewPosLoc, &camera.position, SHADER_UNIFORM_VEC3);
    Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
    SetShaderValue(s_tubeShader, s_lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    float uvLength = 3.0f; // TUBE_UV_LENGTH_SCALE
    SetShaderValue(s_tubeShader, s_uvLengthLoc, &uvLength, SHADER_UNIFORM_FLOAT);
    
    rlColor4ub(255, 255, 255, 255);
    
    TubeMeshData mesh;
    TubeMeshConfig config = ProceduralMesh_DefaultTubeConfig();
    ProceduralMesh_BuildTube(&mesh, p0, p1, p2, p3, radius,
                             progress, time, 30, 20, &config);
    ProceduralMesh_DrawTube(&mesh, 3.0f);
    
    SkillManager_EndShader();
    EndShaderMode();
    EndBlendMode();
    rlDisableBackfaceCulling();
    rlEnableDepthMask();
}

static Vector3 GetWoodPointAt(Vector3 startPos, Vector3 p1, Vector3 p2, Vector3 contactPos, Vector3 targetPos, float t, float sizeScale, int branchIndex, int branchCount) {
    if (t <= 1.0f) {
        Vector3 pos = GetBezierPoint(startPos, p1, p2, contactPos, t);
        Vector3 dir = Vector3Normalize(Vector3Subtract(targetPos, startPos));
        Vector3 perp = (Vector3){-dir.z, 0, dir.x};
        if (Vector3Length(perp) == 0.0f) perp = (Vector3){0, 0, 1};
        perp = Vector3Normalize(perp);
        
        float wiggle = sinf(t * 18.0f) + sinf(t * 31.0f) * 0.4f;
        pos = Vector3Add(pos, Vector3Scale(perp, wiggle * 0.06f * sizeScale));
        return pos;
    } else {
        float t_spiral = t - 1.0f;
        float ratio = t_spiral / 0.8f;
        if (ratio > 1.0f) ratio = 1.0f;
        
        float contactAngle = atan2f(contactPos.z - targetPos.z, contactPos.x - targetPos.x);
        float coilDir = (branchIndex % 2 == 0) ? 1.0f : -1.0f;
        float theta = ratio * 2.2f * (2.0f * PI) * coilDir +
                      sinf(ratio * 8.0f) * 0.8f + sinf(ratio * 19.0f) * 0.25f;
        theta += sinf(ratio * 15.0f) * 0.25f;
        
        float phiOffset = 0.0f;
        if (branchCount > 1) {
            phiOffset = ((float)branchIndex / (float)branchCount) * PI;
        }
        float phi = contactAngle + phiOffset;
        
        float r_a = 0.28f * sizeScale;
        float r_b = 0.28f * sizeScale;
        
        float wobbleScale = ratio > 0.1f ? 1.0f : ratio / 0.1f;
        float wobble = (sinf(theta * 6.0f) * 0.04f + cosf(theta * 11.0f) * 0.015f) * wobbleScale;
        
        float tighten = 1.0f - ratio * 0.45f;
        float curr_ra = (r_a + wobble) * tighten;
        float curr_rb = (r_b + wobble) * tighten;
        float wrapFactor = 0.75f + 0.25f * sinf(ratio * 10.0f);
        
        curr_ra *= wrapFactor;
        curr_rb *= wrapFactor;
        
        float radiusNoise = 1.0f + sinf(theta * 3.0f) * 0.08f;
        curr_ra *= radiusNoise;
        curr_rb *= radiusNoise;
        
        float height = 0.8f * sizeScale;
        float y = targetPos.y - height * 0.5f + ratio * height;
        
        return (Vector3){
            targetPos.x + curr_ra * cosf(theta) * cosf(phi) - curr_rb * sinf(theta) * sinf(phi),
            y + wobble * 0.1f,
            targetPos.z + curr_ra * cosf(theta) * sinf(phi) + curr_rb * sinf(theta) * cosf(phi)
        };
    }
}

void VFX_ComposeGlowingVine(Vector3 startPos, Vector3 targetPos, Vector3 p1, Vector3 p2, Vector3 contactPos, float progress, float time, float sizeScale, int branchIndex, int branchCount) {
    const int pointCount = 64;
    static RibbonPoint ribbonPoints[64];
    
    float drawProgress = progress;
    if (drawProgress > 1.8f) drawProgress = 1.8f;
    
    // Generate path points dynamically
    for (int i = 0; i < pointCount; i++) {
        float norm = (float)i / (float)(pointCount - 1);
        float t = norm * drawProgress;
        
        Vector3 pt = GetWoodPointAt(startPos, p1, p2, contactPos, targetPos, t, sizeScale, branchIndex, branchCount);
        float taper = powf(1.0f - norm, 0.6f);
        
        ribbonPoints[i].position = pt;
        ribbonPoints[i].halfWidth = 0.06f * sizeScale * taper;
        ribbonPoints[i].v = norm;
        
        float alpha = 1.0f;
        if (progress > 1.8f) {
            alpha = 1.0f - (progress - 1.8f) / (2.4f - 1.8f);
            if (alpha < 0.0f) alpha = 0.0f;
        }
        ribbonPoints[i].tint = ColorAlpha(ELEMENT_COLOR_WOOD, alpha * 0.8f);
    }
    
    // Custom Material presets for the glowing emerald wood
    EffectMaterialParams matParams = {0};
    matParams.baseColor = ColorAlpha(ELEMENT_COLOR_WOOD, 0.8f);
    matParams.rimStrength = 2.5f;
    matParams.fresnelPower = 2.0f;
    matParams.emissiveIntensity = 2.0f;
    matParams.distortionStrength = 0.0f;
    matParams.translucency = 0.5f;
    
    EffectMaterial mat = Material_LoadCustom(matParams);
    
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);
    
    Material_Begin(mat);
    DrawRibbonStrip(ribbonPoints, pointCount, (Texture2D){0}, camera);
    Material_End();
    
    // Additive glow pass for the bright inner core
    BeginBlendMode(BLEND_ADDITIVE);
    matParams.emissiveIntensity = 3.5f;
    matParams.translucency = 0.0f;
    EffectMaterial matGlow = Material_LoadCustom(matParams);
    
    Material_Begin(matGlow);
    for (int i = 0; i < pointCount; i++) {
        ribbonPoints[i].halfWidth *= 0.5f; // thinner core
        float alpha = progress > 1.8f ? (1.0f - (progress - 1.8f)/(2.4f - 1.8f)) : 1.0f;
        ribbonPoints[i].tint = ColorAlpha(WHITE, alpha * 0.4f);
    }
    DrawRibbonStrip(ribbonPoints, pointCount, (Texture2D){0}, camera);
    Material_End();
    
    EndBlendMode();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}
