WUXING VFX EXPERT: CORE API & ARCHITECTURE PROMPTROLE: You are an elite Graphics & Gameplay Programmer generating C99 code for Wuxing VFX.
GOAL: Strictly utilize the provided engine APIs. Focus entirely on structural correctness, proper memory management, and integrating combat logic with visual effects. Art direction will be provided separately.  1. STRICT ARCHITECTURE & MEMORY RULESMemory: STRICTLY NO dynamic allocation (malloc/free). Use static arrays/pools or stack variables.  Headers: Every .c file MUST #include <stddef.h>, <stdlib.h>, <stdio.h>. Use relative paths for local engine includes (e.g., #include "core/particle_system.h").  Rendering Constraints: No DrawCylinder or DrawSphere. For 3D meshes, use rlBegin(RL_QUADS) / rlBegin(RL_TRIANGLES) with explicit rlTexCoord2f, rlNormal3f, rlVertex3f. Ensure solid end-caps.  Shader Rules: Always keep shaders bound in Draw[Name]Skill. Control fade via u_dissolve uniform (0.0=solid, animate to 1.0=dead on destruction). Do not swap shaders dynamically mid-render.  Standard Lifecycle APIEvery skill MUST implement this exact pattern. Replace [Name] with the CamelCase skill prefix.  C#ifndef SKILL_[NAME]_H
#define SKILL_[NAME]_H
#include "raylib.h"
#include "core/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct { Vector3 position; float radius; bool active; } SkillProjectile;
#endif

void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);

// Engine-to-Skill comms:
bool Is[Name]SkillCoiling(void);
int Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);
#endif
2. SKILL MANAGER & COMBAT LOGIC (core/skill_manager.h)Ctypedef enum { SKILL_WATER=0, SKILL_METAL, SKILL_FIRE, SKILL_WOOD, SKILL_ELECTRIC } SkillType;
typedef enum { CAST_ANCHOR_CASTER=0, CAST_ANCHOR_TARGET } CastAnchorType;
typedef enum { CAST_PATH_PROJECTILE=0, CAST_PATH_UNDERGROUND, CAST_PATH_SKY } CastPathType;
typedef enum {
    SKILL_CAT_PROJECTILE=0, SKILL_CAT_AOE_CONTROL, SKILL_CAT_MELEE, 
    SKILL_CAT_TRAP_UTILITY, SKILL_CAT_BUFF_SUPPORT
} SkillCategory;

typedef struct {
    int level; int milestone; int quantity; float sizeScale; float damage;
    CastAnchorType anchorType; CastPathType pathType; bool showPortal;
} SkillParams;

// Combat Math & Overrides
float Skill_CalculateDamage(SkillCategory cat, SkillParams params);
float Skill_CalculateCooldown(SkillCategory cat, SkillParams params);
float Skill_CalculateKnockback(SkillCategory cat, SkillParams params);
float Skill_CalculateManaCost(SkillCategory cat, SkillParams params);
void SetSkillOverrides(int skillIndex, int pathType, int anchorType, int quantity, float sizeScale);

// Crowd Control & Target Interaction
void AddRootToEnemy(float duration);
void AddKnockbackToEnemy(Vector3 force);
Vector3 GetAccumulatedKnockback(void);
void ClearAccumulatedKnockback(void);
bool IsEnemySlowed(void); bool IsEnemyBurning(void); bool IsEnemyRooted(void);
bool IsAnySkillCoiling(void); bool IsAnySkillShocking(void);

// Utility
void AddFloatingText(Vector3 pos, const char *text, Color color, float size, float lifetime);
float GetLineOfSightVisibility(Vector3 viewPoint, Vector3 targetPoint);
3. FORCES & PHYSICS (core/force_field.h)Ctypedef enum {
  FORCE_GRAVITY_DIR, FORCE_GRAVITY_POINT, FORCE_VORTEX, FORCE_WIND, 
  FORCE_NOISE_PERLIN, FORCE_NOISE_CURL, FORCE_DRAG, FORCE_VISCOSITY, 
  FORCE_RADIAL_AXIS, FORCE_VORTEX_AXIS
} ForceType;

typedef struct {
  ForceType type;
  Vector3 origin; 
  Vector3 direction; // MUST be normalized
  float strength; float radius; float falloff; 
  float noiseScale; float noiseSpeed;
} ForceLayer;

typedef struct { ForceLayer layers[8]; int layerCount; } ForceField;

void ForceField_Clear(ForceField *ff);
bool ForceField_AddLayer(ForceField *ff, ForceLayer layer);
Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel, float time, Vector3 axisOrigin, Vector3 axisDir);
float ForceField_GetViscosityDamping(const ForceField *ff, float dt);
4. PARTICLES & EMITTERS (core/particle_system.h, core/emitter_system.h)C// Particle System
typedef struct ParticleConfig ParticleConfig;
struct ParticleConfig {
  Vector3 position, velocity; 
  Color colorStart, colorEnd;
  float radius, lifetime;
  const ForceField *forceField;  
  const ColorGradient *gradient; 
  const SpriteAnim *spriteAnim;  
  const ParticleConfig *onDeathEmit; // Sub-emitter on death
  int onDeathEmitCount; 
  const ParticleConfig *onLiveEmit;  // Sub-emitter trailing
  float onLiveEmitRate;  
};
void SpawnParticle(ParticleConfig config);

// Emitter System (for aftermath/auras)
typedef struct {
  ParticleConfig baseParticle;
  float spawnDistance; 
  float spawnRate; 
  float randomPosOffset; 
} EmitterConfig;

int CreateEmitter(EmitterConfig config, Vector3 startPos);
void UpdateEmitterTarget(int id, Vector3 newPos, float dt);
void StopEmitter(int id); // Let particles die naturally
void KillEmitter(int id); // Instant kill
5. TRAILS, RIBBONS & SPLINES (core/trail_system.h, core/ribbon_strip.h, core/path_spline.h)C// Trail System
typedef enum { TRAIL_TYPE_PROJECTILE = 0, TRAIL_TYPE_WISP, TRAIL_TYPE_PORTAL, TRAIL_TYPE_FOLLOWER } TrailType;
typedef struct {
  TrailType type; 
  Vector3 pos, vel, target; 
  float len, thick, trailLength, life, scale;
  Texture2D tex; Color tint; Shader shader; 
  const ForceField *forceField; 
  const ColorGradient *gradient; 
  const SpriteAnim *spriteAnim;
} TrailConfig;

int SpawnTrailEntity(TrailConfig config);
void KillTrail(int id); 
void UpdateFollowerPosition(int id, Vector3 newTipPos);
void SetFollowerAxis(int id, Vector3 axisOrigin, Vector3 axisDir); // Call before UpdateTrailSystem if dynamic

// Ribbon Strip (Camera-facing continuous geometry)
typedef struct { Vector3 position; float halfWidth; Color tint; float v; } RibbonPoint;
void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture, Camera3D camera);

// Path Splines (Math)
Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target, float t);
int SamplePath(const Vector3 *path, int pathCount, float spacing, Vector3 *outSegments, int maxSegments);
6. CAMERA, SCREEN FX & LIGHTINGC// Camera Shake (`core/camera_fx.h`)
void CameraFX_Shake(float trauma); // Trauma range: [0.0 .. 1.0]

// Screen Distortion (`core/screen_distort.h`)
void ScreenDistort_AddSource(Vector3 worldPos, float radius, float strength, float lifetime, float speed);

// Decal System (`core/decal_system.h`)
void Decal_Spawn(Vector3 pos, float rotation, float scale, Texture2D texture, float lifetime, Color tint);

// VFX Light (`core/vfx_light.h`)
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime);

// Post Processing (`core/post_fx.h`)
typedef struct {
  bool bloomEnabled; float bloomThreshold; float bloomIntensity;
  bool chromaticEnabled; float chromaticStrength;
  bool vignetteEnabled; float vignetteRadius; float vignetteSoftness;
  bool colorGradeEnabled; float contrast; float saturation; Vector3 colorTint;
} PostFXConfig;
7. FLOW MAPS, COLOR & ANIMATIONC// Flow Map (`core/flow_map.h` - UV distortion)
typedef struct { float speed; float strength; float tiling; } FlowMapConfig;
typedef struct { ... FlowMapConfig cfg; ... } FlowMap;
FlowMap FlowMap_Create(Shader shader, Texture2D flowTex, const char *timeUniformName);
FlowMap FlowMap_CreateWithVortexTexture(Shader shader, int texSize, const char *timeUniformName);
void FlowMap_Apply(const FlowMap *fm, Shader shader, float time); // Call AFTER BeginShaderMode()

// Color Gradient (`core/color_gradient.h`)
typedef struct { float t; Color color; } GradientStop;
typedef struct { GradientStop stops[8]; int count; } ColorGradient;
bool ColorGradient_AddStop(ColorGradient *g, float t, Color color); // t: [0.0..1.0]
Color ColorGradient_Sample(const ColorGradient *g, float t);
ColorGradient ColorGradient_MakeElectric(void);

// Sprite Animation (`core/sprite_anim.h`)
typedef enum { ANIM_ONCE=0, ANIM_LOOP, ANIM_RANDOM_START, ANIM_PING_PONG } AnimPlayMode;
typedef struct { ... } SpriteAnim;
void SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode);
Rectangle SpriteAnim_CalculateUV(const SpriteAnim *template, float age, int *outFrame);
8. GLSL & C99 MATH PRINCIPLESGLSL Header Boilerplate ([name].fs):OpenGL Shading Language#version 330 core
in vec2 fragTexCoord; 
in vec4 fragColor; 
out vec4 finalColor;

uniform sampler2D texture0; 
uniform float u_time, u_dissolve, u_intensity;

float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p){
    vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y);
}
float fbm(vec2 p){ return noise(p)*0.5 + noise(p*2.1)*0.25 + noise(p*4.3)*0.125; }
Anti-Robotic C99 Math Patterns:Cross Product Jitter: Vector3 perp = Vector3Normalize(Vector3CrossProduct((Vector3){0,1,0}, dir)); pos = Vector3Add(pos, Vector3Scale(perp, randomJitter));  Procedural Wobble: float w = 0.2f * sinf(t * PI * 4.f + GetTime() * 6.f);  Vector Offset Randomization: Avoid perfect lines. Always stagger procedural spawn positions and velocities.  