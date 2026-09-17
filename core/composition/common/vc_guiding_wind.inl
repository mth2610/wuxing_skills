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

// Generate an ultra-smooth procedural Gaussian silk streamline texture
// Features 5 fine undulating micro-filaments with translucent air channels
static Texture2D GuidingWind_GetSilkTexture(void)
{
    if (s_gwindSilkTex.id == 0)
    {
        const int W = 128; // Cross-width
        const int H = 256; // Along-length
        Image img = GenImageColor(W, H, BLANK);

        for (int y = 0; y < H; y++)
        {
            float normY = (float)y / (float)(H - 1); // [0..1]
            // Serpentine undulation along streamline length
            float wave1 = sinf(normY * 3.0f * PI) * 0.06f;
            float wave2 = cosf(normY * 5.0f * PI + 0.7f) * 0.05f;
            float wave3 = sinf(normY * 7.0f * PI + 1.8f) * 0.04f;
            float wave4 = cosf(normY * 9.0f * PI + 2.5f) * 0.03f;

            for (int x = 0; x < W; x++)
            {
                float normX = (float)x / (float)(W - 1); // [0..1]

                // Multiple fine, delicate Gaussian silk filaments
                // 1. Central brilliant filament
                float d0 = (normX - (0.50f + wave1)) / 0.065f;
                float f0 = expf(-d0 * d0) * 1.0f;

                // 2. Left inner wisp
                float d1 = (normX - (0.34f + wave2)) / 0.050f;
                float f1 = expf(-d1 * d1) * 0.78f;

                // 3. Right inner wisp
                float d2 = (normX - (0.66f + wave3)) / 0.055f;
                float f2 = expf(-d2 * d2) * 0.75f;

                // 4. Left outer delicate thread
                float d3 = (normX - (0.20f + wave4)) / 0.042f;
                float f3 = expf(-d3 * d3) * 0.55f;

                // 5. Right outer delicate thread
                float d4 = (normX - (0.80f - wave1 * 0.8f)) / 0.045f;
                float f4 = expf(-d4 * d4) * 0.50f;

                // Soft overall body envelope (tapers smoothly at lateral borders)
                float body = sinf(normX * PI);
                body = powf(body, 1.35f);

                float filaments = fmaxf(f0, fmaxf(f1 * 0.85f, fmaxf(f2 * 0.82f, fmaxf(f3 * 0.65f, f4 * 0.60f))));
                float val = filaments * 0.78f + (f0 + f1 + f2 + f3 + f4) * 0.16f + body * 0.12f;
                float alpha = Clamp(val * body, 0.0f, 1.0f);

                ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(alpha * 255.0f) });
            }
        }
        s_gwindSilkTex = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(s_gwindSilkTex, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(s_gwindSilkTex, TEXTURE_WRAP_REPEAT);
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
#define GWIND_RIBBON_COUNT 4

static inline float GuidingWind_SmoothStep(float edge0, float edge1, float x)
{
    float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

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
    float sPhase = u * PI;
    float macroSide = sinf(sPhase) * 0.95f + sinf(u * 2.0f * PI) * 0.30f;
    float macroUp   = sinf(sPhase) * 0.40f;
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
        pts[i].tint = ColorAlpha(profile->color, Clamp(globalAlpha * taper, 0.0f, 1.0f));
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

    // ── 4 Dải lụa gió lượn lờ thanh thoát (Không xoay tròn hỗn loạn) ─────────
    // Dải gió chính dẫn đường kèm 3 dải tơ lụa phụ bay song hành nhịp nhàng:
    static const GuidingWindRibbonProfile profiles[GWIND_RIBBON_COUNT] = {
        // 1. Dải chính tâm: Lượn sóng nhẹ dẫn đầu
        {
            0.00f,  0.0f,  0.00f,
            0.00f,  0.00f,
            0.16f,  0.48f, 0.00f,
            (Color){ 248, 252, 255, 175 }
        },
        // 2. Dải tơ bên phải trên: Lướt nhẹ phía trên sang phải
        {
            0.00f,  0.0f,  0.85f,
            0.38f,  0.22f,
            0.11f,  0.42f, 0.05f,
            (Color){ 230, 246, 255, 140 }
        },
        // 3. Dải tơ bên trái dưới: Lướt nhẹ là là mặt cỏ phía trái
        {
            0.00f,  0.0f,  2.10f,
           -0.35f, -0.14f,
            0.10f,  0.38f, 0.09f,
            (Color){ 225, 245, 255, 130 }
        },
        // 4. Dải tơ thanh mảnh bay theo sau
        {
            0.00f,  0.0f,  3.50f,
            0.12f,  0.34f,
            0.08f,  0.32f, 0.14f,
            (Color){ 240, 250, 255, 120 }
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
