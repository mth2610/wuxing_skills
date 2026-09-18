#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "core/ribbon_strip.h"
#include "core/wind/wind_system.h"
#include "core/resource_manager.h"
#include "core/time_fx.h"
#include "core/vfx_render.h"
#include <math.h>
#include <stdbool.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Forward declaration of map ground query
float VFX_GroundHeightFromMap(float worldX, float worldZ, void *unused);

// ─────────────────────────────────────────────────────────────────────────────
// STATIC RESOURCES & SHADER STATE
// ─────────────────────────────────────────────────────────────────────────────
static Shader    s_gwindShader       = {0};
static int       s_gwindLocTime      = -1;
static int       s_gwindLocNoise     = -1;
static int       s_gwindLocScale     = -1;
static int       s_gwindLocSpeed1    = -1;
static int       s_gwindLocSpeed2    = -1;
static int       s_gwindLocErosion   = -1;
static int       s_gwindLocSoftness  = -1;
static int       s_gwindLocWindColor = -1;

static Texture2D s_gwindSilkTex      = {0};
static bool      s_gwindInitialized  = false;

static inline float GuidingWind_SmoothStep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Generate an authentic procedural 3-strand smoke/silk wind texture:
// 1 slightly bolder center strand + 2 faint side strands, all rolling & breaking intermittently like drifting smoke
static Texture2D GuidingWind_GetSilkTexture(void)
{
    if (s_gwindSilkTex.id == 0)
    {
        s_gwindSilkTex = ResourceManager_LoadTexture("assets/textures/smoke_strand.png");
        if (s_gwindSilkTex.id != 0)
        {
            SetTextureFilter(s_gwindSilkTex, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_gwindSilkTex, TEXTURE_WRAP_REPEAT);
        }
        else
        {
            const int W = 256; // Cross-width
            const int H = 512; // Along-length
            Image img = GenImageColor(W, H, BLANK);

            for (int y = 0; y < H; y++)
            {
                float normY = (float)y / (float)(H - 1);
                for (int x = 0; x < W; x++)
                {
                    float normX = (float)x / (float)(W - 1);
                    float d0 = (normX - 0.50f) / 0.12f;
                    float r = expf(-d0 * d0);
                    float d1 = (normX - 0.50f) / 0.08f;
                    float g = expf(-d1 * d1);
                    float b = 0.5f + 0.3f * sinf(normY * 2.0f * PI + normX * 4.0f);
                    float a = 0.5f + 0.5f * sinf(normY * 4.0f * PI);
                    ImageDrawPixel(&img, x, y, (Color){
                        (unsigned char)(Clamp(r, 0.0f, 1.0f) * 255.0f),
                        (unsigned char)(Clamp(g, 0.0f, 1.0f) * 255.0f),
                        (unsigned char)(Clamp(b, 0.0f, 1.0f) * 255.0f),
                        (unsigned char)(Clamp(a, 0.0f, 1.0f) * 255.0f)
                    });
                }
            }
            s_gwindSilkTex = LoadTextureFromImage(img);
            UnloadImage(img);
            SetTextureFilter(s_gwindSilkTex, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(s_gwindSilkTex, TEXTURE_WRAP_REPEAT);
        }
    }
    return s_gwindSilkTex;
}

static void GuidingWind_InitResources(void)
{
    if (s_gwindInitialized) return;

    s_gwindShader = ResourceManager_LoadShader(NULL, "core/shaders/wind_ribbon.fs");
    if (s_gwindShader.id != 0)
    {
        s_gwindLocTime      = GetShaderLocation(s_gwindShader, "u_time");
        s_gwindLocNoise     = GetShaderLocation(s_gwindShader, "noiseTex");
        s_gwindLocScale     = GetShaderLocation(s_gwindShader, "u_noiseScale");
        s_gwindLocSpeed1    = GetShaderLocation(s_gwindShader, "u_speed1");
        s_gwindLocSpeed2    = GetShaderLocation(s_gwindShader, "u_speed2");
        s_gwindLocErosion   = GetShaderLocation(s_gwindShader, "u_erosionAmount");
        s_gwindLocSoftness  = GetShaderLocation(s_gwindShader, "u_softness");
        s_gwindLocWindColor = GetShaderLocation(s_gwindShader, "u_windColor");
    }

    GuidingWind_GetSilkTexture();
    s_gwindInitialized = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// STREAMLINE TRAJECTORY EVALUATOR (Ghost of Tsushima Style)
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    float spiralRadius;   // Radius of 3D spiral around spine (m, 0 = main spine)
    float spiralTurns;    // Number of helical rotations along path
    float phaseOffset;    // Angular phase difference
    float lateralOffset;  // Constant lateral lane offset
    float verticalOffset; // Constant vertical lane offset
    float baseWidth;      // Peak half-width of ribbon (m)
    float span;           // Length of ribbon stream in [0, 1] space
    float delay;          // Staggered launch delay
    Color color;          // Ribbon base color & alpha
} GuidingWindRibbonProfile;

#define GWIND_WAYPOINTS 48
#define GWIND_RIBBON_COUNT 1

static inline Vector3 GuidingWind_EvalTrajectory(Vector3 startPos, Vector3 forwardDir,
                                                float totalDist, Vector3 sideVec, Vector3 upVec,
                                                float u, float time,
                                                const GuidingWindRibbonProfile *profile)
{
    // 1. Base interpolation along guiding direction
    float currentDist = totalDist * u;
    Vector3 pos = Vector3Add(startPos, Vector3Scale(forwardDir, currentDist));

    // 2. Gentle aerodynamic S-curve swoop (lượn lờ thanh thoát dọc triền không gian)
    float uClamped = Clamp(u, 0.0f, 1.0f);
    float sPhase = uClamped * PI;
    float macroSide = sinf(sPhase) * 1.05f + sinf(sPhase) * sinf(uClamped * 2.0f * PI) * 0.20f;
    float macroUp   = sinf(sPhase) * 0.42f;
    pos = Vector3Add(pos, Vector3Scale(sideVec, macroSide));
    pos = Vector3Add(pos, Vector3Scale(upVec,   macroUp));

    // 3. Phân làn dải gió êm dịu, nhấp nhô nhẹ nhàng tự nhiên (Wisp separation & wave)
    float laneEnvelope = sqrtf(fmaxf(0.0f, sinf(uClamped * PI)));
    float wispWave = sinf(u * 3.0f * PI + profile->phaseOffset) * 0.14f * laneEnvelope;
    pos = Vector3Add(pos, Vector3Scale(sideVec, (profile->lateralOffset + wispWave) * laneEnvelope));
    pos = Vector3Add(pos, Vector3Scale(upVec,   (profile->verticalOffset + wispWave * 0.4f) * laneEnvelope));

    // 4. Terrain-Aware Look-Ahead Lift
    // Samples current ground elevation and checks ahead for rising slopes
    float hCurrent = VFX_GroundHeightFromMap(pos.x, pos.z, NULL);
    const float lookAheadDist = 3.2f;
    Vector3 probeAhead = Vector3Add(pos, Vector3Scale(forwardDir, lookAheadDist));
    float hAhead = VFX_GroundHeightFromMap(probeAhead.x, probeAhead.z, NULL);

    float slopeDelta = fmaxf(0.0f, hAhead - hCurrent);
    float slopeLift = (slopeDelta / lookAheadDist) * 2.2f;
    if (slopeLift > 8.0f) slopeLift = 8.0f;

    // Hover altitude above terrain with safe clearance H_safe (1.35m)
    float flightHeight = 1.35f + slopeLift + sinf(uClamped * 2.0f * PI) * 0.20f;
    pos.y = fmaxf(pos.y, hCurrent + flightHeight);

    // 5. Vorticle & Macro Wind Coupling
    Vector3 windVel = Wind_EvaluateVelocity(pos, time);
    pos = Vector3Add(pos, Vector3Scale(windVel, 0.05f));

    // 6. Helical tendency (chỉ kích hoạt khi spiralRadius > 0.001f)
    if (profile->spiralRadius > 0.001f)
    {
        float helixEnvelope = sqrtf(fmaxf(0.0f, sinf(uClamped * PI)));
        float theta = u * profile->spiralTurns * PI * 2.0f + profile->phaseOffset;
        float r = profile->spiralRadius * helixEnvelope;

        float offX = cosf(theta) * r;
        float offY = sinf(theta) * (r * 0.75f);

        pos = Vector3Add(pos, Vector3Scale(sideVec, offX));
        pos = Vector3Add(pos, Vector3Scale(upVec,   offY));
    }

    return pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// DRAW SINGLE WIND RIBBON STRIP
// ─────────────────────────────────────────────────────────────────────────────
static void GuidingWind_DrawRibbon(Vector3 rootStartPos, Vector3 forwardDir, float totalDist,
                                  Vector3 sideVec, Vector3 upVec,
                                  float globalProgress,
                                  const GuidingWindRibbonProfile *profile,
                                  float time, Camera3D camera)
{
    // Staggered timing calculation: map globalProgress to local ribbon lifetime
    float localT = (globalProgress - profile->delay) / (0.86f - profile->delay);
    if (localT <= 0.0f || localT >= 1.0f) return;

    float t = localT;

    // Lướt gió chuyển động về phía trước thanh thoát:
    // headU xuất phát gần điểm bắt đầu và lướt mượt mà ra xa
    float headU = powf(t, 0.85f) * 1.25f;

    // Dải gió tự do, không bị neo giữ tại gốc hay ngọn:
    float growth = GuidingWind_SmoothStep(0.0f, 0.18f, t);
    float dissipation = (t > 0.68f) ? (1.0f - (t - 0.68f) / 0.32f) : 1.0f;
    float currentSpan = profile->span * growth * dissipation;
    float tailU = headU - currentSpan;

    if (currentSpan < 0.005f) return;

    // Độ mờ êm dịu, xuất hiện nhẹ nhàng và tan biến thanh thoát ở phía chân trời
    float fadeIn = GuidingWind_SmoothStep(0.0f, 0.08f, t);
    float fadeOut = 1.0f - GuidingWind_SmoothStep(0.68f, 0.98f, t);
    float globalAlpha = fadeIn * fadeOut;
    if (globalAlpha <= 0.003f) return;

    static RibbonPoint pts[GWIND_WAYPOINTS];
    Texture2D silkTex = GuidingWind_GetSilkTexture();

    for (int i = 0; i < GWIND_WAYPOINTS; i++)
    {
        float frac = (float)i / (float)(GWIND_WAYPOINTS - 1);
        // frac = 0 is tail, frac = 1 is head
        float u = tailU + currentSpan * frac;

        Vector3 p = GuidingWind_EvalTrajectory(rootStartPos, forwardDir, totalDist,
                                               sideVec, upVec, u, time, profile);

        // Sinusoidal aerodynamic taper (sharp needle at head & tail, full body)
        float taper = sinf(frac * PI);
        taper = powf(taper, 0.72f);

        pts[i].position = p;
        pts[i].halfWidth = profile->baseWidth * taper;
        pts[i].tint = ColorAlpha(profile->color, Clamp(globalAlpha, 0.0f, 1.0f));
    }

    // Normalized arc length mapping along path
    Ribbon_ComputeArcLengthUV(pts, GWIND_WAYPOINTS);

    // Geometry submission with procedural silk texture
    DrawRibbonStripEx(pts, GWIND_WAYPOINTS, silkTex, camera,
                      RIBBON_CAMERA_FACING, (Vector3){0, 1, 0});
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC CORE API: VFX_ComposeGuidingWind
// ─────────────────────────────────────────────────────────────────────────────
void VFX_ComposeGuidingWind(Vector3 startPos, Vector3 targetPos, float progress, Camera3D camera)
{
    if (progress <= 0.0f || progress >= 1.0f) return;

    GuidingWind_InitResources();

    Vector3 forwardDir = Vector3Subtract(targetPos, startPos);
    float totalDist = Vector3Length(forwardDir);
    if (totalDist < 0.2f)
    {
        forwardDir = (Vector3){ 0.0f, 0.0f, 22.0f };
        totalDist = 22.0f;
    }
    forwardDir = Vector3Scale(forwardDir, 1.0f / totalDist);

    // Compute coordinate frame orthogonal to motion
    Vector3 sideVec = Vector3CrossProduct(forwardDir, (Vector3){ 0.0f, 1.0f, 0.0f });
    if (Vector3Length(sideVec) < 0.001f)
    {
        sideVec = (Vector3){ 1.0f, 0.0f, 0.0f };
    }
    else
    {
        sideVec = Vector3Normalize(sideVec);
    }
    Vector3 upVec = Vector3Normalize(Vector3CrossProduct(sideVec, forwardDir));

    float time = TimeFX_Elapsed();

    // ── CẤU TRÚC 3 SỢI GIÓ CUỘN TRÔI THANH MẢNH (baseWidth = 0.18m, span = 0.28) ──
    static const GuidingWindRibbonProfile profiles[GWIND_RIBBON_COUNT] = {
        {
            0.00f,  0.0f,  0.00f,
            0.00f,  0.00f,
            0.18f,  0.28f, 0.00f,
            (Color){ 255, 255, 255, 245 }
        }
    };

    // ── 1. BODY PASS (Alpha-blended eroded noise ribbon) ────────────────────
    VFXRenderScope renderScope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY,
                                                    VFX_SURFACE_ALPHA, false);

    Texture2D silkTex = GuidingWind_GetSilkTexture();

    if (s_gwindShader.id != 0)
    {
        rlDrawRenderBatchActive();
        BeginShaderMode(s_gwindShader);

        float noiseScale = 1.0f;
        float speed1 = 2.4f;
        float speed2 = 3.6f;
        float erosionProgress = progress * 0.80f;
        float softness = 0.26f;
        Vector4 windCol = { 0.92f, 0.96f, 1.0f, 0.52f };

        SetShaderValue(s_gwindShader, s_gwindLocTime,     &time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_gwindShader, s_gwindLocScale,    &noiseScale, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_gwindShader, s_gwindLocSpeed1,   &speed1, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_gwindShader, s_gwindLocSpeed2,   &speed2, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_gwindShader, s_gwindLocErosion,  &erosionProgress, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_gwindShader, s_gwindLocSoftness, &softness, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_gwindShader, s_gwindLocWindColor,&windCol, SHADER_UNIFORM_VEC4);

        if (silkTex.id != 0)
        {
            SetShaderValueTexture(s_gwindShader, s_gwindLocNoise, silkTex);
        }

        // Draw the 4 braided streamlines
        for (int r = 0; r < GWIND_RIBBON_COUNT; r++)
        {
            GuidingWind_DrawRibbon(startPos, forwardDir, totalDist, sideVec, upVec,
                                  progress, &profiles[r], time, camera);
        }

        rlDrawRenderBatchActive();
        EndShaderMode();
    }
    else
    {
        // Fallback unshaded draw
        for (int r = 0; r < GWIND_RIBBON_COUNT; r++)
        {
            GuidingWind_DrawRibbon(startPos, forwardDir, totalDist, sideVec, upVec,
                                  progress, &profiles[r], time, camera);
        }
    }

    VFXRender_EndDraw(&renderScope);
}
