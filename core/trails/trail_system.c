#include "trail_system.h"
#include "core/time_fx.h"   // TimeFX_Elapsed — pinned clock; GetTime() is wall clock
#include "core/force_field.h"
#include "core/composition/visual_composer.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/vfx_render.h"
#include "core/tuning.h"
#include "core/uv/surface_flow.h"
// core/deform/mesh_deform.h already arrives transitively via
// core/geometry/procedural_mesh_utils.h (included above) — MeshDeformField/
// MeshDeform_Apply/MeshDeformLocs are all in scope without a fresh include.
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

static void KillTrailInternal(int id);
static TrailEntity trailPool[MAX_TRAIL_PARTICLES];
static int freeListHead = 0;
static int activeCount = 0;
static int s_activeIds[MAX_TRAIL_PARTICLES];
static int s_slotListIndex[MAX_TRAIL_PARTICLES];
// Loc index cache cho Flowmap Shader Uniforms

typedef struct
{
    Vector3 right;
    Vector3 up;
} TrailCameraBasis;

static Texture2D s_tubeFlatTex = {0};
static Image s_tubeNoiseImg = {0};
static Texture2D s_globalTrailTex = {0};
void TrailSystem_SetGlobalTexture(Texture2D tex) { s_globalTrailTex = tex; }
/* One trail_glow.fs source, one program per blend law a trail can be drawn
 * under — body (BLEND_ALPHA) and emission (BLEND_ADDITIVE). Those two are the
 * only outcomes: a premultiplied appearance is mapped onto alpha body plus
 * additive emission below, not passed through. Indexed by
 * TrailUsesAdditiveBlend(). */
enum { TRAIL_SHADER_BODY = 0, TRAIL_SHADER_EMISSION = 1 };
static Shader s_defaultShader[2];
static Shader s_bodyShader;
static bool s_bodyShaderTried = false;
static int s_bodyContrastParamsLoc = -1;
/* Uber deform shader (trail_deform.vs/.fs): five uniform-selected vertex
 * deform modes + the packed 4-channel wisp material. Trails route here if
 * they use EITHER half: deform.mode 0 + material.mode 1 is a FLAT ribbon
 * with the wisp material (the "deform off" energy trail — the material's
 * dissolve/tear/tail fade still run, only the vertex waves are off). */
static Shader s_deformShader;
static bool s_deformShaderTried = false;
static Shader s_volumeShader;
static bool s_volumeShaderTried = false;
/* THE VOLUME TUBE'S TWO-SHEET PAN, GENERALISED 05/08/2026 — was a bespoke
 * u_volPan uniform + hand-rolled shader math, now core/uv's SurfaceFlow
 * (core/uv/surface_flow.h), the same "mesh + UVDeformField + SurfaceFlow =
 * effect" module core/composition/common/vc_shield_shell.inl already uses
 * for its own flow — see there for the reference pattern this copies
 * (SurfaceFlow_CacheLocations once, SurfaceFlow_Clear/AddLayer to build,
 * SurfaceFlow_Apply per draw). Built ONCE (the two layers are constants,
 * not per-frame data) rather than per group like the old pan[4] literal was
 * — nothing here varies frame to frame, so there is nothing to rebuild. */
static SurfaceFlow s_volFlow;
static SurfaceFlowLocs s_volFlowLocs;
/* Debug view for the volume tube — see trail_volume.fs. Live in tuning.cfg so a
 * term can be inspected without a rebuild. */
static float s_volDebug = 0.0f;
static float s_volDensity = 1.75f;   /* u_volMask.w — xem chỗ đăng ký tunable */
static float s_volDepthMode = 0.0f;  /* u_volMask.x — hệ số số hạng THÂN */
static float s_volRim = 1.0f;        /* u_volRim    — hệ số số hạng RÌA */
static float s_volDepthPow = 2.0f;   /* u_volMask.y — số mũ chung của cả hai */
static float s_volCull = 1.0f;       /* u_volCull   — 0 = vẽ cả hai mặt */
static float s_volNormalSrc = 0.0f;  /* u_volNormalSrc — 1 = dFdx thay attribute */
static float s_volErode = 0.0f;      /* u_volErode    — noise erosion biên, 0 = tắt */
static float s_volErodeBand = 0.2f;  /* u_volErodeBand — bề rộng vùng tưa theo b/R */
/* Set only while DrawTrailEntitiesLayer owns the draw; layered helpers use it
 * to keep alpha body to the one textured material layer. */
static int s_drawLayerFilter = -1;

static bool TrailUsesDeformShader(const TrailEntity *t)
{
    return (t->deform.mode > 0.0f || t->material.mode > 0.0f) && s_deformShader.id != 0;
}

#define TRAIL_SHADER_CACHE_SIZE 16
static unsigned int shaderCacheIds[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheTimeLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheCoreStrLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheFlowTimeLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheFlowSpeedLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheFlowStrengthLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheFlowTilingLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheFlowTexLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheDissolveLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheMaskTilingLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheMaskTexLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheCount = 0;

// Deform/material uniform locations, one struct per cached shader. The
// per-name arrays above cover the legacy uniforms; these 19 ride a single
// cache entry because they are always set together (ApplyDeformUniforms).
typedef struct
{
    int deformMode, waveAmpA, waveAmpB, waveFreq, waveSpeed;
    int wavePhase, waveEnv, waveStrength, curlScale, stripNormal;
    int matMode, wispMix, dissolve, dissolveSoft;
    int tiling, panSpeed, tailFadeA, tailFadeB;
    int bandShape, pathArc, colHot, strandFlow, coreShape;
    int renderPass, bodyOpacity, contrastParams, colTail, tailShape;
    // The mode-2 sine warp's coordinate half, generalised 05/08/2026 onto
    // core/uv's UVDeformField (u_sinWave is GONE — see the "SIN-WAVE STRAND
    // TRAIL" push site for the reproduction). uv_field.glsl is shape-neutral
    // uniform naming (u_uvField/u_uvMeta), so this cache is exactly
    // UVDeform_CacheLocations(shader), same as SurfaceFlowLocs elsewhere.
    UVDeformLocs uvWarp;
    // Mode 1's (sin-multi) vertex wave — core/deform's FIRST GLSL mirror,
    // 05/08/2026 (core/deform/shaders/mesh_deform.glsl). u_meshDeform/
    // u_meshDeformMeta, cached exactly like uvWarp above.
    MeshDeformLocs meshWarp;
} DeformLocs;

static DeformLocs shaderCacheDeformLocs[TRAIL_SHADER_CACHE_SIZE];
static DeformLocs s_fallbackDeformLocs;

static void CacheShaderLocs(Shader shader);   // defined below; called first by GetCachedDeformLocs

static void FillDeformLocs(Shader shader, DeformLocs *l)
{
    l->deformMode    = GetShaderLocation(shader, "u_deformMode");
    l->waveAmpA      = GetShaderLocation(shader, "u_waveAmpA");
    l->waveAmpB      = GetShaderLocation(shader, "u_waveAmpB");
    l->waveFreq      = GetShaderLocation(shader, "u_waveFreq");
    l->waveSpeed     = GetShaderLocation(shader, "u_waveSpeed");
    l->wavePhase     = GetShaderLocation(shader, "u_wavePhase");
    l->waveEnv       = GetShaderLocation(shader, "u_waveEnv");
    l->waveStrength  = GetShaderLocation(shader, "u_waveStrength");
    l->curlScale     = GetShaderLocation(shader, "u_curlScale");
    l->stripNormal   = GetShaderLocation(shader, "u_stripNormal");
    l->matMode       = GetShaderLocation(shader, "u_matMode");
    l->wispMix       = GetShaderLocation(shader, "u_wispMix");
    l->dissolve      = GetShaderLocation(shader, "u_dissolve");
    l->dissolveSoft  = GetShaderLocation(shader, "u_dissolveSoft");
    l->tiling        = GetShaderLocation(shader, "u_tiling");
    l->panSpeed      = GetShaderLocation(shader, "u_panSpeed");
    l->tailFadeA     = GetShaderLocation(shader, "u_tailFadeA");
    l->tailFadeB     = GetShaderLocation(shader, "u_tailFadeB");
    l->uvWarp        = UVDeform_CacheLocations(shader);
    l->meshWarp      = MeshDeform_CacheLocations(shader);
    l->bandShape     = GetShaderLocation(shader, "u_bandShape");
    l->coreShape     = GetShaderLocation(shader, "u_coreShape");
    l->pathArc       = GetShaderLocation(shader, "u_pathArc");
    l->colHot        = GetShaderLocation(shader, "u_colHot");
    l->strandFlow    = GetShaderLocation(shader, "u_strandFlow");
    l->renderPass    = GetShaderLocation(shader, "u_renderPass");
    l->bodyOpacity   = GetShaderLocation(shader, "u_bodyOpacity");
    l->contrastParams = GetShaderLocation(shader, "u_contrastParams");
    l->colTail       = GetShaderLocation(shader, "u_colTail");
    l->tailShape     = GetShaderLocation(shader, "u_tailShape");
}

static const DeformLocs *GetCachedDeformLocs(Shader shader)
{
    CacheShaderLocs(shader);
    for (int i = 0; i < shaderCacheCount; i++)
        if (shaderCacheIds[i] == shader.id)
            return &shaderCacheDeformLocs[i];
    // Cache miss (over budget): compute into the fallback slot — still once
    // per shader, never per instance.
    FillDeformLocs(shader, &s_fallbackDeformLocs);
    return &s_fallbackDeformLocs;
}

static void CacheShaderLocs(Shader shader)
{
    if (shaderCacheCount >= TRAIL_SHADER_CACHE_SIZE)
        return;
    for (int i = 0; i < shaderCacheCount; i++)
        if (shaderCacheIds[i] == shader.id)
            return;
    shaderCacheIds[shaderCacheCount] = shader.id;
    shaderCacheTimeLocs[shaderCacheCount] = GetShaderLocation(shader, "u_time");
    shaderCacheCoreStrLocs[shaderCacheCount] = GetShaderLocation(shader, "u_coreStrength");
    shaderCacheFlowTimeLocs[shaderCacheCount] = GetShaderLocation(shader, "u_flowTime");
    shaderCacheFlowSpeedLocs[shaderCacheCount] = GetShaderLocation(shader, "uSpeed");
    shaderCacheFlowStrengthLocs[shaderCacheCount] = GetShaderLocation(shader, "uStrength");
    shaderCacheFlowTilingLocs[shaderCacheCount] = GetShaderLocation(shader, "uTiling");
    shaderCacheFlowTexLocs[shaderCacheCount] = GetShaderLocation(shader, "flowTex");
    shaderCacheDissolveLocs[shaderCacheCount] = GetShaderLocation(shader, "uDissolve");
    shaderCacheMaskTilingLocs[shaderCacheCount] = GetShaderLocation(shader, "uMaskTiling");
    shaderCacheMaskTexLocs[shaderCacheCount] = GetShaderLocation(shader, "maskTex");
    FillDeformLocs(shader, &shaderCacheDeformLocs[shaderCacheCount]);
    shaderCacheCount++;
}

static int GetCachedShaderLoc(Shader shader, const int *locs, const char *name)
{
    CacheShaderLocs(shader);
    for (int i = 0; i < shaderCacheCount; i++)
        if (shaderCacheIds[i] == shader.id)
            return locs[i];
    // The small cache is deliberately bounded; preserve correctness if a scene
    // uses more than its shader budget.
    return GetShaderLocation(shader, name);
}

static int GetCachedTimeLoc(Shader shader)
{
    return GetCachedShaderLoc(shader, shaderCacheTimeLocs, "u_time");
}

static int GetCachedCoreStrLoc(Shader shader)
{
    return GetCachedShaderLoc(shader, shaderCacheCoreStrLocs, "u_coreStrength");
}

static int GetCachedFlowTimeLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheFlowTimeLocs, "u_flowTime");
}

static int GetCachedFlowSpeedLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheFlowSpeedLocs, "uSpeed");
}

static int GetCachedFlowStrengthLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheFlowStrengthLocs, "uStrength");
}

static int GetCachedFlowTilingLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheFlowTilingLocs, "uTiling");
}

static int GetCachedFlowTexLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheFlowTexLocs, "flowTex");
}

static int GetCachedDissolveLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheDissolveLocs, "uDissolve");
}

static int GetCachedMaskTilingLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheMaskTilingLocs, "uMaskTiling");
}

static int GetCachedMaskTexLoc(Shader sh)
{
    return GetCachedShaderLoc(sh, shaderCacheMaskTexLocs, "maskTex");
}

static RibbonPoint scratchOuter[TRAIL_HISTORY_COUNT];
static RibbonPoint scratchInner[TRAIL_HISTORY_COUNT];
static RibbonPoint scratchLayer[TRAIL_HISTORY_COUNT];
static Vector3 scratchNodePrevPos[TRAIL_HISTORY_COUNT];
static float scratchTaper[TRAIL_HISTORY_COUNT];
static float scratchSegRatio[TRAIL_HISTORY_COUNT];

#define WISP_CONSTRAINT_ITERS 2

static bool TrailUsesAdditiveBlend(const TrailEntity *t)
{
    BlendMode blend = t->useCustomBlendMode ? t->blendMode
                                             : ((t->blendMode > 0) ? t->blendMode : BLEND_ADDITIVE);
    return blend != BLEND_ALPHA;
}

/* A trail carrying its own shader keeps it — that caller owns the pairing. */
static inline Shader ResolveShader(const TrailEntity *t)
{
    if (t->shader.id != 0) return t->shader;
    return s_defaultShader[TrailUsesAdditiveBlend(t) ? TRAIL_SHADER_EMISSION
                                                     : TRAIL_SHADER_BODY];
}

static void EnsureTrailBodyShader(void)
{
    if (s_bodyShaderTried) return;
    s_bodyShaderTried = true;
    s_bodyShader = ResourceManager_LoadShader(NULL, "core/trails/shaders/trail_body.fs");
    if (s_bodyShader.id != 0)
        s_bodyContrastParamsLoc = GetShaderLocation(s_bodyShader,
                                                     "u_contrastParams");
}

static void EnsureTrailDeformShader(void)
{
    if (s_deformShaderTried) return;
    s_deformShaderTried = true;
    s_deformShader = ResourceManager_LoadShader("core/trails/shaders/trail_deform.vs",
                                                "core/trails/shaders/trail_deform.fs");
    if (s_deformShader.id == 0)
    {
        TraceLog(LOG_WARNING, "TRAIL: trail_deform.vs/.fs failed to load — deform modes fall back to flat ribbons");
        return;
    }
    /* DIAGNOSTIC, 05/08/2026 — mode 2's w0/w1/w2 came off trail_deform.fs's
     * own uniforms onto u_uvField/u_uvMeta (uv_field.glsl) this session, and
     * strand trail came out wrong (a smooth wide rainbow smear, no braided
     * bundles) on the very next real build. "The shader loaded" and "its
     * uniforms resolved" are different facts — see trail_volume.fs's own
     * precedent for this exact print. If fieldLoc/metaLoc come back -1, every
     * SetShaderValueV in UVDeform_Apply is silently skipped and the shader
     * reads whatever u_uvField already held (compiled-in zero, most likely —
     * kind=0 IS UV_DEFORM_SINE, amplitude=0 collapses all three bundles onto
     * the SAME centre, losing the crossing/braiding this mode exists for). */
    const DeformLocs *diagLocs = GetCachedDeformLocs(s_deformShader);
    TraceLog(LOG_INFO,
             "TRAIL: trail_deform loaded — shader id %u, u_uvField loc %d, "
             "u_uvMeta loc %d%s",
             (unsigned)s_deformShader.id, diagLocs->uvWarp.fieldLoc,
             diagLocs->uvWarp.metaLoc,
             (diagLocs->uvWarp.fieldLoc < 0)
                 ? "  <-- MISSING: mode 2's wave layers never reach the GPU, "
                   "every bundle collapses onto the same centre"
                 : "");
}

static void EnsureTrailVolumeShader(void)
{
    if (s_volumeShaderTried) return;
    s_volumeShaderTried = true;
    s_volumeShader = ResourceManager_LoadShader("core/trails/shaders/trail_volume.vs",
                                                "core/trails/shaders/trail_volume.fs");
    if (s_volumeShader.id == 0)
    {
        TraceLog(LOG_WARNING,
                 "TRAIL: trail_volume.vs/.fs failed to load — volume tubes fall "
                 "back to the default shader, i.e. an opaque lumpy solid with no "
                 "fade and no edge falloff. That looks like polished stone, not "
                 "gas, and nothing else will say why.");
        return;
    }
    /* UNCONDITIONAL, and it prints the UNIFORM LOCATIONS, not just the id.
     *
     * A shader that loads and a shader whose uniforms resolve are different
     * facts. If u_volMask comes back -1 the pushes below are skipped in silence
     * and every constant in it reads as ZERO — which makes the edge rolloff
     * `smoothstep(0, 0.001, d)`, i.e. a hard edge, and no amount of retuning
     * that constant changes anything on screen. That is indistinguishable from
     * "the value is wrong" unless the location is printed. */
    Tuning_RegisterFloat("volume_debug", &s_volDebug, 0.0f);
    /* u_volMask.w — ĐỘ ĐẬM CHUNG của mọi volume trail (cột khói, smoke trail,
     * volume trail...). Thành tunable 06/08/2026, cùng lúc với bản sửa dấu
     * của số hạng độ dày quang học trong trail_volume.fs: bản sửa đó nâng độ
     * đục trung bình dọc thân lên 8.02 lần (đo ở
     * core/tests/volume_optical_depth_test.c), nên 1.75 chỉ đúng cho
     * `vol_depth_mode = 0`. Hai knob ĐI CẶP:
     *     mode 0 (mặc định, dạng viền cũ)      -> density ~1.75
     *     mode 1 (độ dày quang học đúng vật lý) -> density ~0.60
     *
     * LÀ TUNABLE chứ không phải hằng số mới, vì con số đúng là chuyện THẨM
     * MỸ và nó dùng chung cho nhiều hiệu ứng: quét nó trong tuning.cfg lúc
     * game đang chạy rẻ hơn nhiều so với đoán rồi build lại (core/CLAUDE.md
     * §5). */
    Tuning_RegisterFloat("vol_density", &s_volDensity, 1.75f);
    /* u_volMask.x — DẠNG của số hạng độ dày. Mặc định 0 = giữ NGUYÊN hình
     * cột khói đã có từ trước, vì đổi nó sang dạng đúng vật lý (mode 1) làm
     * cột đọc ra "không tự nhiên như trước" dù smoke trail thì cần đúng dạng
     * đó. Một bản sửa đúng về vật lý vẫn là hồi quy nếu nó lấy mất cái nhìn
     * người dùng đã ưng — nên nó là công tắc, không phải bản thay thế. */
    Tuning_RegisterFloat("vol_depth_mode", &s_volDepthMode, 0.0f);
    /* Hai knob còn lại của cùng công thức. Mặc định (mode 0 / rim 1 / pow 2)
     * cho lại ĐÚNG TỪNG BIT công thức trước 06/08/2026, nên không caller nào
     * đổi hình cho tới khi có người cố ý vặn. */
    Tuning_RegisterFloat("vol_rim", &s_volRim, 1.0f);
    Tuning_RegisterFloat("vol_depth_pow", &s_volDepthPow, 2.0f);
    /* Xem doc dài ở u_volCull trong trail_volume.fs. Mặc định 1 = hành vi cũ;
     * chỉ có nghĩa khi đi cùng vol_depth_mode, số đo ở silhouette_test.c. */
    Tuning_RegisterFloat("vol_cull", &s_volCull, 1.0f);
    /* Xem doc dai o u_volNormalSrc trong trail_volume.fs. Mac dinh 0 = duong
     * cu, nen bat cong tac la mot lan luu file chu khong phai mot lan build. */
    Tuning_RegisterFloat("vol_normal_src", &s_volNormalSrc, 0.0f);
    /* Noise erosion biên (kỹ thuật 2) — xem doc dài ở u_volErode trong
     * trail_volume.fs. Mặc định 0 = biên mềm như cũ; bật lên cho biên tưa. */
    Tuning_RegisterFloat("vol_erode", &s_volErode, 0.0f);
    Tuning_RegisterFloat("vol_erode_band", &s_volErodeBand, 0.2f);
    /* `vol_view_src` is GONE (06/08/2026): the view vector is settled at
     * normalize(-fragPosition) — see the viewPos comment further down and
     * trail_volume.vs's header. A switch over a settled question only rots. */
    s_volFlowLocs = SurfaceFlow_CacheLocations(s_volumeShader);

    /* Reproduces the OLD u_volPan/u_volMask.x constants exactly —
     * pan[4] = {-0.085, 0, 0.043, 0}, mask[4].x = 1.63 — see
     * trail_volume.fs's own comment on the SurfaceFlow that replaced it.
     *
     * SurfaceFlow_LayerUV's tile-mode formula is `mat*tiling - pan`
     * (SUBTRACT), where the old code SAMPLED at `fragTexCoord + pan1/2`
     * (ADD). Reproducing the same net scroll direction under a repeat-
     * wrapped sampler (mod 1, so `x - fract(k*t) == x + fract(-k*t)` on the
     * screen) means negating the old pan CONSTANT, not the formula:
     *   sheet 1 (layer 0): old `pan1.y = fract(-0.085*t)`, sampled ADDED ->
     *     net v - 0.085t. New: tiling (1,1) [no scale, matches old
     *     fragTexCoord unscaled], pan.y = +0.085 -> v*1 - fract(0.085t) ==
     *     v - 0.085t (mod 1). Around (.x) pan/tiling stay at their
     *     identity values (0 pan, 1 tiling) on BOTH layers — panning u
     *     rotates the sheet about the tube's axis, and with any along-pan
     *     the sum is a screw thread; smoke rises, it does not spin.
     *   sheet 2 (layer 1): old `pan2.y = fract(0.043*t)`, sampled ADDED
     *     onto `v*1.63` -> net v*1.63 + 0.043t. New: tiling (1,1.63)
     *     [reproduces mask[4].x], pan.y = -0.043 -> v*1.63 -
     *     fract(-0.043t) == v*1.63 + 0.043t (mod 1). MUL blend against
     *     layer 0 reproduces the old `s1.a * s2.a`.
     * env = NONE on both (UVDeform_Envelope(NONE,...) == 1.0, i.e. no
     * gating) — the old code never gated pattern by anything either. */
    SurfaceFlow_Clear(&s_volFlow);
    SurfaceFlow_AddLayer(&s_volFlow, (SurfaceFlowLayer){
        .tiling = {1.0f, 1.0f}, .pan = {0.0f, 0.085f},
        .blend = SURFACE_FLOW_MUL, .env = UV_ENV_NONE});
    SurfaceFlow_AddLayer(&s_volFlow, (SurfaceFlowLayer){
        .tiling = {1.0f, 1.63f}, .pan = {0.0f, -0.043f},
        .blend = SURFACE_FLOW_MUL, .env = UV_ENV_NONE});
    s_volFlow.stretchUV = false; // TILE mode — layer 1 needs its 1.63x scale,
                                 // which STRETCH mode ignores entirely
    s_volFlow.envAxis = 1;      // inert (env = NONE on both layers), set for
                                 // sanity: 1 = along the body, not around it

    TraceLog(LOG_INFO,
             "TRAIL: trail_volume loaded — shader id %u, u_flowLayer loc %d, "
             "u_volMask loc %d%s",
             (unsigned)s_volumeShader.id,
             s_volFlowLocs.layerLoc,
             GetShaderLocation(s_volumeShader, "u_volMask"),
             (GetShaderLocation(s_volumeShader, "u_volMask") < 0)
                 ? "  <-- MISSING: every mask constant reads as 0, edge goes hard"
                 : "");
}

Shader Trail_GetVolumeShader(void)
{
    EnsureTrailVolumeShader();
    return s_volumeShader;
}

static bool TrailUsesVolumeShader(const TrailEntity *t)
{
    return t->tubeVolumeShading && s_volumeShader.id != 0;
}

static inline float SmoothStepC(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f)
        return 0.0f;
    if (t > 1.0f)
        return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static inline float ComputeWispStyleTaper(float segRatio)
{
    return SmoothStepC(0.0f, TRAIL_WISP_HEAD_TAPER_EDGE, segRatio) *
           SmoothStepC(1.0f, TRAIL_WISP_TAIL_TAPER_EDGE, 1.0f - segRatio);
}

// [TỐI ƯU PERFORMANCE] Thay thế powf() bằng đa thức xấp xỉ nhanh
static float ComputeWidthEnvelopeFast(const TrailEntity *t, float segRatio, float time)
{
    if (t->widthCurve)
    {
        return SkillCurve_Eval(t->widthCurve, segRatio);
    }
    switch (t->widthEnvelope)
    {
    case TRAIL_WIDTH_ENVELOPE_TAPER_TAIL:
        // Xấp xỉ powf(segRatio, 1.2f) bằng đa thức nhanh: x * (0.55 + 0.45 * x)
        return segRatio * (0.55f + 0.45f * segRatio);
    case TRAIL_WIDTH_ENVELOPE_TAPER_BOTH:
    {
        // Xấp xỉ powf(sinf(x * PI), 0.6f) bằng đường cong x*(1-x) nhanh
        float p = 4.0f * segRatio * (1.0f - segRatio);
        return p * (0.6f + 0.4f * p);
    }
    case TRAIL_WIDTH_ENVELOPE_PULSE:
        return 1.0f + 0.25f * sinf(segRatio * 12.0f - time * 10.0f);
    case TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE:
    {
        // segRatio runs tail->head in reverse: age 0 is fresh source smoke;
        // age 1 is the oldest tail. It grows first, billows in the middle and
        // reaches zero before the tail is culled by follower idle fade.
        float age = 1.0f - segRatio;
        float grow = SmoothStepC(0.0f, 0.22f, age);
        float dissolve = 1.0f - SmoothStepC(0.66f, 1.0f, age);
        return (0.18f + 0.82f * grow) * (0.70f + 0.30f * age) * dissolve;
    }
    case TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN:
    {
        // Widen-only: a compact plume head that spreads monotonically to FULL
        // width at the tail. The width itself never collapses — the material
        // tail ramp (tailFadeA/B in the fragment shader) evaporates the tip.
        float age = 1.0f - segRatio;
        float grow = SmoothStepC(0.0f, 0.25f, age);
        return 0.15f + 0.85f * grow;
    }
    case TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE:
    {
        // Compact head -> widest just behind it -> long taper to a needle.
        // The tail floor is deliberately non-zero: a width of exactly 0 makes
        // the last quad degenerate, which renders as a dark wedge rather than
        // as nothing (the same trap documented on SmokeTrail's radius).
        float age = 1.0f - segRatio; // 0 at the head, 1 at the tail
        float lead = SmoothStepC(0.0f, 0.14f, age);
        float body = 1.0f - SmoothStepC(0.14f, 1.0f, age);
        return (0.35f + 0.65f * lead) * (0.06f + 0.94f * body * body);
    }
    case TRAIL_WIDTH_ENVELOPE_UNIFORM:
    default:
        return 1.0f;
    }
}

static inline Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;

    float f0 = -0.5f * t3 + t2 - 0.5f * t;
    float f1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
    float f2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    float f3 = 0.5f * t3 - 0.5f * t2;

    return (Vector3){
        p0.x * f0 + p1.x * f1 + p2.x * f2 + p3.x * f3,
        p0.y * f0 + p1.y * f1 + p2.y * f2 + p3.y * f3,
        p0.z * f0 + p1.z * f1 + p2.z * f2 + p3.z * f3};
}

static inline int GetHistoryNodeIndex(const TrailEntity *t, int i)
{
    if (t->type == TRAIL_TYPE_WISP)
    {
        return t->historyCount - 1 - i;
    }
    else
    {
        return (t->historyHead - (t->historyCount - 1 - i) + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
    }
}

static inline int NodeIndexForSegRatio(const TrailEntity *t, int drawCount, int h)
{
    if (t->historyCount < 1)
        return 0;
    float segRatio = (drawCount > 1) ? 1.0f - (float)h / (float)(drawCount - 1) : 1.0f;
    int i = (int)(segRatio * (float)(t->historyCount - 1) + 0.5f);
    if (i < 0)
        i = 0;
    if (i > t->historyCount - 1)
        i = t->historyCount - 1;
    return GetHistoryNodeIndex(t, i);
}

static Vector3 GetInterpolatedPosition(const TrailEntity *t, float segRatio)
{
    if (t->historyCount < 1)
        return (Vector3){0, 0, 0};
    if (t->historyCount == 1)
        return t->history[GetHistoryNodeIndex(t, 0)];

    float idx = segRatio * (t->historyCount - 1);
    int i = (int)floorf(idx);
    float f = idx - (float)i;

    int N = t->historyCount;
    int p0 = i - 1;
    if (p0 < 0)
        p0 = 0;
    int p1 = i;
    if (p1 >= N)
        p1 = N - 1;
    int p2 = i + 1;
    if (p2 >= N)
        p2 = N - 1;
    int p3 = i + 2;
    if (p3 >= N)
        p3 = N - 1;

    int idxP0 = GetHistoryNodeIndex(t, p0);
    int idxP1 = GetHistoryNodeIndex(t, p1);
    int idxP2 = GetHistoryNodeIndex(t, p2);
    int idxP3 = GetHistoryNodeIndex(t, p3);

    return CatmullRom(t->history[idxP0], t->history[idxP1], t->history[idxP2], t->history[idxP3], f);
}

// Render-time only: history, attachment and UV distance retain the real path.
// At the newest node (segRatio == 1), grow is exactly zero, welding the visible
// filament to its source even when the rest of the tail has a large radius.
static Vector3 ApplyAnchoredHelix(const TrailEntity *t, Vector3 pos, float segRatio)
{
    if (t->helixRadius <= 0.0f || t->helixTurns == 0.0f ||
        Vector3LengthSqr(t->helixAxis) < 1e-8f)
        return pos;

    Vector3 axis = Vector3Normalize(t->helixAxis);
    Vector3 ref = (fabsf(axis.y) > 0.9f) ? (Vector3){1.0f, 0.0f, 0.0f}
                                          : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 u = Vector3Normalize(Vector3CrossProduct(axis, ref));
    Vector3 v = Vector3CrossProduct(axis, u);
    float age = 1.0f - segRatio;
    float grow = Clamp(age * 5.5f, 0.0f, 1.0f);
    float phase = t->helixPhase + age * t->helixTurns * 2.0f * PI;
    Vector3 offset = Vector3Add(Vector3Scale(u, cosf(phase)),
                                Vector3Scale(v, sinf(phase)));
    return Vector3Add(pos, Vector3Scale(offset, t->helixRadius * grow));
}

// [TỐI ƯU PERFORMANCE] Frustum & Distance Culling loại bỏ vẽ trail ngoài màn hình
static inline bool IsTrailVisible(const TrailEntity *t, Camera3D camera)
{
    if (t->historyCount < 1)
        return false;

    Vector3 head = t->history[t->historyHead];
    float radius = (t->length > t->thickness) ? t->length : t->thickness;
    if (t->trailLength > radius)
        radius = t->trailLength;
    radius += t->distortionStrength + 1.0f;

    Vector3 camToHead = Vector3Subtract(head, camera.position);
    float distSqr = Vector3LengthSqr(camToHead);

    // Culling nếu xa quá 500m
    if (distSqr > 250000.0f)
        return false;

    Vector3 camFwd = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    float proj = Vector3DotProduct(camToHead, camFwd);

    // Culling nếu nằm hoàn toàn phía sau Camera
    if (proj < -radius)
        return false;

    return true;
}

// [TỐI ƯU PERFORMANCE] Tính toán số lượng đỉnh chia động theo độ dài thực tế
static inline int CalculateDrawCount(const TrailEntity *t)
{
    int hc = t->historyCount;
    if (hc <= 1)
        return hc;
    if (!t->smoothSpline)
        return hc;

    Vector3 head = t->history[t->historyHead];
    int tailIdx = GetHistoryNodeIndex(t, 0);
    Vector3 tail = t->history[tailIdx];
    float distSqr = Vector3LengthSqr(Vector3Subtract(head, tail));

    // Nếu trail quá ngắn (< 0.5m), không ép buộc sinh 30 điểm Spline
    if (distSqr < 0.25f)
        return hc;

    int drawCount = hc < 30 ? 30 : hc;
    return (drawCount > TRAIL_HISTORY_COUNT) ? TRAIL_HISTORY_COUNT : drawCount;
}

static void DrawCameraFacingQuad(const TrailCameraBasis *cam, Vector3 center,
                                 float width, float height, float rotation,
                                 Color tint, Texture2D tex, Rectangle uvRect)
{
    Vector3 rVec = Vector3Scale(cam->right, width * 0.5f);
    Vector3 uVec = Vector3Scale(cam->up, height * 0.5f);

    if (rotation != 0.0f)
    {
        float cosR = cosf(rotation);
        float sinR = sinf(rotation);
        Vector3 tR = rVec;
        rVec = (Vector3){rVec.x * cosR + uVec.x * sinR, rVec.y * cosR + uVec.y * sinR, rVec.z * cosR + uVec.z * sinR};
        uVec = (Vector3){uVec.x * cosR - tR.x * sinR, uVec.y * cosR - tR.y * sinR, uVec.z * cosR - tR.z * sinR};
    }

    Vector3 tl = {center.x - rVec.x + uVec.x, center.y - rVec.y + uVec.y, center.z - rVec.z + uVec.z};
    Vector3 tr = {center.x + rVec.x + uVec.x, center.y + rVec.y + uVec.y, center.z + rVec.z + uVec.z};
    Vector3 bl = {center.x - rVec.x - uVec.x, center.y - rVec.y - uVec.y, center.z - rVec.z - uVec.z};
    Vector3 br = {center.x + rVec.x - uVec.x, center.y + rVec.y - uVec.y, center.z + rVec.z - uVec.z};

    if (tex.id > 0)
        rlSetTexture(tex.id);

    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);

    rlTexCoord2f(uvRect.x, uvRect.y);
    rlVertex3f(tl.x, tl.y, tl.z);
    rlTexCoord2f(uvRect.x, uvRect.y + uvRect.height);
    rlVertex3f(bl.x, bl.y, bl.z);
    rlTexCoord2f(uvRect.x + uvRect.width, uvRect.y + uvRect.height);
    rlVertex3f(br.x, br.y, br.z);
    rlTexCoord2f(uvRect.x + uvRect.width, uvRect.y);
    rlVertex3f(tr.x, tr.y, tr.z);

    rlEnd();
}

static inline void ConstrainRibbonSegment(Vector3 *a, Vector3 *b, float restLen, bool pinnedA)
{
    if (restLen <= 1e-6f)
        return;

    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float dz = b->z - a->z;
    float dist2 = dx * dx + dy * dy + dz * dz;
    float restLen2 = restLen * restLen;

    if (fabsf(dist2 - restLen2) < restLen2 * 1e-8f || dist2 < 1e-10f)
        return;

    float dist = sqrtf(dist2);
    float invDist = 1.0f / dist;
    float err = dist - restLen;

    float dirX = dx * invDist * err;
    float dirY = dy * invDist * err;
    float dirZ = dz * invDist * err;

    if (pinnedA)
    {
        b->x -= dirX;
        b->y -= dirY;
        b->z -= dirZ;
    }
    else
    {
        float hX = dirX * 0.5f, hY = dirY * 0.5f, hZ = dirZ * 0.5f;
        a->x += hX;
        a->y += hY;
        a->z += hZ;
        b->x -= hX;
        b->y -= hY;
        b->z -= hZ;
    }
}

static inline void GrowHistoryTowardMaxNodes(TrailEntity *t)
{
    int maxNodes = (t->trailLength > 0.0f) ? (int)t->trailLength : TRAIL_HISTORY_COUNT;
    if (maxNodes > TRAIL_HISTORY_COUNT)
        maxNodes = TRAIL_HISTORY_COUNT;
    if (maxNodes < 1)
        maxNodes = 1;

    if (t->historyCount < maxNodes)
        t->historyCount++;
    else if (t->historyCount > maxNodes)
        t->historyCount = maxNodes;
}

static void UpdateProjectilePhysics(int id, TrailEntity *t, float dt, float time)
{
    float velLen = Vector3Length(t->velocity);
    Vector3 dir = (velLen > 1e-6f) ? (Vector3){t->velocity.x / velLen, t->velocity.y / velLen, t->velocity.z / velLen}
                                   : (Vector3){0.0f, 0.0f, 1.0f};

    Vector3 spawnPos = (Vector3){
        t->position.x - dir.x * (t->length * TRAIL_PROJECTILE_SPAWN_OFFSET_MUL),
        t->position.y - dir.y * (t->length * TRAIL_PROJECTILE_SPAWN_OFFSET_MUL),
        t->position.z - dir.z * (t->length * TRAIL_PROJECTILE_SPAWN_OFFSET_MUL)};

    bool shouldInsert = true;
    if (t->minVertexDistance > 0.0f && t->historyCount > 0)
    {
        Vector3 lastNode = t->history[t->historyHead];
        float distSqr = (spawnPos.x - lastNode.x) * (spawnPos.x - lastNode.x) +
                        (spawnPos.y - lastNode.y) * (spawnPos.y - lastNode.y) +
                        (spawnPos.z - lastNode.z) * (spawnPos.z - lastNode.z);
        if (distSqr < t->minVertexDistance * t->minVertexDistance)
        {
            shouldInsert = false;
        }
    }

    if (shouldInsert)
    {
        t->historyHead = (t->historyHead + 1) % TRAIL_HISTORY_COUNT;
        GrowHistoryTowardMaxNodes(t);
    }

    t->history[t->historyHead] = spawnPos;
    t->wobblePhase += dt * TRAIL_PROJECTILE_WOBBLE_FREQ;
    Vector3 posBeforeMove = t->position;

    Vector3 toTarget = Vector3Subtract(t->target, t->position);
    float distSqr = toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z;

    if (distSqr > TRAIL_PROJECTILE_RETARGET_DIST_SQR)
    {
        float curveRange = (t->curveRangeOverride > 0.0f) ? t->curveRangeOverride : TRAIL_PROJECTILE_CURVE_RANGE;
        float wobbleAmp = (t->wobbleAmplitudeOverride > 0.0f) ? t->wobbleAmplitudeOverride : TRAIL_PROJECTILE_WOBBLE_AMPLITUDE;

        float distToTarget = sqrtf(distSqr);
        float invDist = 1.0f / distToTarget;
        Vector3 desiredDir = {toTarget.x * invDist, toTarget.y * invDist, toTarget.z * invDist};

        float newSpeed = fminf(velLen + TRAIL_PROJECTILE_ACCEL_RATE * dt, TRAIL_PROJECTILE_MAX_SPEED);
        float curveStrength = fminf(distToTarget / curveRange, 1.0f);

        Vector3 perpDir = {-desiredDir.z, 0.0f, desiredDir.x};
        float wobble = sinf(t->wobblePhase) * wobbleAmp * curveStrength * dt;

        Vector3 desiredVel = {
            desiredDir.x * newSpeed + perpDir.x * wobble,
            desiredDir.y * newSpeed + perpDir.y * wobble,
            desiredDir.z * newSpeed + perpDir.z * wobble};

        t->velocity = Vector3Lerp(t->velocity, desiredVel, dt * TRAIL_PROJECTILE_STEER_LERP_RATE);
    }

    const bool windActive = WindZone_IsActive();
    if (t->forceField || windActive)
    {
        Vector3 acc = (Vector3){0, 0, 0};
        if (t->forceField)
        {
            acc = ForceField_Evaluate(t->forceField, t->position, t->velocity, time, (Vector3){0}, (Vector3){0});
        }
        if (windActive)
        {
            Vector3 windAcc = WindZone_Evaluate(t->position, t->velocity, time);
            acc.x += windAcc.x;
            acc.y += windAcc.y;
            acc.z += windAcc.z;
        }
        t->velocity.x += acc.x * dt;
        t->velocity.y += acc.y * dt;
        t->velocity.z += acc.z * dt;

        if (t->forceField)
        {
            float viscDamp = ForceField_GetViscosityDamping(t->forceField, dt);
            t->velocity.x *= viscDamp;
            t->velocity.y *= viscDamp;
            t->velocity.z *= viscDamp;
        }
    }

    t->position.x += t->velocity.x * dt;
    t->position.y += t->velocity.y * dt;
    t->position.z += t->velocity.z * dt;

    Vector3 moveDelta = Vector3Subtract(t->position, posBeforeMove);
    float moveLenSqr = moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y + moveDelta.z * moveDelta.z;
    Vector3 toTargetFromStart = Vector3Subtract(t->target, posBeforeMove);

    float closestDistSqr;
    if (moveLenSqr < 1e-8f)
    {
        closestDistSqr = Vector3LengthSqr(Vector3Subtract(t->target, t->position));
    }
    else
    {
        float proj = Vector3DotProduct(toTargetFromStart, moveDelta) / moveLenSqr;
        proj = fmaxf(0.0f, fminf(1.0f, proj));
        Vector3 closestPoint = {posBeforeMove.x + moveDelta.x * proj, posBeforeMove.y + moveDelta.y * proj, posBeforeMove.z + moveDelta.z * proj};
        closestDistSqr = Vector3LengthSqr(Vector3Subtract(t->target, closestPoint));
    }

    bool isHit = (closestDistSqr < TRAIL_PROJECTILE_HIT_DIST_SQR);
    if (!isHit && t->collisionCheck != NULL)
    {
        isHit = t->collisionCheck(id, t->position);
    }

    if (isHit)
    {
        t->type = TRAIL_TYPE_FOLLOWER;
        t->attachedTransform = NULL;
        t->timeSinceLastFollowerUpdate = 0.0f;
        t->fadeAccumulator = 0.0f;
    }
}

static void UpdateWispPhysics(TrailEntity *t, float dt, float time)
{
    const bool windActive = WindZone_IsActive();
    if ((!t->forceField && !windActive) || t->historyCount < 2 || t->nodeRestLen <= 0.0f)
        return;
    float viscDamp = t->forceField ? ForceField_GetViscosityDamping(t->forceField, dt) : 1.0f;
    float restLen = t->nodeRestLen;

    for (int h = 0; h < t->historyCount; h++)
    {
        Vector3 acc = (Vector3){0, 0, 0};
        if (t->forceField)
        {
            acc = ForceField_Evaluate(t->forceField, t->history[h], t->nodeVelocity[h], time, (Vector3){0}, (Vector3){0});
        }
        if (windActive)
        {
            Vector3 windAcc = WindZone_Evaluate(t->history[h], t->nodeVelocity[h], time);
            acc.x += windAcc.x;
            acc.y += windAcc.y;
            acc.z += windAcc.z;
        }
        t->nodeVelocity[h] = (Vector3){
            (t->nodeVelocity[h].x + acc.x * dt) * viscDamp,
            (t->nodeVelocity[h].y + acc.y * dt) * viscDamp,
            (t->nodeVelocity[h].z + acc.z * dt) * viscDamp};
        scratchNodePrevPos[h] = t->history[h];
        t->history[h] = (Vector3){
            t->history[h].x + t->nodeVelocity[h].x * dt,
            t->history[h].y + t->nodeVelocity[h].y * dt,
            t->history[h].z + t->nodeVelocity[h].z * dt};
    }

    for (int iter = 0; iter < WISP_CONSTRAINT_ITERS; iter++)
    {
        for (int h = 1; h < t->historyCount; h++)
        {
            ConstrainRibbonSegment(&t->history[h - 1], &t->history[h], restLen, false);
        }
        for (int h = t->historyCount - 1; h >= 1; h--)
        {
            ConstrainRibbonSegment(&t->history[h - 1], &t->history[h], restLen, false);
        }
    }

    if (dt > 1e-7f)
    {
        float invDt = 1.0f / dt;
        for (int h = 0; h < t->historyCount; h++)
        {
            t->nodeVelocity[h] = (Vector3){
                (t->history[h].x - scratchNodePrevPos[h].x) * invDt,
                (t->history[h].y - scratchNodePrevPos[h].y) * invDt,
                (t->history[h].z - scratchNodePrevPos[h].z) * invDt};
        }
    }
    t->position = t->history[0];
}

static void ClampFollowerDeviation(TrailEntity *t, int idx, int h)
{
    if (t->nodeHomeSpring <= 0.0f)
        return;
    int lead = (t->historyHead - h + 1 + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
    Vector3 off = Vector3Subtract(t->history[idx], t->nodeHome[idx]);

    if (t->nodeOrderFrac > 0.0f)
    {
        Vector3 seg = Vector3Subtract(t->nodeHome[lead], t->nodeHome[idx]);
        float segLen = Vector3Length(seg);
        if (segLen > 1e-5f)
        {
            Vector3 dir = Vector3Scale(seg, 1.0f / segLen);
            float along = Vector3DotProduct(off, dir);
            float spacing = fminf(t->nodeRest[idx], t->nodeRest[lead]);
            float alongMax = t->nodeOrderFrac * spacing;
            float clamped = along;
            if (clamped > alongMax)
                clamped = alongMax;
            if (clamped < -alongMax)
                clamped = -alongMax;
            if (clamped != along)
                off = Vector3Add(off, Vector3Scale(dir, clamped - along));
        }
    }

    if (t->nodeHomeMaxDev > 0.0f)
    {
        float offLen = Vector3Length(off);
        if (offLen > t->nodeHomeMaxDev)
            off = Vector3Scale(off, t->nodeHomeMaxDev / offLen);
    }

    Vector3 corrected = Vector3Add(t->nodeHome[idx], off);
    if (Vector3DistanceSqr(corrected, t->history[idx]) > 1e-10f)
    {
        t->history[idx] = corrected;
        t->nodeVelocity[idx] = Vector3Scale(t->nodeVelocity[idx], 0.5f);
    }
}

static void UpdateFollowerPhysics(int i, TrailEntity *t, float dt, float time)
{
    // A static diagnostic path is intentionally not fed by a transform. Do not
    // treat that as an idle trail which should drain away.
    if (t->frozen)
        return;

    t->timeSinceLastFollowerUpdate += dt;
    if (t->timeSinceLastFollowerUpdate > TRAIL_FOLLOWER_IDLE_FADE_TIME)
    {
        t->fadeAccumulator += TRAIL_FOLLOWER_FADE_RATE_PER_SEC * dt;
        int fadeCount = (int)t->fadeAccumulator;
        if (fadeCount > 0)
        {
            t->historyCount -= fadeCount;
            t->fadeAccumulator -= (float)fadeCount;
        }
        if (t->historyCount <= 0)
        {
            KillTrailInternal(i);
            return;
        }
    }

    const bool windActive = WindZone_IsActive();
    if ((!t->forceField && !windActive) || t->historyCount < 2)
        return;
    float viscDamp = t->forceField ? ForceField_GetViscosityDamping(t->forceField, dt) : 1.0f;

    for (int h = 1; h < t->historyCount; h++)
    {
        int idx = (t->historyHead - h + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
        Vector3 acc = (Vector3){0, 0, 0};
        if (t->forceField)
        {
            acc = ForceField_Evaluate(t->forceField, t->history[idx], t->nodeVelocity[idx], time, t->axisOrigin, t->axisDir);
        }
        if (windActive)
        {
            Vector3 windAcc = WindZone_Evaluate(t->history[idx], t->nodeVelocity[idx], time);
            acc.x += windAcc.x;
            acc.y += windAcc.y;
            acc.z += windAcc.z;
        }

        if (t->nodeHomeSpring > 0.0f)
        {
            Vector3 pull = Vector3Subtract(t->nodeHome[idx], t->history[idx]);
            acc.x += pull.x * t->nodeHomeSpring;
            acc.y += pull.y * t->nodeHomeSpring;
            acc.z += pull.z * t->nodeHomeSpring;
            float depth = (float)h / (float)(t->historyCount - 1);
            float scale = 0.25f + 0.75f * depth;
            acc.x *= scale;
            acc.y *= scale;
            acc.z *= scale;
        }

        t->nodeVelocity[idx] = (Vector3){
            (t->nodeVelocity[idx].x + acc.x * dt) * viscDamp,
            (t->nodeVelocity[idx].y + acc.y * dt) * viscDamp,
            (t->nodeVelocity[idx].z + acc.z * dt) * viscDamp};
        t->history[idx] = (Vector3){
            t->history[idx].x + t->nodeVelocity[idx].x * dt,
            t->history[idx].y + t->nodeVelocity[idx].y * dt,
            t->history[idx].z + t->nodeVelocity[idx].z * dt};

        ClampFollowerDeviation(t, idx, h);
    }

    if (t->nodeHomeSpring > 0.0f)
    {
        for (int pass = 0; pass < TRAIL_CLOTH_CONSTRAIN_ITERS; pass++)
        {
            for (int h = 1; h < t->historyCount; h++)
            {
                int idx = (t->historyHead - h + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
                int lead = (t->historyHead - h + 1 + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;
                Ribbon_ConstrainSegment(&t->history[lead], &t->history[idx],
                                        t->nodeRest[idx], true, RIBBON_CONSTRAIN_MAX);
                Ribbon_ConstrainSegment(&t->history[lead], &t->history[idx],
                                        t->nodeRest[idx] * TRAIL_CLOTH_MIN_SPACING, true,
                                        RIBBON_CONSTRAIN_MIN);
            }
        }
    }
}

void InitTrailSystem(void)
{
    /* The module loads its own default material now. It used to be handed one
     * Shader by main.c, which cannot work once the shader is a permutation: the
     * caller would have to know which blend each trail ends up using. */
    s_defaultShader[TRAIL_SHADER_BODY] = ResourceManager_LoadShaderVariant(
        NULL, "core/trails/shaders/trail_glow.fs",
        VFXRender_OutputDefines(VFX_SURFACE_ALPHA));
    s_defaultShader[TRAIL_SHADER_EMISSION] = ResourceManager_LoadShaderVariant(
        NULL, "core/trails/shaders/trail_glow.fs",
        VFXRender_OutputDefines(VFX_SURFACE_ADDITIVE));
    shaderCacheCount = 0;
    for (int i = 0; i < MAX_TRAIL_PARTICLES; i++)
    {
        trailPool[i].active = false;
        trailPool[i].nextFree = i + 1;
        s_slotListIndex[i] = -1;
    }
    freeListHead = 0;
    activeCount = 0;

    if (s_tubeNoiseImg.data == NULL)
    {
        s_tubeNoiseImg = LoadImage("assets/textures/volume_noise.png");
        if (s_tubeNoiseImg.data != NULL)
            ImageFormat(&s_tubeNoiseImg, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        else
            TraceLog(LOG_WARNING, "TRAIL: volume_noise.png missing — tube deformation "
                                  "falls back to the procedural field");
    }
    if (s_tubeFlatTex.id == 0)
    {
        Image flat = GenImageColor(2, 2, WHITE);
        s_tubeFlatTex = LoadTextureFromImage(flat);
        UnloadImage(flat);
    }
}

TrailEntity *GetTrail(int id) { return (id < 0 || id >= MAX_TRAIL_PARTICLES) ? NULL : &trailPool[id]; }

static void KillTrailInternal(int id)
{
    if (trailPool[id].onDeath)
        trailPool[id].onDeath(trailPool[id].position, trailPool[id].scale);
    trailPool[id].active = false;
    int listIdx = s_slotListIndex[id];
    int lastId = s_activeIds[activeCount - 1];
    s_activeIds[listIdx] = lastId;
    s_slotListIndex[lastId] = listIdx;
    s_slotListIndex[id] = -1;
    activeCount--;
    trailPool[id].nextFree = freeListHead;
    freeListHead = id;
}

void KillTrail(int id)
{
    if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active)
        KillTrailInternal(id);
}
int GetActiveTrailCount(void) { return activeCount; }

static bool EvictLowestPriorityTrail(VFXPriority incomingPriority)
{
    int evictIdx = -1;
    VFXPriority evictPriority = VFX_PRIORITY_HIGH_ULTIMATE;
    float evictLifetime = 999999.0f;
    for (int i = 0; i < MAX_TRAIL_PARTICLES; i++)
    {
        if (!trailPool[i].active)
            continue;
        if (evictIdx == -1 || trailPool[i].priority < evictPriority ||
            (trailPool[i].priority == evictPriority && trailPool[i].lifetime < evictLifetime))
        {
            evictIdx = i;
            evictPriority = trailPool[i].priority;
            evictLifetime = trailPool[i].lifetime;
        }
    }
    if (evictIdx == -1 || evictPriority > incomingPriority)
        return false;
    KillTrailInternal(evictIdx);
    return true;
}

int SpawnTrailEntity(TrailConfig config)
{
    TrailConfig_Unify(&config);
    if (freeListHead >= MAX_TRAIL_PARTICLES)
        if (!EvictLowestPriorityTrail(config.priority))
            return -1;

    int index = freeListHead;
    freeListHead = trailPool[index].nextFree;
    TrailEntity *t = &trailPool[index];

    t->type = config.type;
    t->position = config.pos;
    t->velocity = config.vel;
    t->target = config.target;
    t->length = config.len;
    t->thickness = config.thick;
    t->trailLength = config.trailLength;
    t->lifetime = config.life;
    t->maxLifetime = config.life;
    t->active = true;
    t->angle = config.initialAngle;
    t->wobblePhase = config.wobblePhase;
    t->scale = config.scale;
    t->sprite = config.tex;
    t->shader = config.shader;
    t->tint = config.tint;
    t->onUpdate = config.onUpdate;
    t->onDeath = config.onDeath;
    t->ownerTag = config.ownerTag;
    t->wobbleAmplitudeOverride = config.wobbleAmplitudeOverride;
    t->curveRangeOverride = config.curveRangeOverride;
    t->forceField = config.forceField;
    t->gradient = config.gradient;
    t->spriteAnim = config.spriteAnim;
    t->priority = config.priority;
    t->orbitRadius = config.orbitRadius;
    t->orbitSpeed = config.orbitSpeed;
    t->orbitAxis = config.orbitAxis;
    t->orbitPhase = config.orbitPhase;
    t->blendMode = config.blendMode;
    t->timeSinceLastFollowerUpdate = 0.0f;
    t->fadeAccumulator = 0.0f;
    t->historyHead = 0;
    t->driftVelocity = (Vector3){0, 0, 0};
    t->axisOrigin = (Vector3){0, 0, 0};
    t->axisDir = (Vector3){0, 0, 0};
    t->attachedTransform = NULL;
    t->attachLocalOffset = (Vector3){0, 0, 0};

    t->collisionCheck = config.collisionCheck;
    t->uvTiling = (config.uvTiling != 0.0f) ? config.uvTiling : 1.0f;
    t->uvScrollSpeed = config.uvScrollSpeed;
    t->uvScrollOffset = 0.0f;
    t->tubeNoiseSpanLen = 0.0f;
    t->minVertexDistance = config.minVertexDistance;
    t->widthEnvelope = config.widthEnvelope;
    t->smoothSpline = config.smoothSpline;
    t->disableInnerCore = config.disableInnerCore;
    t->useCustomBlendMode = config.useCustomBlendMode;
    t->widthCurve = config.widthCurve;
    t->alphaCurve = config.alphaCurve;
    t->distortionStrength = config.distortionStrength;
    t->distortionSpeed = (config.distortionSpeed != 0.0f) ? config.distortionSpeed : 1.0f;
    t->helixAxis = config.helixAxis;
    t->helixRadius = config.helixRadius;
    t->helixTurns = config.helixTurns;
    t->helixPhase = config.helixPhase;
    t->ribbonMode = config.ribbonMode;
    t->fixedNormal = config.fixedNormal;

    t->layers = config.layers;
    t->layerCount = (config.layerCount > TRAIL_MAX_LAYERS) ? TRAIL_MAX_LAYERS : config.layerCount;
    if (t->layers == NULL)
        t->layerCount = 0;
    t->uvMetresPerTile = config.uvMetresPerTile;
    t->laidDist = 0.0f;
    t->nodeHomeSpring = config.nodeHomeSpring;
    t->nodeHomeMaxDev = config.nodeHomeMaxDev;
    t->nodeOrderFrac = (config.nodeOrderFrac > 0.49f) ? 0.49f : config.nodeOrderFrac;
    t->sampleHz = config.sampleHz;
    t->sampleAcc = 0.0f;
    t->teleportSpeed = config.teleportSpeed;
    t->idleSpeed = config.idleSpeed;
    t->shape = config.shape;
    t->tubeRadialSegs = (config.tubeRadialSegs > 0) ? config.tubeRadialSegs
                                                    : TRAIL_TUBE_RADIAL_DEFAULT;
    if (t->tubeRadialSegs > TRAIL_TUBE_RADIAL_MAX)
        t->tubeRadialSegs = TRAIL_TUBE_RADIAL_MAX;
    t->tubeMaxRings = (config.tubeMaxRings > 0) ? config.tubeMaxRings
                                                : TRAIL_TUBE_RINGS_DEFAULT;
    /* 0 = rơi về tubeMaxRings, tức đúng hành vi trước 06/08/2026 cho mọi
     * caller không khai báo. Xem doc field ở trail_system.h. */
    t->tubeGeomSegs = (config.tubeGeomSegs > 0) ? config.tubeGeomSegs : 0;
    t->section = (config.sectionCount >= 3) ? config.section : NULL;
    t->sectionCount = (t->section != NULL) ? config.sectionCount : 0;
    if (t->sectionCount > TRAIL_TUBE_RADIAL_MAX)
        t->sectionCount = TRAIL_TUBE_RADIAL_MAX;
    t->tubeCaps = config.tubeCaps;
    t->tubeVolumeShading = config.tubeVolumeShading;
    t->tubeDeformFrozen = config.tubeDeformFrozen;
    if (config.tubeVolumeShading) EnsureTrailVolumeShader();
    t->tubeSingleSided = config.tubeSingleSided;
    t->tubeNoiseAmp = config.tubeNoiseAmp;
    t->dropletConfig = config.dropletConfig;
    t->tubeShapeConfig = config.tubeShapeConfig;
    t->prevAttachPos = config.pos;
    t->lateralOffset = (Vector3){0, 0, 0};
    t->hasPrevAttach = false;
    t->frozen = false;

    // ── BỔ SUNG FLOWMAP CONFIG ──────────────────────────────────────────
    t->flowMap = (config.flowMap != NULL) ? *config.flowMap : (Texture2D){0};
    t->flowSpeed = config.flowSpeed;
    t->flowStrength = config.flowStrength;
    t->flowTiling = (config.flowTiling != 0.0f) ? config.flowTiling : 1.0f;
    t->useFlowMap = config.useFlowMap || (t->flowMap.id > 0);
    t->noiseMask = (config.noiseMask != NULL) ? *config.noiseMask : (Texture2D){0};
    t->dissolve = config.dissolve;
    t->maskTiling = (config.maskTiling > 0.0f) ? config.maskTiling : 1.0f;
    t->flowTimeAccumulator = 0.0f;

    // GPU deform + packed material (mode 0 = classic pipeline untouched).
    t->deform = config.deform;
    t->material = config.material;
    if (config.material.appearance != VFX_APPEARANCE_INHERIT)
    {
        BlendMode legacyBlend = config.useCustomBlendMode ? config.blendMode
            : ((config.blendMode > 0) ? config.blendMode : BLEND_ADDITIVE);
        VFXSurfaceMode legacySurface = legacyBlend == BLEND_ALPHA
            ? VFX_SURFACE_ALPHA
            : (legacyBlend == BLEND_ALPHA_PREMULTIPLY
                   ? VFX_SURFACE_PREMULTIPLIED : VFX_SURFACE_ADDITIVE);
        VFXResolvedAppearance appearance = VFXAppearance_Resolve(
            config.material.appearance,
            (VFXResolvedAppearance){
                .surface = legacySurface,
                .contrast = config.material.contrastProfile,
                .bodyOpacity = config.material.bodyOpacity,
                .emissionIntensity = config.material.hdrGain,
                .emissionThreshold = 1.0f,
                .unlit = true
            });
        t->useCustomBlendMode = true;
        // Trail shaders already expose separate BODY and EMISSION resolvers.
        // A premultiplied semantic therefore maps to alpha body + additive
        // emission, rather than asking straight-colour ribbon vertices to obey
        // a premultiplied blend contract they cannot encode.
        t->blendMode = appearance.surface == VFX_SURFACE_ALPHA
                           ? BLEND_ALPHA : BLEND_ADDITIVE;
        t->material.contrastProfile = appearance.contrast;
        t->material.bodyOpacity = appearance.bodyOpacity;
        t->material.hdrGain = appearance.emissionIntensity > 0.0f
                                  ? appearance.emissionIntensity : 1.0f;
    }
    // ────────────────────────────────────────────────────────────────────

    for (int h = 0; h < TRAIL_HISTORY_COUNT; h++)
    {
        t->nodeVelocity[h] = (Vector3){0, 0, 0};
        t->nodeHome[h] = config.pos;
        t->nodeRest[h] = 0.0f;
        t->nodeUV[h] = 0.0f;
    }

    if (config.type == TRAIL_TYPE_WISP)
    {
        int maxNodes = (config.trailLength > 0.0f) ? (int)config.trailLength : TRAIL_HISTORY_COUNT;
        if (maxNodes > TRAIL_HISTORY_COUNT)
            maxNodes = TRAIL_HISTORY_COUNT;
        if (maxNodes < 2)
            maxNodes = 2;
        t->historyCount = maxNodes;
        t->nodeRestLen = (maxNodes > 1 && config.len > 0.0f) ? config.len / (float)(maxNodes - 1) : 0.0f;
        Vector3 strandDir = (Vector3LengthSqr(config.target) > 1e-8f) ? Vector3Normalize(config.target) : (Vector3){0, 0, 1};
        for (int h = 0; h < maxNodes; h++)
        {
            float u = (maxNodes > 1) ? (float)h / (float)(maxNodes - 1) : 0.0f;
            t->history[h] = (Vector3){config.pos.x + strandDir.x * u * config.len, config.pos.y + strandDir.y * u * config.len, config.pos.z + strandDir.z * u * config.len};
            t->nodeVelocity[h] = config.vel;
        }
    }
    else if (config.type == TRAIL_TYPE_FOLLOWER)
    {
        t->historyCount = 0;
        t->nodeRestLen = 0.0f;
    }
    else
    {
        t->historyCount = 0;
        t->nodeRestLen = 0.0f;
        for (int h = 0; h < TRAIL_HISTORY_COUNT; h++)
            t->history[h] = config.pos;
    }

    activeCount++;
    s_slotListIndex[index] = activeCount - 1;
    s_activeIds[activeCount - 1] = index;
    return index;
}

void UpdateFollowerPosition(int id, Vector3 newTipPos)
{
    if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active || trailPool[id].type != TRAIL_TYPE_FOLLOWER)
        return;
    TrailEntity *t = &trailPool[id];

    bool shouldInsert = true;
    if (t->minVertexDistance > 0.0f && t->historyCount > 0)
    {
        Vector3 lastNode = t->history[t->historyHead];
        float distSqr = (newTipPos.x - lastNode.x) * (newTipPos.x - lastNode.x) +
                        (newTipPos.y - lastNode.y) * (newTipPos.y - lastNode.y) +
                        (newTipPos.z - lastNode.z) * (newTipPos.z - lastNode.z);
        if (distSqr < t->minVertexDistance * t->minVertexDistance)
        {
            shouldInsert = false;
        }
    }

    if (shouldInsert)
    {
        int prev = t->historyHead;
        t->historyHead = (t->historyHead + 1) % TRAIL_HISTORY_COUNT;
        GrowHistoryTowardMaxNodes(t);

        float step = (t->historyCount > 1) ? Vector3Distance(newTipPos, t->history[prev]) : 0.0f;
        if (step < 1e-4f)
            step = 1e-4f;
        t->nodeRest[t->historyHead] = step;
        t->laidDist += step;
        t->nodeUV[t->historyHead] = t->laidDist;
    }

    t->history[t->historyHead] = newTipPos;
    t->nodeHome[t->historyHead] = newTipPos;
    t->nodeVelocity[t->historyHead] = (Vector3){0, 0, 0};
    t->position = newTipPos;
    t->timeSinceLastFollowerUpdate = 0.0f;
    t->fadeAccumulator = 0.0f;
}

// A cut throws the PATH away — the emitter is somewhere unrelated and the
// laid history no longer describes anything. It must not also throw away where
// the SURFACE had got to.
//
// `laidDist` is the odometer the strand material phases its texture against
// (nodeUV[] is laidDist at the moment each node was laid, and the fragment
// stage reads it as `u_pathArc.x`). Zeroing it here snapped that phase back to
// the start, so a cut cost a stall AND an instant jump in the pattern — the
// "stutters once, then changes phase" the owner reported. The stall is what a
// cut IS and stays; the jump was never part of it. Keeping the odometer running
// is safe because nothing else reads it: it is not the aspect cap's length
// (that is measured against the drawn span), and the draw path already folds it
// at 8192 m for float precision, so an unbounded value was always expected.
static void FollowerCut(TrailEntity *t, Vector3 tip)
{
    t->historyHead = 0;
    t->historyCount = 1;
    t->history[0] = tip;
    t->nodeHome[0] = tip;
    t->nodeVelocity[0] = (Vector3){0, 0, 0};
    t->nodeRest[0] = 1e-4f;
    t->nodeUV[0] = t->laidDist; // NOT 0 — see above
    t->prevAttachPos = tip;
    t->sampleAcc = 0.0f;
}

void Trail_SetLateralOffset(int id, Vector3 worldOffset)
{
    if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active)
        return;
    trailPool[id].lateralOffset = worldOffset;
}

void Trail_SetFrozen(int id, bool frozen)
{
    if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active)
        return;
    trailPool[id].frozen = frozen;
}

// Per-entity EMISSION lift, after creation. The material is a COPY taken when
// the entity is created (see the appearance resolve above), so editing a
// composition's recipe afterwards reaches nothing — this is the only way in.
void Trail_SetHdrGain(int id, float gain)
{
    if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active)
        return;
    if (gain < 0.0f) gain = 0.0f;
    trailPool[id].material.hdrGain = gain;
}

void Trail_SetStaticPath(int id, Vector3 tail, Vector3 head, int nodeCount)
{
    if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active)
        return;

    TrailEntity *t = &trailPool[id];
    if (t->type != TRAIL_TYPE_FOLLOWER)
        return;
    if (nodeCount < 2)
        nodeCount = 2;
    if (nodeCount > TRAIL_HISTORY_COUNT)
        nodeCount = TRAIL_HISTORY_COUNT;

    float length = Vector3Distance(tail, head);
    t->historyCount = nodeCount;
    t->historyHead = nodeCount - 1;
    t->laidDist = length;
    for (int i = 0; i < nodeCount; i++)
    {
        float u = (float)i / (float)(nodeCount - 1);
        Vector3 p = Vector3Lerp(tail, head, u);
        t->history[i] = p;
        t->nodeHome[i] = p;
        t->nodeVelocity[i] = (Vector3){0, 0, 0};
        t->nodeRest[i] = (i > 0) ? length / (float)(nodeCount - 1) : 0.0f;
        t->nodeUV[i] = length * u;
    }
    t->position = head;
    t->prevAttachPos = head;
    t->hasPrevAttach = true;
    t->attachedTransform = NULL;
    t->sampleAcc = 0.0f;
    t->fadeAccumulator = 0.0f;
    t->frozen = true;
}

void SetFollowerAxis(int id, Vector3 axisOrigin, Vector3 axisDir)
{
    if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active && trailPool[id].type == TRAIL_TYPE_FOLLOWER)
    {
        trailPool[id].axisOrigin = axisOrigin;
        trailPool[id].axisDir = axisDir;
    }
}

void Trail_AttachToTransform(int id, const Matrix *targetTransform, Vector3 localOffset)
{
    if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active && trailPool[id].type == TRAIL_TYPE_FOLLOWER)
    {
        trailPool[id].attachedTransform = targetTransform;
        trailPool[id].attachLocalOffset = localOffset;
    }
}

void Trail_SetFollowerOrbit(int id, float radius, float speed, Vector3 axis, float phase)
{
    if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active && trailPool[id].type == TRAIL_TYPE_FOLLOWER)
    {
        trailPool[id].orbitRadius = radius;
        trailPool[id].orbitSpeed = speed;
        trailPool[id].orbitAxis = axis;
        trailPool[id].orbitPhase = phase;
    }
}

void UpdateTrailSystem(float dt)
{
    float time = TimeFX_Elapsed();

    for (int a = 0; a < activeCount;)
    {
        int i = s_activeIds[a];
        TrailEntity *t = &trailPool[i];

        t->lifetime -= dt;
        if (t->lifetime <= 0.0f)
        {
            KillTrailInternal(i);
            continue;
        }

        // Keep an unscaled clock. The shader applies uSpeed exactly once;
        // scaling here too made flow-map speed unintentionally quadratic.
        if (t->useFlowMap)
        {
            t->flowTimeAccumulator += dt;
        }
        t->uvScrollOffset += t->uvScrollSpeed * dt;

        /* LÀM MỊN chiều dài path thật cho tọa độ nhiễu — nguồn của
         * "đổi pha một cái rụp" báo 05/08/2026: PMTube_BuildAlongPath tính
         * spanLen THÔ lại từ đầu MỖI KHUNG HÌNH, và MỘT giá trị đó lái toạ độ
         * nhiễu của TOÀN BỘ mesh cùng lúc (tNoise = t*spanLen/noiseWavelength
         * cho MỌI vành) — spanLen nhảy dù chỉ một khung là mọi vành cùng nhảy
         * tọa độ đọc trường nhiễu CÙNG LÚC, có thể xuyên qua ranh giới ô
         * lattice và đổi hẳn sang một mảng giá trị không tương quan — cả mesh
         * "chớp" sang một hình dạng khác trong một khung, đúng cảm giác "rụp".
         *
         * Chỉ tính khi caller thật sự cần (đã bật noiseWavelength) — tránh
         * tốn cho cột khói/spark trail/ember trail không dùng cờ này. Đi từ
         * history THÔ (không phải path đã resample bằng Catmull-Rom lúc vẽ)
         * vì Update có dt thật để làm mịn đúng — Draw thì không, và chỉ cần
         * MỘT ước lượng ổn định, không cần khớp chính xác mét. */
        if (t->tubeShapeConfig != NULL && t->tubeShapeConfig->noiseWavelength > 0.0f &&
            t->historyCount > 1)
        {
            float rawSpanLen = 0.0f;
            int limit = t->historyCount - 1;
            if (limit > TRAIL_HISTORY_COUNT - 1) limit = TRAIL_HISTORY_COUNT - 1;
            for (int k = 0; k < limit; k++)
            {
                int i0 = GetHistoryNodeIndex(t, k);
                int i1 = GetHistoryNodeIndex(t, k + 1);
                rawSpanLen += Vector3Distance(t->history[i0], t->history[i1]);
            }
            if (t->tubeNoiseSpanLen <= 0.0f)
                t->tubeNoiseSpanLen = rawSpanLen; // mẫu đầu tiên — không có gì để trễ theo
            else
            {
                // Hằng số thời gian ~0.35s: đủ chậm để chặn cú nhảy-một-khung,
                // đủ nhanh để vẫn bám kịp một pha tăng/giảm tốc thật của emitter.
                float tau = 0.35f;
                float alpha = 1.0f - expf(-dt / tau);
                t->tubeNoiseSpanLen += (rawSpanLen - t->tubeNoiseSpanLen) * alpha;
            }
        }

        if (t->type == TRAIL_TYPE_FOLLOWER && t->attachedTransform != NULL)
        {
            Vector3 localPos = t->attachLocalOffset;
            if (t->orbitRadius > 0.0f)
            {
                t->orbitPhase += t->orbitSpeed * dt;
                Vector3 axis = Vector3Normalize(t->orbitAxis);
                if (Vector3LengthSqr(axis) > 0.0f)
                {
                    Quaternion q = QuaternionFromAxisAngle(axis, t->orbitPhase);
                    Vector3 arbitrary = (fabsf(axis.x) > 0.9f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
                    Vector3 ortho = Vector3Normalize(Vector3CrossProduct(axis, arbitrary));
                    Vector3 rotated = Vector3RotateByQuaternion(ortho, q);
                    localPos = (Vector3){localPos.x + rotated.x * t->orbitRadius, localPos.y + rotated.y * t->orbitRadius, localPos.z + rotated.z * t->orbitRadius};
                }
            }
            Vector3 tip = Vector3Transform(localPos, *t->attachedTransform);
            tip = Vector3Add(tip, t->lateralOffset);
            if (!t->hasPrevAttach)
            {
                t->prevAttachPos = tip;
                t->hasPrevAttach = true;
            }

            float moved = Vector3Distance(tip, t->prevAttachPos);
            if (t->teleportSpeed > 0.0f && moved > t->teleportSpeed * dt)
            {
                // Log the ODOMETER too: a cut is visible as a stall, and the
                // question that always follows is whether the surface re-phased
                // with it. Printing the value that answers that is cheaper than
                // the bisection which found this in the first place.
                TraceLog(LOG_INFO,
                         "TRAIL: follower %d cut — transform jumped %.2f m "
                         "(> %.1f m/s * %.4f s); path odometer held at %.2f m.",
                         i, moved, t->teleportSpeed, dt, t->laidDist);
                FollowerCut(t, tip);
            }
            else if (t->frozen)
            {
                t->prevAttachPos = tip;
                t->timeSinceLastFollowerUpdate = 0.0f;
            }
            else if (t->idleSpeed > 0.0f && dt > 1e-6f && (moved / dt) <= t->idleSpeed)
            {
                t->prevAttachPos = tip;
            }
            else if (t->sampleHz > 0.0f)
            {
                float sampleDt = 1.0f / t->sampleHz;
                t->sampleAcc += dt;
                int steps = (int)(t->sampleAcc / sampleDt);
                if (steps > TRAIL_SAMPLE_STEPS_MAX)
                {
                    steps = TRAIL_SAMPLE_STEPS_MAX;
                    t->sampleAcc = 0.0f;
                }
                else
                {
                    t->sampleAcc -= (float)steps * sampleDt;
                }
                for (int n = 1; n <= steps; n++)
                    UpdateFollowerPosition(i, Vector3Lerp(t->prevAttachPos, tip, (float)n / (float)steps));
                t->prevAttachPos = tip;
            }
            else
            {
                UpdateFollowerPosition(i, tip);
                t->prevAttachPos = tip;
            }
        }

        switch (t->type)
        {
        case TRAIL_TYPE_PROJECTILE:
            UpdateProjectilePhysics(i, t, dt, time);
            break;
        case TRAIL_TYPE_WISP:
            UpdateWispPhysics(t, dt, time);
            break;
        case TRAIL_TYPE_PORTAL:
            t->angle += TRAIL_PORTAL_SPIN_DEG_PER_SEC * dt;
            break;
        case TRAIL_TYPE_FOLLOWER:
            UpdateFollowerPhysics(i, t, dt, time);
            break;
        }

        if (t->active && t->onUpdate)
            t->onUpdate(i, dt);
        a++;
    }
}

// Forward declaration — DrawTrailRibbon is defined after the layered draw
// functions but called from DrawLayeredRibbon.
static void DrawTrailRibbon(const TrailEntity *t, const RibbonPoint *points,
                            int count, Texture2D texture, Camera3D camera);

/* A layer stack authored for an ADDITIVE trail carries EMISSION WEIGHTS, not
 * coverage. VFX_RIBBON_MAIN's three layers are 0.10 / 0.36 / 0.30 because they
 * SUM as light; reused verbatim as BLEND_ALPHA coverage in the body pass they
 * cap the body at ~0.36, so `scene*(1-cov)` keeps two thirds of the destination
 * and the trail cannot hold its hue over anything bright — measured on a bright
 * clear: peak chroma 0.32 against 0.61 on the night sky, and the body layer ALONE
 * only reached 0.18. `material.bodyOpacity` is the separately-authored coverage
 * for exactly this (see its comment in trail_system.h); the deform path has
 * always honoured it and the classic layered path silently did not.
 *
 * Returns the alpha multiplier this layer should draw the CURRENT pass with.
 * Left at the layer's own weight whenever bodyOpacity is unset (0), so every
 * existing trail keeps its authored look. Deliberately does NOT apply the
 * contrast profile's alpha: the vertex colour picks that up downstream, and
 * applying it here would run the same opacity policy twice — the same reasoning
 * the deform path states at its own bodyOpacity upload. */
static float TrailLayerPassAlphaMul(const TrailEntity *t, const TrailLayer *ly)
{
    float aMul = (ly->alphaMul > 0.0f) ? ly->alphaMul : 1.0f;
    if (s_drawLayerFilter != 0 || !TrailUsesAdditiveBlend(t)) return aMul;
    if (t->material.bodyOpacity <= 0.0f) return aMul;
    return (t->material.bodyOpacity > 1.0f) ? 1.0f : t->material.bodyOpacity;
}

/* Whitening a layer toward 255 is an EMISSION idea — it reads as "hotter than
 * its own colour" only where the result is added to the scene. In the alpha body
 * pass it desaturates the one layer whose whole job is to carry hue, so the body
 * arrives at the compositor already washed out and no amount of coverage can put
 * the colour back. Emission keeps it; body does not. */
static bool TrailLayerWhitensThisPass(const TrailEntity *t)
{
    return !(s_drawLayerFilter == 0 && TrailUsesAdditiveBlend(t) &&
             t->material.bodyOpacity > 0.0f);
}

static void DrawLayeredRibbon(const TrailEntity *t, int drawCount, Texture2D fallbackTex, Camera3D camera)
{
    for (int L = 0; L < t->layerCount; L++)
    {        // Halo/core are emission energy. Putting them into the alpha body
        // stacks three soft quads and erases the authored flow texture.
        if (s_drawLayerFilter == 0 && TrailUsesAdditiveBlend(t) &&
            t->layerCount >= 2 && L != 1) continue;
        const TrailLayer *ly = &t->layers[L];
        float wMul = (ly->widthMul > 0.0f) ? ly->widthMul : 1.0f;
        float aMul = TrailLayerPassAlphaMul(t, ly);
        float sMul = (ly->scrollMul != 0.0f) ? ly->scrollMul : 1.0f;
        for (int h = 0; h < drawCount; h++)
        {
            scratchLayer[h] = scratchOuter[h];
            scratchLayer[h].halfWidth *= wMul;
            // Deform trails keep texcoord.y = normalized seg (wave envelope);
            // their texture scroll lives in the packed-material shader.
            if (!TrailUsesDeformShader(t))
                scratchLayer[h].v -= t->uvScrollOffset * sMul;
            float a = (float)scratchOuter[h].tint.a * aMul;
            if (ly->headAlphaPow > 0.0f)
                a *= powf(scratchSegRatio[h], ly->headAlphaPow);
            if (a < 0.0f)
                a = 0.0f;
            if (a > 255.0f)
                a = 255.0f;
            Color col = scratchOuter[h].tint;
            if (ly->whiten > 0.0f && TrailLayerWhitensThisPass(t))
            {
                float w = (ly->whiten > 1.0f) ? 1.0f : ly->whiten;
                col.r = (unsigned char)(col.r + (255 - col.r) * w);
                col.g = (unsigned char)(col.g + (255 - col.g) * w);
                col.b = (unsigned char)(col.b + (255 - col.b) * w);
            }
            col.a = (unsigned char)a;
            scratchLayer[h].tint = col;
        }
        Texture2D tex = (ly->texture != NULL) ? *ly->texture : fallbackTex;
        DrawTrailRibbon(t, scratchLayer, drawCount, tex, camera);
    }
}

static void DrawLayeredTube(const TrailEntity *t, int drawCount, Texture2D fallbackTex)
{
    if (drawCount < 2)
        return;

    static Vector3 path[TRAIL_HISTORY_COUNT];
    int n = drawCount;
    if (n > TRAIL_HISTORY_COUNT)
        n = TRAIL_HISTORY_COUNT;
    for (int i = 0; i < n; i++)
        path[i] = scratchOuter[n - 1 - i].position;

    int radial = t->tubeRadialSegs;
    if (radial < 3)
        radial = 3;
    if (radial > TUBE_MESH_MAX_RADIAL)
        radial = TUBE_MESH_MAX_RADIAL;
    /* SỐ LÁT HÌNH HỌC, không phải số node lịch sử — hai câu hỏi khác nhau,
     * xem doc `tubeGeomSegs` ở trail_system.h. 0 = rơi về tubeMaxRings như
     * trước. Path vẫn có đủ n node; PMTubeSamplePath lấy mẫu lại theo chiều
     * dài cung nên ít lát hơn số node là hoàn toàn hợp lệ. */
    int segs = (t->tubeGeomSegs > 0) ? t->tubeGeomSegs
             : ((t->tubeMaxRings > 0) ? t->tubeMaxRings : TRAIL_TUBE_RINGS_DEFAULT);
    if (segs > TUBE_MESH_MAX_SEGMENTS)
        segs = TUBE_MESH_MAX_SEGMENTS;
    if (segs < 2)
        segs = 2;

    /* MỘT trong ba loại mesh, do CALLER chọn — ba module hình là độc lập với
     * nhau, nên caller chọn LOẠI MESH chứ không chọn tham số của một đường bao
     * dùng chung. Không có mặc định: trail nào bật TRAIL_SHAPE_TUBE mà không
     * khai báo hình thì không có hình để dựng, và nói ra điều đó.
     *
     * Trạng thái THEO THỜI GIAN (noiseAmp, noiseOffset) vẫn của trail —
     * noiseOffset là thứ đẩy vùng cuộn trào chạy DỌC thân trên cùng đồng hồ mà
     * sheet đang cuộn. HÌNH DẠNG là của caller. */
    float runNoiseAmp = t->tubeNoiseAmp;
    /* Cả hai đồng hồ của deform bị chặn cùng lúc, nếu không thì "đóng băng" chỉ
     * dừng một nửa: noiseOffset đẩy trường DỌC thân, còn buildTime chạy trục W
     * của noise (nó cuộn tại chỗ). Dừng một cái vẫn còn chuyển động, và đó lại
     * đúng là thứ nhìn giống hệt "vẫn xoay". */
    float runNoiseOffset = t->tubeDeformFrozen ? 0.0f : (-t->uvScrollOffset * 0.5f);
    float buildTime = t->tubeDeformFrozen ? 0.0f : TimeFX_Elapsed();
    const unsigned char *runPixels = (const unsigned char *)s_tubeNoiseImg.data;
    int runW = s_tubeNoiseImg.width, runH = s_tubeNoiseImg.height;

    static PMDropletMesh dropMesh;
    static PMTubeMesh tubeMesh;
    PMDropletConfig dropCfg;
    PMTubeConfig tubeCfg;
    int shape = 0; /* 1 = giọt nước, 2 = ống */
    if (t->dropletConfig != NULL)
    {
        shape = 1;
        dropCfg = *t->dropletConfig;
        dropCfg.noiseAmp = runNoiseAmp;
        dropCfg.noiseOffset = runNoiseOffset;
        if (dropCfg.noisePixels == NULL)
        { dropCfg.noisePixels = runPixels; dropCfg.noiseImgW = runW; dropCfg.noiseImgH = runH; }
    }
    else if (t->tubeShapeConfig != NULL)
    {
        shape = 2;
        tubeCfg = *t->tubeShapeConfig;
        tubeCfg.noiseAmp = runNoiseAmp;
        // *tubeCfg.noiseOffsetScrollMul: caller-decided (PMTubeConfig doc ở
        // procedural_mesh_utils.h). Cột đứng yên cần đồng hồ THẬT này vì t
        // của nó không mang nghĩa tuổi vật chất; một trail đang di chuyển đã
        // có tuổi vật chất THẬT từ chính chuyển động, cộng thêm đồng hồ này
        // là hai nguồn chuyển động không ăn khớp — nhân 0 tắt hẳn, không đổi
        // gì cho ai đang để mặc định 1.0 (PMTube_DefaultConfig).
        tubeCfg.noiseOffset = runNoiseOffset * tubeCfg.noiseOffsetScrollMul;
        // Bản làm mịn theo thời gian (UpdateTrailSystem, dt thật) thay cho
        // spanLen thô mỗi khung — xem doc noiseSpanLenOverride ở
        // procedural_mesh_utils.h và tubeNoiseSpanLen ở trail_system.h. 0 khi
        // noiseWavelength chưa bật (UpdateTrailSystem không tính) nên không
        // đổi gì cho caller không dùng cờ này.
        tubeCfg.noiseSpanLenOverride = t->tubeNoiseSpanLen;
        if (tubeCfg.noisePixels == NULL)
        { tubeCfg.noisePixels = runPixels; tubeCfg.noiseImgW = runW; tubeCfg.noiseImgH = runH; }
    }
    else
    {
        /* Im lặng ở đây là chỗ mọi lỗi hình học của module này từng trốn. */
        TraceLog(LOG_WARNING,
                 "TRAIL: TRAIL_SHAPE_TUBE nhưng không có dropletConfig lẫn "
                 "tubeShapeConfig — không có hình để dựng, bỏ qua");
        return;
    }

    float headR = scratchOuter[0].halfWidth;
    if (headR <= 1e-4f)
        return;

    float arcLen = 0.0f;
    for (int i = 1; i < n; i++)
        arcLen += Vector3Distance(path[i], path[i - 1]);

    // The mesh is rebuilt from a sliding history window every frame.  Its
    // local arc length therefore changes as nodes enter at the head and leave
    // at the tail.  Anchor UVs in accumulated trail distance instead of that
    // transient local range, otherwise the reparameterisation can visually
    // cancel uvScrollOffset whenever the follower moves.
    int tailNode = NodeIndexForSegRatio(t, drawCount, drawCount - 1);
    int headNode = NodeIndexForSegRatio(t, drawCount, 0);

    /* NEO VẬT CHẤT CHO TRƯỜNG NHIỄU — cùng đúng một cách UV ngay trên đã neo,
     * và vì đúng một lý do (comment ngay trên: cửa sổ history trượt mỗi khung
     * nên phạm vi CỤC BỘ là tạm thời, phải neo vào QUÃNG ĐƯỜNG TÍCH LUỸ).
     *
     * Thiếu nó, toạ độ nhiễu là `tNoise = t * spanLen / wavelength`, tức
     * "khoảng cách tính từ node CŨ NHẤT" — mà node cũ nhất bị bỏ đi liên tục,
     * nên cái mốc đó TRƯỢT theo. Một cục phình vì thế đứng yên so với HÌNH
     * ống chứ không so với KHỐI KHÍ: cả cụm hoa văn bị kéo lê nguyên khối
     * cùng ống thay vì trôi ngược qua thân khi vật chất già đi. Đó đúng là
     * cái mesh_deform.h cảnh báo ở doc tham số `mat`: "passing surf.y here
     * instead is the mistake that makes a churning body read as a
     * pre-squeezed shape being dragged" — ở đây không phải surf.y, nhưng là
     * cùng một sai lầm: một toạ độ đo từ mốc di động.
     *
     * nodeUV[] = laidDist, quãng đường tích luỹ lúc node được đặt (:1295) —
     * nhãn vật chất thật. Cộng nó vào biến tNoise (đo từ đuôi) cho ra quãng
     * đường TUYỆT ĐỐI tại từng vành, chia cho wavelength là ra đúng toạ độ
     * vật chất theo mét.
     *
     * Chỉ khi noiseWavelength > 0: caller không bật cờ đó thì tNoise = t
     * (phân số), cộng một số đo bằng mét vào là vô nghĩa — và đó là mọi
     * caller cũ (cột khói, spark, ember), không đổi một bit nào.
     *
     * fmodf 8192: nodeUV tăng không giới hạn, và phần thập phân của nó là
     * thứ lattice đọc — cùng lý do và cùng hằng số dòng :1968 đã dùng. */
    if (shape == 2 && tubeCfg.noiseWavelength > 0.0f)
        tubeCfg.noiseOffset += fmodf(t->nodeUV[tailNode], 8192.0f) / tubeCfg.noiseWavelength;

    if (shape == 1)
        PMDroplet_BuildAlongPath(&dropMesh, path, n, headR, 0.0f, 1.0f,
                                 buildTime, segs, radial, &dropCfg);
    else if (shape == 2)
        PMTube_BuildAlongPath(&tubeMesh, path, n, headR, 0.0f, 1.0f,
                              buildTime, segs, radial, &tubeCfg);

    for (int L = 0; L < t->layerCount; L++)
    {
        if (s_drawLayerFilter == 0 && TrailUsesAdditiveBlend(t) &&
            t->layerCount >= 2 && L != 1) continue;
        const TrailLayer *ly = &t->layers[L];
        float aMul = TrailLayerPassAlphaMul(t, ly);
        VFXContrastLayer contrastLayer =
            (s_drawLayerFilter == 0 ||
             (s_drawLayerFilter < 0 && !TrailUsesAdditiveBlend(t)))
                ? VFX_CONTRAST_BODY
                : VFX_CONTRAST_EMISSION;
        Color col = VFXContrast_ApplyColor(scratchOuter[0].tint,
                                            t->material.contrastProfile,
                                            contrastLayer);
        float a = (float)col.a * aMul;
        if (a > 255.0f)
            a = 255.0f;
        if (ly->whiten > 0.0f && TrailLayerWhitensThisPass(t))
        {
            float w = (ly->whiten > 1.0f) ? 1.0f : ly->whiten;
            col.r = (unsigned char)(col.r + (255 - col.r) * w);
            col.g = (unsigned char)(col.g + (255 - col.g) * w);
            col.b = (unsigned char)(col.b + (255 - col.b) * w);
        }
        Texture2D tex = (ly->texture != NULL && ly->texture->id != 0)
                            ? *ly->texture
                            : ((s_tubeFlatTex.id != 0) ? s_tubeFlatTex : fallbackTex);
        float sMul = (ly->scrollMul != 0.0f) ? ly->scrollMul : 1.0f;
        float mpt = (t->uvMetresPerTile > 0.01f) ? t->uvMetresPerTile : 1.0f;
        float uvBase = t->nodeUV[tailNode] / mpt;
        float tiles = (t->nodeUV[headNode] - t->nodeUV[tailNode]) / mpt;
        // A just-spawned follower can have a one-node/near-zero-distance
        // window; keep a visible first tile until accumulated distance exists.
        if (tiles <= 0.0f)
            tiles = arcLen / mpt;
        if (tiles < 0.5f)
            tiles = 0.5f;
        rlSetTexture(tex.id);
        float uvOff = uvBase - t->uvScrollOffset * sMul;
        if (shape == 2 && TrailUsesVolumeShader(t))
        {
            /* Mặt nạ tắt dần hai đầu đi bằng MÀU ĐỈNH, nên không gọi
             * rlColor4ub ở đây — màu nền đi vào qua tham số. Chân tắt nhanh
             * (khói phải dính vào nguồn), ngọn tan chậm hơn. */
            Color base = (Color){col.r, col.g, col.b, (unsigned char)a};
            PMTube_DrawFaded(&tubeMesh, tiles, uvOff, base, 0.10f, 0.72f, mpt);
        }
        else
        {
            rlColor4ub(col.r, col.g, col.b, (unsigned char)a);
            if (shape == 1)
                PMDroplet_DrawEx(&dropMesh, tiles, uvOff);
            else if (shape == 2)
                PMTube_DrawEx(&tubeMesh, tiles, uvOff);
        }
    }
    rlSetTexture(0);
    rlColor4ub(255, 255, 255, 255);
}

// Per-instance deform + packed-material uniforms, pushed just before this
// trail's geometry inside its render group. Wave params are per-trail state,
// so they live here — only the shared u_time clock stays at group level.
static void ApplyDeformUniforms(const TrailEntity *t, Camera3D camera)
{
    if (s_deformShader.id == 0)
        return;
    const DeformLocs *L = GetCachedDeformLocs(s_deformShader);
    const TrailDeformConfig *d = &t->deform;
    const TrailMaterialConfig *m = &t->material;
    float contrastParams[4];
    VFXContrast_GetShaderParams(m->contrastProfile, contrastParams);
    if (L->contrastParams >= 0)
        SetShaderValue(s_deformShader, L->contrastParams, contrastParams,
                       SHADER_UNIFORM_VEC4);

    if (L->deformMode >= 0) SetShaderValue(s_deformShader, L->deformMode, &d->mode, SHADER_UNIFORM_FLOAT);
    if (L->waveAmpA >= 0) SetShaderValue(s_deformShader, L->waveAmpA, d->ampA, SHADER_UNIFORM_VEC3);
    if (L->waveAmpB >= 0) SetShaderValue(s_deformShader, L->waveAmpB, d->ampB, SHADER_UNIFORM_VEC3);
    if (L->waveFreq >= 0) SetShaderValue(s_deformShader, L->waveFreq, d->freq, SHADER_UNIFORM_VEC3);
    if (L->waveSpeed >= 0) SetShaderValue(s_deformShader, L->waveSpeed, d->speed, SHADER_UNIFORM_VEC3);
    if (L->wavePhase >= 0) SetShaderValue(s_deformShader, L->wavePhase, &d->phase, SHADER_UNIFORM_FLOAT);
    if (L->waveStrength >= 0) SetShaderValue(s_deformShader, L->waveStrength, &d->strength, SHADER_UNIFORM_FLOAT);
    if (L->curlScale >= 0) SetShaderValue(s_deformShader, L->curlScale, &d->curlScale, SHADER_UNIFORM_FLOAT);
    {
        float env[2] = {d->envHead, d->envTail};
        if (L->waveEnv >= 0) SetShaderValue(s_deformShader, L->waveEnv, env, SHADER_UNIFORM_VEC2);
    }
    // MODE 1 (sin-multi) ONLY — core/deform's first GLSL mirror,
    // 05/08/2026 (core/deform/shaders/mesh_deform.glsl). 2 octaves along
    // `side` (MESH_DEFORM_DIR_AXIS) + 2 along u_stripNormal
    // (MESH_DEFORM_DIR_TANGENT) — down from the shader's old hardcoded 3+3,
    // to fit MeshDeformField's MESH_DEFORM_MAX_LAYERS=4 budget without
    // changing that shared layout. Modes 2-4 (curl3/helix/noise) still read
    // the plain u_waveAmpA/u_waveFreq/u_waveSpeed/u_curlScale pushed above
    // — untouched, still bespoke, see trail_deform.vs's own comment on why
    // they do not fit this module's layer-sum model.
    if (d->mode >= 0.5f && d->mode < 1.5f)
    {
        // amplitude = 2x the caller's ampA/ampB: MESH_DEFORM_SINE's own
        // formula is `raw = 0.5 + 0.5*sin(...)`, so `(raw-0.5)*amplitude`
        // works out to `0.5*sin(...)*amplitude` — HALF the amplitude a
        // naive `amplitude*sin(...)` would give. Doubling here is what
        // makes the packed layer's FINAL contribution equal
        // `ampA.x*sin(...)`, matching what the old inline code wrote
        // directly. See core/deform/mesh_deform.c's MeshDeform_EvaluateLayer
        // SINE branch — this is a property of that formula, not something
        // specific to this call site.
        //
        // speed is in TURNS/second here (core/uv/uv_deform.h's convention,
        // shared via UVDeform_SinePhase), not the old inline code's
        // radians/second — a deliberate, documented deviation (see
        // trail_deform.vs's own comment at the call site) since nothing
        // currently spawns mode 1 to have a fidelity bar to clear.
        MeshDeformField warp;
        MeshDeform_Clear(&warp);
        MeshDeform_AddLayer(&warp, (MeshDeformLayer){
            .kind = MESH_DEFORM_SINE, .direction = MESH_DEFORM_DIR_AXIS,
            .tiling = {0.0f, d->freq[0]}, .amplitude = 2.0f * d->ampA[0],
            .speed = d->speed[0], .phase = d->phase, .env = UV_ENV_NONE});
        MeshDeform_AddLayer(&warp, (MeshDeformLayer){
            .kind = MESH_DEFORM_SINE, .direction = MESH_DEFORM_DIR_AXIS,
            .tiling = {0.0f, d->freq[1]}, .amplitude = 2.0f * d->ampA[1],
            .speed = d->speed[1], .phase = d->phase, .env = UV_ENV_NONE});
        MeshDeform_AddLayer(&warp, (MeshDeformLayer){
            .kind = MESH_DEFORM_SINE, .direction = MESH_DEFORM_DIR_TANGENT,
            .tiling = {0.0f, d->freq[0]}, .amplitude = 2.0f * d->ampB[0],
            .speed = d->speed[0], .phase = d->phase, .env = UV_ENV_NONE});
        MeshDeform_AddLayer(&warp, (MeshDeformLayer){
            .kind = MESH_DEFORM_SINE, .direction = MESH_DEFORM_DIR_TANGENT,
            .tiling = {0.0f, d->freq[1]}, .amplitude = 2.0f * d->ampB[1],
            .speed = d->speed[1], .phase = d->phase, .env = UV_ENV_NONE});
        // Field-level amplitude/timeScale stay neutral (1.0/default-via-
        // Clear) — the x2 compensation and per-octave speed already live
        // in each layer, so nothing should scale them again here.
        MeshDeform_Apply(&warp, s_deformShader, &L->meshWarp);
    }
    {
        // Strip plane normal, mirroring ribbon_strip.c's ResolveFrameNormals:
        // view direction for camera-facing, world-up for RIBBON_WORLD_UP,
        // fixedNormal for RIBBON_FIXED_NORMAL.
        Vector3 n;
        if (t->ribbonMode == RIBBON_WORLD_UP)
            n = (Vector3){0.0f, 1.0f, 0.0f};
        else if (t->ribbonMode == RIBBON_FIXED_NORMAL)
            n = Vector3Normalize(t->fixedNormal);
        else
            n = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        if (L->stripNormal >= 0) SetShaderValue(s_deformShader, L->stripNormal, &n, SHADER_UNIFORM_VEC3);
    }

    {
        // Which of the two render passes this draw belongs to. DrawTrailEntities
        // (the combined, non-split entry point) reports -1; treat that as the
        // EMISSION formula, which is what the combined path has always drawn.
        // Getting this wrong is invisible in a dark scene and washes the whole
        // effect out in a bright one — see trail_deform.fs's header.
        // 2 = the EMISSION pass drawn under BLEND_ALPHA_PREMULTIPLY. The blend
        // state and the fragment formula are ONE decision: additive is
        // (SRC_ALPHA, ONE), so the hardware applies coverage; premultiplied is
        // (ONE, ONE_MINUS_SRC_ALPHA) and the shader must apply it instead.
        // Selecting the blend without telling the shader is a 1/alpha
        // brightening of every soft edge, which is what it measured as.
        BlendMode srcBm = t->useCustomBlendMode
                              ? t->blendMode
                              : ((t->blendMode > 0) ? t->blendMode : BLEND_ADDITIVE);
        float pass = (s_drawLayerFilter == 0)
                         ? 0.0f
                         : ((srcBm == BLEND_ALPHA_PREMULTIPLY) ? 2.0f : 1.0f);
        // Vertex colour already carries the profile's alpha multiplier. Keep
        // the separately-authored body coverage unchanged so a deform trail
        // does not apply the same opacity policy twice.
        float bodyOpacity = (m->bodyOpacity > 0.0f) ? m->bodyOpacity : 0.0f;
        if (bodyOpacity > 1.0f) bodyOpacity = 1.0f;
        if (L->renderPass >= 0) SetShaderValue(s_deformShader, L->renderPass, &pass, SHADER_UNIFORM_FLOAT);
        if (L->bodyOpacity >= 0) SetShaderValue(s_deformShader, L->bodyOpacity, &bodyOpacity, SHADER_UNIFORM_FLOAT);
    }

    if (L->matMode >= 0) SetShaderValue(s_deformShader, L->matMode, &m->mode, SHADER_UNIFORM_FLOAT);
    if (L->wispMix >= 0) SetShaderValue(s_deformShader, L->wispMix, &m->wispMix, SHADER_UNIFORM_FLOAT);
    if (L->dissolve >= 0) SetShaderValue(s_deformShader, L->dissolve, &m->dissolve, SHADER_UNIFORM_FLOAT);
    if (L->dissolveSoft >= 0) SetShaderValue(s_deformShader, L->dissolveSoft, &m->dissolveSoft, SHADER_UNIFORM_FLOAT);
    {
        float tiling[2] = {m->tilingX, m->tilingY};
        if (L->tiling >= 0) SetShaderValue(s_deformShader, L->tiling, tiling, SHADER_UNIFORM_VEC2);
    }
    {
        float pan[4] = {m->panCoarse, m->panFine, 0.0f, 0.0f};
        if (L->panSpeed >= 0) SetShaderValue(s_deformShader, L->panSpeed, pan, SHADER_UNIFORM_VEC4);
    }
    {
        // Tail dissolve ramp in segment space; disabled when start >= end.
        float a = m->tailFadeA >= m->tailFadeB ? 1.0f : m->tailFadeA;
        float b = m->tailFadeA >= m->tailFadeB ? 1.0f : m->tailFadeB;
        if (L->tailFadeA >= 0) SetShaderValue(s_deformShader, L->tailFadeA, &a, SHADER_UNIFORM_FLOAT);
        if (L->tailFadeB >= 0) SetShaderValue(s_deformShader, L->tailFadeB, &b, SHADER_UNIFORM_FLOAT);
    }

    // ── SIN-WAVE STRAND TRAIL (material mode 2) ─────────────────────────────
    if (m->mode >= 1.5f)
    {
        // THE WARP HALF (w0/w1/w2), generalised 05/08/2026 onto core/uv's
        // UVDeformField — was a raw u_sinWave vec4 the shader detuned by hand
        // per bundle. UVDeform_LayerOffset (uv_deform.glsl) already computes
        // exactly `sin(fract(drive*freq + t*speed)*TAU + phase) * amp`, with
        // `amp = layerAmplitude * envelope(mat.y)` built in — i.e. `ramp`
        // (the head-weld gate) was always this function's own envelope term,
        // not something mode 2 had to hand-roll. The three bundles' detune
        // (spread) is a per-instance CONSTANT, so it folds into each layer's
        // packed freq/speed/phase/amplitude once here instead of being
        // reapplied per fragment — algebraically identical to the old
        // per-bundle multiply, though not guaranteed bit-identical (the
        // generic path multiplies amplitude*envelope in a fixed order, so a
        // detuned layer's final product can differ by up to 1 ULP from the
        // old `(amp*ramp)*detune` grouping — invisible on screen, unlike the
        // structural bit-identity trail_volume.fs's migration required).
        UVDeformField warp;
        UVDeform_Clear(&warp);
        float spread = m->waveSpread;
        UVDeform_AddLayer(&warp, (UVDeformLayer){
            .kind = UV_DEFORM_SINE, .driveAxis = 0, .outAxis = 0,
            .amplitude = m->waveAmp, .frequency = m->waveFreq,
            .speed = m->waveTravel, .phase = d->phase,
            .env = UV_ENV_HEAD_WELD, .envAxis = 1,
            .envStart = 0.0f, .envEnd = d->envHead});
        UVDeform_AddLayer(&warp, (UVDeformLayer){
            .kind = UV_DEFORM_SINE, .driveAxis = 0, .outAxis = 0,
            .amplitude = m->waveAmp * (1.0f - 0.28f * spread),
            .frequency = m->waveFreq * (1.0f + 0.73f * spread),
            .speed = m->waveTravel * 1.41f, .phase = d->phase * 2.3f,
            .env = UV_ENV_HEAD_WELD, .envAxis = 1,
            .envStart = 0.0f, .envEnd = d->envHead});
        UVDeform_AddLayer(&warp, (UVDeformLayer){
            .kind = UV_DEFORM_SINE, .driveAxis = 0, .outAxis = 0,
            .amplitude = m->waveAmp * (1.0f + 0.25f * spread),
            .frequency = m->waveFreq * (1.0f - 0.39f * spread),
            .speed = m->waveTravel * 0.67f, .phase = d->phase * 4.1f,
            .env = UV_ENV_HEAD_WELD, .envAxis = 1,
            .envStart = 0.0f, .envEnd = d->envHead});
        UVDeform_Apply(&warp, s_deformShader, &L->uvWarp);
        // FIXED INDICES 0/1/2 IN THE SHADER, NOT LOOPED — trail_deform.fs
        // reads u_uvField[0..2]/[3..5]/[6..8] directly (w0/w1/w2 each feed a
        // SPECIFIC sheet channel downstream, r/g/r — they are named bundles,
        // not an interchangeable summed stack). UVDeform_PackGPU COMPACTS: a
        // layer whose amplitude rounds to ~0 is dropped and every later
        // layer's GPU index shifts down (core/uv/uv_deform.c, "GPU indices
        // are therefore NOT CPU indices"). All three amplitudes here are
        // waveAmp scaled by a factor near 1 (1 ± ~0.3*spread, spread < 1), so
        // none of the three known styles (ENERGY/SMOKE) ever get close to
        // that threshold — but a FUTURE style must keep waveAmp meaningfully
        // non-zero, or the fixed-index read on the shader side silently
        // pairs the wrong bundle's detune with the wrong sampled channel.
        float hdrGain = (m->hdrGain > 0.0f) ? m->hdrGain : 1.0f;
        // Energy trails keep their compact radiance inside the body shader to
        // avoid a duplicate geometry pass; hdrGain is still an EMISSION
        // property even when the render target is VFXBody.
        if (TrailUsesAdditiveBlend(t))
            hdrGain = VFXContrast_ApplyEmissionIntensity(
                hdrGain, m->contrastProfile);
        float band[4] = {m->bundleWidth, m->edgeSoft, hdrGain,
                         (m->strandGain > 0.0f) ? m->strandGain : 1.0f};
        float strandFlow[4] = {m->flowStrength, m->bundleWeight, m->stretchUV, 0.0f};
        if (L->bandShape >= 0) SetShaderValue(s_deformShader, L->bandShape, band, SHADER_UNIFORM_VEC4);
        if (L->strandFlow >= 0) SetShaderValue(s_deformShader, L->strandFlow, strandFlow, SHADER_UNIFORM_VEC4);
        // ZERO MEANS UNSET, and the fallbacks are the literals trail_deform.fs
        // used to carry (0.18 half-width, 0.45 density gate). Resolved HERE
        // rather than in the shader so a material that leaves them at zero
        // cannot render differently depending on which shader variant loads.
        float coreShape[2] = {(m->coreHalfWidth > 0.0f) ? m->coreHalfWidth : 0.18f,
                              (m->coreDensityGate > 0.0f) ? m->coreDensityGate : 0.45f};
        if (L->coreShape >= 0) SetShaderValue(s_deformShader, L->coreShape, coreShape, SHADER_UNIFORM_VEC2);

        float tailShape[4] = {m->tailStagger, m->tailDissolve,
                              (m->tailNarrow > 0.0f) ? m->tailNarrow : 1.0f, 0.0f};
        if (L->tailShape >= 0) SetShaderValue(s_deformShader, L->tailShape, tailShape, SHADER_UNIFORM_VEC4);

        // The wave is anchored in METRES of the path the emitter actually
        // laid, not in normalized segment space: nodeUV[] already carries the
        // cumulative distance per node, so the tail node's distance is the
        // arc origin and the head-minus-tail span is the length the fragment
        // stage interpolates across (texcoord.y grows head -> tail, so the
        // shader SUBTRACTS the span from the head distance). Anchoring here is
        // the whole reason the crests stand still on the path while only time
        // moves them — in segment space a growing trail stretches the entire
        // waveform and the ribbon reads as a rigid rope being swung.
        float arc[2] = {0.0f, 1.0f};
        if (t->historyCount > 1)
        {
            int drawCount = CalculateDrawCount(t);
            if (drawCount > 1)
            {
                int headNode = NodeIndexForSegRatio(t, drawCount, 0);
                int tailNode = NodeIndexForSegRatio(t, drawCount, drawCount - 1);
                float span = t->nodeUV[headNode] - t->nodeUV[tailNode];
                if (span > 0.001f)
                {
                    // nodeUV is CUMULATIVE distance and never resets, so a
                    // long-lived trail on a moving emitter walks it into the
                    // tens of thousands of metres, where a float32 can no
                    // longer resolve a fraction of a texture tile. Fold it.
                    // 8192 m keeps the shader's fract() inputs precise and is
                    // ~30 minutes of continuous movement; the fold re-phases
                    // the waves once when it wraps, which is the honest cost
                    // of not letting the arithmetic decay instead.
                    arc[0] = fmodf(t->nodeUV[headNode], 8192.0f);
                    arc[1] = span;
                }
            }
        }
        if (L->pathArc >= 0) SetShaderValue(s_deformShader, L->pathArc, arc, SHADER_UNIFORM_VEC2);

        VFXContrastLayer hotLayer = TrailUsesAdditiveBlend(t)
                                        ? VFX_CONTRAST_EMISSION
                                        : VFX_CONTRAST_BODY;
        Color hotColor = VFXContrast_ApplyColor(
            m->hotColor, m->contrastProfile, hotLayer);
        float hot[3] = {hotColor.r / 255.0f, hotColor.g / 255.0f,
                        hotColor.b / 255.0f};
        if (m->hotColor.r == 0 && m->hotColor.g == 0 && m->hotColor.b == 0)
        {
            Color fallbackHot = VFXContrast_ApplyColor(
                WHITE, m->contrastProfile, hotLayer);
            hot[0] = fallbackHot.r / 255.0f;
            hot[1] = fallbackHot.g / 255.0f;
            hot[2] = fallbackHot.b / 255.0f;
        }
        if (L->colHot >= 0) SetShaderValue(s_deformShader, L->colHot, hot, SHADER_UNIFORM_VEC3);

        // Tail colour. Unset (black) means "no along-trail ramp": fall back to
        // the head tint so an author who never sets it gets a flat hue rather
        // than a trail that fades into black.
        Color tailColor = VFXContrast_ApplyColor(
            m->tailColor, m->contrastProfile, VFX_CONTRAST_BODY);
        float tailCol[3] = {tailColor.r / 255.0f, tailColor.g / 255.0f,
                            tailColor.b / 255.0f};
        if (m->tailColor.r == 0 && m->tailColor.g == 0 && m->tailColor.b == 0)
        {
            Color fallbackTail = VFXContrast_ApplyColor(
                t->tint, m->contrastProfile, VFX_CONTRAST_BODY);
            tailCol[0] = fallbackTail.r / 255.0f;
            tailCol[1] = fallbackTail.g / 255.0f;
            tailCol[2] = fallbackTail.b / 255.0f;
        }
        if (L->colTail >= 0) SetShaderValue(s_deformShader, L->colTail, tailCol, SHADER_UNIFORM_VEC3);
    }
}

// Ribbon submission router: deform trails go through the Deformed variant
// (side vector in the normal slot, seg in texcoord.y), classic trails keep
// the old path untouched.
static void DrawTrailRibbon(const TrailEntity *t, const RibbonPoint *points, int count,
                            Texture2D texture, Camera3D camera)
{
    VFXContrastLayer contrastLayer =
        (s_drawLayerFilter == 0 ||
         (s_drawLayerFilter < 0 && !TrailUsesAdditiveBlend(t)))
            ? VFX_CONTRAST_BODY
            : VFX_CONTRAST_EMISSION;
    if (TrailUsesDeformShader(t))
        DrawRibbonStripDeformedProfiledEx(
            points, count, texture, camera, t->ribbonMode, t->fixedNormal,
            t->material.contrastProfile, contrastLayer);
    else
        DrawRibbonStripProfiledEx(
            points, count, texture, camera, t->ribbonMode, t->fixedNormal,
            t->material.contrastProfile, contrastLayer);
}

static void DrawTrailGeometry(TrailEntity *t, Camera3D camera, const TrailCameraBasis *camBasis, float time)
{
    float lifeRatio = t->lifetime / t->maxLifetime;
    Color c = t->tint;

    { // u_coreStrength: 0 khi disableInnerCore, 1 khi có core
        Shader sh = ResolveShader(t);
        int loc = GetCachedCoreStrLoc(sh);
        if (loc >= 0)
        {
            float v = t->disableInnerCore ? 0.0f : 1.0f;
            SetShaderValue(sh, loc, &v, SHADER_UNIFORM_FLOAT);
        }
    }

    if (TrailUsesDeformShader(t))
        ApplyDeformUniforms(t, camera);

    if (t->type == TRAIL_TYPE_PROJECTILE)
    {
        if (t->historyCount > 1)
        {
            int drawCount = CalculateDrawCount(t);

            for (int h = 0; h < drawCount; h++)
            {
                float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
                float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                                  ? ComputeWidthEnvelopeFast(t, segRatio, time)
                                  : segRatio * (0.55f + 0.45f * segRatio);
                Color nodeColor = t->gradient ? ColorGradient_Sample(t->gradient, segRatio) : c;
                if (t->alphaCurve)
                {
                    float aMul = SkillCurve_Eval(t->alphaCurve, segRatio);
                    nodeColor.a = (unsigned char)((float)nodeColor.a * (aMul < 0.0f ? 0.0f : (aMul > 1.0f ? 1.0f : aMul)));
                }

                Vector3 posNode = GetInterpolatedPosition(t, segRatio);
                if (t->distortionStrength > 0.0f)
                {
                    float dTime = time * t->distortionSpeed;
                    float nX = (Noise_Perlin3D(posNode.x * 0.8f + dTime, posNode.y * 0.8f, posNode.z * 0.8f) - 0.5f) * 2.0f;
                    float nY = (Noise_Perlin3D(posNode.x * 0.8f, posNode.y * 0.8f + dTime, posNode.z * 0.8f + 17.7f) - 0.5f) * 2.0f;
                    float nZ = (Noise_Perlin3D(posNode.x * 0.8f + 31.4f, posNode.y * 0.8f, posNode.z * 0.8f + dTime) - 0.5f) * 2.0f;
                    posNode.x += nX * t->distortionStrength;
                    posNode.y += nY * t->distortionStrength;
                    posNode.z += nZ * t->distortionStrength;
                }

                scratchOuter[h].position = posNode;
                scratchOuter[h].halfWidth = t->thickness * TRAIL_PROJECTILE_OUTER_WIDTH_MUL * taper;
                // Deform trails carry the normalized segment (0 = head, 1 = tail)
                // in texcoord.y — the deform VS needs it for the wave envelope;
                // texture tiling/scroll moves into the packed-material shader.
                scratchOuter[h].v = TrailUsesDeformShader(t)
                                        ? (float)h / (float)(drawCount - 1)
                                        : (segRatio * t->uvTiling - t->uvScrollOffset);
                scratchOuter[h].tint = (Color){(unsigned char)(segRatio * nodeColor.r), nodeColor.g, nodeColor.b, (unsigned char)((nodeColor.a / 255.0f) * TRAIL_PROJECTILE_OUTER_ALPHA_MAX * lifeRatio)};

                scratchInner[h].position = scratchOuter[h].position;
                scratchInner[h].halfWidth = t->thickness * TRAIL_PROJECTILE_INNER_WIDTH_MUL * taper;
                scratchInner[h].v = scratchOuter[h].v;
                scratchInner[h].tint = (Color){(unsigned char)(segRatio * nodeColor.r), nodeColor.g, nodeColor.b, (unsigned char)(nodeColor.a * lifeRatio)};
            }
            Texture2D ribbonTex = t->sprite.id > 0 ? t->sprite : s_globalTrailTex;
            DrawTrailRibbon(t, scratchOuter, drawCount, ribbonTex, camera);
            if (!t->disableInnerCore)
            {
                DrawTrailRibbon(t, scratchInner, drawCount, ribbonTex, camera);
            }
        }

        Vector3 right = camBasis->right;
        Vector3 up = camBasis->up;

        float vx = t->velocity.x, vy = t->velocity.y, vz = t->velocity.z;
        float len2 = vx * vx + vy * vy + vz * vz;
        float rotation = 0.0f;
        if (len2 > 1e-6f)
        {
            float invL = 1.0f / sqrtf(len2);
            Vector3 vDir = {vx * invL, vy * invL, vz * invL};
            rotation = atan2f(Vector3DotProduct(vDir, up), Vector3DotProduct(vDir, right));
        }

        Color spriteTint = {128, 128, 128, (unsigned char)(255.0f * lifeRatio)};
        Rectangle uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
        if (t->spriteAnim)
            uvRect = SpriteAnim_CalculateUV(t->spriteAnim, t->maxLifetime - t->lifetime, NULL);
        float quadHeight = t->spriteAnim ? (t->length * TRAIL_PROJECTILE_QUAD_LENGTH_MUL) : (t->thickness * TRAIL_PROJECTILE_QUAD_THICK_MUL);

        if (t->sprite.id > 0)
            DrawCameraFacingQuad(camBasis, t->position, t->length * TRAIL_PROJECTILE_QUAD_LENGTH_MUL, quadHeight, rotation, spriteTint, t->sprite, uvRect);
    }
    else if (t->type == TRAIL_TYPE_WISP)
    {
        if (t->historyCount > 1)
        {
            int drawCount = CalculateDrawCount(t);

            for (int h = 0; h < drawCount; h++)
            {
                float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
                float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                                  ? ComputeWidthEnvelopeFast(t, segRatio, time)
                                  : ComputeWispStyleTaper(segRatio);
                Color nodeColor = c;
                if (t->gradient)
                {
                    Color gradCol = ColorGradient_Sample(t->gradient, segRatio);
                    nodeColor = (Color){(unsigned char)((gradCol.r / 255.0f) * c.r), (unsigned char)((gradCol.g / 255.0f) * c.g), (unsigned char)((gradCol.b / 255.0f) * c.b), (unsigned char)((gradCol.a / 255.0f) * c.a)};
                }
                if (t->alphaCurve)
                {
                    float aMul = SkillCurve_Eval(t->alphaCurve, segRatio);
                    nodeColor.a = (unsigned char)((float)nodeColor.a * (aMul < 0.0f ? 0.0f : (aMul > 1.0f ? 1.0f : aMul)));
                }

                Vector3 posNode = GetInterpolatedPosition(t, segRatio);
                if (t->distortionStrength > 0.0f)
                {
                    float dTime = time * t->distortionSpeed;
                    float nX = (Noise_Perlin3D(posNode.x * 0.15f + dTime, posNode.y * 0.15f, posNode.z * 0.15f) - 0.5f) * 2.0f;
                    float nY = (Noise_Perlin3D(posNode.x * 0.15f, posNode.y * 0.15f + dTime, posNode.z * 0.15f + 17.7f) - 0.5f) * 2.0f;
                    float nZ = (Noise_Perlin3D(posNode.x * 0.15f + 31.4f, posNode.y * 0.15f, posNode.z * 0.15f + dTime) - 0.5f) * 2.0f;
                    posNode.x += nX * t->distortionStrength;
                    posNode.y += nY * t->distortionStrength;
                    posNode.z += nZ * t->distortionStrength;
                }

                scratchOuter[h].position = posNode;
                scratchOuter[h].halfWidth = t->thickness * taper;
                scratchOuter[h].v = TrailUsesDeformShader(t)
                                        ? (float)h / (float)(drawCount - 1)
                                        : (segRatio * t->uvTiling - t->uvScrollOffset);
                scratchOuter[h].tint = (Color){nodeColor.r, nodeColor.g, nodeColor.b, (unsigned char)((nodeColor.a / 255.0f) * 180.0f * lifeRatio * taper)};
            }
            DrawTrailRibbon(t, scratchOuter, drawCount, t->sprite.id > 0 ? t->sprite : s_globalTrailTex, camera);

            if (!t->disableInnerCore)
            {
                for (int h = 0; h < drawCount; h++)
                {
                    float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
                    float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                                      ? ComputeWidthEnvelopeFast(t, segRatio, time)
                                      : ComputeWispStyleTaper(segRatio);
                    scratchInner[h].position = scratchOuter[h].position;
                    scratchInner[h].halfWidth = t->thickness * 0.35f * taper;
                    scratchInner[h].v = scratchOuter[h].v;
                    scratchInner[h].tint = (Color){255, 255, 255, (unsigned char)(180.0f * lifeRatio * taper)};
                }
                DrawTrailRibbon(t, scratchInner, drawCount, t->sprite.id > 0 ? t->sprite : s_globalTrailTex, camera);
            }
        }
    }
    else if (t->type == TRAIL_TYPE_PORTAL)
    {
        float radius = t->length;
        float age = t->maxLifetime - t->lifetime;
        if (age < TRAIL_PORTAL_SPAWN_GROW_TIME)
            radius *= (age / TRAIL_PORTAL_SPAWN_GROW_TIME);
        Rectangle uvRect = t->spriteAnim ? SpriteAnim_CalculateUV(t->spriteAnim, age, NULL) : (Rectangle){0, 0, 1, 1};
        DrawCameraFacingQuad(camBasis, t->position, radius * TRAIL_PORTAL_QUAD_SIZE_MUL, radius * TRAIL_PORTAL_QUAD_SIZE_MUL, t->angle * DEG2RAD, (Color){c.r, c.g, c.b, (unsigned char)(c.a * lifeRatio)}, (Texture2D){0}, uvRect);
    }
    else if (t->type == TRAIL_TYPE_FOLLOWER)
    {
        if (t->historyCount > 1)
        {
            int drawCount = CalculateDrawCount(t);

            for (int h = 0; h < drawCount; h++)
            {
                float segRatio = 1.0f - (float)h / (float)(drawCount - 1);
                float taper = (t->widthEnvelope != TRAIL_WIDTH_ENVELOPE_UNIFORM || t->widthCurve)
                                  ? ComputeWidthEnvelopeFast(t, segRatio, time)
                                  : ComputeWispStyleTaper(segRatio);
                Color nodeColor = c;
                if (t->gradient)
                {
                    Color gradCol = ColorGradient_Sample(t->gradient, segRatio);
                    nodeColor = (Color){(unsigned char)((gradCol.r / 255.0f) * c.r), (unsigned char)((gradCol.g / 255.0f) * c.g), (unsigned char)((gradCol.b / 255.0f) * c.b), (unsigned char)((gradCol.a / 255.0f) * c.a)};
                }
                if (t->alphaCurve)
                {
                    float aMul = SkillCurve_Eval(t->alphaCurve, segRatio);
                    nodeColor.a = (unsigned char)((float)nodeColor.a * (aMul < 0.0f ? 0.0f : (aMul > 1.0f ? 1.0f : aMul)));
                }

                Vector3 posNode = GetInterpolatedPosition(t, segRatio);
                if (t->distortionStrength > 0.0f)
                {
                    float dTime = time * t->distortionSpeed;
                    float nX = (Noise_Perlin3D(posNode.x * 0.8f + dTime, posNode.y * 0.8f, posNode.z * 0.8f) - 0.5f) * 2.0f;
                    float nY = (Noise_Perlin3D(posNode.x * 0.8f, posNode.y * 0.8f + dTime, posNode.z * 0.8f + 17.7f) - 0.5f) * 2.0f;
                    float nZ = (Noise_Perlin3D(posNode.x * 0.8f + 31.4f, posNode.y * 0.8f, posNode.z * 0.8f + dTime) - 0.5f) * 2.0f;
                    posNode.x += nX * t->distortionStrength;
                    posNode.y += nY * t->distortionStrength;
                    posNode.z += nZ * t->distortionStrength;
                }
                posNode = ApplyAnchoredHelix(t, posNode, segRatio);
                scratchOuter[h].position = posNode;
                scratchOuter[h].halfWidth = t->thickness * taper;
                // Deform trails: texcoord.y carries the normalized segment
                // (0 = head, 1 = tail) for the wave envelope; tiling/scroll
                // live in the packed-material shader.
                scratchOuter[h].v = TrailUsesDeformShader(t)
                                        ? (float)h / (float)(drawCount - 1)
                                        : ((t->uvMetresPerTile > 0.0f)
                                               ? (t->nodeUV[NodeIndexForSegRatio(t, drawCount, h)] / t->uvMetresPerTile)
                                               : (segRatio * t->uvTiling));
                scratchOuter[h].tint = (Color){nodeColor.r, nodeColor.g, nodeColor.b,
                                               (unsigned char)(nodeColor.a * lifeRatio * taper)};
                scratchTaper[h] = taper;
                scratchSegRatio[h] = segRatio;
            }

            Texture2D ribbonTex = t->sprite.id > 0 ? t->sprite : s_globalTrailTex;
            if (t->layerCount > 0 && t->shape == TRAIL_SHAPE_TUBE)
            {
                rlDrawRenderBatchActive();
                if (!t->tubeSingleSided)
                    rlDisableBackfaceCulling();
                DrawLayeredTube(t, drawCount, ribbonTex);
                rlDrawRenderBatchActive();
                rlEnableBackfaceCulling();
            }
            else if (t->layerCount > 0)
            {
                DrawLayeredRibbon(t, drawCount, ribbonTex, camera);
            }
            else
            {
                // [TỐI ƯU PERFORMANCE] Loop Fusion: Gom 2 vòng lặp dải Outer & Inner làm 1
                for (int h = 0; h < drawCount; h++)
                {
                    float taper = scratchTaper[h];
                    scratchInner[h] = scratchOuter[h];

                    // Outer Strip adjustments
                    scratchOuter[h].halfWidth = t->thickness * 1.5f * taper;
                    scratchOuter[h].tint.a = (unsigned char)((float)scratchOuter[h].tint.a * (180.0f / 255.0f));
                    // Deform trails keep texcoord.y = normalized seg; the scroll
                    // for texture motion is applied in the material shader.
                    if (!TrailUsesDeformShader(t))
                        scratchOuter[h].v -= t->uvScrollOffset;

                    // Inner Strip adjustments
                    scratchInner[h].v = scratchOuter[h].v;
                    scratchInner[h].halfWidth = t->thickness * 0.4f * taper;
                    scratchInner[h].tint = (Color){255, 255, 255, (unsigned char)(255.0f * lifeRatio * taper)};
                }
                DrawTrailRibbon(t, scratchOuter, drawCount, ribbonTex, camera);
                if (!t->disableInnerCore)
                {
                    DrawTrailRibbon(t, scratchInner, drawCount, ribbonTex, camera);
                }
            }
        }
    }
}

typedef struct
{
    BlendMode bm;
    Shader sh;
    Texture2D tex;
    Texture2D flowMap;
    Texture2D noiseMask;
    float flowSpeed;
    float flowStrength;
    float flowTiling;
    float dissolve;
    float maskTiling;
    VFXContrastProfileId contrastProfile;
    bool useFlowMap;
} RenderGroup;

static bool TrailMatchesRenderGroup(const TrailEntity *t, const RenderGroup *g,
                                    BlendMode bm, Shader sh, Texture2D tex)
{
    if (g->bm != bm || g->sh.id != sh.id || g->tex.id != tex.id ||
        g->useFlowMap != t->useFlowMap ||
        g->noiseMask.id != t->noiseMask.id ||
        g->dissolve != t->dissolve || g->maskTiling != t->maskTiling)
        return false;
    if (g->contrastProfile != t->material.contrastProfile)
        return false;
    if (!t->useFlowMap)
        return true;
    return g->flowMap.id == t->flowMap.id &&
           g->flowSpeed == t->flowSpeed &&
           g->flowStrength == t->flowStrength &&
           g->flowTiling == t->flowTiling;
}

static void DrawTrailEntitiesLayer(Camera3D camera, int layerFilter)
{
    if (activeCount == 0)
        return;

    s_drawLayerFilter = layerFilter;
    if (layerFilter == 0) EnsureTrailBodyShader();
    EnsureTrailDeformShader();

    // WRAPPED clock. GetTime() grows without bound, and every trail shader
    // multiplies it by a pan/travel speed before using it as a UV or a sine
    // phase. After an hour that product is in the tens of thousands, where a
    // float32 step is ~0.004 — visible stepping in a panned texture and a
    // juddering wave. Both consumers are periodic (sampler wrap is REPEAT, sine
    // has period 2pi), so folding the clock is invisible while it lasts. This is
    // the C-side half of the reference's frac() rule; the shader folds again
    // after each multiply. WRAP is a multiple of 8 so the fold lands on a clean
    // binary boundary for the common integer-ish speeds.
    float time = fmodf(TimeFX_Elapsed(), 4096.0f);
    Matrix matView = GetCameraMatrix(camera);
    TrailCameraBasis camBasis = {
        {matView.m0, matView.m4, matView.m8},
        {matView.m1, matView.m5, matView.m9},
    };

    rlDrawRenderBatchActive();
    rlDisableDepthMask();

    RenderGroup groups[32];
    int groupCount = 0;

    // Gom cụm RenderGroup dựa trên BlendMode, Shader và Texture ID
    for (int a = 0; a < activeCount; a++)
    {
        TrailEntity *t = &trailPool[s_activeIds[a]];

        // Tối ưu culling sớm ngay tại vòng lặp thu thập
        if (!IsTrailVisible(t, camera))
            continue;

        // MUST mirror the selection in the draw loop below, exactly. The two
        // are what put a trail in a group and what checks it belongs there; if
        // they disagree the trail matches no group and is silently never drawn.
        Shader sh;
        if (TrailUsesVolumeShader(t))
            sh = s_volumeShader;
        else if (TrailUsesDeformShader(t))
            sh = s_deformShader;
        else
            sh = (layerFilter == 0 && TrailUsesAdditiveBlend(t) && s_bodyShader.id != 0)
                     ? s_bodyShader : ResolveShader(t);
        BlendMode sourceBm = t->useCustomBlendMode ? t->blendMode
                                                   : ((t->blendMode > 0) ? t->blendMode : BLEND_ADDITIVE);
        if (layerFilter == 1 && sourceBm == BLEND_ALPHA) continue;
        BlendMode bm = (layerFilter == 0) ? BLEND_ALPHA : sourceBm;
        Texture2D tex = t->sprite.id > 0 ? t->sprite : s_globalTrailTex;

        bool found = false;
        for (int g = 0; g < groupCount; g++)
        {
            if (TrailMatchesRenderGroup(t, &groups[g], bm, sh, tex))
            {
                found = true;
                break;
            }
        }
        if (!found && groupCount < 32)
        {
            groups[groupCount].bm = bm;
            groups[groupCount].sh = sh;
            groups[groupCount].tex = tex;
            groups[groupCount].flowMap = t->flowMap;
            groups[groupCount].noiseMask = t->noiseMask;
            groups[groupCount].flowSpeed = t->flowSpeed;
            groups[groupCount].flowStrength = t->flowStrength;
            groups[groupCount].flowTiling = t->flowTiling;
            groups[groupCount].dissolve = t->dissolve;
            groups[groupCount].maskTiling = t->maskTiling;
            groups[groupCount].contrastProfile = t->material.contrastProfile;
            groups[groupCount].useFlowMap = t->useFlowMap;
            groupCount++;
        }
    }

    for (int g = 0; g < groupCount; g++)
    {
        BeginBlendMode(groups[g].bm);

        Shader fullShader = groups[g].sh;
        BeginShaderMode(fullShader);
        // rlvk writes to the currently active shader, not the Shader argument.
        int timeLoc = GetCachedTimeLoc(fullShader);
        if (timeLoc >= 0)
            SetShaderValue(fullShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
        if (s_bodyShader.id != 0 && fullShader.id == s_bodyShader.id &&
            s_bodyContrastParamsLoc >= 0)
        {
            float contrastParams[4];
            VFXContrast_GetShaderParams(groups[g].contrastProfile,
                                        contrastParams);
            SetShaderValue(fullShader, s_bodyContrastParamsLoc, contrastParams,
                           SHADER_UNIFORM_VEC4);
        }
        // Volume-tube constants, pushed ONCE PER GROUP and never between
        // instances. Per-instance uniform writes are the pattern that empties
        // rlvk's UBO arena (ENGINE_LANDMINES §8); everything that genuinely
        // varies per column already rides in a vertex attribute.
        if (s_volumeShader.id != 0 && fullShader.id == s_volumeShader.id)
        {
            int maskLoc = GetShaderLocation(fullShader, "u_volMask");
            // The two-sheet pan itself is now s_volFlow (built once in
            // EnsureTrailVolumeShader — see the exact-reproduction comment
            // there), pushed exactly like vc_shield_shell.inl pushes its own
            // SurfaceFlow. Still once per group, still no per-instance
            // uniform write — s_volFlow never changes, so there is nothing
            // instance-specific to push in the first place.
            SurfaceFlow_Apply(&s_volFlow, fullShader, &s_volFlowLocs, time);
            // .x ĐÃ CÓ CHỦ LẠI, 06/08/2026 — nó từng bỏ trống (pan của sheet
            // 2 dọn sang s_volFlow), giờ là hệ số số hạng THÂN. Cả 4 ô đều
            // load-bearing: .x hệ số thân, .y số mũ, .z độ mềm viền, .w độ
            // đậm chung; hệ số RÌA đi riêng qua u_volRim vì hết ô.
            //
            // .y = 2.0: the volume power. core/tests/silhouette_test.c
            // (Test_ThePowerMustBeAtLeastTwo) proved a power below 1 CANNOT
            // dissolve the edge at any constant — |N.V| leaves the silhouette
            // with an infinite derivative (~sqrt of screen distance), and
            // squaring cancels that root so the falloff is linear in screen
            // distance, which is what reads as soft. The 0.85 this used to be
            // was tried alongside a two-sided mesh with no cull, which the
            // same test file (Test_CullingIsWhatMakesItWork) showed cannot
            // work regardless of power — a grazing ray crosses an unbounded
            // number of two-sided facets. Both fixes are now in: this power,
            // and trail_volume.fs's `if (facing < 0.0) discard;`.
            /* .x hệ số THÂN, .y số mũ, .z độ mềm viền, .w độ đậm chung —
             * ba trong bốn là tunable, xem chỗ đăng ký. THÂN và ĐỘ ĐẬM ĐI
             * CẶP: bật thân lên 1 làm độ đục trung bình tăng ~8 lần, đổi một
             * cái mà quên cái kia là cháy trắng hoặc mờ tịt. */
            float mask[4] = {s_volDepthMode, s_volDepthPow, 0.34f, s_volDensity};
            if (maskLoc >= 0) SetShaderValue(fullShader, maskLoc, mask, SHADER_UNIFORM_VEC4);
            /* viewPos — the volume shader NO LONGER USES IT for shading, and
             * that is the fix, not an oversight (06/08/2026).
             *
             * The tube is drawn in IMMEDIATE MODE, so rlgl has already
             * view-transformed its vertices on the CPU and trail_volume.vs
             * passes them straight through: fragPosition is VIEW space, where
             * the camera IS the origin. The view vector is normalize(-fragPos)
             * and no uniform can fail to arrive. Mixing a world-space viewPos
             * into that is exactly what produced the inverted |N.V| that cost
             * a whole session — full chain in trail_volume.vs's header and
             * core/docs/VOLUME_SHADING_HANDOFF.md.
             *
             * Still pushed because volume_debug = 15 reads it back as a
             * diagnostic band, and because fs_header.glsl declares it for
             * every consumer; a raw BeginShaderMode (what this function uses,
             * not SkillManager_BeginShader) never sets it otherwise. */
            int viewPosLoc = GetShaderLocation(fullShader, "viewPos");
            if (viewPosLoc >= 0)
            {
                Vector3 vp = camera.position;
                SetShaderValue(fullShader, viewPosLoc, &vp, SHADER_UNIFORM_VEC3);
            }
            int nsrcLoc = GetShaderLocation(fullShader, "u_volNormalSrc");
            if (nsrcLoc >= 0)
                SetShaderValue(fullShader, nsrcLoc, &s_volNormalSrc, SHADER_UNIFORM_FLOAT);
            int cullLoc = GetShaderLocation(fullShader, "u_volCull");
            if (cullLoc >= 0)
                SetShaderValue(fullShader, cullLoc, &s_volCull, SHADER_UNIFORM_FLOAT);
            int rimLoc = GetShaderLocation(fullShader, "u_volRim");
            if (rimLoc >= 0)
                SetShaderValue(fullShader, rimLoc, &s_volRim, SHADER_UNIFORM_FLOAT);
            int erodeLoc = GetShaderLocation(fullShader, "u_volErode");
            if (erodeLoc >= 0)
                SetShaderValue(fullShader, erodeLoc, &s_volErode, SHADER_UNIFORM_FLOAT);
            int bandLoc = GetShaderLocation(fullShader, "u_volErodeBand");
            if (bandLoc >= 0)
                SetShaderValue(fullShader, bandLoc, &s_volErodeBand, SHADER_UNIFORM_FLOAT);
            int dbgLoc = GetShaderLocation(fullShader, "u_volDebug");
            if (dbgLoc >= 0)
                SetShaderValue(fullShader, dbgLoc, &s_volDebug, SHADER_UNIFORM_FLOAT);
            /* ONCE. Proves the volume path is the one drawing, and with what —
             * "it looks the same" and "this code never ran" are the same
             * picture. */
            static bool announced = false;
            if (!announced)
            {
                announced = true;
                TraceLog(LOG_INFO,
                         "TRAIL: volume group DRAWING — shader %u, mask loc %d, "
                         "viewPos loc %d, debug %.0f loc %d (-1 = KHONG TOI NOI), "
                         "floor %.2f, density %.2f",
                         (unsigned)fullShader.id, maskLoc,
                         GetShaderLocation(fullShader, "viewPos"),
                         (double)s_volDebug,
                         GetShaderLocation(fullShader, "u_volDebug"),
                         mask[2], mask[3]);
            }
        }
        // Locations are shader-level state, not per-trail state. Resolve them
        // once per pass; several simultaneous smoke trails previously repeated
        // five GetShaderLocation calls for every instance, every frame.
        int flowTimeLoc = GetCachedFlowTimeLoc(fullShader);
        int flowSpeedLoc = GetCachedFlowSpeedLoc(fullShader);
        int flowStrengthLoc = GetCachedFlowStrengthLoc(fullShader);
        int flowTilingLoc = GetCachedFlowTilingLoc(fullShader);
        int flowTexLoc = GetCachedFlowTexLoc(fullShader);
        int dissolveLoc = GetCachedDissolveLoc(fullShader);
        int maskTilingLoc = GetCachedMaskTilingLoc(fullShader);
        int maskTexLoc = GetCachedMaskTexLoc(fullShader);
        if (groups[g].useFlowMap)
        {
            // Keep one clock for the batch. Flow phase is visual state, not
            // per-instance geometry, so grouping it prevents rlvk UBO churn.
            if (flowTimeLoc >= 0)
                SetShaderValue(fullShader, flowTimeLoc, &time, SHADER_UNIFORM_FLOAT);
            if (flowSpeedLoc >= 0)
                SetShaderValue(fullShader, flowSpeedLoc, &groups[g].flowSpeed, SHADER_UNIFORM_FLOAT);
            if (flowStrengthLoc >= 0)
                SetShaderValue(fullShader, flowStrengthLoc, &groups[g].flowStrength, SHADER_UNIFORM_FLOAT);
            if (flowTilingLoc >= 0)
                SetShaderValue(fullShader, flowTilingLoc, &groups[g].flowTiling, SHADER_UNIFORM_FLOAT);
            if (flowTexLoc >= 0 && groups[g].flowMap.id > 0)
                SetShaderValueTexture(fullShader, flowTexLoc, groups[g].flowMap);
        }
        else if (flowStrengthLoc >= 0)
        {
            // Shader uniforms persist across draw groups.  Explicitly clear a
            // previous flow-enabled batch so ordinary ribbons never inherit
            // UV distortion from the group rendered just before them.
            const float noFlow = 0.0f;
            SetShaderValue(fullShader, flowStrengthLoc, &noFlow, SHADER_UNIFORM_FLOAT);
        }
        if (dissolveLoc >= 0)
            SetShaderValue(fullShader, dissolveLoc, &groups[g].dissolve, SHADER_UNIFORM_FLOAT);
        if (maskTilingLoc >= 0)
            SetShaderValue(fullShader, maskTilingLoc, &groups[g].maskTiling, SHADER_UNIFORM_FLOAT);
        if (maskTexLoc >= 0)
        {
            Texture2D maskTex = (groups[g].noiseMask.id != 0)
                                    ? groups[g].noiseMask : s_tubeFlatTex;
            if (maskTex.id != 0)
                SetShaderValueTexture(fullShader, maskTexLoc, maskTex);
        }

        for (int a = 0; a < activeCount; a++)
        {
            TrailEntity *t = &trailPool[s_activeIds[a]];
            if (!IsTrailVisible(t, camera))
                continue;

            BlendMode sourceBm = t->useCustomBlendMode ? t->blendMode
                                                       : ((t->blendMode > 0) ? t->blendMode : BLEND_ADDITIVE);
            if (layerFilter == 1 && sourceBm == BLEND_ALPHA) continue;
            BlendMode currentBm = (layerFilter == 0) ? BLEND_ALPHA : sourceBm;
            Texture2D currentTex = t->sprite.id > 0 ? t->sprite : s_globalTrailTex;

            Shader currentShader;
            if (TrailUsesVolumeShader(t))
                currentShader = s_volumeShader;
            else if (TrailUsesDeformShader(t))
                currentShader = s_deformShader;
            else
                currentShader = (layerFilter == 0 && TrailUsesAdditiveBlend(t) && s_bodyShader.id != 0)
                                     ? s_bodyShader : ResolveShader(t);
            if (TrailMatchesRenderGroup(t, &groups[g], currentBm, currentShader, currentTex))
            {
                DrawTrailGeometry(t, camera, &camBasis, time);
            }
        }

        EndShaderMode();
        EndBlendMode();
    }

    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    s_drawLayerFilter = -1;
}

void DrawTrailEntities(Camera3D camera) { DrawTrailEntitiesLayer(camera, -1); }
void DrawTrailEntitiesBody(Camera3D camera) { DrawTrailEntitiesLayer(camera, 0); }
void DrawTrailEntitiesEmission(Camera3D camera) { DrawTrailEntitiesLayer(camera, 1); }

void UnloadTrailSystem(void)
{
    // Gọi callback onDeath cho tất cả các entity đang hoạt động trước khi dọn dẹp
    for (int i = 0; i < activeCount; i++)
    {
        int index = s_activeIds[i];
        if (index >= 0 && index < MAX_TRAIL_PARTICLES)
        {
            TrailEntity *t = &trailPool[index];
            if (t->active)
            {
                if (t->onDeath != NULL)
                {
                    t->onDeath(t->position, t->scale);
                }
                t->active = false;
            }
        }
    }

    // Reset lại toàn bộ trạng thái của Pool và Free List về ban đầu
    activeCount = 0;
    freeListHead = 0;

    for (int i = 0; i < MAX_TRAIL_PARTICLES; i++)
    {
        trailPool[i].active = false;
        trailPool[i].nextFree = (i < MAX_TRAIL_PARTICLES - 1) ? (i + 1) : -1;
        s_slotListIndex[i] = -1;
        s_activeIds[i] = -1;
    }
}

void TrailSystem_GetStats(int *active, int *max)
{
    *active = GetActiveTrailCount();
    *max = MAX_TRAIL_PARTICLES;
}

void Trail_SetFlowMap(int id, Texture2D flowMap, float speed, float strength, float tiling)
{
    TrailEntity *e = GetTrail(id);
    if (!e)
        return;

    e->flowMap = flowMap;
    e->flowSpeed = speed;
    e->flowStrength = strength;
    e->flowTiling = (tiling > 0.0f) ? tiling : 1.0f;
    e->useFlowMap = (flowMap.id > 0);
}
