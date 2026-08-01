#ifndef TRAIL_SYSTEM_H
#define TRAIL_SYSTEM_H

#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/ribbon_strip.h"
#include "core/sprite_anim.h"
#include "core/vfx_config.h"
#include "core/vfx_light.h"
#include "raylib.h"

#define MAX_TRAIL_PARTICLES 500
#define TRAIL_HISTORY_COUNT 60

#define TRAIL_PROJECTILE_TAPER_POWER 1.2f
#define TRAIL_PROJECTILE_OUTER_WIDTH_MUL 1.3f
#define TRAIL_PROJECTILE_INNER_WIDTH_MUL 0.65f
#define TRAIL_PROJECTILE_OUTER_ALPHA_MAX 80.0f
#define TRAIL_PROJECTILE_SPAWN_OFFSET_MUL 0.45f
#define TRAIL_PROJECTILE_WOBBLE_FREQ 8.0f
#define TRAIL_PROJECTILE_RETARGET_DIST_SQR 0.04f
#define TRAIL_PROJECTILE_HIT_DIST_SQR 0.09f
#define TRAIL_PROJECTILE_ACCEL_RATE 1.5f
#define TRAIL_PROJECTILE_MAX_SPEED 30.0f
#define TRAIL_PROJECTILE_CURVE_RANGE 2.5f
#define TRAIL_PROJECTILE_WOBBLE_AMPLITUDE 3.5f
#define TRAIL_PROJECTILE_STEER_LERP_RATE 3.2f
#define TRAIL_PROJECTILE_QUAD_LENGTH_MUL 1.1f
#define TRAIL_PROJECTILE_QUAD_THICK_MUL 2.0f

#define TRAIL_WISP_WAVE_FREQ_MIN 10
#define TRAIL_WISP_WAVE_FREQ_MAX 20
#define TRAIL_WISP_WAVE_AMP_MIN 5
#define TRAIL_WISP_WAVE_AMP_MAX 18
#define TRAIL_WISP_DRAG_RATE 0.8f
#define TRAIL_WISP_WOBBLE_FREQ 15.0f
#define TRAIL_WISP_WRIGGLE_FREQ 15.0f
#define TRAIL_WISP_WRIGGLE_AMPLITUDE 12.0f
#define TRAIL_WISP_HEAD_TAPER_EDGE 0.2f
#define TRAIL_WISP_TAIL_TAPER_EDGE 0.5f

#define TRAIL_PORTAL_SPIN_DEG_PER_SEC 140.0f
#define TRAIL_PORTAL_SPAWN_GROW_TIME 0.12f
#define TRAIL_PORTAL_QUAD_SIZE_MUL 2.6f

#define TRAIL_FOLLOWER_IDLE_FADE_TIME 0.15f
#define TRAIL_FOLLOWER_FADE_RATE_PER_SEC 40.0f
#define TRAIL_SAMPLE_STEPS_MAX 6
#define TRAIL_CLOTH_CONSTRAIN_ITERS 2
#define TRAIL_CLOTH_MIN_SPACING 0.60f

typedef enum
{
    TRAIL_TYPE_PROJECTILE = 0,
    TRAIL_TYPE_WISP = 1,
    TRAIL_TYPE_PORTAL = 2,
    TRAIL_TYPE_FOLLOWER = 3
} TrailType;

// ── Layered ribbons ─────────────────────────────────────────────────────────
#define TRAIL_MAX_LAYERS 4

typedef struct
{
    float widthMul;           // x the entity's thickness
    float alphaMul;           // x the node's alpha
    float whiten;             // 0 = the node's own colour, 1 = white
    float scrollMul;          // x uvScrollSpeed — parallax between layers
    float headAlphaPow;       // Alpha *= pow(segRatio, headAlphaPow)
    const Texture2D *texture; // Texture cho layer này (NULL = flat color/glow)

    // ── MAP FLOW (FLOWMAP) NÂNG CAO CHO LAYER ───────────────────────────────
    const Texture2D *flowMap; // Flowmap riêng cho layer (NULL = dùng chung hoặc tắt)
    float flowSpeed;          // Tốc độ cuộn/biến dạng dòng chảy cho layer này
    float flowStrength;       // Cường độ làm lệch UV (UV Distortion scale)
} TrailLayer;

// ── Trail Cross-section ─────────────────────────────────────────────────────
typedef enum
{
    TRAIL_SHAPE_RIBBON = 0, // Flat strip
    TRAIL_SHAPE_TUBE = 1    // Swept tube (Volume)
} TrailShape;

typedef struct
{
    float x, y;
} TrailSectionPoint;

#define TRAIL_TUBE_RADIAL_DEFAULT 8
#define TRAIL_TUBE_RADIAL_MAX 16
#define TRAIL_TUBE_RINGS_DEFAULT 24
#define TRAIL_TUBE_MIN_ALPHA 3

typedef enum
{
    TRAIL_WIDTH_ENVELOPE_UNIFORM = 0,
    TRAIL_WIDTH_ENVELOPE_TAPER_TAIL = 1,
    TRAIL_WIDTH_ENVELOPE_TAPER_BOTH = 2,
    TRAIL_WIDTH_ENVELOPE_PULSE = 3,
    // Small at the source, full in the body, then expands and dissolves.
    // Width and alpha share this envelope, so a smoke ribbon has no hard end.
    TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE = 4
} TrailWidthEnvelopeType;

typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);
typedef bool (*TrailCollisionCheckCallback)(int trailId, Vector3 currentPos);

typedef struct
{
    TrailType type;
    Vector3 pos;
    Vector3 vel;
    float len;
    float thick;
    float trailLength;
    float life;
    Vector3 target;
    float initialAngle;
    float wobblePhase;
    float scale;
    Texture2D tex;
    Color tint;
    Shader shader;
    TrailUpdateCallback onUpdate;
    TrailDeathCallback onDeath;
    int ownerTag;

    float wobbleAmplitudeOverride;
    float curveRangeOverride;
    const ForceField *forceField;
    const ColorGradient *gradient;
    const SpriteAnim *spriteAnim;
    VFXPriority priority;

    // Follower orbit
    float orbitRadius;
    float orbitSpeed;
    Vector3 orbitAxis;
    float orbitPhase;
    BlendMode blendMode;

    // Callbacks & Basic UV
    TrailCollisionCheckCallback collisionCheck;
    float uvTiling;
    float uvScrollSpeed;
    float minVertexDistance;
    TrailWidthEnvelopeType widthEnvelope;
    bool smoothSpline;
    bool disableInnerCore;
    bool useCustomBlendMode;

    const SkillCurve *widthCurve;
    const SkillCurve *alphaCurve;
    float distortionStrength;
    float distortionSpeed;

    // Render-only helix deformation. The head stays on its sampled path; the
    // radius grows behind it, so a filament can corkscrew without its emitter
    // orbiting away from the source.
    Vector3 helixAxis;
    float helixRadius;
    float helixTurns;
    float helixPhase;

    // Layer & Physics
    const TrailLayer *layers;
    int layerCount;
    float uvMetresPerTile;
    float nodeHomeSpring;
    float nodeHomeMaxDev;
    float nodeOrderFrac;
    float sampleHz;
    float teleportSpeed;

    // Tube & Geometry
    TrailShape shape;
    int tubeRadialSegs;
    int tubeMaxRings;
    const TrailSectionPoint *section;
    int sectionCount;
    bool tubeCaps;
    bool tubeSingleSided;
    float tubeNoiseAmp;
    float idleSpeed;

    RibbonMode ribbonMode;
    Vector3 fixedNormal;

    // ── MAP FLOW (FLOWMAP) GLOBAL CONFIG ────────────────────────────────────
    const Texture2D *flowMap; // Flowmap texture (RG channels contain flow vectors)
    float flowSpeed;          // Tốc độ chu kỳ dòng chảy
    float flowStrength;       // Độ biến dạng / dịch chuyển UV
    float flowTiling;         // UV Tiling riêng cho Flowmap
    bool useFlowMap;          // Bật chế độ Flowmap shader pipeline

    // Independent scalar mask (R channel): erodes alpha after the main sheet
    // and flow-map pass. Keep it separate from flow direction so smoke can
    // break into irregular puffs without making UV motion look turbulent.
    const Texture2D *noiseMask;
    float dissolve;
    float maskTiling;

    // Unified Config
    VFX_GeneralConfig general;
    VFX_GeometryConfig geometry;
    VFX_PhysicsConfig physics;
    VFX_AnimationConfig animation;
    VFX_RenderConfig render;
} TrailConfig;

static inline void TrailConfig_Unify(TrailConfig *cfg)
{
    // 1. Unify legacy to unified
    if (cfg->general.life == 0.0f && cfg->life != 0.0f)
    {
        cfg->general.life = cfg->life;
        cfg->general.priority = cfg->priority;
        cfg->general.tag = cfg->ownerTag;
    }
    if (cfg->geometry.scale == 0.0f && cfg->scale != 0.0f)
    {
        cfg->geometry.scale = cfg->scale;
        cfg->geometry.radius = 0.0f;
        cfg->geometry.width = cfg->thick;
        cfg->geometry.length = cfg->len;
    }
    if (cfg->physics.position.x == 0.0f && cfg->physics.position.y == 0.0f && cfg->physics.position.z == 0.0f)
    {
        cfg->physics.position = cfg->pos;
        cfg->physics.velocity = cfg->vel;
        cfg->physics.speed = 0.0f;
        cfg->physics.forceField = cfg->forceField;
    }
    if (cfg->animation.spriteAnim == NULL && cfg->spriteAnim != NULL)
    {
        cfg->animation.spriteAnim = cfg->spriteAnim;
        cfg->animation.radiusCurve = NULL;
        cfg->animation.speedCurve = NULL;
        cfg->animation.alphaCurve = cfg->alphaCurve;
        cfg->animation.emissiveCurve = NULL;
        cfg->animation.widthCurve = cfg->widthCurve;
    }
    if (cfg->render.gradient == NULL && cfg->gradient != NULL)
    {
        cfg->render.gradient = cfg->gradient;
        cfg->render.colorStart = cfg->tint;
        cfg->render.colorEnd = cfg->tint;
        cfg->render.tint = cfg->tint;
        cfg->render.shader = cfg->shader;
        cfg->render.distortionStrength = cfg->distortionStrength;
        cfg->render.distortionSpeed = cfg->distortionSpeed;
    }
    else if (cfg->render.tint.a == 0 && cfg->tint.a != 0)
    {
        cfg->render.tint = cfg->tint;
        cfg->render.colorStart = cfg->tint;
        cfg->render.colorEnd = cfg->tint;
        cfg->render.gradient = cfg->gradient;
        cfg->render.shader = cfg->shader;
        cfg->render.distortionStrength = cfg->distortionStrength;
        cfg->render.distortionSpeed = cfg->distortionSpeed;
    }

    // 2. Unify unified to legacy
    if (cfg->life == 0.0f && cfg->general.life != 0.0f)
    {
        cfg->life = cfg->general.life;
        cfg->priority = cfg->general.priority;
        cfg->ownerTag = cfg->general.tag;
    }
    if (cfg->scale == 0.0f && cfg->geometry.scale != 0.0f)
    {
        cfg->scale = cfg->geometry.scale;
        cfg->thick = cfg->geometry.width;
        cfg->len = cfg->geometry.length;
    }
    if (cfg->forceField == NULL && cfg->physics.forceField != NULL)
    {
        cfg->forceField = cfg->physics.forceField;
    }
    if (cfg->pos.x == 0.0f && cfg->pos.y == 0.0f && cfg->pos.z == 0.0f)
    {
        cfg->pos = cfg->physics.position;
        cfg->vel = cfg->physics.velocity;
    }
    if (cfg->spriteAnim == NULL && cfg->animation.spriteAnim != NULL)
    {
        cfg->spriteAnim = cfg->animation.spriteAnim;
    }
    if (cfg->widthCurve == NULL && cfg->animation.widthCurve != NULL)
    {
        cfg->widthCurve = cfg->animation.widthCurve;
    }
    if (cfg->alphaCurve == NULL && cfg->animation.alphaCurve != NULL)
    {
        cfg->alphaCurve = cfg->animation.alphaCurve;
    }
    if (cfg->gradient == NULL && cfg->render.gradient != NULL)
    {
        cfg->gradient = cfg->render.gradient;
    }
    if (cfg->distortionStrength == 0.0f && cfg->render.distortionStrength != 0.0f)
    {
        cfg->distortionStrength = cfg->render.distortionStrength;
        cfg->distortionSpeed = cfg->render.distortionSpeed;
    }
    if (cfg->tint.a == 0 && cfg->render.tint.a != 0)
    {
        cfg->tint = cfg->render.tint;
        cfg->shader = cfg->render.shader;
    }
}

// ── OPTIMIZED STRUCT PADDING (Đã sắp xếp theo kích thước dữ liệu giảm dần) ────
typedef struct
{
    // 1. Con trỏ (Pointers) - 8 bytes mỗi biến
    TrailUpdateCallback onUpdate;
    TrailDeathCallback onDeath;
    const ForceField *forceField;
    const ColorGradient *gradient;
    const SpriteAnim *spriteAnim;
    const SkillCurve *widthCurve;
    const SkillCurve *alphaCurve;
    const Matrix *attachedTransform;
    TrailCollisionCheckCallback collisionCheck;
    const TrailLayer *layers;
    const TrailSectionPoint *section;

    // 2. Mảng và Struct lớn (Vectors, Raylib Textures/Shaders)
    Vector3 history[TRAIL_HISTORY_COUNT];
    Vector3 nodeVelocity[TRAIL_HISTORY_COUNT];
    Vector3 nodeHome[TRAIL_HISTORY_COUNT];
    float nodeRest[TRAIL_HISTORY_COUNT];
    float nodeUV[TRAIL_HISTORY_COUNT];

    Vector3 position;
    Vector3 velocity;
    Vector3 target;
    Vector3 driftVelocity;
    Vector3 axisOrigin;
    Vector3 axisDir;
    Vector3 attachLocalOffset;
    Vector3 fixedNormal;
    Vector3 orbitAxis;
    Vector3 prevAttachPos;
    Vector3 lateralOffset;
    Vector3 helixAxis;

    Texture2D sprite;  // 20 bytes
    Texture2D flowMap; // 20 bytes (Thêm Flowmap Texture vào Entity)
    Texture2D noiseMask;
    Shader shader;     // 12/16 bytes
    Color tint;        // 4 bytes

    // 3. Các biến thực (Floats) - 4 bytes
    float length;
    float thickness;
    float trailLength;
    float lifetime;
    float maxLifetime;
    float angle;
    float wobblePhase;
    float scale;
    float wobbleAmplitudeOverride;
    float curveRangeOverride;
    float timeSinceLastFollowerUpdate;
    float fadeAccumulator;
    float nodeRestLen;
    float orbitRadius;
    float orbitSpeed;
    float orbitPhase;
    float uvTiling;
    float uvScrollSpeed;
    float uvScrollOffset;
    float minVertexDistance;
    float distortionStrength;
    float distortionSpeed;
    float helixRadius;
    float helixTurns;
    float helixPhase;
    float uvMetresPerTile;
    float laidDist;
    float nodeHomeSpring;
    float nodeHomeMaxDev;
    float nodeOrderFrac;
    float sampleHz;
    float sampleAcc;
    float teleportSpeed;
    float idleSpeed;
    float tubeNoiseAmp;

    // Các tham số biến đổi Flowmap thời gian thực (Floats)
    float flowSpeed;
    float flowStrength;
    float flowTiling;
    float flowTimeAccumulator; // Bộ đếm thời gian riêng cho phase cuộn flowmap
    float dissolve;
    float maskTiling;

    // 4. Số nguyên và Enum (Int/Enum) - 4 bytes
    TrailType type;
    VFXPriority priority;
    int historyCount;
    int historyHead;
    int ownerTag;
    int nextFree;
    BlendMode blendMode;
    TrailWidthEnvelopeType widthEnvelope;
    RibbonMode ribbonMode;
    int layerCount;
    TrailShape shape;
    int tubeRadialSegs;
    int tubeMaxRings;
    int sectionCount;

    // 5. Kiểu Boolean - 1 byte
    bool active;
    bool smoothSpline;
    bool disableInnerCore;
    bool useCustomBlendMode;
    bool frozen;
    bool hasPrevAttach;
    bool tubeCaps;
    bool tubeSingleSided;
    bool useFlowMap; // Bật/Tắt tính năng Flowmap
} TrailEntity;

// ── Function Declarations ───────────────────────────────────────────────────

void TrailSystem_SetGlobalTexture(Texture2D tex);
void InitTrailSystem(Shader defaultShader);
int SpawnTrailEntity(TrailConfig config);
TrailEntity *GetTrail(int id);
void KillTrail(int id);
void UpdateTrailSystem(float dt);
void DrawTrailEntities(Camera3D camera);
void DrawTrailEntitiesBody(Camera3D camera);
void DrawTrailEntitiesEmission(Camera3D camera);
void UnloadTrailSystem(void);
int GetActiveTrailCount(void);
void TrailSystem_GetStats(int *active, int *max);

void UpdateFollowerPosition(int id, Vector3 newTipPos);
void Trail_AttachToTransform(int id, const Matrix *targetTransform, Vector3 localOffset);
void Trail_SetFollowerOrbit(int id, float radius, float speed, Vector3 axis, float phase);
void SetFollowerAxis(int id, Vector3 axisOrigin, Vector3 axisDir);
void Trail_SetLateralOffset(int id, Vector3 worldOffset);
void Trail_SetFrozen(int id, bool frozen);
// Replaces a follower's history with a fixed world-space segment and freezes it.
// UV/flow clocks still advance in UpdateTrailSystem, making this useful for
// isolating texture motion from emitter movement.
void Trail_SetStaticPath(int id, Vector3 tail, Vector3 head, int nodeCount);

// API mở rộng hỗ trợ cập nhật FlowMap động thời gian thực
void Trail_SetFlowMap(int id, Texture2D flowMap, float speed, float strength, float tiling);

#endif // TRAIL_SYSTEM_H
