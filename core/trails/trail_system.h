#ifndef TRAIL_SYSTEM_H
#define TRAIL_SYSTEM_H

#include "core/color_gradient.h"
#include "core/force_field.h"
#include "core/ribbon_strip.h"
#include "core/sprite_anim.h"
#include "core/vfx_config.h"
#include "core/vfx_light.h"
#include "core/geometry/procedural_mesh_utils.h"
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

// ── GPU DEFORM (trail_deform.vs) — shape from the vertex shader ─────────────
// The ribbon strip itself stays a flat camera-facing band; trail_deform.vs
// displaces its vertices in the strip's own (side, stripNormal) frame. One
// uber shader, five uniform-selected modes, per-instance params — so a single
// program becomes a fire wisp, a smoking blade, a corkscrew filament or a
// lightning swirl by changing params, not code. mode == 0 leaves the classic
// CPU-cloth pipeline untouched (and the classic shaders selected).
typedef struct
{
    float mode;       // 0 = off, 1 = sin-multi, 2 = curl3, 3 = helix, 4 = noise
    float ampA[3];    // per-octave amplitude along the ribbon side (across width)
    float ampB[3];    // per-octave amplitude along the strip normal
    float freq[3];    // per-octave frequency (cycles along the path, seg 0..1)
    float speed[3];   // per-octave travel speed
    float phase;      // per-spawn random phase — desyncs simultaneous casts
    float envHead;    // wave amplitude -> 0 at the head over this seg fraction
    float envTail;    // wave amplitude -> 0 at the tail over this seg fraction
    float strength;   // global multiplier
    float curlScale;  // noise frequency for curl3 / noise modes
} TrailDeformConfig;

// ── PACKED 4-CHANNEL MATERIAL (trail_deform.fs) ─────────────────────────────
// One RGBA texture carries four roles — R = coarse wisp, G = fine wisp,
// B = dissolve noise, A = turbulence — so one bind and one asset replace the
// sheet + flow map + mask trio. Two samples of the same texture pan R+B and
// G+A at different speeds (u_panSpeed) for internal motion. The same final
// colour feeds BLEND_ALPHA body and BLEND_ADDITIVE emission passes.
//
// MODE 2 (sin-wave strand trail) reads the SAME texture with different channel
// meanings — R/G are filament patterns, B is flow distortion, A is dissolve —
// and builds the trail from three overlapping strand bundles, each riding its
// own wave field. Put the wave here, not in the vertex stage: UV-space waves
// cannot fold the geometry, and the arc-length anchor (waveFreq is cycles per
// METRE) keeps the crests standing on the laid path while only time moves
// them, which is what separates "flowing energy" from "a rigid swinging rope".
//
// The two modes' sheets are NOT interchangeable. Mode 1 needs an edge fade
// baked into every channel (it never reads texture alpha); mode 2 needs strand
// density and no baked band, because the strands ARE its silhouette.
typedef struct
{
    float mode;         // 0 = passthrough, 1 = packed wisp, 2 = sin-wave strands
    float wispMix;      // coarse(R) -> fine(G)
    float dissolve;     // B-channel dissolve threshold
    float dissolveSoft; // dissolve edge softness
    float turbStrength; // A-channel mix jitter
    float tilingX;      // texture tiles along the path
    float tilingY;      // texture tiles across the width
    float panCoarse;    // coarse-channel pan (UV units/sec)
    float panFine;      // fine-channel pan (UV units/sec)
    float edgeTear;     // fine-noise jitter of the dissolve threshold at the
                        // band edges (0 = off) — torn/ragged silhouette
    float tailFadeA;    // segment fade start (0 = head, 1 = tail)
    float tailFadeB;    // segment fade end — the tail dissolves to zero here;
                        // tailFadeA >= tailFadeB disables the fade

    // ── SIN-WAVE STRAND TRAIL (mode 2 only) ─────────────────────────────────
    // waveAmp + bundleWidth must stay under 1.0: both are in units of the
    // strip HALF-width, so a sum over 1 pushes a bundle past the quad edge
    // where the edge mask cuts it flat. Give the ribbon a radius ~3x the
    // thickness you want to see — the extra width is the room the waves swing
    // in, not visible mass.
    //
    // There is no band silhouette to configure: the trail is THREE strand
    // bundles sampled from the sheet's own filament density, crossing each
    // other. The sheet must therefore be strand-authored
    // (scripts/gen_energy_wisp_texture.py) — cloud noise can only modulate a
    // shape's brightness, never split it into hairs.
    float waveAmp;      // wave excursion across the strip (0..1 of half-width)
    float waveFreq;     // cycles per METRE of laid path (arc-anchored)
    float waveTravel;   // cycles/sec the crests travel toward the tail
    float waveSpread;   // detune between the three wave fields; 0 = they stack
                        // into one thick bundle, higher = a wider braid
    float bundleWidth;  // one strand bundle's half-width (0..1 of half-width)
    float edgeSoft;     // quad edge-mask softness — the hard clamp on the waves
    float hdrGain;      // output multiplier; > 1 is what bloom catches
    float strandGain;   // strand contrast exponent; < 1 fattens the hairs,
                        // > 1 thins them and opens the gaps between them
    float flowStrength; // B-channel UV distortion; ramps to the tail, so this
                        // is the knob that makes the TAIL fan out and fray
    float bundleWeight; // weight of the third (widest) bundle, 0..1
    // 0 = the sheet is a repeating filament MATERIAL, tiled by metres of path
    //     (tilingX = tiles per metre).
    // 1 = the sheet is one complete TRAIL SHAPE, head/tail taper painted in,
    //     stretched once over the whole trail. Tiling a shape sheet gives a
    //     rope of identical segments; stretching a material sheet smears it.
    //     The sheet's authoring decides this, not taste.
    float stretchUV;

    // ── HOW THE TAIL COMES APART ────────────────────────────────────────────
    // A tail whose every layer stops at the same segment reads as clipped
    // however soft the fade over it — softness blurs a cut, it does not remove
    // one. These three make it unravel instead.
    float tailStagger;  // spread between the three bundles' end points, in
                        // segment space. 0 = they all stop together.
    float tailDissolve; // extra dissolve threshold over the dying stretch, so
                        // the end breaks into holes rather than dimming evenly
    float tailNarrow;   // bundle half-width multiplier at the tip (1 = none).
                        // Below 1 the trail thins to threads before it goes.
    Color hotColor;     // HDR core colour (the hot centre of a strand)
    // Colour at the TAIL of the trail; the head uses the entity tint. The
    // along-trail ramp is what makes a trail read as cooling (or as smoke
    // thinning); without it the whole ribbon is one flat hue. Leave black to
    // disable — the head colour is then used end to end.
    Color tailColor;

    // ── RENDER SPLIT (both material modes) ──────────────────────────────────
    // How much this trail OCCLUDES in the BLEND_ALPHA body pass. Additive
    // energy alone cannot hold contrast over a bright destination — it can only
    // add — so a trail with bodyOpacity 0 is legible over the night arena and
    // washes out over a lit floor or a daylit sky. This is the knob for that,
    // and it is the only one: 0 = pure emissive (correct for sparks and
    // glints), ~0.5 = a coloured core inside a glow, ~0.9 = smoke, which must
    // occlude what is behind it.
    float bodyOpacity;

    // Renderer-level readability policy shared with particles, ribbons and
    // decals. Zero/NONE is identity for all existing trail materials.
    VFXContrastProfileId contrastProfile;

    // ── THE RECIPE (core/trails/trail_recipe.h) ─────────────────────────────
    // Non-NULL = this trail is described by a recipe, and the renderer reads
    // the warp, the layered sampling, the masks and the colour from there
    // instead of from the per-mode fields above. Points into the composer's own
    // pool slot, never at a caller's stack — the same lifetime contract as
    // `TrailConfig.layers`.
    //
    // The fields above it are the pre-recipe encoding, kept only for the trails
    // that have not moved (the volume tube and the smoke puff). A trail sets one
    // or the other, never both; when a recipe is present `mode` is ignored.
    const struct TrailRecipe *recipe;
} TrailMaterialConfig;

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
    TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE = 4,
    // Thin at the source, monotonically widening to FULL width at the tail —
    // a plume that rolls out and stays broad; the material tail ramp
    // (tailFadeA/B) evaporates the very tip instead of a hard band.
    TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN = 5,
    // The reference "Width over Trail" curve for an energy ribbon: a compact
    // head, the widest point just BEHIND it, then a long smooth taper to a
    // needle-fine tail. The opposite of SMOKE_WIDEN — use this one whenever
    // the trail should read as a blade/comet streak rather than a plume.
    TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE = 6
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
    /* SỐ LÁT DỌC của MESH, tách khỏi tubeMaxRings — 06/08/2026.
     *
     * `tubeMaxRings` trả lời "giữ bao nhiêu NODE LỊCH SỬ", tức ĐUÔI DÀI BAO
     * LÂU. `tubeGeomSegs` trả lời "dựng hình bằng bao nhiêu LÁT", tức ĐỘ MỊN
     * HÌNH HỌC. Trước đây một con số làm cả hai việc, nên muốn đuôi dài 1 s
     * là buộc phải có 60 lát — dù hình chỉ cần 24.
     *
     * TẠI SAO DÀY VÀNH LẠI LÀ VẤN ĐỀ, chứ không phải "mịn hơn thì tốt hơn":
     * biến dạng churn đo bằng MÉT và không biết gì về khoảng cách hai vành,
     * nên vành càng khít thì độ dốc bề mặt giữa hai vành liền kề càng đứng.
     * Đo trên smoke trail (48 lát / ~3.9 m = ringGap 8.1 cm, churn maxDev
     * 0.30-0.66 m): dốc 75-80° so với trục, và pháp tuyến dựng lại bằng sai
     * phân trung tâm ở đó gần như nằm ngang, đảo dấu giữa các vành liền kề —
     * thấy rõ thành sọc xen kẽ ở volume_debug=5. Cột khói cùng biên độ chỉ
     * dốc 50° vì ringGap của nó là 12.5 cm. Hai cái phanh biến dạng cũng đo
     * theo ringGap (PM_TUBE_MAX_OFFSET_RINGS), nên vành khít còn siết luôn
     * tầng NORMAL_OFFSET: đo được offsetClamped 20-65%.
     *
     * 0 = giữ nguyên hành vi cũ (dùng tubeMaxRings). Mọi caller có sẵn không
     * đổi một bit nào. */
    int tubeGeomSegs;
    const TrailSectionPoint *section;
    int sectionCount;
    bool tubeCaps;
    bool tubeSingleSided;
    /* Đổ bóng KHỐI cho ống: trail_volume.vs/.fs thay vì shader mặc định.
     *
     * Hình học chỉ dựng ra được một khối ĐẶC lồi lõm. Ba thừa số biến nó thành
     * khí — hai lớp sheet NHÂN nhau (chồng alpha-over chỉ làm đục thêm), mặt nạ
     * tắt dần hai đầu, và fresnel xoá vùng phẳng đang quay vào mắt. Xem
     * core/trails/shaders/trail_volume.fs. */
    bool tubeVolumeShading;
    /* ĐÓNG BĂNG BIẾN DẠNG — công cụ chẩn đoán, và nó có thật một lý do.
     *
     * Ống khối có ĐÚNG hai thứ chuyển động: lưới cuộn (vertex) và sheet trượt
     * (texture). Nhìn vào kết quả thì không tách được cái nào gây ra cái gì —
     * "trông như lốc xoáy" đúng với cả hai. Tệ hơn: hai đồng hồ đang DÍNH nhau,
     * vì noiseOffset của deform lấy từ uvScrollOffset, nên vặn scroll về 0 là
     * đứng cả hai và vẫn không phân biệt được.
     *
     * Cờ này đóng băng RIÊNG lưới: hình dạng giữ nguyên, thời gian của nó dừng,
     * sheet vẫn trượt. Bật lên mà vẫn thấy xoáy thì thủ phạm là texture; hết
     * xoáy thì là deform. Một lần chạy thay cho một chuỗi phỏng đoán. */
    bool tubeDeformFrozen;
    float tubeNoiseAmp;
    /* HÌNH DẠNG của ống — do CALLER quyết định, không phải DrawLayeredTube.
     *
     * NULL = giữ nguyên bản mặc định cũ (profile capsule "giọt nước" của water
     * stream: bóp cả hai đầu, phình giữa). Đúng cho tia nước và beam, SAI cho
     * bất cứ thứ gì cần trụ hay phễu — và trước 03/08/2026 không caller nào
     * đổi được, vì DrawLayeredTube gọi ProceduralMesh_DefaultTubeConfig() rồi
     * ghi đè cứng. Một cột khói dựng qua đó ra hình giọt nước xoáy lốc.
     *
     * Trỏ vào TubeMeshConfig của riêng mình là caller sở hữu toàn bộ: đường bao
     * bán kính (capsuleFloor/tailTaper/headGrowth), khung vận chuyển, và trường
     * biến dạng core/deform (noiseField). Con trỏ phải sống lâu hơn trail.
     *
     * Các trường THEO THỜI GIAN CHẠY vẫn do trail điền đè lên: noiseAmp từ
     * tubeNoiseAmp, noiseOffset từ uvScrollOffset. Đó là trạng thái, không phải
     * hình dạng. */
    const PMDropletConfig *dropletConfig;
    /* ...hoặc ỐNG. Ba hình là ba module độc lập (pm_tube/pm_droplet/pm_capsule),
     * nên caller chọn LOẠI MESH chứ không chọn tham số của một đường bao dùng
     * chung. Cả hai NULL = bộ máy cũ (pm_sweep_legacy.inl). */
    const PMTubeConfig *tubeShapeConfig;
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

    // GPU deform + packed material (trail_deform.vs/.fs). mode == 0 keeps the
    // classic CPU-cloth + sheet/flow/mask pipeline and shaders untouched.
    TrailDeformConfig deform;
    TrailMaterialConfig material;

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
    /* Bản LÀM MỊN theo thời gian của tổng chiều dài path thật — nguồn cho
     * PMTubeConfig.noiseSpanLenOverride (xem doc field ở
     * procedural_mesh_utils.h). Chỉ cập nhật khi tubeShapeConfig->
     * noiseWavelength > 0 (UpdateTrailSystem tự lọc). 0 = chưa có mẫu nào,
     * dùng thô. RIÊNG CỦA TỌA ĐỘ NHIỄU — ringGap/offsetLimit hình học vẫn
     * đọc chiều dài THẬT (không làm mịn), vì kẹp hình học cần chính xác
     * ngay-khung-này để tránh tự cắt, còn tọa độ nhiễu chỉ cần ổn định. */
    float tubeNoiseSpanLen;
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
    /* HÌNH DẠNG của ống — do CALLER quyết định, không phải DrawLayeredTube.
     *
     * NULL = giữ nguyên bản mặc định cũ (profile capsule "giọt nước" của water
     * stream: bóp cả hai đầu, phình giữa). Đúng cho tia nước và beam, SAI cho
     * bất cứ thứ gì cần trụ hay phễu — và trước 03/08/2026 không caller nào
     * đổi được, vì DrawLayeredTube gọi ProceduralMesh_DefaultTubeConfig() rồi
     * ghi đè cứng. Một cột khói dựng qua đó ra hình giọt nước xoáy lốc.
     *
     * Trỏ vào TubeMeshConfig của riêng mình là caller sở hữu toàn bộ: đường bao
     * bán kính (capsuleFloor/tailTaper/headGrowth), khung vận chuyển, và trường
     * biến dạng core/deform (noiseField). Con trỏ phải sống lâu hơn trail.
     *
     * Các trường THEO THỜI GIAN CHẠY vẫn do trail điền đè lên: noiseAmp từ
     * tubeNoiseAmp, noiseOffset từ uvScrollOffset. Đó là trạng thái, không phải
     * hình dạng. */
    const PMDropletConfig *dropletConfig;
    /* ...hoặc ỐNG. Ba hình là ba module độc lập (pm_tube/pm_droplet/pm_capsule),
     * nên caller chọn LOẠI MESH chứ không chọn tham số của một đường bao dùng
     * chung. Cả hai NULL = bộ máy cũ (pm_sweep_legacy.inl). */
    const PMTubeConfig *tubeShapeConfig;

    // Các tham số biến đổi Flowmap thời gian thực (Floats)
    float flowSpeed;
    float flowStrength;
    float flowTiling;
    float flowTimeAccumulator; // Bộ đếm thời gian riêng cho phase cuộn flowmap
    float dissolve;
    float maskTiling;

    // GPU deform + packed material (trail_deform.vs/.fs); mode 0 = classic.
    TrailDeformConfig deform;
    TrailMaterialConfig material;

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
    int tubeGeomSegs; /* 0 = dùng tubeMaxRings — xem doc ở TrailConfig */
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
    bool tubeVolumeShading;
    bool tubeDeformFrozen;
    bool useFlowMap; // Bật/Tắt tính năng Flowmap
} TrailEntity;

// ── Function Declarations ───────────────────────────────────────────────────

/* Shader khối, để test cô lập dựng được đúng đường ống mà cột khói đi qua.
 * id == 0 nghĩa là chưa nạp. */
Shader Trail_GetVolumeShader(void);

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
