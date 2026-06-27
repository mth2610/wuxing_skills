# WUXING SKILLS DEVELOPMENT: AI PROMPT CONTEXT
> Upload this system prompt to Claude/GPT along with your skill request. 

## 1. PROJECT SPECIFICATION
* **Tech:** C99, Raylib 5.5, OpenGL 3.3, 3D Isometric Night-time Arena.
* **Memory constraint:** **Strictly NO dynamic allocation (`malloc`, `free`)**. Use static pools/arrays, stack variables.
* **Standard Headers:** Every `.c` file **MUST** explicitly `#include <stddef.h>`, `#include <stdlib.h>`, and `#include <stdio.h>` to avoid compilation errors regarding `NULL` and `snprintf`. Do not assume they are implicitly included.
* **Includes:** Use relative paths from root: `#include "core/particle_system.h"`, etc.

## 2. DIRECTORY STRUCTURE
Simply copy your skill files into a subfolder under `skills/`:
```
skills/[element]/[skill_name]/
    ├── [skill_name].h  # Lifecycle prototypes (Matches standard convention)
    ├── [skill_name].c  # Physics & VFX (Matches standard convention)
    ├── [shader].fs     # Custom shader (optional, auto-copied)
    └── [texture].png   # Sprite texture (optional, auto-copied)
```
*(Everything compiles, registers, and populates the Debug UI dynamically! No manual setup required!)*

## 3. STANDARD LIFECYCLE API (`[skill_name].h`)
Any new skill MUST follow this naming convention for automatic detection & registration:
```c
#ifndef SKILL_[NAME]_H
#define SKILL_[NAME]_H
#include "raylib.h"
#include "core/skill_manager.h" // For SkillParams and SkillCategory structs

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Lifecycle functions must be named exactly like this (replace [Name] with your Skill Prefix):
void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);

// For Engine-to-Skill communication (decoupled from direct enemy entity references):
bool Is[Name]SkillCoiling(void); // Returns true if skill is currently pinning/rooting the enemy
int Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);
#endif
```

## 4. CORE VFX APIS REFERENCE
### Particle System (`#include "core/particle_system.h"`)
`void SpawnParticle(ParticleConfig config);`
```c
struct ParticleConfig {
    Vector3 position, velocity;
    Color colorStart, colorEnd;
    float radius, lifetime;
    const ForceField *forceField;   // NULL if unused
    const ColorGradient *gradient;  // NULL if unused
    const SpriteAnim *spriteAnim;   // NULL if unused
    const ParticleConfig *onDeathEmit; // [Sub-Emitter] (NULL for child emitter)
    int onDeathEmitCount;
    const ParticleConfig *onLiveEmit;  // [Trail-Emitter] (NULL for child emitter)
    float onLiveEmitRate;
};
```
* **Color Gradient (`#include "core/color_gradient.h"`)**:
  `bool ColorGradient_AddStop(ColorGradient *g, float t, Color color);`
  `Color ColorGradient_Sample(const ColorGradient *g, float t);`

* **Emitter System (`#include "core/emitter_system.h"`)**: 
  `int CreateEmitter(EmitterConfig cfg, Vector3 start);` `void UpdateEmitterTarget(int id, Vector3 pos, float dt);` `void StopEmitter(int id);` `void KillEmitter(int id);`

### Trail/Ribbon System (`#include "core/trail_system.h"`)
`void SpawnTrailEntity(TrailConfig config);`
* *TrailConfig fields:* `TrailType type (TRAIL_TYPE_PROJECTILE/TRAIL_TYPE_RIBBON); Vector3 pos, vel; float len, thick, trailLength, life; const ColorGradient *gradient; const SpriteAnim *spriteAnim; Texture2D tex;`

### Ground Marks (`#include "core/decal_system.h"`)
`void Decal_Spawn(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);` (Auto-offset to prevent Z-fighting).

### Post-Processing & Screen Effects
* **Distortion (`#include "core/screen_distort.h"`)**: `void ScreenDistort_AddSource(Vector3 pos, float rad, float str, float life, float speed);`
* **VFX Light (`#include "core/vfx_light.h"`)**: `void VFXLight_Spawn(Vector3 pos, Color col, float rad, float life);`
* **Camera Shake (`#include "core/camera_fx.h"`)**: `void CameraFX_Shake(float trauma);`


---

## 5. TEMPLATE A: BALL PROJECTILE & EXPLOSION (`earth_rock.c`)
```c
#include "skills/earth/earth_rock/earth_rock.h"
#include "core/particle_system.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/camera_fx.h"
#include "raymath.h"

static Texture2D crackTex;
static ColorGradient earthGrad;

#define MAX_ROCKS 8
typedef struct { Vector3 pos, vel; float scale, life; bool active; } Rock;
static Rock rocks[MAX_ROCKS];

void InitEarthRockSkill(int w, int h) {
    (void)w; (void)h;
    crackTex = LoadTexture("assets/textures/crack.png");
    ColorGradient_AddStop(&earthGrad, 0.0f, (Color){139, 69, 19, 255});
    ColorGradient_AddStop(&earthGrad, 1.0f, (Color){50, 50, 50, 0});
    for(int i=0; i<MAX_ROCKS; i++) rocks[i].active = false;
}
void CastEarthRockSkill(Vector3 start, Vector3 target, SkillParams p) {
    for(int i=0; i<MAX_ROCKS; i++) {
        if(!rocks[i].active) {
            rocks[i] = (Rock){ {target.x, start.y+150.f, target.z}, {0,-200.f,0}, 10.f*p.sizeScale, 2.f, true };
            break;
        }
    }
}
void UpdateEarthRockSkill(float dt, Vector3 enemyPos, float enemyRad) {
    for(int i=0; i<MAX_ROCKS; i++) {
        if(!rocks[i].active) continue;
        Rock *r = &rocks[i];
        r->pos = Vector3Add(r->pos, Vector3Scale(r->vel, dt));
        r->life -= dt;
        
        // Spawn tail particle
        SpawnParticle((ParticleConfig){ r->pos, {0, 5.f, 0}, BROWN, BLACK, 5.f, 0.5f, NULL, &earthGrad, NULL, NULL, 0, NULL, 0.f });

        if(r->pos.y <= 0.0f) {
            r->active = false;
            CameraFX_Shake(0.5f);
            ScreenDistort_AddSource(r->pos, 120.f, 0.8f, 0.8f, 200.f);
            Decal_Spawn(r->pos, (float)GetRandomValue(0,360), r->scale*2.f, crackTex, 4.f, WHITE);
            for(int p=0; p<15; p++) {
                SpawnParticle((ParticleConfig){ r->pos, {(float)GetRandomValue(-100,100), (float)GetRandomValue(50,150), (float)GetRandomValue(-100,100)}, BROWN, BLACK, 6.f, 1.f, NULL, &earthGrad, NULL, NULL, 0, NULL, 0.f });
            }
        }
        if(r->life <= 0.f) r->active = false;
    }
}
void DrawEarthRockSkill(void) {}
void UnloadEarthRockSkill(void) { UnloadTexture(crackTex); }
```

---

## 6. TEMPLATE B: DYNAMIC 3D MESH GENERATION (`element_stream.c`)
Draws a dynamic twisting Bezier-curve ribbon tube with local Frenet frame extrusion and time-based vertex waves.
```c
#include "skills/water/water_stream/element_stream.h"
#include "rlgl.h"
#include "raymath.h"

#define MAX_STREAMS 4
#define TUBE_SEGS 20
#define RADIAL_SEGS 12
typedef struct { Vector3 p0, p1, p2, p3; float progress, scale, life; bool active; } Stream;
static Stream streams[MAX_STREAMS];
static Shader streamShader;
static int timeLoc;

void InitElementStreamSkill(int w, int h) {
    (void)w; (void)h;
    streamShader = LoadShader(0, "skills/water/water_stream/stream.fs");
    timeLoc = GetShaderLocation(streamShader, "u_time");
    for(int i=0; i<MAX_STREAMS; i++) streams[i].active = false;
}
void CastElementStreamSkill(Vector3 start, Vector3 target, SkillParams p) {
    for(int i=0; i<MAX_STREAMS; i++) {
        if(!streams[i].active) {
            streams[i] = (Stream){ start, Vector3Add(start, (Vector3){0,20,0}), Vector3Subtract(target, (Vector3){0,20,0}), target, 0.f, p.sizeScale, 1.5f, true };
            break;
        }
    }
}
void UpdateElementStreamSkill(float dt, Vector3 enemyPos, float enemyRad) {
    for(int i=0; i<MAX_STREAMS; i++) {
        if(!streams[i].active) continue;
        streams[i].life -= dt;
        streams[i].progress = Clamp(streams[i].progress + dt*2.f, 0.f, 1.f);
        if(streams[i].life <= 0.f) streams[i].active = false;
    }
}
void DrawElementStreamSkill(void) {
    float time = (float)GetTime();
    BeginShaderMode(streamShader);
    SetShaderValue(streamShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    for(int idx=0; idx<MAX_STREAMS; idx++) {
        if(!streams[idx].active) continue;
        Stream *s = &streams[idx];
        Vector3 rings[TUBE_SEGS + 1][RADIAL_SEGS];
        Vector3 normals[TUBE_SEGS + 1][RADIAL_SEGS];

        // Frenet frame math & vertex mesh generation
        for(int i=0; i<=TUBE_SEGS; i++) {
            float t = (float)i/TUBE_SEGS;
            float currT = t * s->progress;
            
            // Bezier Point & Tangent
            float u = 1.f - currT;
            Vector3 pos = Vector3Add(Vector3Scale(s->p0, u*u*u), Vector3Add(Vector3Scale(s->p1, 3.f*u*u*currT), Vector3Add(Vector3Scale(s->p2, 3.f*u*currT*currT), Vector3Scale(s->p3, currT*currT*currT))));
            Vector3 tan = Vector3Normalize(Vector3Add(Vector3Scale(Vector3Subtract(s->p1, s->p0), 3.f*u*u), Vector3Add(Vector3Scale(Vector3Subtract(s->p2, s->p1), 6.f*u*currT), Vector3Scale(Vector3Subtract(s->p3, s->p2), 3.f*currT*currT))));
            
            // Local Coordinate Frame
            Vector3 upTemp = (fabsf(tan.y) > 0.99f) ? (Vector3){1,0,0} : (Vector3){0,1,0};
            Vector3 rVec = Vector3Normalize(Vector3CrossProduct(upTemp, tan));
            Vector3 uVec = Vector3CrossProduct(tan, rVec);
            
            // Twisting Wobble & Taper
            float wobble = 0.2f * sinf(t * PI * 4.f + time * 6.f);
            Vector3 twistedUp = Vector3Add(Vector3Scale(uVec, cosf(wobble)), Vector3Scale(rVec, sinf(wobble)));
            Vector3 twistedRight = Vector3Normalize(Vector3CrossProduct(twistedUp, tan));
            float radius = 8.f * s->scale * sinf(t * PI);

            for(int j=0; j<RADIAL_SEGS; j++) {
                float phi = j * (2.f*PI)/RADIAL_SEGS;
                Vector3 n = Vector3Add(Vector3Scale(twistedRight, cosf(phi)), Vector3Scale(twistedUp, sinf(phi)));
                float wave = 1.f + 0.1f * sinf(t * 12.f + phi * 2.f - time * 8.f);
                normals[i][j] = n;
                rings[i][j] = Vector3Add(pos, Vector3Scale(n, radius * wave));
            }
        }

        // Draw quads using rlgl
        rlPushMatrix();
        rlBegin(RL_QUADS);
        for(int i=0; i<TUBE_SEGS; i++) {
            float v1 = (float)i/TUBE_SEGS * 3.f, v2 = (float)(i+1)/TUBE_SEGS * 3.f;
            for(int j=0; j<RADIAL_SEGS; j++) {
                int next = (j+1)%RADIAL_SEGS;
                float u1 = (float)j/RADIAL_SEGS, u2 = (float)(j+1)/RADIAL_SEGS;
                
                rlNormal3f(normals[i][j].x, normals[i][j].y, normals[i][j].z); rlTexCoord2f(u1, v1); rlVertex3f(rings[i][j].x, rings[i][j].y, rings[i][j].z);
                rlNormal3f(normals[i][next].x, normals[i][next].y, normals[i][next].z); rlTexCoord2f(u2, v1); rlVertex3f(rings[i][next].x, rings[i][next].y, rings[i][next].z);
                rlNormal3f(normals[i+1][next].x, normals[i+1][next].y, normals[i+1][next].z); rlTexCoord2f(u2, v2); rlVertex3f(rings[i+1][next].x, rings[i+1][next].y, rings[i+1][next].z);
                rlNormal3f(normals[i+1][j].x, normals[i+1][j].y, normals[i+1][j].z); rlTexCoord2f(u1, v2); rlVertex3f(rings[i+1][j].x, rings[i+1][j].y, rings[i+1][j].z);
            }
        }
        rlEnd();
        rlPopMatrix();
    }
    EndShaderMode();
}
void UnloadElementStreamSkill(void) { UnloadShader(streamShader); }
```

---

## 7. CRITICAL GRAPHICS & PERFORMANCE RULES
* **STRICTLY PROHIBITED: High-level Raylib primitives (such as `DrawCylinder()`, `DrawCone()`, `DrawCube()`, `DrawSphere()`, or their `Wires` counterparts)** for rendering core skill visuals or projectiles.
  - *Rationale:* Primitives lack proper UV texture mapping and vertex normal control required for custom shaders, and drawing `Wires` generates generic grid-like overlapping "cages" (debug line tunnels) which ruin the night-time aesthetic.
  - *Requirement:* You must procedurally generate your vertex meshes using `rlgl` low-level drawing calls (`rlBegin(RL_TRIANGLES)` or `rlBegin(RL_QUADS)`). Each vertex must specify its texture coordinate (`rlTexCoord2f`), surface normal (`rlNormal3f`), and position (`rlVertex3f`).

## 8. AESTHETICS & ORGANIC POLISH (ANTI-ROBOTIC DESIGN)
To keep the wuxing world immersive and natural, you must apply the following anti-robotic visual rules:
1. **No Perfect Lines (Perpendicular Jitter):**
   - When spawning sequential elements (like earth spikes, lightning strikes, or trail nodes), DO NOT align them in a mathematically straight line.
   - Add a small perpendicular displacement: `jitter = Perpendicular(dir) * GetRandomValue(-maxJitter, maxJitter)`.
2. **Randomization of Angle, Rotation, and Scale:**
   - **Rotation:** Always apply random rotation to each spawned entity (e.g. rotate ground spikes randomly around the Y-axis: `rotation = GetRandomValue(0, 360)`).
   - **Scale:** Randomly vary the scale of each individual instance (e.g. `scale = sizeScale * GetRandomValue(85, 115) / 100.0f`).
   - **Angle:** Add minor pitch/roll tilts to vertical elements so they emerge pointing in slightly organic angles.
3. **Organic Meshes via Perturbation:**
   - When drawing procedurally (using `rlgl`), add noise or trigonometric perturbation to the radius (e.g. `radius * (1.0f + 0.15f * sinf(height * frequency + angle * 3.0f))`) to create rough, jagged, rocky, or flowing organic shapes rather than perfect cylinders/hexagons.
4. **Continuous Shader Application (No Visual Popping):**
   - Bind the skill shader (`BeginShaderMode`) during **all active phases** (e.g. rising, holding, flying, and dissolving), not just during the final phase.
   - Set the uniform `u_dissolve = 0.0f` when the skill is fully intact. This ensures consistent lighting, outlines, and color blending, preventing a jarring graphical pop when the dissolve phase starts.

## 9. ADVANCED PROCEDURAL 3D MESH TECHNIQUES (THE TUBE SKILL METHODOLOGY)
To create premium, flowing 3D organic skills (such as curving water streams, energy tubes, or twisting branches), use the mathematical framework of the **Tube Skill**:

### 1. Bezier Curve Path & Tangents
Generate a smooth curving path from `p0` (start) to `p3` (target) using a cubic Bezier curve. To move the mesh, scale the evaluation parameter `currentT = t * progress` (where `t` is segment percentage `0..1`, and `progress` is cast progress `0..1`).
```c
Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(p0, u * u * u),
        Vector3Add(
            Vector3Scale(p1, 3.0f * u * u * t),
            Vector3Add(Vector3Scale(p2, 3.0f * u * t * t), Vector3Scale(p3, t * t * t))
        )
    );
}
// Tangent (first derivative) is required to orient the rings forward:
Vector3 GetBezierDerivative(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u),
        Vector3Add(
            Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t),
            Vector3Scale(Vector3Subtract(p3, p2), 3.0f * t * t)
        )
    );
}
```

### 2. Local Frenet / Parallel Transport Frame
To extrude circles along the curve, construct a local orthogonal coordinate frame (`Right`, `Up`, `Tangent`) at each point:
```c
Vector3 tangent = Vector3Normalize(GetBezierDerivative(p0, p1, p2, p3, currentT));
Vector3 up = (fabsf(tangent.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
Vector3 right = Vector3Normalize(Vector3CrossProduct(up, tangent));
up = Vector3CrossProduct(tangent, right);
```

### 3. Twisting Wobble (Trigonometric Rotation)
To simulate swirling liquid or twisting vines, rotate the local coordinate frame along the length `t` and time:
```c
float wobble = wobbleAmplitude * sinf(t * PI * wobbleFrequency + time * wobbleSpeed);
Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)), Vector3Scale(right, sinf(wobble)));
Vector3 twistedRight = Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));
```

### 4. Tapering & Capsule Shaping (Ends control)
Avoid blocky cut-off ends by tapering the radius towards the tail (`t = 0`) and the head (`t = 1`):
```c
float baseCapsule = 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI))); // Tapers both ends smoothly
float tailTaper = 0.15f + 0.85f * t; // Specifically narrows the tail to 15%
float finalRadius = baseRadius * baseCapsule * tailTaper;
```

### 5. Circumferential Wave Deformation
Add multi-frequency ripples around the circumference to prevent rigid, robotic cylinders:
```c
float deform1 = sinf(t * 18.0f + radialAngle * 3.0f + time * 10.0f);
float deform2 = sinf(t * 9.0f - radialAngle * 5.0f - time * 6.0f);
float deformFactor = 1.0f + deform1 * 0.12f + deform2 * 0.08f;
float perturbedRadius = finalRadius * deformFactor;
```

### 6. Solid End-Capping (No Hollow Shells)
Always cap the start and end rings of the tube with a vertex fan using `RL_TRIANGLES`. A hollow cylinder looks cheap when viewed from high angles:
- **Tail Cap:** Fan vertices from a shifted center apex point `tailCenter - tailTangent * radius * 0.25f` to the vertices of the `t = 0` ring.
- **Head Cap:** Fan vertices from `headCenter + headTangent * radius * 0.25f` to the vertices of the `t = 1` ring.
- **UV Coordinates:** Set V coordinate of the apex slightly negative (e.g. `-0.1f`) so the flow map wraps around the closed tips seamlessly.



