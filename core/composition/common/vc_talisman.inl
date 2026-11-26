// ============================================================================
// VC_TALISMAN.INL — Phù Chỉ 3D (Daoist Talisman VFX + 6-DOF Paper Aerodynamics)
//
// Powered by core/motion/sheet_aerodynamics.{h,c} and core/shaders/talisman.{vs,fs}.
// Zero heap allocations per frame: static pool (120 sheets) + one-time procedural
// 256x512 atlas generation (paperTex + runeGlowTex with 8.5% outer bloom margin).
// ============================================================================

#include "core/motion/sheet_aerodynamics.h"
#include <string.h>

#define TALISMAN_MAX_INSTANCES 120
#define TALISMAN_TEX_W 256
#define TALISMAN_TEX_H 512
#define TALISMAN_HALF_W 0.2175f
#define TALISMAN_HALF_H 0.5500f

typedef struct {
    SheetAeroState aero;
    int   paletteId;    // 0 = Imperial, 1 = Fire, 2 = Thunder
    float throwBoost;
    float alpha;
    bool  active;
} TalismanInstance;

static TalismanInstance s_talismanPool[TALISMAN_MAX_INSTANCES];
static int   s_talismanActiveCount = 0;
static int   s_talismanThrowCursor = 0;
static float s_talismanThrowAutoTimer = 0.0f;
static float s_talismanModeBlend = 0.0f;
static int   s_talismanLastFlightMode = -1;
static int   s_talismanLastCount = -1;
static int   s_talismanLastPalette = -1;
static bool  s_talismanPoolInitialized = false;
static bool  s_talismanActiveThisFrame = false;
static bool  s_talismanDrawThisFrame = false;
static bool  s_talismanShadowActive = false;
static Vector3 s_talismanOrigin = { 6.0f, 0.0f, 4.4f };

static VFX_TalismanConfig s_talismanLiveCfg;
static bool s_talismanLiveCfgInit = false;

// GPU resources (loaded/generated once, cached statically)
static bool      s_talismanGpuReady = false;
static Shader    s_talismanShader = {0};
static int       s_talismanLocModeBlend = -1;
static int       s_talismanLocGlowIntensity = -1;
static int       s_talismanLocPremultiplied = -1;
static int       s_talismanLocWarmPos = -1;
static int       s_talismanLocWarmCol = -1;
static int       s_talismanLocCoolPos = -1;
static int       s_talismanLocCoolCol = -1;
static Texture2D s_talismanAtlasTex = {0};

// Static scratch buffers for one-time procedural 256x1024 stacked atlas (zero malloc)
static unsigned char s_talAtlasRGBA[TALISMAN_TEX_W * (TALISMAN_TEX_H * 2) * 4];
static float         s_talBufA[TALISMAN_TEX_W * TALISMAN_TEX_H];
static float         s_talBufB[TALISMAN_TEX_W * TALISMAN_TEX_H];
static float         s_talTmp[TALISMAN_TEX_W * TALISMAN_TEX_H];
static float         s_talGlowAccum[TALISMAN_TEX_W * TALISMAN_TEX_H];
static float         s_talBorder[TALISMAN_TEX_W * TALISMAN_TEX_H];
static float         s_talRune[TALISMAN_TEX_W * TALISMAN_TEX_H];

static inline float Tal_Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static inline float Tal_Hash1(float n)
{
    float s = sinf(n * 127.1f + 311.7f) * 43758.5453123f;
    return s - floorf(s);
}

static inline float Tal_Noise1D(float x, float seed)
{
    float i = floorf(x);
    float f = x - i;
    float u = f * f * (3.0f - 2.0f * f);
    return Tal_Hash1(i + seed * 53.7f) * (1.0f - u) + Tal_Hash1(i + 1.0f + seed * 53.7f) * u;
}

static inline float Tal_Fbm1D(float x, float seed)
{
    return Tal_Noise1D(x, seed) * 0.56f
         + Tal_Noise1D(x * 2.4f, seed + 17.0f) * 0.28f
         + Tal_Noise1D(x * 5.8f, seed + 31.0f) * 0.16f;
}

static inline float Tal_Randf(unsigned int *rng)
{
    *rng = (*rng * 1664525u + 1013904223u);
    return (float)(*rng & 0x00FFFFFFu) / (float)0x01000000u;
}

// ── 2D Procedural Rasterizer Helpers for Talisman Atlas ─────────────────────

static void Tal_PlotDisc(float *buf, float cx, float cy, float r, float alpha)
{
    int x0 = (int)floorf(cx - r - 1.0f);
    int x1 = (int)ceilf(cx + r + 1.0f);
    int y0 = (int)floorf(cy - r - 1.0f);
    int y1 = (int)ceilf(cy + r + 1.0f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= TALISMAN_TEX_W) x1 = TALISMAN_TEX_W - 1;
    if (y1 >= TALISMAN_TEX_H) y1 = TALISMAN_TEX_H - 1;
    float invFeather = 1.0f / fmaxf(0.85f, r * 0.22f);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y - cy;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = Tal_Clamp01((r + 0.6f - d) * invFeather) * alpha;
            if (cov > 0.0f) {
                int idx = y * TALISMAN_TEX_W + x;
                buf[idx] = fmaxf(buf[idx], cov);
            }
        }
    }
}

static void Tal_StrokeLine(float *buf, float x0, float y0, float x1, float y1, float halfWidth, float alpha)
{
    float dx = x1 - x0, dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    int steps = (int)ceilf(dist / fmaxf(0.8f, halfWidth * 0.45f));
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        Tal_PlotDisc(buf, x0 + dx * t, y0 + dy * t, halfWidth, alpha);
    }
}

static void Tal_StrokeBrush(float *buf, const float pts[][2], int count, float width, float dryFactor)
{
    if (count < 2) return;
    float totalLen = 0.0f;
    for (int i = 0; i < count - 1; i++) {
        totalLen += hypotf(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]);
    }
    if (totalLen < 0.5f) return;

    float accum = 0.0f;
    for (int i = 0; i < count - 1; i++) {
        float ax = pts[i][0], ay = pts[i][1];
        float bx = pts[i + 1][0], by = pts[i + 1][1];
        float seg = hypotf(bx - ax, by - ay);
        int steps = (int)ceilf(seg / 1.4f);
        if (steps < 2) steps = 2;
        float nx = (seg > 1e-4f) ? -(by - ay) / seg : 0.0f;
        float ny = (seg > 1e-4f) ?  (bx - ax) / seg : 1.0f;

        for (int s = 0; s <= steps; s++) {
            float t = (float)s / (float)steps;
            float u = (accum + seg * t) / totalLen;
            float px = ax + (bx - ax) * t;
            float py = ay + (by - ay) * t;
            float taper = fminf(1.0f, fminf(u * 8.5f, (1.0f - u) * 6.5f));
            float w = fmaxf(1.0f, width * (0.38f + 0.62f * taper) * (0.86f + 0.20f * sinf(u * 8.0f)));
            Tal_PlotDisc(buf, px, py, w * 0.5f, 0.94f);
            if (dryFactor > 0.0f && (s & 1) == 0) {
                for (int b = -1; b <= 1; b++) {
                    float off = (float)b * w * 0.34f + (Tal_Hash1((float)(i * 31 + s) + (float)b * 7.3f) - 0.5f) * 1.1f;
                    Tal_PlotDisc(buf, px + nx * off, py + ny * off, fmaxf(0.55f, w * 0.14f), 0.38f);
                }
            }
        }
        accum += seg;
    }
}

static void Tal_StrokeRect(float *buf, float x, float y, float w, float h, float halfWidth, float alpha)
{
    Tal_StrokeLine(buf, x, y, x + w, y, halfWidth, alpha);
    Tal_StrokeLine(buf, x + w, y, x + w, y + h, halfWidth, alpha);
    Tal_StrokeLine(buf, x + w, y + h, x, y + h, halfWidth, alpha);
    Tal_StrokeLine(buf, x, y + h, x, y, halfWidth, alpha);
}

static void Tal_SeparableBoxBlur(const float *src, float *dst, float *tmp, int radius)
{
    if (radius < 1) {
        memcpy(dst, src, sizeof(float) * TALISMAN_TEX_W * TALISMAN_TEX_H);
        return;
    }
    float inv = 1.0f / (float)(2 * radius + 1);
    // Horizontal pass
    for (int y = 0; y < TALISMAN_TEX_H; y++) {
        int row = y * TALISMAN_TEX_W;
        float sum = 0.0f;
        for (int k = -radius; k <= radius; k++) {
            int cx = k < 0 ? 0 : (k >= TALISMAN_TEX_W ? TALISMAN_TEX_W - 1 : k);
            sum += src[row + cx];
        }
        for (int x = 0; x < TALISMAN_TEX_W; x++) {
            tmp[row + x] = sum * inv;
            int outX = x - radius;
            int inX  = x + radius + 1;
            if (outX < 0) outX = 0;
            if (inX >= TALISMAN_TEX_W) inX = TALISMAN_TEX_W - 1;
            sum += src[row + inX] - src[row + outX];
        }
    }
    // Vertical pass
    for (int x = 0; x < TALISMAN_TEX_W; x++) {
        float sum = 0.0f;
        for (int k = -radius; k <= radius; k++) {
            int cy = k < 0 ? 0 : (k >= TALISMAN_TEX_H ? TALISMAN_TEX_H - 1 : k);
            sum += tmp[cy * TALISMAN_TEX_W + x];
        }
        for (int y = 0; y < TALISMAN_TEX_H; y++) {
            dst[y * TALISMAN_TEX_W + x] = sum * inv;
            int outY = y - radius;
            int inY  = y + radius + 1;
            if (outY < 0) outY = 0;
            if (inY >= TALISMAN_TEX_H) inY = TALISMAN_TEX_H - 1;
            sum += tmp[inY * TALISMAN_TEX_W + x] - tmp[outY * TALISMAN_TEX_W + x];
        }
    }
}

static void Tal_GenerateAtlasTextures(void)
{
    const int W = TALISMAN_TEX_W;
    const int H = TALISMAN_TEX_H;
    // 8.5% UV margin around paper silhouette for unclipped 3D outer bloom
    const float mx = (float)W * 0.085f;
    const float my = (float)H * 0.082f;
    const float pw = (float)W - mx * 2.0f;
    const float ph = (float)H - my * 2.0f;

    memset(s_talBorder, 0, sizeof(s_talBorder));
    memset(s_talRune,   0, sizeof(s_talRune));
    memset(s_talBufA,   0, sizeof(s_talBufA));
    memset(s_talBufB,   0, sizeof(s_talBufB));

    // A. Paper grain (RGB) + hand-torn deckled edge silhouette (A)
    for (int y = 0; y < H; y++) {
        float fy = (float)y;
        float ty = Tal_Clamp01((fy - my) / ph);
        // Laid bamboo screen wire lines ("vân liềm trúc")
        float laid = (fmodf(fy, 3.0f) < 0.9f) ? -0.045f : 0.0f;
        // Ragged left/right deckle edges
        float leftEdge  = mx + (Tal_Fbm1D(ty * 22.0f, 4.0f) - 0.5f) * 7.0f;
        float rightEdge = (float)W - mx + (Tal_Fbm1D(ty * 22.0f, 2.0f) - 0.5f) * 7.0f;

        for (int x = 0; x < W; x++) {
            float fx = (float)x;
            float tx = Tal_Clamp01((fx - mx) / pw);
            float topEdge    = my + (Tal_Fbm1D(tx * 14.0f, 1.0f) - 0.5f) * 7.5f;
            float bottomEdge = (float)H - my + (Tal_Fbm1D(tx * 14.0f, 3.0f) - 0.5f) * 7.5f;

            // Signed distance inside deckled rectangle
            float dLeft   = fx - leftEdge;
            float dRight  = rightEdge - fx;
            float dTop    = fy - topEdge;
            float dBottom = bottomEdge - fy;
            float dMin    = fminf(fminf(dLeft, dRight), fminf(dTop, dBottom));

            // Delicate wispy deckle edge fuzz
            float edgeNoise = (Tal_Hash1(fx * 1.7f + fy * 3.1f) - 0.5f) * 1.8f;
            float maskA = Tal_Clamp01((dMin + 1.1f + edgeNoise) * 0.55f);

            // Base mulberry paper tone with mottled clouds & aged patina vignette
            float diag = 0.5f * (tx + ty);
            float baseR = 0.953f - 0.125f * diag;
            float baseG = 0.922f - 0.145f * diag;
            float baseB = 0.847f - 0.176f * diag;

            float mottle = (Tal_Noise1D(fx * 0.045f + fy * 0.02f, 11.0f) - 0.5f) * 0.07f
                         + (Tal_Noise1D(fy * 0.055f - fx * 0.025f, 23.0f) - 0.5f) * 0.06f;
            float fiber  = (Tal_Hash1(floorf(fx * 0.5f) * 17.3f + floorf(fy * 1.3f) * 31.7f) - 0.5f) * 0.09f;
            float fleck  = (Tal_Hash1(fx * 19.1f + fy * 29.3f) > 0.992f) ? -0.22f : 0.0f;

            float dxC = (fx - (float)W * 0.5f) / (pw * 0.52f);
            float dyC = (fy - (float)H * 0.5f) / (ph * 0.52f);
            float rC  = sqrtf(dxC * dxC + dyC * dyC);
            float vig = Tal_Clamp01((rC - 0.72f) / 0.55f);

            float rr = Tal_Clamp01(baseR + laid + mottle + fiber + fleck - vig * 0.18f);
            float gg = Tal_Clamp01(baseG + laid + mottle + fiber + fleck - vig * 0.21f);
            float bb = Tal_Clamp01(baseB + laid + mottle + fiber + fleck - vig * 0.24f);

            int idx4 = (y * W + x) * 4;
            s_talAtlasRGBA[idx4 + 0] = (unsigned char)(rr * 255.0f);
            s_talAtlasRGBA[idx4 + 1] = (unsigned char)(gg * 255.0f);
            s_talAtlasRGBA[idx4 + 2] = (unsigned char)(bb * 255.0f);
            s_talAtlasRGBA[idx4 + 3] = (unsigned char)(maskA * 255.0f);
        }
    }

    // B. Sharp Daoist double-border + corner cloud scrolls ("Vân Giác") + spirit diamonds
    const float bx = mx + 6.0f, by = my + 7.0f;
    const float bw = pw - 12.0f, bh = ph - 14.0f;
    Tal_StrokeRect(s_talBorder, bx, by, bw, bh, 2.8f, 1.0f);
    Tal_StrokeRect(s_talBorder, bx + 9.0f, by + 9.0f, bw - 18.0f, bh - 18.0f, 1.3f, 0.96f);

    // 4 Corner cloud-scroll brackets
    const float corners[4][4] = {
        { bx,      by,       1.0f,  1.0f },
        { bx + bw, by,      -1.0f,  1.0f },
        { bx,      by + bh,  1.0f, -1.0f },
        { bx + bw, by + bh, -1.0f, -1.0f }
    };
    for (int c = 0; c < 4; c++) {
        float cx = corners[c][0], cy = corners[c][1], sx = corners[c][2], sy = corners[c][3];
        Tal_StrokeLine(s_talBorder, cx, cy + sy * 22.0f, cx, cy, 1.4f, 1.0f);
        Tal_StrokeLine(s_talBorder, cx, cy, cx + sx * 22.0f, cy, 1.4f, 1.0f);
        Tal_StrokeLine(s_talBorder, cx + sx * 7.0f, cy + sy * 17.0f, cx + sx * 7.0f, cy + sy * 7.0f, 1.1f, 0.95f);
        Tal_StrokeLine(s_talBorder, cx + sx * 7.0f, cy + sy * 7.0f, cx + sx * 17.0f, cy + sy * 7.0f, 1.1f, 0.95f);
        Tal_PlotDisc(s_talBorder, cx + sx * 14.5f, cy + sy * 14.5f, 2.2f, 0.95f);
    }

    // Side midpoint diamond spirit nodes
    const float nodes[4][2] = {
        { bx, (float)H * 0.5f }, { bx + bw, (float)H * 0.5f },
        { (float)W * 0.5f, by }, { (float)W * 0.5f, by + bh }
    };
    for (int n = 0; n < 4; n++) {
        Tal_PlotDisc(s_talBorder, nodes[n][0], nodes[n][1], 4.2f, 1.0f);
    }

    // Copy softer border into rune layer
    for (int i = 0; i < W * H; i++) s_talRune[i] = s_talBorder[i] * 0.88f;

    // C. Daoist Calligraphy (Tam Thanh header, seal bars, Taiji + Bagua, cursive coil, Big Dipper, Master Seal)
    const float midX = (float)W * 0.5f;

    // 1. Tam Thanh (三清) canopy + 3 vertical hooks
    const float canopy[][2] = {
        { midX - 54.0f, my + 49.0f }, { midX - 27.0f, my + 33.0f },
        { midX,         my + 41.0f }, { midX + 27.0f, my + 33.0f },
        { midX + 54.0f, my + 49.0f }
    };
    Tal_StrokeBrush(s_talRune, canopy, 5, 8.0f, 0.35f);
    const float hookL[][2] = { { midX - 32.0f, my + 26.0f }, { midX - 26.0f, my + 42.0f }, { midX - 37.0f, my + 47.0f } };
    const float hookM[][2] = { { midX,         my + 23.0f }, { midX,         my + 44.0f }, { midX - 6.0f,  my + 49.0f } };
    const float hookR[][2] = { { midX + 32.0f, my + 26.0f }, { midX + 26.0f, my + 42.0f }, { midX + 37.0f, my + 47.0f } };
    Tal_StrokeBrush(s_talRune, hookL, 3, 7.2f, 0.30f);
    Tal_StrokeBrush(s_talRune, hookM, 3, 7.6f, 0.30f);
    Tal_StrokeBrush(s_talRune, hookR, 3, 7.2f, 0.30f);

    // 2. Stylized "勅令" twin seal strokes
    // Left glyph (勅)
    const float c1[][2] = { { midX - 52.0f, my + 68.0f }, { midX - 18.0f, my + 68.0f } };
    const float c2[][2] = { { midX - 35.0f, my + 58.0f }, { midX - 35.0f, my + 102.0f } };
    const float c3[][2] = { { midX - 48.0f, my + 78.0f }, { midX - 22.0f, my + 78.0f }, { midX - 22.0f, my + 90.0f }, { midX - 48.0f, my + 90.0f }, { midX - 48.0f, my + 78.0f } };
    const float c4[][2] = { { midX - 35.0f, my + 90.0f }, { midX - 50.0f, my + 103.0f } };
    const float c5[][2] = { { midX - 35.0f, my + 90.0f }, { midX - 18.0f, my + 102.0f } };
    Tal_StrokeBrush(s_talRune, c1, 2, 7.2f, 0.25f);
    Tal_StrokeBrush(s_talRune, c2, 2, 7.6f, 0.25f);
    Tal_StrokeBrush(s_talRune, c3, 5, 6.0f, 0.20f);
    Tal_StrokeBrush(s_talRune, c4, 2, 6.4f, 0.25f);
    Tal_StrokeBrush(s_talRune, c5, 2, 6.4f, 0.25f);
    // Right glyph (令)
    const float l1[][2] = { { midX + 34.0f, my + 58.0f }, { midX + 14.0f, my + 76.0f } };
    const float l2[][2] = { { midX + 34.0f, my + 58.0f }, { midX + 54.0f, my + 76.0f } };
    const float l3[][2] = { { midX + 24.0f, my + 80.0f }, { midX + 46.0f, my + 80.0f }, { midX + 34.0f, my + 92.0f }, { midX + 44.0f, my + 103.0f } };
    const float l4[][2] = { { midX + 28.0f, my + 71.0f }, { midX + 38.0f, my + 75.0f } };
    Tal_StrokeBrush(s_talRune, l1, 2, 7.2f, 0.25f);
    Tal_StrokeBrush(s_talRune, l2, 2, 7.2f, 0.25f);
    Tal_StrokeBrush(s_talRune, l3, 4, 7.2f, 0.25f);
    Tal_StrokeBrush(s_talRune, l4, 2, 6.4f, 0.20f);

    // 3. Taiji Circle + S-curve + Flanking Bagua Trigrams
    const float taiY = my + 135.0f;
    const float taiR = 19.0f;
    float ringPts[25][2];
    for (int k = 0; k <= 24; k++) {
        float a = ((float)k / 24.0f) * 2.0f * PI;
        ringPts[k][0] = midX + cosf(a) * taiR;
        ringPts[k][1] = taiY + sinf(a) * taiR;
    }
    Tal_StrokeBrush(s_talRune, ringPts, 25, 2.9f, 0.20f);
    const float sCurve[][2] = {
        { midX, taiY - taiR }, { midX + 7.5f, taiY - taiR * 0.5f },
        { midX, taiY },        { midX - 7.5f, taiY + taiR * 0.5f },
        { midX, taiY + taiR }
    };
    Tal_StrokeBrush(s_talRune, sCurve, 5, 2.7f, 0.15f);
    Tal_PlotDisc(s_talRune, midX - 1.0f, taiY - 9.0f, 2.8f, 1.0f);
    Tal_PlotDisc(s_talRune, midX + 1.0f, taiY + 9.0f, 2.8f, 1.0f);

    for (int row = 0; row < 3; row++) {
        float y = taiY - 11.0f + (float)row * 11.0f;
        const float trigL[][2]  = { { midX - 62.0f, y }, { midX - 33.0f, y } };
        const float trigR1[][2] = { { midX + 33.0f, y }, { midX + 44.5f, y } };
        const float trigR2[][2] = { { midX + 50.5f, y }, { midX + 62.0f, y } };
        Tal_StrokeBrush(s_talRune, trigL,  2, 3.3f, 0.20f);
        Tal_StrokeBrush(s_talRune, trigR1, 2, 3.3f, 0.20f);
        Tal_StrokeBrush(s_talRune, trigR2, 2, 3.3f, 0.20f);
    }

    // 4. Flowing Cursive Zigzag Talisman Coil + Central Power Spine
    const float coilPts[][2] = {
        { midX - 48.0f, my + 170.0f }, { midX + 49.0f, my + 166.0f },
        { midX - 55.0f, my + 199.0f }, { midX + 56.0f, my + 210.0f },
        { midX - 51.0f, my + 244.0f }, { midX + 53.0f, my + 259.0f },
        { midX - 46.0f, my + 281.0f }, { midX + 46.0f, my + 297.0f },
        { midX - 36.0f, my + 316.0f }, { midX + 33.0f, my + 330.0f },
        { midX - 15.0f, my + 347.0f }, { midX + 13.0f, my + 361.0f },
        { midX,         my + 384.0f }
    };
    Tal_StrokeBrush(s_talRune, coilPts, 13, 6.4f, 0.35f);
    const float spinePts[][2] = {
        { midX,        my + 159.0f }, { midX - 1.0f, my + 265.0f },
        { midX + 1.0f, my + 340.0f }, { midX,        my + 390.0f }
    };
    Tal_StrokeBrush(s_talRune, spinePts, 4, 5.4f, 0.30f);

    // 5. Big Dipper (Thất Tinh / Bắc Đẩu) 7-star constellation
    const float stars[][2] = {
        { midX - 57.0f, my + 334.0f }, { midX - 43.0f, my + 344.0f }, { midX - 27.0f, my + 349.0f },
        { midX - 10.0f, my + 355.0f }, { midX -  2.0f, my + 371.0f }, { midX + 18.0f, my + 368.0f },
        { midX + 24.0f, my + 352.0f }
    };
    Tal_StrokeBrush(s_talRune, stars, 7, 1.7f, 0.12f);
    for (int st = 0; st < 7; st++) {
        Tal_PlotDisc(s_talRune, stars[st][0], stars[st][1], 3.2f, 1.0f);
    }

    // 6. Square Master Seal Stamp ("茅山道印") at lower-right
    const float sSize = 37.0f;
    const float sx = bx + bw - sSize - 13.0f;
    const float sy = by + bh - sSize - 14.0f;
    Tal_StrokeRect(s_talRune, sx, sy, sSize, sSize, 2.0f, 0.96f);
    Tal_StrokeRect(s_talRune, sx + 3.5f, sy + 3.5f, sSize - 7.0f, sSize - 7.0f, 0.8f, 0.92f);
    // Internal 2x2 seal grid strokes
    Tal_StrokeLine(s_talRune, sx + sSize * 0.5f, sy + 5.0f, sx + sSize * 0.5f, sy + sSize - 5.0f, 1.1f, 0.9f);
    Tal_StrokeLine(s_talRune, sx + 5.0f, sy + sSize * 0.5f, sx + sSize - 5.0f, sy + sSize * 0.5f, 1.1f, 0.9f);
    Tal_StrokeLine(s_talRune, sx + 8.0f, sy + 10.0f, sx + 15.0f, sy + 14.0f, 1.2f, 0.9f);
    Tal_StrokeLine(s_talRune, sx + 22.0f, sy + 10.0f, sx + 29.0f, sy + 14.0f, 1.2f, 0.9f);
    Tal_StrokeLine(s_talRune, sx + 8.0f, sy + 24.0f, sx + 15.0f, sy + 29.0f, 1.2f, 0.9f);
    Tal_StrokeLine(s_talRune, sx + 22.0f, sy + 24.0f, sx + 29.0f, sy + 29.0f, 1.2f, 0.9f);

    // D. Multi-pass optical bloom glow (radiates cleanly into the 8.5% UV margin)
    for (int i = 0; i < W * H; i++) {
        s_talBufA[i] = Tal_Clamp01(s_talBorder[i] * 1.0f + s_talRune[i] * 0.65f);
    }
    memset(s_talGlowAccum, 0, sizeof(s_talGlowAccum));

    Tal_SeparableBoxBlur(s_talBufA, s_talBufB, s_talTmp, 4);
    for (int i = 0; i < W * H; i++) s_talGlowAccum[i] += s_talBufB[i] * 0.88f;

    Tal_SeparableBoxBlur(s_talBufB, s_talBufA, s_talTmp, 9);
    for (int i = 0; i < W * H; i++) s_talGlowAccum[i] += s_talBufA[i] * 0.76f;

    Tal_SeparableBoxBlur(s_talBufA, s_talBufB, s_talTmp, 16);
    for (int i = 0; i < W * H; i++) s_talGlowAccum[i] += s_talBufB[i] * 0.68f;

    // E. Back-face ink bleed-through (soft blur of s_talRune into s_talBufB)
    Tal_SeparableBoxBlur(s_talRune, s_talBufB, s_talTmp, 2);

    // F. Pack bottom half of stacked 256x1024 atlas:
    //    R = crisp rune, G = outer bloom (faded at canvas edge), B = back bleed, A = sharp border
    const float padX = 6.0f, padY = 8.0f;
    const int bottomOffset = W * H * 4;
    for (int y = 0; y < H; y++) {
        float fadeY = Tal_Clamp01(fminf((float)y / padY, (float)(H - 1 - y) / padY));
        for (int x = 0; x < W; x++) {
            float fadeX = Tal_Clamp01(fminf((float)x / padX, (float)(W - 1 - x) / padX));
            float edgeFade = fadeX * fadeY;

            int idx = y * W + x;
            int idx4 = bottomOffset + idx * 4;
            float rVal = Tal_Clamp01(s_talRune[idx]);
            float gVal = Tal_Clamp01(s_talGlowAccum[idx] * edgeFade);
            float bVal = Tal_Clamp01(s_talBufB[idx] * 0.72f);
            float aVal = Tal_Clamp01(s_talBorder[idx]);

            s_talAtlasRGBA[idx4 + 0] = (unsigned char)(rVal * 255.0f);
            s_talAtlasRGBA[idx4 + 1] = (unsigned char)(gVal * 255.0f);
            s_talAtlasRGBA[idx4 + 2] = (unsigned char)(bVal * 255.0f);
            s_talAtlasRGBA[idx4 + 3] = (unsigned char)(aVal * 255.0f);
        }
    }

    Image atlasImg = {
        .data = s_talAtlasRGBA, .width = W, .height = H * 2,
        .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    s_talismanAtlasTex = LoadTextureFromImage(atlasImg);
    GenTextureMipmaps(&s_talismanAtlasTex);
    SetTextureFilter(s_talismanAtlasTex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(s_talismanAtlasTex, TEXTURE_WRAP_CLAMP);
}

static void Talisman_EnsureGpuReady(void)
{
    if (s_talismanGpuReady) return;
    s_talismanGpuReady = true;

    s_talismanShader = ResourceManager_LoadShader("core/shaders/talisman.vs", "core/shaders/talisman.fs");
    if (s_talismanShader.id != 0) {
        s_talismanLocModeBlend     = GetShaderLocation(s_talismanShader, "u_modeBlend");
        s_talismanLocGlowIntensity = GetShaderLocation(s_talismanShader, "u_glowIntensity");
        s_talismanLocPremultiplied = GetShaderLocation(s_talismanShader, "u_premultiplied");
        s_talismanLocWarmPos       = GetShaderLocation(s_talismanShader, "u_warmLightPos");
        s_talismanLocWarmCol       = GetShaderLocation(s_talismanShader, "u_warmLightColor");
        s_talismanLocCoolPos       = GetShaderLocation(s_talismanShader, "u_coolLightPos");
        s_talismanLocCoolCol       = GetShaderLocation(s_talismanShader, "u_coolLightColor");
    }
    Tal_GenerateAtlasTextures();
}

// ── Pool & Mode Spawners ────────────────────────────────────────────────────

static int Talisman_ResolvePalette(int paletteMode, int idx)
{
    if (paletteMode == VFX_TALISMAN_PALETTE_MIXED) return idx % 3;
    if (paletteMode < 0) return 0;
    if (paletteMode > 2) return 2;
    return paletteMode;
}

static void Talisman_SpawnCloseUp(int i, Vector3 origin, int paletteMode)
{
    TalismanInstance *inst = &s_talismanPool[i];
    Quaternion q = QuaternionFromEuler(0.04f, 0.08f, 0.02f);
    Vector3 pos = { origin.x, origin.y + 1.54f, origin.z };
    SheetAero_InitState(&inst->aero, pos, q, 1.48f, 0.08f, 13.7f);
    inst->aero.cup = 0.08f;
    inst->aero.flutterAmp = 0.01f;
    inst->paletteId = Talisman_ResolvePalette(paletteMode, i);
    inst->throwBoost = 0.0f;
    inst->alpha = 1.0f;
    inst->active = true;
}

static void Talisman_SpawnFalling(int i, Vector3 origin, bool highOnly, int paletteMode, unsigned int *rng)
{
    TalismanInstance *inst = &s_talismanPool[i];
    float a = Tal_Randf(rng) * 2.0f * PI;
    float r = Tal_Randf(rng) * 3.8f;
    float y = highOnly ? (5.2f + Tal_Randf(rng) * 2.8f) : (0.9f + Tal_Randf(rng) * 5.6f);
    Vector3 pos = { origin.x + cosf(a) * r, origin.y + y, origin.z + sinf(a) * r };
    Quaternion q = QuaternionFromEuler(
        (Tal_Randf(rng) - 0.5f) * 1.3f + PI * 0.35f,
        Tal_Randf(rng) * 2.0f * PI,
        (Tal_Randf(rng) - 0.5f) * 1.2f
    );
    float curl = (Tal_Randf(rng) - 0.5f) * 0.24f;
    float scale = 0.90f + Tal_Randf(rng) * 0.18f;
    SheetAero_InitState(&inst->aero, pos, q, scale, curl, Tal_Randf(rng) * 100.0f);
    inst->aero.vel = (Vector3){
        (Tal_Randf(rng) - 0.5f) * 1.4f,
        -0.35f - Tal_Randf(rng) * 0.45f,
        (Tal_Randf(rng) - 0.5f) * 1.4f
    };
    inst->aero.angVel = (Vector3){
        (Tal_Randf(rng) - 0.5f) * 1.4f,
        (Tal_Randf(rng) - 0.5f) * 1.4f,
        (Tal_Randf(rng) - 0.5f) * 1.4f
    };
    inst->paletteId = Talisman_ResolvePalette(paletteMode, i);
    inst->throwBoost = 0.0f;
    inst->alpha = 1.0f;
    inst->active = true;
}

static void Talisman_SpawnVortex(int i, int total, Vector3 origin, int paletteMode, unsigned int *rng)
{
    TalismanInstance *inst = &s_talismanPool[i];
    float a = ((float)i / (float)(total > 0 ? total : 1)) * 2.0f * PI + Tal_Randf(rng) * 0.4f;
    float r = 1.5f + Tal_Randf(rng) * 2.6f;
    Vector3 pos = { origin.x + cosf(a) * r, origin.y + 0.8f + Tal_Randf(rng) * 3.4f, origin.z + sinf(a) * r };
    Quaternion q = QuaternionFromEuler(PI * 0.42f, -a, (Tal_Randf(rng) - 0.5f) * 0.8f);
    float curl = (Tal_Randf(rng) - 0.5f) * 0.22f;
    float scale = 0.90f + Tal_Randf(rng) * 0.18f;
    SheetAero_InitState(&inst->aero, pos, q, scale, curl, Tal_Randf(rng) * 100.0f);
    inst->aero.vel = (Vector3){ -sinf(a) * 2.6f, (Tal_Randf(rng) - 0.5f) * 0.6f, cosf(a) * 2.6f };
    inst->aero.bend = 0.15f;
    inst->paletteId = Talisman_ResolvePalette(paletteMode, i);
    inst->throwBoost = 0.0f;
    inst->alpha = 1.0f;
    inst->active = true;
}

void VFX_Talisman_Throw(Vector3 origin, Vector3 aimDir)
{
    static unsigned int s_throwRng = 881723u;
    int maxThrown = s_talismanLiveCfgInit ? s_talismanLiveCfg.count : 36;
    if (maxThrown < 16) maxThrown = 16;
    if (maxThrown > TALISMAN_MAX_INSTANCES) maxThrown = TALISMAN_MAX_INSTANCES;

    int idx;
    if (s_talismanActiveCount < maxThrown) {
        idx = s_talismanActiveCount++;
    } else {
        idx = s_talismanThrowCursor % maxThrown;
        s_talismanThrowCursor++;
    }

    Vector3 lead = aimDir;
    if (Vector3LengthSqr(lead) < 1e-5f) lead = (Vector3){ 1.0f, 0.16f, 0.0f };
    lead.y = fmaxf(-0.06f, lead.y + 0.14f);
    lead = Vector3Normalize(lead);

    float bank = (Tal_Randf(&s_throwRng) - 0.5f) * 0.52f;
    Quaternion q;
    Vector3 side, leadN, norm;
    SheetAero_ComputeLaunchBasis(lead, bank, &q, &side, &leadN, &norm);

    TalismanInstance *inst = &s_talismanPool[idx];
    float curl = (Tal_Randf(&s_throwRng) - 0.5f) * 0.20f;
    SheetAero_InitState(&inst->aero, origin, q, 1.0f, curl, Tal_Randf(&s_throwRng) * 100.0f);

    float speed = 13.5f + Tal_Randf(&s_throwRng) * 2.5f;
    inst->aero.vel = Vector3Scale(leadN, speed);
    float rollSpin = (Tal_Randf(&s_throwRng) - 0.5f) * 2.2f;
    inst->aero.angVel = (Vector3){
        side.x * 1.1f + leadN.x * rollSpin,
        side.y * 1.1f + leadN.y * rollSpin,
        side.z * 1.1f + leadN.z * rollSpin
    };
    inst->aero.bend  = 0.05f;
    inst->aero.sBend = 0.35f;
    inst->aero.sBendV = -1.2f;
    inst->aero.twist = (Tal_Randf(&s_throwRng) - 0.5f) * 0.3f;
    inst->aero.cup   = 0.10f;
    inst->aero.flutterAmp = 0.09f;

    int palMode = s_talismanLiveCfgInit ? (int)s_talismanLiveCfg.palette : VFX_TALISMAN_PALETTE_IMPERIAL;
    inst->paletteId = Talisman_ResolvePalette(palMode, idx);
    inst->throwBoost = 1.35f;
    inst->alpha = 1.0f;
    inst->active = true;
}

VFX_TalismanConfig VFX_Talisman_DefaultConfig(void)
{
    VFX_TalismanConfig cfg = {0};
    cfg.origin        = (Vector3){ 6.0f, 0.0f, 4.4f };
    cfg.flightMode    = VFX_TALISMAN_MODE_VORTEX;
    cfg.visualMode    = VFX_TALISMAN_VISUAL_GLOW;
    cfg.palette       = VFX_TALISMAN_PALETTE_IMPERIAL;
    cfg.count         = 36;
    cfg.windStrength  = 1.0f;
    cfg.paperSoftness = 0.55f;
    cfg.glowIntensity = 1.15f;
    cfg.castShadow    = true;
    return cfg;
}

VFX_TalismanConfig *VFX_Talisman_GetLiveConfig(void)
{
    if (!s_talismanLiveCfgInit) {
        s_talismanLiveCfg = VFX_Talisman_DefaultConfig();
        s_talismanLiveCfgInit = true;
    }
    return &s_talismanLiveCfg;
}

int VFX_Talisman_GetParams(VFX_TalismanConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    static const char * const kFlightNames[]  = { "CLOSE-UP", "FALLING", "VORTEX", "FLICK THROW" };
    static const char * const kVisualNames[]  = { "HANDMADE PAPER", "LUMINOUS GLOW" };
    static const char * const kPaletteNames[] = { "IMPERIAL YELLOW", "FIRE VERMILION", "THUNDER INDIGO", "MIXED" };

    int n = 0;
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Flight Mode",   .group = "Talisman", .type = VFX_PARAM_ENUM,  .valPtr = &cfg->flightMode,    .minInt = 0, .maxInt = VFX_TALISMAN_MODE_COUNT - 1,    .enumNames = kFlightNames,  .enumCount = VFX_TALISMAN_MODE_COUNT };
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Visual Style",  .group = "Talisman", .type = VFX_PARAM_ENUM,  .valPtr = &cfg->visualMode,    .minInt = 0, .maxInt = VFX_TALISMAN_VISUAL_COUNT - 1,  .enumNames = kVisualNames,  .enumCount = VFX_TALISMAN_VISUAL_COUNT };
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Palette",       .group = "Talisman", .type = VFX_PARAM_ENUM,  .valPtr = &cfg->palette,       .minInt = 0, .maxInt = VFX_TALISMAN_PALETTE_COUNT - 1, .enumNames = kPaletteNames, .enumCount = VFX_TALISMAN_PALETTE_COUNT };
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Count",         .group = "Talisman", .type = VFX_PARAM_INT,   .valPtr = &cfg->count,         .minInt = 8, .maxInt = 100 };
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Wind Strength", .group = "Talisman", .type = VFX_PARAM_FLOAT, .valPtr = &cfg->windStrength,  .minFloat = 0.0f,  .maxFloat = 2.5f, .stepFloat = 0.25f };
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Paper Softness",.group = "Talisman", .type = VFX_PARAM_FLOAT, .valPtr = &cfg->paperSoftness, .minFloat = 0.15f, .maxFloat = 1.0f, .stepFloat = 0.10f };
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Glow Intensity",.group = "Talisman", .type = VFX_PARAM_FLOAT, .valPtr = &cfg->glowIntensity, .minFloat = 0.4f,  .maxFloat = 2.2f, .stepFloat = 0.20f };
    if (n < maxParams) outParams[n++] = (VFX_ParamDef){ .name = "Cast Shadow",   .group = "Talisman", .type = VFX_PARAM_BOOL,  .valPtr = &cfg->castShadow };
    return n;
}

void VFX_Talisman_Reset(void)
{
    for (int i = 0; i < TALISMAN_MAX_INSTANCES; i++) {
        s_talismanPool[i].active = false;
    }
    s_talismanActiveCount = 0;
    s_talismanThrowCursor = 0;
    s_talismanThrowAutoTimer = 0.0f;
    s_talismanLastFlightMode = -1;
    s_talismanLastCount = -1;
    s_talismanLastPalette = -1;
    s_talismanActiveThisFrame = false;
    s_talismanDrawThisFrame = false;
    s_talismanShadowActive = false;
    s_talismanPoolInitialized = true;
}

void VFX_ComposeTalisman(const VFX_TalismanConfig *config)
{
    if (!config) return;
    s_talismanLiveCfg = *config;
    s_talismanLiveCfgInit = true;
    s_talismanOrigin = config->origin;
    s_talismanActiveThisFrame = true;
    s_talismanDrawThisFrame = true;
    s_talismanShadowActive = config->castShadow;

    if (!s_talismanPoolInitialized) {
        VFX_Talisman_Reset();
        s_talismanModeBlend = (config->visualMode == VFX_TALISMAN_VISUAL_GLOW) ? 1.0f : 0.0f;
    }

    int cnt = config->count;
    if (cnt < 1) cnt = 1;
    if (cnt > TALISMAN_MAX_INSTANCES) cnt = TALISMAN_MAX_INSTANCES;

    int mode = (int)config->flightMode;
    int pal  = (int)config->palette;

    if (mode != s_talismanLastFlightMode) {
        static unsigned int s_spawnRng = 1234567u;
        s_talismanLastFlightMode = mode;
        s_talismanLastCount = cnt;
        s_talismanLastPalette = pal;
        s_talismanThrowAutoTimer = 0.0f;
        for (int i = 0; i < TALISMAN_MAX_INSTANCES; i++) s_talismanPool[i].active = false;

        if (mode == VFX_TALISMAN_MODE_CLOSEUP) {
            s_talismanActiveCount = 1;
            Talisman_SpawnCloseUp(0, s_talismanOrigin, pal);
        } else if (mode == VFX_TALISMAN_MODE_FALLING) {
            s_talismanActiveCount = cnt;
            for (int i = 0; i < cnt; i++) Talisman_SpawnFalling(i, s_talismanOrigin, false, pal, &s_spawnRng);
        } else if (mode == VFX_TALISMAN_MODE_VORTEX) {
            s_talismanActiveCount = cnt;
            for (int i = 0; i < cnt; i++) Talisman_SpawnVortex(i, cnt, s_talismanOrigin, pal, &s_spawnRng);
        } else {
            s_talismanActiveCount = 0;
            s_talismanThrowCursor = 0;
            Vector3 launchPos = { s_talismanOrigin.x - 1.5f, s_talismanOrigin.y + 1.45f, s_talismanOrigin.z };
            VFX_Talisman_Throw(launchPos, (Vector3){ 1.0f, 0.16f, 0.05f });
        }
    } else {
        if (cnt != s_talismanLastCount && (mode == VFX_TALISMAN_MODE_FALLING || mode == VFX_TALISMAN_MODE_VORTEX)) {
            static unsigned int s_resizeRng = 77319u;
            if (cnt > s_talismanActiveCount) {
                for (int i = s_talismanActiveCount; i < cnt; i++) {
                    if (mode == VFX_TALISMAN_MODE_FALLING) Talisman_SpawnFalling(i, s_talismanOrigin, false, pal, &s_resizeRng);
                    else Talisman_SpawnVortex(i, cnt, s_talismanOrigin, pal, &s_resizeRng);
                }
            } else {
                for (int i = cnt; i < s_talismanActiveCount; i++) s_talismanPool[i].active = false;
            }
            s_talismanActiveCount = cnt;
            s_talismanLastCount = cnt;
        }
        if (pal != s_talismanLastPalette) {
            s_talismanLastPalette = pal;
            for (int i = 0; i < TALISMAN_MAX_INSTANCES; i++) {
                s_talismanPool[i].paletteId = Talisman_ResolvePalette(pal, i);
            }
        }
    }
}

void VC_Talisman_Update(float dt)
{
    if (!s_talismanActiveThisFrame) {
        s_talismanShadowActive = false;
        return;
    }
    if (s_talismanActiveCount <= 0 || dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    const VFX_TalismanConfig *cfg = VFX_Talisman_GetLiveConfig();
    float targetBlend = (cfg->visualMode == VFX_TALISMAN_VISUAL_GLOW) ? 1.0f : 0.0f;
    s_talismanModeBlend += (targetBlend - s_talismanModeBlend) * fminf(1.0f, dt * 8.0f);

    float t = TimeFX_Elapsed();
    float wStr = cfg->windStrength;
    int mode = (int)cfg->flightMode;

    // Auto-throw periodic volleys in FLICK THROW mode
    if (mode == VFX_TALISMAN_MODE_THROW) {
        s_talismanThrowAutoTimer += dt;
        if (s_talismanThrowAutoTimer >= 0.48f) {
            s_talismanThrowAutoTimer = 0.0f;
            float spreadZ = sinf(t * 2.7f) * 0.28f;
            float spreadY = 0.14f + 0.08f * cosf(t * 1.9f);
            Vector3 launchPos = { s_talismanOrigin.x - 1.8f, s_talismanOrigin.y + 1.45f, s_talismanOrigin.z + spreadZ * 0.6f };
            VFX_Talisman_Throw(launchPos, (Vector3){ 1.0f, spreadY, spreadZ });
        }
    }

    SheetAeroParams aeroP = SheetAero_DefaultPaper();
    aeroP.halfWidth  = TALISMAN_HALF_W;
    aeroP.halfHeight = TALISMAN_HALF_H;
    aeroP.softness   = cfg->paperSoftness;

    if (mode == VFX_TALISMAN_MODE_CLOSEUP) {
        aeroP.tetherEnabled  = true;
        aeroP.tetherPos      = (Vector3){ s_talismanOrigin.x, s_talismanOrigin.y + 1.54f, s_talismanOrigin.z };
        aeroP.uprightRestore = 5.2f;
        aeroP.enableFloor    = false;
    } else if (mode == VFX_TALISMAN_MODE_VORTEX) {
        aeroP.gravity = 0.72f;
    } else {
        aeroP.gravity = 3.15f;
    }

    static unsigned int s_recycleRng = 551923u;

    for (int i = 0; i < s_talismanActiveCount; i++) {
        TalismanInstance *inst = &s_talismanPool[i];
        if (!inst->active) continue;

        Vector3 p = inst->aero.pos;
        float relX = p.x - s_talismanOrigin.x;
        float relY = p.y - s_talismanOrigin.y;
        float relZ = p.z - s_talismanOrigin.z;
        float sd = inst->aero.seed;

        // Sample mode-specific wind + engine environmental Wind_EvaluateVelocity
        Vector3 wind = Wind_EvaluateVelocity(p, t);
        if (mode == VFX_TALISMAN_MODE_CLOSEUP) {
            float gust = 0.60f + 0.45f * sinf(t * 0.95f) + 0.22f * sinf(t * 2.4f);
            wind.x += (sinf(t * 0.72f + relY * 1.1f) * 0.55f + 0.35f * gust) * wStr * 0.95f;
            wind.y += cosf(t * 0.65f) * 0.18f * wStr;
            wind.z += (cosf(t * 0.88f + relX * 1.2f) * 0.65f - 0.32f * gust) * wStr * 0.95f;
        } else if (mode == VFX_TALISMAN_MODE_FALLING) {
            float sWave = fmaxf(0.0f, sinf(t * 0.52f + relX * 0.32f + relZ * 0.2f));
            float floorGust = (relY < 0.55f) ? (sWave * sWave * sWave * sWave * 3.2f) : 0.0f;
            wind.x += (sinf(t * 0.42f + relY * 0.5f + relZ * 0.3f) * 0.85f + 0.32f + floorGust) * wStr;
            wind.y += (sinf(t * 0.65f + relX * 0.55f) * 0.16f + floorGust * 0.35f) * wStr;
            wind.z += (cosf(t * 0.36f + relY * 0.45f - relX * 0.25f) * 0.75f) * wStr;
        } else if (mode == VFX_TALISMAN_MODE_VORTEX) {
            float r = hypotf(relX, relZ) + 0.001f;
            float tx = -relZ / r, tz = relX / r;
            float ang = atan2f(relZ, relX);
            float targetR = 2.35f + sinf(t * 0.45f + sd * 0.3f + relY * 0.65f) * 1.05f;
            float targetY = 2.10f + sinf(ang * 2.0f - t * 0.85f + sd * 0.2f) * 1.15f;
            float swirl = 2.9f + 0.45f * sinf(t * 0.85f + relY);
            float sFactor = 0.58f + wStr * 0.55f;
            wind.x += (tx * swirl + (relX / r) * (targetR - r) * 0.85f + sinf(t * 1.3f + relY) * 0.35f) * sFactor;
            wind.y += ((targetY - relY) * 0.95f + cosf(t * 1.1f + r) * 0.35f) * sFactor;
            wind.z += (tz * swirl + (relZ / r) * (targetR - r) * 0.85f + cosf(t * 1.2f + relY) * 0.35f) * sFactor;
        } else {
            wind.x += sinf(t * 0.48f + relY * 0.55f) * 0.48f * wStr;
            wind.y += cosf(t * 0.40f + relX * 0.50f) * 0.12f * wStr;
            wind.z += cosf(t * 0.44f + relZ * 0.55f) * 0.42f * wStr;
        }

        aeroP.floorY = MapManager_GetGroundHeightAt(p.x, p.z);
        SheetAero_Step(&inst->aero, &aeroP, wind, 0.0f, dt, t);

        if (inst->throwBoost > 0.001f) {
            inst->throwBoost = fmaxf(0.0f, inst->throwBoost - dt * 1.45f);
        }

        // Recycle out-of-bounds or long-settled sheets in swarm modes
        float distXZ = hypotf(inst->aero.pos.x - s_talismanOrigin.x, inst->aero.pos.z - s_talismanOrigin.z);
        if (mode == VFX_TALISMAN_MODE_FALLING) {
            if (inst->aero.restTime > 4.5f || distXZ > 8.5f) {
                Talisman_SpawnFalling(i, s_talismanOrigin, true, (int)cfg->palette, &s_recycleRng);
            }
        } else if (mode == VFX_TALISMAN_MODE_VORTEX) {
            if (distXZ > 9.0f || (inst->aero.pos.y - s_talismanOrigin.y) > 8.5f) {
                Talisman_SpawnVortex(i, s_talismanActiveCount, s_talismanOrigin, (int)cfg->palette, &s_recycleRng);
            }
        }
    }

    s_talismanActiveThisFrame = false;
}

static void Talisman_EmitSheetGrid(const TalismanInstance *inst, int segX, int segY)
{
    const SheetAeroState *s = &inst->aero;
    Vector3 R, U, N;
    SheetAero_ExtractBasis(s->quat, &R, &U, &N);

    float sc = s->scale;
    unsigned char colR = (unsigned char)(inst->paletteId == 0 ? 0 : (inst->paletteId == 1 ? 128 : 255));
    unsigned char colG = (unsigned char)(Tal_Clamp01(inst->throwBoost * 0.5f) * 255.0f);
    unsigned char colB = (unsigned char)(Tal_Clamp01(inst->alpha) * 255.0f);
    rlColor4ub(colR, colG, colB, 255);

    // Precompute deformed grid vertices & normals (max 9 x 21 = 189 verts on stack)
    Vector3 wPos[21][9];
    Vector3 wNrm[21][9];
    float   uCoord[9];
    float   vCoord[21];

    for (int ix = 0; ix <= segX; ix++) uCoord[ix] = (float)ix / (float)segX;
    for (int iy = 0; iy <= segY; iy++) vCoord[iy] = (float)iy / (float)segY;

    for (int iy = 0; iy <= segY; iy++) {
        float ly = (-1.0f + 2.0f * vCoord[iy]) * TALISMAN_HALF_H;
        for (int ix = 0; ix <= segX; ix++) {
            float lx = (-1.0f + 2.0f * uCoord[ix]) * TALISMAN_HALF_W;
            Vector3 dp, dn;
            SheetAero_DeformPoint((Vector3){ lx, ly, 0.0f }, TALISMAN_HALF_W, TALISMAN_HALF_H,
                                  s->bend, s->sBend, s->twist, s->cup,
                                  s->flutterAmp, s->flutterPhase, &dp, &dn);
            wPos[iy][ix] = (Vector3){
                s->pos.x + (R.x * dp.x + U.x * dp.y + N.x * dp.z) * sc,
                s->pos.y + (R.y * dp.x + U.y * dp.y + N.y * dp.z) * sc,
                s->pos.z + (R.z * dp.x + U.z * dp.y + N.z * dp.z) * sc
            };
            wNrm[iy][ix] = (Vector3){
                R.x * dn.x + U.x * dn.y + N.x * dn.z,
                R.y * dn.x + U.y * dn.y + N.y * dn.z,
                R.z * dn.x + U.z * dn.y + N.z * dn.z
            };
        }
    }

    for (int iy = 0; iy < segY; iy++) {
        float v0 = 1.0f - vCoord[iy];
        float v1 = 1.0f - vCoord[iy + 1];
        for (int ix = 0; ix < segX; ix++) {
            float u0 = uCoord[ix];
            float u1 = uCoord[ix + 1];

            Vector3 p00 = wPos[iy][ix],     n00 = wNrm[iy][ix];
            Vector3 p10 = wPos[iy][ix + 1], n10 = wNrm[iy][ix + 1];
            Vector3 p11 = wPos[iy + 1][ix + 1], n11 = wNrm[iy + 1][ix + 1];
            Vector3 p01 = wPos[iy + 1][ix], n01 = wNrm[iy + 1][ix];

            // Triangle 1: (00, 10, 11)
            rlNormal3f(n00.x, n00.y, n00.z); rlTexCoord2f(u0, v0); rlVertex3f(p00.x, p00.y, p00.z);
            rlNormal3f(n10.x, n10.y, n10.z); rlTexCoord2f(u1, v0); rlVertex3f(p10.x, p10.y, p10.z);
            rlNormal3f(n11.x, n11.y, n11.z); rlTexCoord2f(u1, v1); rlVertex3f(p11.x, p11.y, p11.z);

            // Triangle 2: (00, 11, 01)
            rlNormal3f(n00.x, n00.y, n00.z); rlTexCoord2f(u0, v0); rlVertex3f(p00.x, p00.y, p00.z);
            rlNormal3f(n11.x, n11.y, n11.z); rlTexCoord2f(u1, v1); rlVertex3f(p11.x, p11.y, p11.z);
            rlNormal3f(n01.x, n01.y, n01.z); rlTexCoord2f(u0, v1); rlVertex3f(p01.x, p01.y, p01.z);
        }
    }
}

void VC_Talisman_Draw3D(Camera3D cam)
{
    (void)cam;
    if (!s_talismanDrawThisFrame || s_talismanActiveCount <= 0) return;
    s_talismanDrawThisFrame = false;
    Talisman_EnsureGpuReady();
    if (s_talismanShader.id == 0 || s_talismanAtlasTex.id == 0) return;

    const VFX_TalismanConfig *cfg = VFX_Talisman_GetLiveConfig();
    bool isCloseUp = (cfg->flightMode == VFX_TALISMAN_MODE_CLOSEUP);
    int segX = isCloseUp ? 8 : 4;
    int segY = isCloseUp ? 20 : 10;

    bool isGlowMode = (s_talismanModeBlend >= 0.5f);
    float premulFlag = isGlowMode ? 1.0f : 0.0f;
    VFXRenderScope scope = VFXRender_BeginDraw(
        isGlowMode ? VFX_RENDER_PASS_EMISSION : VFX_RENDER_PASS_BODY,
        isGlowMode ? VFX_SURFACE_PREMULTIPLIED : VFX_SURFACE_ALPHA,
        !isGlowMode);

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();

    SkillManager_BeginShader(s_talismanShader);

    if (s_talismanLocModeBlend >= 0)
        SetShaderValue(s_talismanShader, s_talismanLocModeBlend, &s_talismanModeBlend, SHADER_UNIFORM_FLOAT);
    if (s_talismanLocGlowIntensity >= 0)
        SetShaderValue(s_talismanShader, s_talismanLocGlowIntensity, &cfg->glowIntensity, SHADER_UNIFORM_FLOAT);
    if (s_talismanLocPremultiplied >= 0)
        SetShaderValue(s_talismanShader, s_talismanLocPremultiplied, &premulFlag, SHADER_UNIFORM_FLOAT);

    Vector3 warmPos = { s_talismanOrigin.x + 3.1f, s_talismanOrigin.y + 3.8f, s_talismanOrigin.z - 4.6f };
    Vector3 coolPos = { s_talismanOrigin.x - 5.2f, s_talismanOrigin.y + 6.4f, s_talismanOrigin.z + 4.8f };
    float warmCol[3] = { 1.00f, 0.62f, 0.24f };
    float coolCol[3] = { 0.49f, 0.68f, 1.00f };
    if (s_talismanLocWarmPos >= 0) SetShaderValue(s_talismanShader, s_talismanLocWarmPos, &warmPos, SHADER_UNIFORM_VEC3);
    if (s_talismanLocCoolPos >= 0) SetShaderValue(s_talismanShader, s_talismanLocCoolPos, &coolPos, SHADER_UNIFORM_VEC3);
    if (s_talismanLocWarmCol >= 0) SetShaderValue(s_talismanShader, s_talismanLocWarmCol, warmCol,  SHADER_UNIFORM_VEC3);
    if (s_talismanLocCoolCol >= 0) SetShaderValue(s_talismanShader, s_talismanLocCoolCol, coolCol,  SHADER_UNIFORM_VEC3);

    rlSetTexture(s_talismanAtlasTex.id);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < s_talismanActiveCount; i++) {
        if (!s_talismanPool[i].active) continue;
        Talisman_EmitSheetGrid(&s_talismanPool[i], segX, segY);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);

    SkillManager_EndShader();
    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
    VFXRender_EndDraw(&scope);
}

static void Talisman_EmitSheetGridShadow(const TalismanInstance *inst, int segX, int segY)
{
    const SheetAeroState *s = &inst->aero;
    Vector3 R, U, N;
    SheetAero_ExtractBasis(s->quat, &R, &U, &N);

    float sc = s->scale;
    // Crop to physical paper bounds (inside the 8.5% outer bloom margin) so the
    // cast shadow matches the true curved paper silhouette, not the transparent halo.
    const float uMin = 0.085f, uMax = 0.915f;
    const float vMin = 0.082f, vMax = 0.918f;

    Vector3 wPos[13][7];
    for (int iy = 0; iy <= segY; iy++) {
        float tv = vMin + (vMax - vMin) * ((float)iy / (float)segY);
        float ly = (-1.0f + 2.0f * tv) * TALISMAN_HALF_H;
        for (int ix = 0; ix <= segX; ix++) {
            float tu = uMin + (uMax - uMin) * ((float)ix / (float)segX);
            float lx = (-1.0f + 2.0f * tu) * TALISMAN_HALF_W;
            Vector3 dp;
            SheetAero_DeformPoint((Vector3){ lx, ly, 0.0f }, TALISMAN_HALF_W, TALISMAN_HALF_H,
                                  s->bend, s->sBend, s->twist, s->cup,
                                  s->flutterAmp, s->flutterPhase, &dp, NULL);
            wPos[iy][ix] = (Vector3){
                s->pos.x + (R.x * dp.x + U.x * dp.y + N.x * dp.z) * sc,
                s->pos.y + (R.y * dp.x + U.y * dp.y + N.y * dp.z) * sc,
                s->pos.z + (R.z * dp.x + U.z * dp.y + N.z * dp.z) * sc
            };
        }
    }

    rlColor4ub(255, 255, 255, 255);
    for (int iy = 0; iy < segY; iy++) {
        for (int ix = 0; ix < segX; ix++) {
            Vector3 p00 = wPos[iy][ix];
            Vector3 p10 = wPos[iy][ix + 1];
            Vector3 p11 = wPos[iy + 1][ix + 1];
            Vector3 p01 = wPos[iy + 1][ix];

            rlVertex3f(p00.x, p00.y, p00.z);
            rlVertex3f(p10.x, p10.y, p10.z);
            rlVertex3f(p11.x, p11.y, p11.z);

            rlVertex3f(p00.x, p00.y, p00.z);
            rlVertex3f(p11.x, p11.y, p11.z);
            rlVertex3f(p01.x, p01.y, p01.z);
        }
    }
}

void VC_Talisman_DrawShadowPass(void)
{
    if (!s_talismanShadowActive || s_talismanActiveCount <= 0) return;

    const VFX_TalismanConfig *cfg = VFX_Talisman_GetLiveConfig();
    if (!cfg->castShadow) return;

    bool isCloseUp = (cfg->flightMode == VFX_TALISMAN_MODE_CLOSEUP);
    int segX = isCloseUp ? 6 : 3;
    int segY = isCloseUp ? 12 : 6;

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDrawRenderBatchActive();

    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < s_talismanActiveCount; i++) {
        if (!s_talismanPool[i].active) continue;
        Talisman_EmitSheetGridShadow(&s_talismanPool[i], segX, segY);
    }
    rlEnd();
    rlDrawRenderBatchActive();

    rlEnableBackfaceCulling();
    rlDrawRenderBatchActive();
}
