# WUXING SKILLS CORE API — CONDENSED AI REFERENCE
> Compact C99 API & mathematical specification for procedurally generating skills in the Wuxing engine.

---

## 1. COMPILATION & ARCHITECTURE CONSTRAINT
* **Standard:** Strict C99. No dynamic allocations (`malloc`/`free`). Use `static` arrays for entity pools.
* **Mandatory Headers:** Every `.c` file MUST explicitly include `<stddef.h>`, `<stdlib.h>`, and `<stdio.h>`.
* **Registry Auto-Detection:** Save files in `skills/[element]/[skill_name]_skill/` as `[skill_name]_skill.h` and `[skill_name]_skill.c`.
* **Include Folder Matching:** Include your own header using the exact directory name: `#include "skills/[element]/[skill_name]_skill/[skill_name]_skill.h"`. Never omit the `_skill` folder suffix.

---

## 2. SHADERS & CACHING RESOURCES (`core/resource_manager.h`)
* `Texture2D ResourceManager_LoadTexture(const char* path)` -> Loads/retrieves cached texture (e.g. `"assets/textures/water_caustics.png"`).
* `Shader ResourceManager_LoadShader(const char* vs, const char* fs)` -> Loads/retrieves cached shader. Pass `NULL` for `vs` to use default vertex shader.
* **Unload Rule:** **DO NOT** call Raylib `UnloadTexture` or `UnloadShader` in `Unload[Name]Skill` (cleanup is managed globally).

---

## 3. UNIFIED ELEMENT COLOR MACROS (`core/skill_manager.h`)
* `ELEMENT_COLOR_WATER` : `(Color){ 41, 128, 185, 255 }` (Dodger Blue)
* `ELEMENT_COLOR_WOOD`  : `(Color){ 46, 204, 113, 255 }` (Lime Green)
* `ELEMENT_COLOR_FIRE`  : `(Color){ 231, 76, 60, 255 }` (Crimson Red)
* `ELEMENT_COLOR_EARTH` : `(Color){ 230, 126, 34, 255 }` (Ochre Brown)
* `ELEMENT_COLOR_METAL` : `(Color){ 149, 165, 166, 255 }` (Silver Gray)
* `ELEMENT_COLOR_TAIJI` : `(Color){ 155, 89, 182, 255 }` (Deep Purple)
* **Derived Shading:** Use `ColorLerp(color, BLACK/WHITE, factor)` and `ColorAlpha(color, alpha)`.

---

## 4. LIFECYCLE CALLBACKS (`[skill_name]_skill.h`)
```c
#ifndef SKILL_[NAME]_H
#define SKILL_[NAME]_H
#include "raylib.h"
#include "core/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct { Vector3 position; float radius; bool active; } SkillProjectile;
#endif

// WARNING: When calling Skill_CalculateDamage or Skill_CalculateKnockback,
// ONLY use exact enum values from `core/skill_manager.h` (e.g. SKILL_CAT_AOE_CONTROL).
// DO NOT hallucinate category names like SKILL_CAT_AOE_BURST or compilation will fail!

void Init[Name]Skill(int w, int h);
void Cast[Name]Skill(Vector3 start, Vector3 target, SkillParams p);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);
bool Is[Name]SkillCoiling(void);
int Get[Name]SkillProjectiles(SkillProjectile *out, int max);
void Deactivate[Name]Projectile(int index);
#endif
```

---

## 5. FORCE FIELD CONFIGURATION (`core/force_field.h`)
Up to 8 active layers, evaluated internally by the engine:
* Types: `FORCE_GRAVITY_DIR`, `FORCE_GRAVITY_POINT`, `FORCE_VORTEX`, `FORCE_WIND`, `FORCE_NOISE_PERLIN`, `FORCE_NOISE_CURL`, `FORCE_DRAG`, `FORCE_VISCOSITY`, `FORCE_RADIAL_AXIS`, `FORCE_VORTEX_AXIS`.
* Falloff: `0` = Constant, `1` = Linear drop-off to 0 at radius, `2` = Quadratic.

**WARNING:** `ForceField` is a container, NOT a layer! To modify it: `myField.layers[0].type = FORCE_VORTEX;`, `myField.layers[0].direction = (Vector3){0,1,0};`, `myField.layers[0].noiseScale = 0.05f;`, `myField.layerCount = 1;`. DO NOT use non-existent fields like `.frequency` or `.axisDir`.
**WARNING:** DO NOT hallucinate global functions like `ApplyDamage()`, `ApplyKnockback()`, or `GetVfxActiveCameraPosition()`. Use `AddFloatingText()` and `AddKnockbackToEnemy()` instead, and `extern Camera3D camera` if you need camera position.

---

## 6. VFX & POST-PROCESSING SYSTEM APIS
* **Particles (`core/particle_system.h`):** `void SpawnParticle(ParticleConfig cfg);`
  - `ParticleConfig`: `Vector3 position, velocity; Color colorStart, colorEnd; float radius, lifetime; const ForceField *forceField; const ColorGradient *gradient; const SpriteAnim *spriteAnim; const ParticleConfig *onDeathEmit; int onDeathEmitCount; const ParticleConfig *onLiveEmit; float onLiveEmitRate;`
* **Gradients (`core/color_gradient.h`):** `bool ColorGradient_AddStop(ColorGradient *g, float t, Color col);` (gradient overrides colorStart/colorEnd).
* **Emitters (`core/emitter_system.h`):** `int CreateEmitter(EmitterConfig cfg, Vector3 start);` `UpdateEmitterTarget`, `StopEmitter`, `KillEmitter`.
* **Trails (`core/trail_system.h`):** `int SpawnTrailEntity(TrailConfig cfg);` `KillTrail`. Types: `TRAIL_TYPE_PROJECTILE`, `TRAIL_TYPE_WISP`, `TRAIL_TYPE_PORTAL`, `TRAIL_TYPE_FOLLOWER`.
* **Decals (`core/decal_system.h`):** `void DecalSystem_Add(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);` (Must have Y offset small, e.g., +0.02f)
  - Shaded automatically via `decal.fs` radial mask (eliminates square edges).
  - **Draw order:** Render decals *before* 3D opaque meshes (underfoot sorting).
  - **Aesthetic Scale:** Set scale to `4.0x - 5.5x` of structure base radius (e.g. `baseRadius * scale * 5.2f`).
* **VFX Quality Standards:**
  - **Emissive Shading:** Keep dark organic textures; multiply diffuse by breathing multiplier: `diffuse.rgb * (1.35f + 0.1f * sin(u_time * 3.5f))`. No flat neon colors.
  - **Continuous Aura:** During holding/active phases, continuously emit theme-specific particles along the height/radius of the mesh.
  - **Persistent Lights:** Match dynamic light lifetime to active phase duration (e.g. rise + hold time = `1.4f`s).
* **Distortion (`core/screen_distort.h`):** `void ScreenDistort_Add(Vector3 pos, float rad, float str, float life, float speed);`
* **Lights (`core/vfx_light.h`):** `void VFXLight_Spawn(Vector3 pos, Color col, float rad, float life);`
* **Camera Shake (`core/camera_fx.h`):** `void CameraFX_Shake(float trauma);`
* **Combat API (`core/skill_manager.h`):** `void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);` (automatic internal enemy tracking, spawns damage texts and hurls targets).
* **Shader Binding (`core/skill_manager.h`):** `void SkillManager_BeginShader(Shader shader);` and `void SkillManager_EndShader(void);` (auto-binds `u_time`, `u_viewPos`, `u_resolution`).
* **Procedural Helpers (`core/procedural_mesh_utils.h`):** (Dùng `rlgl` nguyên bản cực nhẹ).
  - `DrawCoreSphere`, `DrawCoreCylinder`, `DrawCoreCone`, `DrawCoreCube`.
  - `DrawCorePlanePolygon(..., sides)`: `sides=3` (Tam giác), `sides=6` (Lục giác), `sides=32` (Hình tròn) -> Dùng làm khiên/trận pháp.
  - `DrawCorePrism(..., sides)`: `sides=6` ra khối lục giác đá/băng.
  - `DrawCoreTorus(...)`: Vòng đai quang luân/lốc xoáy 3D.
  - `DrawCorePlaneRect(...)`: Mặt phẳng vuông/chữ nhật.

---

## 7. PROHIBITED PROCEDURES & ANTI-ROBOTIC LAWS
1. **No Standard Primitives:** High-level calls (`DrawCylinder`, `DrawSphere`, etc.) and `Wires` renderers are strictly **PROHIBITED**. Render procedural meshes using the Core System's `core/procedural_mesh_utils.h` helpers or custom low-level `rlgl` buffers.
2. **Jitter:** Add random spatial offsets perpendicular to movement line directions.
3. **Variation:** Apply `scale = baseScale * GetRandomValue(85, 115) / 100.f` and `rotation = GetRandomValue(0, 360) * DEG2RAD`.
4. **Dissolve Transitions:** Run custom shaders continuously. Set uniform `u_dissolve = 0.0f` during the active phase, then sweep `0.0f -> 1.0f` on fade/death. Do not toggle shaders abruptly.

---

## 8. DYNAMIC TUBE MESH MATH (Bezier Extrusion Pipeline)
1. **Cubic Bezier Path:** $P(t) = P_0(1-t)^3 + 3P_1(1-t)^2t + 3P_2(1-t)t^2 + P_3t^3$.
2. **Tangent Vector (Derivative):** $T(t) = 3(P_1-P_0)(1-t)^2 + 6(P_2-P_1)(1-t)t + 3(P_3-P_2)t^2$.
3. **Frenet Frame Coordinates:** $T = \text{Normalize}(T(t))$, $U_{temp} = (T_y > 0.99) ? (1,0,0) : (0,1,0)$. $R = \text{Normalize}(U_{temp} \times T)$, $U = T \times R$.
4. **Wobble Twisting:** Rotate frame by $\theta = \text{amp} \cdot \sin(t \cdot \pi \cdot \text{freq} + \text{time} \cdot \text{speed})$.
   $U_{twisted} = U \cos\theta + R \sin\theta$, $R_{twisted} = \text{Normalize}(U_{twisted} \times T)$.
5. **Capsule Tapering:** $R_{final} = R_{base} \cdot (0.3 + 0.7\sqrt{\sin(t\pi)}) \cdot (0.15 + 0.85t)$.
6. **Deformation Wave:** $deform = 1.0 + 0.12\sin(18t + radialAngle \cdot 3 + time \cdot 10) + 0.08\sin(9t - radialAngle \cdot 5 - time \cdot 6)$.
7. **Triangle End-Caps:** Cap head/tail boundaries using apex-to-ring triangle fans (`rlBegin(RL_TRIANGLES)`). Set UV $V$ coordinate to $-0.1$ for the apex to flow cleanly.

---

## 9. CODE SKELETON: PROCEDURAL WATER STREAM (TUBE)
```c
#include "skills/water/water_stream/water_stream_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

#define MAX_STREAMS 4
#define TUBE_SEGMENTS 30
#define RADIAL_SEGS 16
#define BASE_RADIUS 8.0f

typedef struct {
    Vector3 p0, p1, p2, p3;
    float progress; // 0.0 -> 1.0
    float scale, life;
    Vector3 headPos;
    bool active, impactTriggered;
    float damage, knockback;
} WaterStream;

static WaterStream s_streams[MAX_STREAMS];
static Texture2D s_causticsTex;
static Shader s_shader;
static int s_uDissolveLoc, s_uTimeLoc;
static ColorGradient s_splashGrad;

static Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(Vector3Scale(p0, u*u*u), Vector3Add(Vector3Scale(p1, 3.f*u*u*t), Vector3Add(Vector3Scale(p2, 3.f*u*t*t), Vector3Scale(p3, t*t*t))));
}
static Vector3 GetBezierDerivative(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(Vector3Scale(Vector3Subtract(p1, p0), 3.f*u*u), Vector3Add(Vector3Scale(Vector3Subtract(p2, p1), 6.f*u*t), Vector3Scale(Vector3Subtract(p3, p2), 3.f*t*t)));
}

static void TriggerImpactVFX(Vector3 pos, float scale) {
    ScreenDistort_Add(pos, 85.0f, 0.7f, 0.6f, 150.0f);
    DecalSystem_Add(pos, (float)GetRandomValue(0,360), BASE_RADIUS*scale*2.5f, s_causticsTex, 4.f, ColorAlpha(ELEMENT_COLOR_WATER, 0.7f));
    VFXLight_Spawn(pos, ELEMENT_COLOR_WATER, 55.f*scale, 0.5f);
    for (int i = 0; i < 15; i++) {
        float a = (float)i / 15.0f * 2.0f * 3.14159f;
        Vector3 v = { cosf(a)*(float)GetRandomValue(55,110)*scale, (float)GetRandomValue(60,140)*scale, sinf(a)*(float)GetRandomValue(55,110)*scale };
        SpawnParticle((ParticleConfig){ pos, v, ELEMENT_COLOR_WATER, ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.3f), 0.f), (float)GetRandomValue(25,55)/10.f, 0.8f, NULL, &s_splashGrad, NULL, NULL, 0, NULL, 0.f });
    }
}

void InitWaterStreamSkill(int w, int h) {
    s_causticsTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    s_shader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs", "skills/water/water_stream/tube.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uTimeLoc = GetShaderLocation(s_shader, "u_time");
    s_splashGrad.count = 0;
    ColorGradient_AddStop(&s_splashGrad, 0.00f, ELEMENT_COLOR_WATER);
    ColorGradient_AddStop(&s_splashGrad, 0.40f, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.2f));
    ColorGradient_AddStop(&s_splashGrad, 1.00f, (Color){0,0,0,0});
}

void CastWaterStreamSkill(Vector3 start, Vector3 target, SkillParams params) {
    for (int i=0; i<MAX_STREAMS; i++) {
        if (!s_streams[i].active) {
            Vector3 to = Vector3Subtract(target, start);
            s_streams[i] = (WaterStream){ start, Vector3Add(start, (Vector3){to.x*0.25f, 25.f, to.z*0.25f}), Vector3Add(start, (Vector3){to.x*0.75f, 25.f, to.z*0.75f}), target, 0.f, (params.sizeScale > 0.f)?params.sizeScale:1.f, 1.8f, start, true, false, Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params), Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params) };
            break;
        }
    }
}

void UpdateWaterStreamSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i=0; i<MAX_STREAMS; i++) {
        if (!s_streams[i].active) continue;
        WaterStream *s = &s_streams[i];
        s->life -= dt;
        if (s->life <= 0.f) { s->active = false; continue; }
        s->progress = Clamp(s->progress + 1.5f * dt, 0.0f, 1.0f);
        s->headPos = GetBezierPoint(s->p0, s->p1, s->p2, s->p3, s->progress);
        if (s->progress < 1.0f) {
            SpawnParticle((ParticleConfig){ s->headPos, Vector3Scale(Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, s->progress)), 12.f), ColorAlpha(ELEMENT_COLOR_WATER, 0.7f), (Color){255,255,255,0}, 2.2f * s->scale, 0.35f, NULL, &s_splashGrad, NULL, NULL, 0, NULL, 0.f });
        }
        if (!s->impactTriggered && s->progress >= 0.95f) {
            if (Vector3DistanceSqr(s->headPos, enemyPos) <= powf((BASE_RADIUS*s->scale)+enemyRadius, 2.f)) {
                TriggerImpactVFX(s->headPos, s->scale);
                ApplyDamage(s->damage); ApplySlow(2.5f, 0.4f);
                ApplyKnockback(Vector3Normalize(Vector3Subtract(enemyPos, s->p0)), s->knockback);
                s->impactTriggered = true;
            }
        }
    }
}

void DrawWaterStreamSkill(void) {
    float time = (float)GetTime();
    rlDisableDepthMask();
    BeginShaderMode(s_shader);
    SetShaderValue(s_shader, s_uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    for (int idx=0; idx<MAX_STREAMS; idx++) {
        if (!s_streams[idx].active) continue;
        WaterStream *s = &s_streams[idx];
        float dissolve = (s->life < 0.4f) ? (1.0f - (s->life / 0.4f)) : 0.0f;
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolve, SHADER_UNIFORM_FLOAT);

        Vector3 rings[TUBE_SEGMENTS+1][RADIAL_SEGS];
        Vector3 normals[TUBE_SEGMENTS+1][RADIAL_SEGS];
        for (int i=0; i<=TUBE_SEGMENTS; i++) {
            float t = (float)i / TUBE_SEGMENTS;
            float currT = t * s->progress;
            Vector3 pos = GetBezierPoint(s->p0, s->p1, s->p2, s->p3, currT);
            Vector3 tangent = Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, currT));
            Vector3 upTemp = (fabsf(tangent.y) > 0.99f) ? (Vector3){1,0,0} : (Vector3){0,1,0};
            Vector3 right = Vector3Normalize(Vector3CrossProduct(upTemp, tangent));
            Vector3 up = Vector3CrossProduct(tangent, right);
            float wobble = 0.25f * sinf(t * 3.14159f * 4.f + time * 6.f);
            Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)), Vector3Scale(right, sinf(wobble)));
            Vector3 twistedRight = Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));
            float r = BASE_RADIUS * s->scale * (0.3f + 0.7f * sqrtf(fmaxf(0.f, sinf(t*3.14159f)))) * (0.15f + 0.85f * t);

            for (int j=0; j<RADIAL_SEGS; j++) {
                float phi = (float)j * (2.f*3.14159f)/RADIAL_SEGS;
                Vector3 n = Vector3Add(Vector3Scale(twistedRight, cosf(phi)), Vector3Scale(twistedUp, sinf(phi)));
                float wave = 1.0f + 0.12f*sinf(t*18.f+phi*3.f+time*10.f) + 0.08f*sinf(t*9.f-phi*5.f-time*6.f);
                normals[i][j] = n;
                rings[i][j] = Vector3Add(pos, Vector3Scale(n, r*wave));
            }
        }

        rlPushMatrix();
        rlBegin(RL_QUADS);
        for (int i=0; i<TUBE_SEGMENTS; i++) {
            float v1 = (float)i/TUBE_SEGMENTS*3.f, v2 = (float)(i+1)/TUBE_SEGMENTS*3.f;
            for (int j=0; j<RADIAL_SEGS; j++) {
                int next = (j+1)%RADIAL_SEGS;
                float u1 = (float)j/RADIAL_SEGS, u2 = (float)(j+1)/RADIAL_SEGS;
                rlNormal3f(normals[i][j].x, normals[i][j].y, normals[i][j].z); rlTexCoord2f(u1, v1); rlVertex3f(rings[i][j].x, rings[i][j].y, rings[i][j].z);
                rlNormal3f(normals[i][next].x, normals[i][next].y, normals[i][next].z); rlTexCoord2f(u2, v1); rlVertex3f(rings[i][next].x, rings[i][next].y, rings[i][next].z);
                rlNormal3f(normals[i+1][next].x, normals[i+1][next].y, normals[i+1][next].z); rlTexCoord2f(u2, v2); rlVertex3f(rings[i+1][next].x, rings[i+1][next].y, rings[i+1][next].z);
                rlNormal3f(normals[i+1][j].x, normals[i+1][j].y, normals[i+1][j].z); rlTexCoord2f(u1, v2); rlVertex3f(rings[i+1][j].x, rings[i+1][j].y, rings[i+1][j].z);
            }
        }
        rlEnd();

        // Caps
        float rTail = BASE_RADIUS * s->scale * 0.15f, rHead = BASE_RADIUS * s->scale * s->progress;
        Vector3 tTail = Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, 0.f));
        Vector3 tHead = Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, s->progress));
        Vector3 tailApex = Vector3Subtract(GetBezierPoint(s->p0,s->p1,s->p2,s->p3, 0.f), Vector3Scale(tTail, rTail*0.25f));
        Vector3 headApex = Vector3Add(GetBezierPoint(s->p0,s->p1,s->p2,s->p3, s->progress), Vector3Scale(tHead, rHead*0.25f));

        rlBegin(RL_TRIANGLES);
        for (int j=0; j<RADIAL_SEGS; j++) {
            int next = (j+1)%RADIAL_SEGS;
            rlNormal3f(-tTail.x, -tTail.y, -tTail.z); rlTexCoord2f((float)j/RADIAL_SEGS, -0.1f); rlVertex3f(tailApex.x, tailApex.y, tailApex.z);
            rlNormal3f(normals[0][next].x, normals[0][next].y, normals[0][next].z); rlTexCoord2f((float)next/RADIAL_SEGS, 0.f); rlVertex3f(rings[0][next].x, rings[0][next].y, rings[0][next].z);
            rlNormal3f(normals[0][j].x, normals[0][j].y, normals[0][j].z); rlTexCoord2f((float)j/RADIAL_SEGS, 0.f); rlVertex3f(rings[0][j].x, rings[0][j].y, rings[0][j].z);
        }
        for (int j=0; j<RADIAL_SEGS; j++) {
            int next = (j+1)%RADIAL_SEGS;
            rlNormal3f(tHead.x, tHead.y, tHead.z); rlTexCoord2f((float)j/RADIAL_SEGS, 3.1f); rlVertex3f(headApex.x, headApex.y, headApex.z);
            rlNormal3f(normals[TUBE_SEGMENTS][j].x, normals[TUBE_SEGMENTS][j].y, normals[TUBE_SEGMENTS][j].z); rlTexCoord2f((float)j/RADIAL_SEGS, 3.f); rlVertex3f(rings[TUBE_SEGMENTS][j].x, rings[TUBE_SEGMENTS][j].y, rings[TUBE_SEGMENTS][j].z);
            rlNormal3f(normals[TUBE_SEGMENTS][next].x, normals[TUBE_SEGMENTS][next].y, normals[TUBE_SEGMENTS][next].z); rlTexCoord2f((float)next/RADIAL_SEGS, 3.f); rlVertex3f(rings[TUBE_SEGMENTS][next].x, rings[TUBE_SEGMENTS][next].y, rings[TUBE_SEGMENTS][next].z);
        }
        rlEnd();
        rlPopMatrix();
    }
    EndShaderMode();
    rlEnableDepthMask();
}

void UnloadWaterStreamSkill(void) {}
bool IsWaterStreamSkillCoiling(void) { return false; }
int GetWaterStreamSkillProjectiles(SkillProjectile *out, int max) {
    int c=0;
    for(int i=0;i<MAX_STREAMS;i++){
        if(s_streams[i].active && c<max){ out[c].position=s_streams[i].headPos; out[c].radius=BASE_RADIUS*s_streams[i].scale; out[c].active=true; c++; }
    }
    return c;
}
void DeactivateWaterStreamProjectile(int idx) {
    if(idx>=0 && idx<MAX_STREAMS && s_streams[idx].active){ TriggerImpactVFX(s_streams[idx].headPos, s_streams[idx].scale); s_streams[idx].active=false; }
}
```

---

## 7. 3D RENDERING & SHADERS (CRITICAL)

* **Reset Vertex Colors:** ALWAYS call `rlColor4ub(255,255,255,255)` before `rlBegin()` for 3D meshes to prevent inheriting random color tints from prior UI/skills.
* **Custom Vertex Shader for 3D Lighting:** Raylib's default vertex shader (`NULL`) does NOT pass `fragNormal` or `fragPosition`. Using them for 3D lighting without a custom vertex shader will cause `NaN` errors, breaking the mesh into flat/black triangles! You MUST write and load a custom `.vs` (e.g. `ResourceManager_LoadShader("skill.vs", "skill.fs")`).
* **World UV Noise:** Don't use high multipliers on World UVs (`fragPosition.xz * 4.0`), it creates TV static. Use low multipliers (`0.05`) and stretch axes for organic fissures.
* **Preserve 3D Volume:** Don't let Emissive colors cover >60% of an object without shading (makes it look flat). Use `smoothstep` to restrict glow to 20-30%, and apply Wrap Lighting (`NdotL`) + Fresnel (`NdotV`) to the base rock/material to emphasize 3D curvature.

---

## 8. HIGH-LEVEL SKILL HELPER API (`#include "core/skill_helper.h"`)

A wrapper library to eliminate boilerplate and synchronize visual and physics events.

### 8.1 Structs & Enums Reference
```c
typedef enum {
    EFFECT_PRESET_FIRE_EXPLOSION, EFFECT_PRESET_ICE_SHATTER,
    EFFECT_PRESET_WATER_SPLASH, EFFECT_PRESET_LIGHTNING_IMPACT, EFFECT_PRESET_EARTH_CRACK
} EffectPresetType;

typedef enum { SHAPE_CIRCLE, SHAPE_BOX, SHAPE_CONE } ShapeType;
typedef struct {
    ShapeType shape; Vector3 center; float radius;
    float damagePerSecond; float tickInterval; float duration;
} DamageVolume;

typedef struct { float current; float duration; } SkillTimeline;

typedef enum { EMITTER_FIRE, EMITTER_SNOW, EMITTER_WATER_SPURT, EMITTER_SHOCKED_SPARKS } EmitterPreset;

typedef enum {
    MESH_PRESET_DISC, MESH_PRESET_RING, MESH_PRESET_CONE, MESH_PRESET_TORNADO,
    MESH_PRESET_CYLINDER, MESH_PRESET_SPHERE, MESH_PRESET_SHOCKWAVE
} MeshPresetType;

typedef enum { MATERIAL_FIRE, MATERIAL_ICE, MATERIAL_WATER, MATERIAL_PORTAL } MaterialPreset;
typedef struct { Shader shader; MaterialPreset preset; int uTimeLoc; int uDissolveLoc; } EffectMaterial;

typedef enum { DECAL_PRESET_BURN, DECAL_PRESET_CRACK, DECAL_PRESET_ICE, DECAL_PRESET_WATER } DecalPresetType;

typedef struct { float magnitude; float duration; float frequency; float falloff; } CameraImpulse;

typedef enum { FORCE_PRESET_FIRE_UPDRAFT, FORCE_PRESET_SNOW_BLIZZARD, FORCE_PRESET_WATER_VORTEX } ForceFieldPreset;

typedef struct {
    Vector3 target; float scale;
    bool hasExplosion; EffectPresetType explosionEffect;
    bool hasDecal; DecalPresetType decalType; float decalRadius; float decalDuration;
    bool hasDamageVolume; float damageRadius; float damageDps; float damageDuration;
} SkillBuildContext;
```

### 8.2 API Functions Prototypes
```c
// VFX impact: screen distort + shake + light + particle burst
void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale);

// AoE Damage-over-Time: auto-ticks and auto-cleans
int SpawnDamageVolume(DamageVolume config);

// Timeline: Timeline_Event return true exactly when crossing triggerTime
void Timeline_Start(SkillTimeline *t, float duration);
bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt);
bool Timeline_Finished(SkillTimeline *t);

// Emitters: continuous particle spawning
int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration);
void Emitter_Stop(int emitterId);

// Drawing: draws shape and automatically resets vertex color state
void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color);

// Material: loads and applies shaders, automatically handles global time uniforms
EffectMaterial Material_Load(MaterialPreset preset);
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
void Material_Begin(EffectMaterial mat);
void Material_End(void);

// Decals: ground scorches and caustics
void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration);

// Camera Shake: physical camera shake that attenuates with distance
void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse);

// Force Field: preset force fields
ForceField ForceField_CreatePreset(ForceFieldPreset preset);

// Skill Builder: build a standard skill in one go
void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale);
void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx);
void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration);
void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration);
void SkillBuilder_Build(SkillBuildContext *ctx);
```
