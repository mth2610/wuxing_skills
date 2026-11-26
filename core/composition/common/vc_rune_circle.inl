// ── E5.2 — VFX_ComposeRuneCircle (Cổ Ấn Vạn Pháp — 6 WebGL Ancient Seals) ────
//
// Ported from the WebGL single-pass blackbody rune circle architecture:
//   - 6 distinct procedural 1024x1024 seals (lazy-cached on CPU, zero malloc):
//       0: Phần Thiên Cổ Ấn   (PHAN THIEN — Ancient Fire Seal)
//       1: Bát Quái Thái Cực  (BAT QUAI   — Classic Dao Gold + Bagua + Octagons)
//       2: Tru Tiên Kiếm Ấn   (TRU TIEN   — Frost Steel Cyan + 8 Sword Blades)
//       3: Huyết Sát Ma Ấn    (HUYET SAT  — Abyssal Crimson + Sawtooth & Spikes)
//       4: U Minh Linh Ấn     (U MINH     — Jade Soul Seal + Mirror Notches)
//       5: Tinh Tú Chu Thiên  (TINH TU    — Violet Astral + 28 Star Mansions)
//   - 3 thin counter-rotating parallax layers (Inner Core+Script, Outer Script+
//     Geometric Motifs, Polar Energy Pulse) + 1 batched Pinpoint Ember draw.
//   - Separate BODY (BLEND_ALPHA) and EMISSION (BLEND_ALPHA_PREMULTIPLY) passes
//     so every seal stays crisp on bright daylight scenery as well as dark rock.
//   - Terrain-conforming 16x16 draped surface (7x7 height grid) on ground seals;
//     flat quad for standing portals or flat/headless arenas.

#include <stdint.h>
#include <string.h>
#include <math.h>
#include "core/vfx_contrast.h"

#ifndef RUNE_PI
#define RUNE_PI  3.14159265358979323846f
#endif
#ifndef RUNE_TAU
#define RUNE_TAU 6.28318530717958647692f
#endif

// ── Tunables (lazily registered) ─────────────────────────────────────────────
static bool  s_runeInit     = false;
static float s_runeGlow     = 1.0f;
static float s_runeSpin     = 1.0f;
static float s_runeEmbers   = 1.0f;
static float s_runeParallax = 1.0f;
static float s_runeConform  = 1.0f;

// ── Variant Definitions (exact WebGL palette, rotation, seed) ────────────────
typedef struct {
    const char *name;
    Color core;
    Color mid;
    Color deep;
    float rot[3];
    uint32_t seed;
} RuneVariantDef;

static const RuneVariantDef s_runeDefs[VFX_RUNE_VARIANT_COUNT] = {
    [VFX_RUNE_PHAN_THIEN] = {
        .name = "PHAN THIEN",
        .core = { 255, 245, 204, 255 }, // #fff5cc
        .mid  = { 255, 140,  26, 255 }, // #ff8c1a
        .deep = { 122,  25,   0, 255 }, // #7a1900
        .rot  = { -0.22f, 0.14f, -0.09f },
        .seed = 103u,
    },
    [VFX_RUNE_BAT_QUAI] = {
        .name = "BAT QUAI",
        .core = { 255, 243, 196, 255 }, // #fff3c4
        .mid  = { 223, 160,  32, 255 }, // #dfa020
        .deep = {  87,  37,   0, 255 }, // #572500
        .rot  = { -0.18f, 0.16f, -0.11f },
        .seed = 211u,
    },
    [VFX_RUNE_TRU_TIEN] = {
        .name = "TRU TIEN",
        .core = { 235, 251, 255, 255 }, // #ebfbff
        .mid  = {  60, 196, 255, 255 }, // #3cc4ff
        .deep = {   7,  44,  77, 255 }, // #072c4d
        .rot  = { -0.28f, 0.19f, -0.15f },
        .seed = 317u,
    },
    [VFX_RUNE_HUYET_SAT] = {
        .name = "HUYET SAT",
        .core = { 255, 216, 204, 255 }, // #ffd8cc
        .mid  = { 230,  34,  23, 255 }, // #e62217
        .deep = {  74,   2,   4, 255 }, // #4a0204
        .rot  = { 0.24f, -0.18f, 0.12f },
        .seed = 421u,
    },
    [VFX_RUNE_U_MINH] = {
        .name = "U MINH",
        .core = { 224, 255, 244, 255 }, // #e0fff4
        .mid  = {  20, 207, 142, 255 }, // #14cf8e
        .deep = {   3,  56,  39, 255 }, // #033827
        .rot  = { 0.19f, -0.14f, 0.08f },
        .seed = 523u,
    },
    [VFX_RUNE_TINH_TU] = {
        .name = "TINH TU",
        .core = { 243, 230, 255, 255 }, // #f3e6ff
        .mid  = { 155, 100, 255, 255 }, // #9b64ff
        .deep = {  39,  12,  82, 255 }, // #270c52
        .rot  = { -0.15f, 0.12f, -0.08f },
        .seed = 631u,
    },
};

const char *VFX_RuneCircleVariant_Name(VFX_RuneCircleVariant variant)
{
    if ((int)variant < 0 || (int)variant >= VFX_RUNE_VARIANT_COUNT)
        return s_runeDefs[0].name;
    return s_runeDefs[variant].name;
}

VFX_RuneCircleVariant VFX_RuneCircle_VariantForMaterial(VC_MaterialId mat)
{
    switch (mat)
    {
        case VC_MAT_FIRE:      return VFX_RUNE_PHAN_THIEN;
        case VC_MAT_EARTH:
        case VC_MAT_TAIJI:
        case VC_MAT_HOLY:      return VFX_RUNE_BAT_QUAI;
        case VC_MAT_METAL:
        case VC_MAT_WATER:
        case VC_MAT_ICE:
        case VC_MAT_LIGHTNING:
        case VC_MAT_QI:        return VFX_RUNE_TRU_TIEN;
        case VC_MAT_WOOD:
        case VC_MAT_POISON:    return VFX_RUNE_U_MINH;
        case VC_MAT_VOID:      return VFX_RUNE_TINH_TU;
        default:               return VFX_RUNE_PHAN_THIEN;
    }
}

// ── Deterministic Mulberry32 PRNG (exact match to WebGL makeRNG) ────────────
static float Rune_RngNext(uint32_t *state)
{
    uint32_t s = (*state += 0x6D2B79F5u);
    uint32_t t = (s ^ (s >> 15)) * (1u | s);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)((t ^ (t >> 14)) >> 8) * (1.0f / 16777216.0f);
}

// ── 256x256 Seamless Periodic Noise Texture (generated once) ────────────────
#define RUNE_NOISE_SIZE 256
static Texture2D s_runeNoiseTex = {0};
static bool      s_runeNoiseBuilt = false;
static uint8_t   s_runeNoiseRGBA[RUNE_NOISE_SIZE * RUNE_NOISE_SIZE * 4];

static float Rune_SamplePeriodic(int x, int y, int period, uint32_t seedOff)
{
    float fx = ((float)x / (float)RUNE_NOISE_SIZE) * (float)period;
    float fy = ((float)y / (float)RUNE_NOISE_SIZE) * (float)period;
    int ix = (int)floorf(fx);
    int iy = (int)floorf(fy);
    float tx = fx - (float)ix;
    float ty = fy - (float)iy;
    float ux = tx * tx * (3.0f - 2.0f * tx);
    float uy = ty * ty * (3.0f - 2.0f * ty);

    int wx0 = ((ix % period) + period) % period;
    int wy0 = ((iy % period) + period) % period;
    int wx1 = (wx0 + 1) % period;
    int wy1 = (wy0 + 1) % period;

    float h[4];
    int px[4] = { wx0, wx1, wx0, wx1 };
    int py[4] = { wy0, wy0, wy1, wy1 };
    for (int k = 0; k < 4; k++)
    {
        uint32_t n = ((uint32_t)(px[k] + 374761393) * 668265263u) ^
                     ((uint32_t)(py[k] + 1274126177) * 2246822519u) ^ seedOff;
        n = (n ^ (n >> 13)) * 1274126177u;
        h[k] = (float)((n ^ (n >> 16)) >> 8) * (1.0f / 16777216.0f);
    }

    float ab = h[0] + (h[1] - h[0]) * ux;
    float cd = h[2] + (h[3] - h[2]) * ux;
    return ab + (cd - ab) * uy;
}

static void Rune_EnsureNoiseTex(void)
{
    if (s_runeNoiseBuilt) return;
    s_runeNoiseBuilt = true;

    for (int y = 0; y < RUNE_NOISE_SIZE; y++)
    {
        for (int x = 0; x < RUNE_NOISE_SIZE; x++)
        {
            int idx = (y * RUNE_NOISE_SIZE + x) * 4;
            float rVal = Rune_SamplePeriodic(x, y, 8, 101u) * 0.65f +
                         Rune_SamplePeriodic(x, y, 16, 202u) * 0.35f;
            float gVal = Rune_SamplePeriodic(x, y, 16, 303u) * 0.55f +
                         Rune_SamplePeriodic(x, y, 32, 404u) * 0.45f;
            float bVal = Rune_SamplePeriodic(x, y, 32, 505u) * 0.50f +
                         Rune_SamplePeriodic(x, y, 64, 606u) * 0.50f;
            float aVal = Rune_SamplePeriodic(x, y * 2, 12, 707u) * 0.60f +
                         Rune_SamplePeriodic(x * 2, y, 24, 808u) * 0.40f;

            int ri = (int)(rVal * 255.0f); if (ri > 255) ri = 255; if (ri < 0) ri = 0;
            int gi = (int)(gVal * 255.0f); if (gi > 255) gi = 255; if (gi < 0) gi = 0;
            int bi = (int)(bVal * 255.0f); if (bi > 255) bi = 255; if (bi < 0) bi = 0;
            int ai = (int)(aVal * 255.0f); if (ai > 255) ai = 255; if (ai < 0) ai = 0;

            s_runeNoiseRGBA[idx + 0] = (uint8_t)ri;
            s_runeNoiseRGBA[idx + 1] = (uint8_t)gi;
            s_runeNoiseRGBA[idx + 2] = (uint8_t)bi;
            s_runeNoiseRGBA[idx + 3] = (uint8_t)ai;
        }
    }

    Image img = {
        .data    = s_runeNoiseRGBA,
        .width   = RUNE_NOISE_SIZE,
        .height  = RUNE_NOISE_SIZE,
        .mipmaps = 1,
        .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    s_runeNoiseTex = LoadTextureFromImage(img);
    SetTextureWrap(s_runeNoiseTex, TEXTURE_WRAP_REPEAT);
    SetTextureFilter(s_runeNoiseTex, TEXTURE_FILTER_BILINEAR);
}

// ── 1024x1024 CPU Razor Seal Forge (lazy-cached per variant) ────────────────
#define RUNE_TEX_SIZE 1024
#define RUNE_TEX_CX   512.0f
#define RUNE_TEX_CY   512.0f

static uint8_t s_forgeR[RUNE_TEX_SIZE * RUNE_TEX_SIZE];
static uint8_t s_forgeG[RUNE_TEX_SIZE * RUNE_TEX_SIZE];
static uint8_t s_forgeB[RUNE_TEX_SIZE * RUNE_TEX_SIZE];
static uint8_t s_forgeTmp[RUNE_TEX_SIZE * RUNE_TEX_SIZE];
static uint8_t s_forgeRGBA[RUNE_TEX_SIZE * RUNE_TEX_SIZE * 4];

typedef struct {
    bool      built;
    Texture2D innerTex;
    Texture2D outerTex;
} RuneTexCache;

static RuneTexCache s_runeCache[VFX_RUNE_VARIANT_COUNT] = {0};

static inline void Forge_BlendMax(uint8_t *buf, int idx, float cov, uint8_t val)
{
    if (cov <= 0.0f) return;
    int v = (int)(cov * (float)val + 0.5f);
    if (v > 255) v = 255;
    int sum = (int)buf[idx] + v;
    buf[idx] = (uint8_t)(sum > 255 ? 255 : sum);
}

static void Forge_AddSeg(uint8_t *buf, float x0, float y0, float x1, float y1,
                         float halfW, uint8_t val)
{
    float pad = halfW + 1.5f;
    int minX = (int)floorf(fminf(x0, x1) - pad);
    int maxX = (int)ceilf(fmaxf(x0, x1) + pad);
    int minY = (int)floorf(fminf(y0, y1) - pad);
    int maxY = (int)ceilf(fmaxf(y0, y1) + pad);
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= RUNE_TEX_SIZE) maxX = RUNE_TEX_SIZE - 1;
    if (maxY >= RUNE_TEX_SIZE) maxY = RUNE_TEX_SIZE - 1;

    float dx = x1 - x0;
    float dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float invLen2 = (len2 > 1e-6f) ? (1.0f / len2) : 0.0f;

    for (int y = minY; y <= maxY; y++)
    {
        float py = (float)y + 0.5f - y0;
        int row = y * RUNE_TEX_SIZE;
        for (int x = minX; x <= maxX; x++)
        {
            float px = (float)x + 0.5f - x0;
            float h = (px * dx + py * dy) * invLen2;
            if (h < 0.0f) h = 0.0f; else if (h > 1.0f) h = 1.0f;
            float ex = px - dx * h;
            float ey = py - dy * h;
            float d = sqrtf(ex * ex + ey * ey);
            float cov = halfW + 0.65f - d;
            if (cov > 0.0f)
            {
                if (cov > 1.0f) cov = 1.0f;
                Forge_BlendMax(buf, row + x, cov, val);
            }
        }
    }
}

static void Forge_AddDisc(uint8_t *buf, float cx, float cy, float r, uint8_t val)
{
    float pad = r + 1.5f;
    int minX = (int)floorf(cx - pad);
    int maxX = (int)ceilf(cx + pad);
    int minY = (int)floorf(cy - pad);
    int maxY = (int)ceilf(cy + pad);
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= RUNE_TEX_SIZE) maxX = RUNE_TEX_SIZE - 1;
    if (maxY >= RUNE_TEX_SIZE) maxY = RUNE_TEX_SIZE - 1;

    for (int y = minY; y <= maxY; y++)
    {
        float dy = (float)y + 0.5f - cy;
        int row = y * RUNE_TEX_SIZE;
        for (int x = minX; x <= maxX; x++)
        {
            float dx = (float)x + 0.5f - cx;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = r + 0.65f - d;
            if (cov > 0.0f)
            {
                if (cov > 1.0f) cov = 1.0f;
                Forge_BlendMax(buf, row + x, cov, val);
            }
        }
    }
}

static void Forge_AddRing(uint8_t *buf, float cx, float cy, float r,
                          float halfW, uint8_t val,
                          const float *dash, int dashCount)
{
    float rOut = r + halfW + 1.5f;
    float rIn  = fmaxf(0.0f, r - halfW - 1.5f);
    int minX = (int)floorf(cx - rOut);
    int maxX = (int)ceilf(cx + rOut);
    int minY = (int)floorf(cy - rOut);
    int maxY = (int)ceilf(cy + rOut);
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= RUNE_TEX_SIZE) maxX = RUNE_TEX_SIZE - 1;
    if (maxY >= RUNE_TEX_SIZE) maxY = RUNE_TEX_SIZE - 1;

    float dashPeriod = 0.0f;
    if (dash != NULL && dashCount >= 2)
    {
        for (int i = 0; i < dashCount; i++) dashPeriod += dash[i];
    }

    for (int y = minY; y <= maxY; y++)
    {
        float dy = (float)y + 0.5f - cy;
        int row = y * RUNE_TEX_SIZE;
        for (int x = minX; x <= maxX; x++)
        {
            float dx = (float)x + 0.5f - cx;
            float d2 = dx * dx + dy * dy;
            if (d2 > rOut * rOut || d2 < rIn * rIn) continue;
            float dist = sqrtf(d2);
            float cov = halfW + 0.65f - fabsf(dist - r);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;

            if (dashPeriod > 0.0f)
            {
                float ang = atan2f(dy, dx);
                if (ang < 0.0f) ang += RUNE_TAU;
                float arcPos = fmodf(ang * r, dashPeriod);
                float acc = 0.0f;
                bool on = true;
                for (int k = 0; k < dashCount; k++)
                {
                    acc += dash[k];
                    if (arcPos < acc) { on = ((k & 1) == 0); break; }
                }
                if (!on) continue;
            }
            Forge_BlendMax(buf, row + x, cov, val);
        }
    }
}

static void Forge_AddArc(uint8_t *buf, float cx, float cy, float r,
                         float a0, float sweep, float halfW, uint8_t val)
{
    int segs = (int)ceilf(fabsf(sweep) * r / 6.0f);
    if (segs < 4) segs = 4;
    if (segs > 96) segs = 96;
    float px = cx + cosf(a0) * r;
    float py = cy + sinf(a0) * r;
    for (int i = 1; i <= segs; i++)
    {
        float a = a0 + sweep * ((float)i / (float)segs);
        float nx = cx + cosf(a) * r;
        float ny = cy + sinf(a) * r;
        Forge_AddSeg(buf, px, py, nx, ny, halfW, val);
        px = nx; py = ny;
    }
}

static void Forge_AddQuadBezier(uint8_t *buf, float x0, float y0,
                                float cx, float cy, float x1, float y1,
                                float halfW, uint8_t val)
{
    const int steps = 10;
    float px = x0, py = y0;
    for (int i = 1; i <= steps; i++)
    {
        float t = (float)i / (float)steps;
        float it = 1.0f - t;
        float nx = it * it * x0 + 2.0f * it * t * cx + t * t * x1;
        float ny = it * it * y0 + 2.0f * it * t * cy + t * t * y1;
        Forge_AddSeg(buf, px, py, nx, ny, halfW, val);
        px = nx; py = ny;
    }
}

static void Forge_AddPolygon(uint8_t *buf, float cx, float cy, float r,
                             int sides, float rot, float halfW, uint8_t val)
{
    for (int i = 0; i < sides; i++)
    {
        float a0 = ((float)i / (float)sides) * RUNE_TAU + rot;
        float a1 = ((float)(i + 1) / (float)sides) * RUNE_TAU + rot;
        Forge_AddSeg(buf,
                     cx + cosf(a0) * r, cy + sinf(a0) * r,
                     cx + cosf(a1) * r, cy + sinf(a1) * r,
                     halfW, val);
    }
}

// Fast O(N) 2D separable box blur (1 pass H + 1 pass V)
static void Forge_BoxBlurPass(uint8_t *buf, uint8_t *tmp, int radius)
{
    const int S = RUNE_TEX_SIZE;
    int win = radius * 2 + 1;
    // Horizontal
    for (int y = 0; y < S; y++)
    {
        const uint8_t *src = &buf[y * S];
        uint8_t *dst = &tmp[y * S];
        int sum = 0;
        for (int k = -radius; k <= radius; k++)
        {
            int x = k < 0 ? 0 : (k >= S ? S - 1 : k);
            sum += src[x];
        }
        for (int x = 0; x < S; x++)
        {
            dst[x] = (uint8_t)(sum / win);
            int xAdd = x + radius + 1; if (xAdd >= S) xAdd = S - 1;
            int xSub = x - radius;     if (xSub < 0)  xSub = 0;
            sum += (int)src[xAdd] - (int)src[xSub];
        }
    }
    // Vertical
    for (int x = 0; x < S; x++)
    {
        int sum = 0;
        for (int k = -radius; k <= radius; k++)
        {
            int y = k < 0 ? 0 : (k >= S ? S - 1 : k);
            sum += tmp[y * S + x];
        }
        for (int y = 0; y < S; y++)
        {
            buf[y * S + x] = (uint8_t)(sum / win);
            int yAdd = y + radius + 1; if (yAdd >= S) yAdd = S - 1;
            int ySub = y - radius;     if (ySub < 0)  ySub = 0;
            sum += (int)tmp[yAdd * S + x] - (int)tmp[ySub * S + x];
        }
    }
}

// ── Calligraphy Glyph Styles (1-to-1 port of WebGL drawCalligraphyGlyph) ───
typedef enum {
    GLYPH_SEAL = 0,
    GLYPH_DAO,
    GLYPH_JADE,
    GLYPH_SWORD,
    GLYPH_BLOOD,
    GLYPH_STAR
} RuneGlyphStyle;

static void Forge_GlyphSeg(uint8_t *buf, float gx, float gy, float ca, float sa,
                           float hw, float hh,
                           float x1, float y1, float x2, float y2,
                           float strokeHalfW, uint8_t val)
{
    float lx1 = x1 * hw, ly1 = y1 * hh;
    float lx2 = x2 * hw, ly2 = y2 * hh;
    float wx1 = gx + lx1 * ca - ly1 * sa;
    float wy1 = gy + lx1 * sa + ly1 * ca;
    float wx2 = gx + lx2 * ca - ly2 * sa;
    float wy2 = gy + lx2 * sa + ly2 * ca;
    Forge_AddSeg(buf, wx1, wy1, wx2, wy2, strokeHalfW, val);
}

static void Forge_DrawGlyph(uint8_t *buf, RuneGlyphStyle style, uint32_t *rng,
                            float gx, float gy, float angle,
                            float w, float h, float strokeHalfW, uint8_t val)
{
    float hw = w * 0.46f;
    float hh = h * 0.45f;
    float ca = cosf(angle);
    float sa = sinf(angle);

    #define GSEG(a,b,c,d) Forge_GlyphSeg(buf, gx, gy, ca, sa, hw, hh, (a),(b),(c),(d), strokeHalfW, val)

    if (style == GLYPH_SEAL || style == GLYPH_DAO || style == GLYPH_JADE)
    {
        int pick = (int)(Rune_RngNext(rng) * 7.0f);
        if (pick == 0)
        {
            GSEG(-0.85f, -0.85f,  0.85f, -0.85f);
            GSEG(-0.75f, -0.85f, -0.75f,  0.82f);
            GSEG( 0.75f, -0.85f,  0.75f,  0.82f);
            GSEG(-0.75f,  0.00f,  0.75f,  0.00f);
            GSEG( 0.00f, -0.95f,  0.00f,  0.95f);
            GSEG(-0.85f,  0.82f,  0.85f,  0.82f);
        }
        else if (pick == 1)
        {
            GSEG( 0.00f, -0.95f,  0.00f,  0.95f);
            GSEG(-0.88f, -0.45f,  0.88f, -0.45f);
            GSEG(-0.65f, -0.88f,  0.65f, -0.88f);
            GSEG( 0.00f, -0.10f, -0.85f,  0.82f);
            GSEG( 0.00f, -0.10f,  0.85f,  0.82f);
            if (Rune_RngNext(rng) > 0.35f) GSEG(-0.55f, 0.42f, 0.55f, 0.42f);
        }
        else if (pick == 2)
        {
            GSEG(-0.85f, -0.85f,  0.85f, -0.85f);
            GSEG( 0.85f, -0.85f,  0.85f,  0.04f);
            GSEG( 0.85f,  0.04f, -0.85f,  0.04f);
            GSEG(-0.85f,  0.04f, -0.85f,  0.85f);
            GSEG(-0.85f,  0.85f,  0.85f,  0.85f);
            GSEG( 0.00f, -0.50f,  0.00f,  0.55f);
        }
        else if (pick == 3)
        {
            GSEG(-0.85f, -0.78f, -0.85f,  0.78f);
            GSEG( 0.00f, -0.95f,  0.00f,  0.85f);
            GSEG( 0.85f, -0.78f,  0.85f,  0.78f);
            GSEG(-0.90f,  0.78f,  0.90f,  0.78f);
            GSEG(-0.85f, -0.15f,  0.85f, -0.15f);
        }
        else if (pick == 4)
        {
            GSEG( 0.00f, -0.98f, -0.90f, -0.20f);
            GSEG( 0.00f, -0.98f,  0.90f, -0.20f);
            GSEG(-0.62f, -0.12f,  0.62f, -0.12f);
            GSEG(-0.56f, -0.12f, -0.56f,  0.84f);
            GSEG( 0.56f, -0.12f,  0.56f,  0.84f);
            GSEG(-0.56f,  0.35f,  0.56f,  0.35f);
            GSEG(-0.82f,  0.84f,  0.82f,  0.84f);
        }
        else if (pick == 5)
        {
            GSEG(-0.65f, -0.92f,  0.00f, -0.58f);
            GSEG( 0.65f, -0.92f,  0.00f, -0.58f);
            GSEG( 0.00f, -0.95f,  0.00f,  0.95f);
            GSEG(-0.75f, -0.32f,  0.75f, -0.02f);
            GSEG( 0.75f, -0.02f, -0.75f,  0.32f);
            GSEG(-0.75f,  0.32f,  0.75f,  0.66f);
        }
        else
        {
            GSEG(-0.78f, -0.82f,  0.78f, -0.82f);
            GSEG(-0.78f,  0.82f,  0.78f,  0.82f);
            GSEG(-0.78f, -0.82f, -0.45f,  0.82f);
            GSEG( 0.78f, -0.82f,  0.45f,  0.82f);
            GSEG(-0.62f,  0.00f,  0.62f,  0.00f);
            GSEG( 0.00f, -0.82f,  0.00f,  0.82f);
        }
        if (Rune_RngNext(rng) > 0.4f)
        {
            float lx = (Rune_RngNext(rng) - 0.5f) * hw * 1.1f;
            float ly = (Rune_RngNext(rng) - 0.5f) * hh * 1.1f;
            Forge_AddDisc(buf, gx + lx * ca - ly * sa, gy + lx * sa + ly * ca, 1.8f, val);
        }
    }
    else if (style == GLYPH_SWORD)
    {
        int p = (int)(Rune_RngNext(rng) * 4.0f);
        GSEG(0.00f, -0.98f, 0.00f, 0.98f);
        if (p == 0)
        {
            GSEG(-0.70f, -0.35f, 0.00f, -0.95f);
            GSEG( 0.70f, -0.35f, 0.00f, -0.95f);
            GSEG(-0.80f,  0.25f, 0.80f,  0.25f);
            GSEG(-0.45f,  0.75f, 0.45f,  0.75f);
        }
        else if (p == 1)
        {
            GSEG(-0.78f, -0.65f,  0.78f,  0.65f);
            GSEG( 0.78f, -0.65f, -0.78f,  0.65f);
            GSEG(-0.60f, -0.82f,  0.60f, -0.82f);
        }
        else if (p == 2)
        {
            GSEG(-0.82f, -0.20f, 0.00f, -0.75f);
            GSEG( 0.82f, -0.20f, 0.00f, -0.75f);
            GSEG(-0.65f,  0.55f, 0.00f,  0.05f);
            GSEG( 0.65f,  0.55f, 0.00f,  0.05f);
        }
        else
        {
            GSEG(-0.72f, -0.85f, -0.72f,  0.45f);
            GSEG( 0.72f, -0.85f,  0.72f,  0.45f);
            GSEG(-0.72f,  0.45f,  0.00f,  0.95f);
            GSEG( 0.72f,  0.45f,  0.00f,  0.95f);
            GSEG(-0.72f, -0.18f,  0.72f, -0.18f);
        }
    }
    else if (style == GLYPH_BLOOD)
    {
        GSEG(-0.85f, -0.82f,  0.85f,  0.82f);
        GSEG( 0.85f, -0.82f, -0.85f,  0.82f);
        if (Rune_RngNext(rng) > 0.3f) GSEG(0.00f, -0.98f, 0.00f, 0.98f);
        GSEG(-0.85f, -0.18f, 0.00f, -0.85f);
        GSEG( 0.85f, -0.18f, 0.00f, -0.85f);
        if (Rune_RngNext(rng) > 0.4f) GSEG(-0.78f, 0.55f, 0.78f, 0.55f);
    }
    else if (style == GLYPH_STAR)
    {
        float pts[6][2];
        int n = 4 + (int)(Rune_RngNext(rng) * 2.0f);
        for (int i = 0; i < n; i++)
        {
            pts[i][0] = Rune_RngNext(rng) * 1.5f - 0.75f;
            pts[i][1] = Rune_RngNext(rng) * 1.6f - 0.80f;
        }
        for (int i = 0; i < n - 1; i++)
        {
            GSEG(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]);
        }
        for (int i = 0; i < n; i += 2)
        {
            float lx = pts[i][0] * hw;
            float ly = pts[i][1] * hh;
            Forge_AddDisc(buf, gx + lx * ca - ly * sa, gy + lx * sa + ly * ca, 2.2f, val);
        }
    }
    #undef GSEG
}

static void Forge_DrawGlyphRing(float r, int count, float gw, float gh, float lw,
                                RuneGlyphStyle style, uint32_t seed, float gapProb)
{
    // Pass 1: Channel G — tight aura base (blurred later)
    uint32_t rngG = seed;
    for (int i = 0; i < count; i++)
    {
        bool skip = Rune_RngNext(&rngG) < gapProb;
        float a = ((float)i / (float)count) * RUNE_TAU;
        if (!skip)
        {
            Forge_DrawGlyph(s_forgeG, style, &rngG,
                            RUNE_TEX_CX + cosf(a) * r, RUNE_TEX_CY + sinf(a) * r,
                            a + RUNE_PI * 0.5f, gw, gh, lw * 0.82f, 200);
        }
    }
    // Pass 2: Channel R — razor sharp zero-blur core
    uint32_t rngR = seed;
    for (int i = 0; i < count; i++)
    {
        bool skip = Rune_RngNext(&rngR) < gapProb;
        float a = ((float)i / (float)count) * RUNE_TAU;
        if (!skip)
        {
            Forge_DrawGlyph(s_forgeR, style, &rngR,
                            RUNE_TEX_CX + cosf(a) * r, RUNE_TEX_CY + sinf(a) * r,
                            a + RUNE_PI * 0.5f, gw, gh, lw * 0.50f, 255);
        }
    }
}

static void Forge_DrawPerimeterCurls(float rBase, int count, uint32_t seed)
{
    uint32_t rng = seed + 77u;
    for (int i = 0; i < 18; i++)
    {
        float a0 = Rune_RngNext(&rng) * RUNE_TAU;
        float span = 0.14f + Rune_RngNext(&rng) * 0.42f;
        float rr = rBase + (Rune_RngNext(&rng) - 0.35f) * 44.0f;
        float lw = 1.0f + Rune_RngNext(&rng) * 1.4f;
        Forge_AddArc(s_forgeB, RUNE_TEX_CX, RUNE_TEX_CY, rr, a0, span, lw * 0.55f, 230);
    }
    for (int i = 0; i < count; i++)
    {
        float a = ((float)i / (float)count) * RUNE_TAU + (Rune_RngNext(&rng) - 0.5f) * 0.12f;
        float rr = rBase + 10.0f + Rune_RngNext(&rng) * 42.0f;
        float curlR = 6.0f + Rune_RngNext(&rng) * 14.0f;
        float px = RUNE_TEX_CX + cosf(a) * rr;
        float py = RUNE_TEX_CY + sinf(a) * rr;
        float startA = a + (Rune_RngNext(&rng) - 0.5f) * 2.0f;
        float sweep = (0.9f + Rune_RngNext(&rng) * 0.9f) * RUNE_PI;
        float lw = 1.1f + Rune_RngNext(&rng) * 1.1f;
        Forge_AddArc(s_forgeB, px, py, curlR, startA, sweep, lw * 0.55f, 230);

        if (Rune_RngNext(&rng) > 0.35f)
        {
            float dx = px + cosf(startA) * (curlR + 4.0f);
            float dy = py + sinf(startA) * (curlR + 4.0f);
            float dr = 1.4f + Rune_RngNext(&rng) * 1.6f;
            Forge_AddDisc(s_forgeR, dx, dy, dr, 220);
            Forge_AddDisc(s_forgeB, dx, dy, dr, 180);
        }
    }
}

static void Forge_ClearBuffers(void)
{
    memset(s_forgeR, 0, sizeof(s_forgeR));
    memset(s_forgeG, 0, sizeof(s_forgeG));
    memset(s_forgeB, 0, sizeof(s_forgeB));
}

static Texture2D Forge_FinalizeTexture(void)
{
    // Tight Gaussian-like aura on G (2 box passes radius 5) & subtle glow on B (radius 2)
    Forge_BoxBlurPass(s_forgeG, s_forgeTmp, 5);
    Forge_BoxBlurPass(s_forgeG, s_forgeTmp, 5);
    Forge_BoxBlurPass(s_forgeB, s_forgeTmp, 2);

    // Clear pure void center (r < 240..262) and pack RGBA8
    for (int y = 0; y < RUNE_TEX_SIZE; y++)
    {
        float dy = (float)y + 0.5f - RUNE_TEX_CY;
        int row = y * RUNE_TEX_SIZE;
        for (int x = 0; x < RUNE_TEX_SIZE; x++)
        {
            float dx = (float)x + 0.5f - RUNE_TEX_CX;
            float r = sqrtf(dx * dx + dy * dy);
            float keep = 1.0f;
            if (r <= 254.0f) keep = 0.0f;
            else if (r < 262.0f) keep = (r - 254.0f) / 8.0f;

            int idx = row + x;
            int idx4 = idx * 4;
            uint8_t rv = (uint8_t)((float)s_forgeR[idx] * keep);
            uint8_t gv = (uint8_t)((float)s_forgeG[idx] * keep);
            uint8_t bv = (uint8_t)((float)s_forgeB[idx] * keep);
            uint8_t av = rv > gv ? (rv > bv ? rv : bv) : (gv > bv ? gv : bv);

            s_forgeRGBA[idx4 + 0] = rv;
            s_forgeRGBA[idx4 + 1] = gv;
            s_forgeRGBA[idx4 + 2] = bv;
            s_forgeRGBA[idx4 + 3] = av;
        }
    }

    Image img = {
        .data    = s_forgeRGBA,
        .width   = RUNE_TEX_SIZE,
        .height  = RUNE_TEX_SIZE,
        .mipmaps = 1,
        .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D tex = LoadTextureFromImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
    return tex;
}

static void Rune_EnsureVariantTextures(VFX_RuneCircleVariant v)
{
    if ((int)v < 0 || (int)v >= VFX_RUNE_VARIANT_COUNT) v = VFX_RUNE_PHAN_THIEN;
    if (s_runeCache[v].built) return;
    s_runeCache[v].built = true;

    const RuneVariantDef *def = &s_runeDefs[v];
    const float R_INNER = 274.0f;
    static const float dashInner[4] = { 22.0f, 16.0f, 8.0f, 14.0f };
    static const float dashOuter[4] = { 26.0f, 12.0f, 6.0f, 14.0f };

    // =========================================================================
    // TEXTURE 1 (LAYER 1): Molten Inner Core Ring + Dense Main Calligraphy Band
    // =========================================================================
    Forge_ClearBuffers();
    Forge_AddRing(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, R_INNER, 6.0f, 235, NULL, 0);
    Forge_AddRing(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, R_INNER, 2.7f, 255, NULL, 0);
    Forge_AddRing(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, R_INNER + 12.0f, 1.0f, 180, NULL, 0);
    Forge_AddRing(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, R_INNER + 12.0f, 0.7f, 230, NULL, 0);
    Forge_AddRing(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, 356.0f, 2.2f, 195, NULL, 0);
    Forge_AddRing(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, 356.0f, 1.1f, 245, NULL, 0);

    if (v == VFX_RUNE_PHAN_THIEN)
    {
        Forge_DrawGlyphRing(317.0f, 64, 24.0f, 38.0f, 2.2f, GLYPH_SEAL, def->seed + 1u, 0.03f);
        for (int i = 0; i < 64; i++)
        {
            float a = ((float)i / 64.0f) * RUNE_TAU + RUNE_PI / 64.0f;
            Forge_AddDisc(s_forgeR, RUNE_TEX_CX + cosf(a) * 290.0f, RUNE_TEX_CY + sinf(a) * 290.0f, 1.45f, 255);
        }
    }
    else if (v == VFX_RUNE_BAT_QUAI)
    {
        static const int trigrams[8][3] = {
            {1,1,1}, {0,1,1}, {1,0,1}, {0,0,1},
            {1,1,0}, {0,1,0}, {1,0,0}, {0,0,0}
        };
        for (int i = 0; i < 8; i++)
        {
            float a = ((float)i / 8.0f) * RUNE_TAU;
            float gx = RUNE_TEX_CX + cosf(a) * 316.0f;
            float gy = RUNE_TEX_CY + sinf(a) * 316.0f;
            float ca = cosf(a + RUNE_PI * 0.5f);
            float sa = sinf(a + RUNE_PI * 0.5f);
            for (int b = 0; b < 3; b++)
            {
                float ly = (float)(b - 1) * 11.0f;
                if (trigrams[i][b] == 1)
                {
                    Forge_GlyphSeg(s_forgeG, gx, gy, ca, sa, 1.0f, 1.0f, -28.0f, ly, 28.0f, ly, 3.3f, 200);
                    Forge_GlyphSeg(s_forgeR, gx, gy, ca, sa, 1.0f, 1.0f, -28.0f, ly, 28.0f, ly, 1.8f, 255);
                }
                else
                {
                    Forge_GlyphSeg(s_forgeG, gx, gy, ca, sa, 1.0f, 1.0f, -28.0f, ly, -5.0f, ly, 3.3f, 200);
                    Forge_GlyphSeg(s_forgeG, gx, gy, ca, sa, 1.0f, 1.0f,   5.0f, ly, 28.0f, ly, 3.3f, 200);
                    Forge_GlyphSeg(s_forgeR, gx, gy, ca, sa, 1.0f, 1.0f, -28.0f, ly, -5.0f, ly, 1.8f, 255);
                    Forge_GlyphSeg(s_forgeR, gx, gy, ca, sa, 1.0f, 1.0f,   5.0f, ly, 28.0f, ly, 1.8f, 255);
                }
            }
        }
        Forge_DrawGlyphRing(316.0f, 48, 17.0f, 28.0f, 1.7f, GLYPH_DAO, def->seed + 2u, 0.33f);
    }
    else if (v == VFX_RUNE_TRU_TIEN)
    {
        Forge_DrawGlyphRing(316.0f, 56, 22.0f, 36.0f, 2.0f, GLYPH_SWORD, def->seed + 3u, 0.04f);
        for (int i = 0; i < 8; i++)
        {
            float a = ((float)i / 8.0f) * RUNE_TAU;
            float x0 = RUNE_TEX_CX + cosf(a) * R_INNER;
            float y0 = RUNE_TEX_CY + sinf(a) * R_INNER;
            float x1 = RUNE_TEX_CX + cosf(a) * 360.0f;
            float y1 = RUNE_TEX_CY + sinf(a) * 360.0f;
            Forge_AddSeg(s_forgeG, x0, y0, x1, y1, 2.3f, 195);
            Forge_AddSeg(s_forgeR, x0, y0, x1, y1, 1.05f, 255);
        }
    }
    else if (v == VFX_RUNE_HUYET_SAT)
    {
        Forge_DrawGlyphRing(317.0f, 58, 24.0f, 38.0f, 2.1f, GLYPH_BLOOD, def->seed + 4u, 0.04f);
        for (int i = 0; i < 72; i++)
        {
            float a0 = ((float)i / 72.0f) * RUNE_TAU;
            float a1 = ((float)(i + 1) / 72.0f) * RUNE_TAU;
            float r0 = (i % 2 == 0) ? 356.0f : 344.0f;
            float r1 = ((i + 1) % 2 == 0) ? 356.0f : 344.0f;
            float x0 = RUNE_TEX_CX + cosf(a0) * r0, y0 = RUNE_TEX_CY + sinf(a0) * r0;
            float x1 = RUNE_TEX_CX + cosf(a1) * r1, y1 = RUNE_TEX_CY + sinf(a1) * r1;
            Forge_AddSeg(s_forgeG, x0, y0, x1, y1, 1.7f, 185);
            Forge_AddSeg(s_forgeR, x0, y0, x1, y1, 0.85f, 255);
        }
    }
    else if (v == VFX_RUNE_U_MINH)
    {
        Forge_DrawGlyphRing(316.0f, 56, 22.0f, 36.0f, 2.0f, GLYPH_JADE, def->seed + 5u, 0.05f);
        for (int i = 0; i < 16; i++)
        {
            float a0 = ((float)i / 16.0f) * RUNE_TAU;
            float a1 = ((float)(i + 1) / 16.0f) * RUNE_TAU;
            float am = (a0 + a1) * 0.5f;
            float x0 = RUNE_TEX_CX + cosf(a0) * 356.0f, y0 = RUNE_TEX_CY + sinf(a0) * 356.0f;
            float xm = RUNE_TEX_CX + cosf(am) * 374.0f, ym = RUNE_TEX_CY + sinf(am) * 374.0f;
            float x1 = RUNE_TEX_CX + cosf(a1) * 356.0f, y1 = RUNE_TEX_CY + sinf(a1) * 356.0f;
            Forge_AddQuadBezier(s_forgeG, x0, y0, xm, ym, x1, y1, 2.2f, 195);
            Forge_AddQuadBezier(s_forgeR, x0, y0, xm, ym, x1, y1, 1.05f, 255);
        }
    }
    else if (v == VFX_RUNE_TINH_TU)
    {
        Forge_DrawGlyphRing(317.0f, 56, 22.0f, 34.0f, 1.9f, GLYPH_STAR, def->seed + 6u, 0.04f);
        for (int i = 0; i < 28; i++)
        {
            float a = ((float)i / 28.0f) * RUNE_TAU;
            float x0 = RUNE_TEX_CX + cosf(a) * 340.0f, y0 = RUNE_TEX_CY + sinf(a) * 340.0f;
            float x1 = RUNE_TEX_CX + cosf(a) * 366.0f, y1 = RUNE_TEX_CY + sinf(a) * 366.0f;
            Forge_AddSeg(s_forgeG, x0, y0, x1, y1, 1.6f, 185);
            Forge_AddSeg(s_forgeR, x0, y0, x1, y1, 0.8f, 255);
        }
    }
    Forge_AddRing(s_forgeB, RUNE_TEX_CX, RUNE_TEX_CY, 366.0f, 0.85f, 220, dashInner, 4);
    s_runeCache[v].innerTex = Forge_FinalizeTexture();

    // =========================================================================
    // TEXTURE 2 (LAYER 2): Counter-Rotating Sub-Script + Geometric Motifs + B
    // =========================================================================
    Forge_ClearBuffers();
    Forge_AddRing(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, 396.0f, 1.8f, 175, NULL, 0);
    Forge_AddRing(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, 396.0f, 0.85f, 235, NULL, 0);
    Forge_AddRing(s_forgeB, RUNE_TEX_CX, RUNE_TEX_CY, 412.0f, 0.75f, 210, dashOuter, 4);

    RuneGlyphStyle subStyle =
        (v == VFX_RUNE_TRU_TIEN)  ? GLYPH_SWORD :
        (v == VFX_RUNE_HUYET_SAT) ? GLYPH_BLOOD :
        (v == VFX_RUNE_TINH_TU)   ? GLYPH_STAR  :
        (v == VFX_RUNE_BAT_QUAI)  ? GLYPH_DAO   : GLYPH_SEAL;

    Forge_DrawGlyphRing(377.0f, 86, 14.0f, 21.0f, 1.4f, subStyle, def->seed + 55u, 0.08f);

    if (v == VFX_RUNE_PHAN_THIEN)
    {
        for (int i = 0; i < 96; i++)
        {
            float a = ((float)i / 96.0f) * RUNE_TAU;
            bool maj = (i % 8 == 0);
            float r1 = maj ? 414.0f : 405.0f;
            float x0 = RUNE_TEX_CX + cosf(a) * 396.0f, y0 = RUNE_TEX_CY + sinf(a) * 396.0f;
            float x1 = RUNE_TEX_CX + cosf(a) * r1,     y1 = RUNE_TEX_CY + sinf(a) * r1;
            if (maj) Forge_AddSeg(s_forgeR, x0, y0, x1, y1, 0.95f, 240);
            else     Forge_AddSeg(s_forgeG, x0, y0, x1, y1, 0.60f, 170);
        }
        Forge_DrawPerimeterCurls(408.0f, 34, def->seed);
    }
    else if (v == VFX_RUNE_BAT_QUAI)
    {
        Forge_AddPolygon(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, 424.0f, 8, 0.0f, 1.6f, 175);
        Forge_AddPolygon(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, 424.0f, 8, 0.0f, 0.9f, 255);
        Forge_AddPolygon(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, 424.0f, 8, RUNE_PI / 8.0f, 1.3f, 175);
        Forge_AddPolygon(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, 424.0f, 8, RUNE_PI / 8.0f, 0.75f, 255);
        Forge_DrawPerimeterCurls(420.0f, 24, def->seed);
    }
    else if (v == VFX_RUNE_TRU_TIEN)
    {
        for (int i = 0; i < 8; i++)
        {
            float a = ((float)i / 8.0f) * RUNE_TAU + RUNE_PI / 8.0f;
            float ca = cosf(a), sa = sinf(a);
            float pts[4][2] = { {0.0f, -470.0f}, {13.0f, -398.0f}, {0.0f, -356.0f}, {-13.0f, -398.0f} };
            for (int k = 0; k < 4; k++)
            {
                int kn = (k + 1) & 3;
                Forge_GlyphSeg(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f,
                               pts[k][0], pts[k][1], pts[kn][0], pts[kn][1], 2.1f, 200);
                Forge_GlyphSeg(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f,
                               pts[k][0], pts[k][1], pts[kn][0], pts[kn][1], 1.0f, 255);
            }
            Forge_GlyphSeg(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f,
                           0.0f, -465.0f, 0.0f, -358.0f, 2.0f, 200);
            Forge_GlyphSeg(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f,
                           0.0f, -465.0f, 0.0f, -358.0f, 0.95f, 255);
        }
        Forge_DrawPerimeterCurls(412.0f, 22, def->seed);
    }
    else if (v == VFX_RUNE_HUYET_SAT)
    {
        for (int i = 0; i < 16; i++)
        {
            float a = ((float)i / 16.0f) * RUNE_TAU;
            float ca = cosf(a), sa = sinf(a);
            float tip = (i % 2 == 0) ? 464.0f : 434.0f;
            Forge_GlyphSeg(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f, -10.0f, -396.0f, 0.0f, -tip, 1.95f, 195);
            Forge_GlyphSeg(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f,   0.0f, -tip,   10.0f, -396.0f, 1.95f, 195);
            Forge_GlyphSeg(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f, -10.0f, -396.0f, 0.0f, -tip, 0.95f, 255);
            Forge_GlyphSeg(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, ca, sa, 1.0f, 1.0f,   0.0f, -tip,   10.0f, -396.0f, 0.95f, 255);
        }
        Forge_DrawPerimeterCurls(414.0f, 28, def->seed);
    }
    else if (v == VFX_RUNE_U_MINH)
    {
        Forge_AddPolygon(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, 430.0f, 6, 0.0f, 1.6f, 175);
        Forge_AddPolygon(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, 430.0f, 6, 0.0f, 0.9f, 255);
        Forge_AddPolygon(s_forgeG, RUNE_TEX_CX, RUNE_TEX_CY, 430.0f, 6, RUNE_PI / 6.0f, 1.35f, 175);
        Forge_AddPolygon(s_forgeR, RUNE_TEX_CX, RUNE_TEX_CY, 430.0f, 6, RUNE_PI / 6.0f, 0.8f, 255);
        Forge_DrawPerimeterCurls(418.0f, 28, def->seed);
    }
    else if (v == VFX_RUNE_TINH_TU)
    {
        Forge_AddRing(s_forgeB, RUNE_TEX_CX + 18.0f, RUNE_TEX_CY - 12.0f, 418.0f, 0.85f, 230, NULL, 0);
        Forge_AddRing(s_forgeG, RUNE_TEX_CX - 16.0f, RUNE_TEX_CY + 14.0f, 418.0f, 0.85f, 185, NULL, 0);
        uint32_t rngS = def->seed + 88u;
        for (int i = 0; i < 28; i++)
        {
            float a = ((float)i / 28.0f) * RUNE_TAU;
            float sr = 424.0f + sinf((float)i * 2.1f) * 14.0f;
            float px = RUNE_TEX_CX + cosf(a) * sr;
            float py = RUNE_TEX_CY + sinf(a) * sr;
            bool big = (i % 4 == 0);
            Forge_AddDisc(s_forgeR, px, py, big ? 4.2f : 2.3f, 255);
            if (big)
            {
                Forge_AddRing(s_forgeG, px, py, 9.5f, 0.85f, 200, NULL, 0);
            }
            if (Rune_RngNext(&rngS) > 0.4f)
            {
                float a2 = ((float)(i + 1) / 28.0f) * RUNE_TAU;
                float sr2 = 424.0f + sinf((float)(i + 1) * 2.1f) * 14.0f;
                Forge_AddSeg(s_forgeB, px, py,
                             RUNE_TEX_CX + cosf(a2) * sr2, RUNE_TEX_CY + sinf(a2) * sr2,
                             0.65f, 180);
            }
        }
        Forge_DrawPerimeterCurls(412.0f, 18, def->seed);
    }
    s_runeCache[v].outerTex = Forge_FinalizeTexture();
}

// ── Pinpoint Ember Seed Table (deterministic, 1 batched draw) ───────────────
#define RUNE_EMBER_COUNT 220
typedef struct {
    float angle;
    float radNorm;   // normalized to quad half-size (0.52..0.84)
    float lifeSpeed;
    float phase;
    float sizeNorm;  // world radius fraction
    float driftSign;
    bool  isSkim;
} RuneEmberSeed;

static RuneEmberSeed s_runeEmbersData[RUNE_EMBER_COUNT];
static bool          s_runeEmbersReady = false;

static void Rune_InitEmbers(void)
{
    if (s_runeEmbersReady) return;
    s_runeEmbersReady = true;

    uint32_t rng = 1012u;
    for (int i = 0; i < RUNE_EMBER_COUNT; i++)
    {
        bool skim = (Rune_RngNext(&rng) > 0.78f);
        float a = Rune_RngNext(&rng) * RUNE_TAU;
        // WebGL: ring radius ~1.93..2.98 on 3.6 half-size -> 0.52..0.83 normalized
        float rNorm = skim
            ? (0.42f + Rune_RngNext(&rng) * 0.58f)
            : (0.52f + powf(Rune_RngNext(&rng), 1.35f) * 0.31f);
        s_runeEmbersData[i].angle     = a;
        s_runeEmbersData[i].radNorm   = rNorm;
        s_runeEmbersData[i].lifeSpeed = 0.28f + Rune_RngNext(&rng) * 0.62f;
        s_runeEmbersData[i].phase     = Rune_RngNext(&rng);
        s_runeEmbersData[i].sizeNorm  = 0.008f + powf(Rune_RngNext(&rng), 2.2f) * 0.016f;
        s_runeEmbersData[i].driftSign = (Rune_RngNext(&rng) > 0.5f ? 1.0f : -1.0f) *
                                        (0.4f + Rune_RngNext(&rng) * 0.7f);
        s_runeEmbersData[i].isSkim    = skim;
    }
}

// ── Shader & Uniform Handles ────────────────────────────────────────────────
typedef struct {
    Shader shader;
    int noiseTex;
    int deepColor, midColor, coreColor;
    int params, style, layerRot;
} RuneShader;

static RuneShader s_runeFx = {0};
static bool       s_runeShaderTried = false;

static void Rune_InitShared(void)
{
    if (s_runeInit) return;
    Tuning_RegisterFloat("rune_glow",     &s_runeGlow,     1.0f);
    Tuning_RegisterFloat("rune_spin",     &s_runeSpin,     1.0f);
    Tuning_RegisterFloat("rune_embers",   &s_runeEmbers,   1.0f);
    Tuning_RegisterFloat("rune_parallax", &s_runeParallax, 1.0f);
    Tuning_RegisterFloat("rune_conform",  &s_runeConform,  1.0f);
    Rune_InitEmbers();
    s_runeInit = true;
}

static void Rune_InitShader(void)
{
    if (s_runeShaderTried) return;
    s_runeShaderTried = true;

    s_runeFx.shader = ResourceManager_LoadShader("core/shaders/rune_circle.vs",
                                                 "core/shaders/rune_circle.fs");
    if (s_runeFx.shader.id == 0)
    {
        TraceLog(LOG_WARNING, "RuneCircle: rune_circle.vs/.fs failed to load");
        return;
    }
    s_runeFx.noiseTex  = GetShaderLocation(s_runeFx.shader, "u_noiseTex");
    s_runeFx.deepColor = GetShaderLocation(s_runeFx.shader, "u_deepColor");
    s_runeFx.midColor  = GetShaderLocation(s_runeFx.shader, "u_midColor");
    s_runeFx.coreColor = GetShaderLocation(s_runeFx.shader, "u_coreColor");
    s_runeFx.params    = GetShaderLocation(s_runeFx.shader, "u_params");
    s_runeFx.style     = GetShaderLocation(s_runeFx.shader, "u_style");
    s_runeFx.layerRot  = GetShaderLocation(s_runeFx.shader, "u_layerRot");
}

static bool Rune_HasShader(void)
{
    return s_runeFx.shader.id != 0 && s_runeFx.deepColor >= 0 &&
           s_runeFx.midColor >= 0 && s_runeFx.coreColor >= 0 &&
           s_runeFx.params >= 0 && s_runeFx.style >= 0 && s_runeFx.layerRot >= 0;
}

static void Rune_PlaneBasis(Vector3 normal, Vector3 *outU, Vector3 *outV)
{
    Vector3 n = Vector3Normalize(normal);
    Vector3 ref = (fabsf(n.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f}
                                       : (Vector3){1.0f, 0.0f, 0.0f};
    *outU = Vector3Normalize(Vector3CrossProduct(n, ref));
    *outV = Vector3CrossProduct(n, *outU);
}

static float Rune_WrapTau(double x)
{
    const double tau = 6.283185307179586;
    double m = fmod(x, tau);
    if (m < 0.0) m += tau;
    return (float)m;
}

static float Rune_Wrap01(double x)
{
    double m = fmod(x, 1.0);
    if (m < 0.0) m += 1.0;
    return (float)m;
}

// ── Terrain-Conforming Surface Grid (7x7 samples -> 16x16 draped quads) ─────
#define RUNE_MESH_CELLS 16
#define RUNE_H_SAMPLES  7
#define RUNE_Y_LIFT     0.035f
#define RUNE_QUAD_EXT   1.22f
#define RUNE_GRID_VERTS ((RUNE_MESH_CELLS + 1) * (RUNE_MESH_CELLS + 1))
#define RUNE_H_COUNT    (RUNE_H_SAMPLES * RUNE_H_SAMPLES)

static Vector3 s_runeVerts[RUNE_GRID_VERTS];
static float   s_runeH[RUNE_H_COUNT];
static int     s_runeCells = 1;

static float Rune_SampleGrid(float fx, float fz)
{
    const int S = RUNE_H_SAMPLES;
    float gx = (fx * 0.5f + 0.5f) * (float)(S - 1);
    float gz = (fz * 0.5f + 0.5f) * (float)(S - 1);
    int i0 = (int)floorf(gx), j0 = (int)floorf(gz);
    if (i0 < 0) i0 = 0; if (i0 > S - 2) i0 = S - 2;
    if (j0 < 0) j0 = 0; if (j0 > S - 2) j0 = S - 2;
    float tx = gx - (float)i0, tz = gz - (float)j0;
    float h00 = s_runeH[j0 * S + i0],       h10 = s_runeH[j0 * S + i0 + 1];
    float h01 = s_runeH[(j0 + 1) * S + i0], h11 = s_runeH[(j0 + 1) * S + i0 + 1];
    return Math_Mix(Math_Mix(h00, h10, tx), Math_Mix(h01, h11, tx), tz);
}

static int Rune_BuildSurface(Vector3 center, Vector3 u, Vector3 v, Vector3 n,
                             float radius)
{
    float half = radius * RUNE_QUAD_EXT;
    Vector3 lift = Vector3Scale(n, RUNE_Y_LIFT);

    Vector3 probePos, probeNrm;
    bool conform = (s_runeConform >= 0.5f) && (n.y > 0.94f) &&
                   MapManager_SampleGroundSurfaceAt(center.x, center.z,
                                                    &probePos, &probeNrm);

    int cells = conform ? RUNE_MESH_CELLS : 1;
    const int S = RUNE_H_SAMPLES;

    if (conform)
    {
        float base = MapManager_GetGroundHeightAt(center.x, center.z);
        for (int j = 0; j < S; j++)
        {
            float fz = -1.0f + 2.0f * (float)j / (float)(S - 1);
            for (int i = 0; i < S; i++)
            {
                float fx = -1.0f + 2.0f * (float)i / (float)(S - 1);
                Vector3 wp = Vector3Add(center,
                    Vector3Add(Vector3Scale(u, fx * half),
                               Vector3Scale(v, fz * half)));
                s_runeH[j * S + i] =
                    MapManager_GetGroundHeightAt(wp.x, wp.z) - base;
            }
        }
    }

    for (int j = 0; j <= cells; j++)
    {
        float fz = -1.0f + 2.0f * (float)j / (float)cells;
        for (int i = 0; i <= cells; i++)
        {
            float fx = -1.0f + 2.0f * (float)i / (float)cells;
            Vector3 p = Vector3Add(center,
                Vector3Add(Vector3Scale(u, fx * half),
                           Vector3Scale(v, fz * half)));
            if (conform) p.y += Rune_SampleGrid(fx, fz);
            s_runeVerts[j * (cells + 1) + i] = Vector3Add(p, lift);
        }
    }
    return cells;
}

static void Rune_SubmitSurface(Vector3 n, int cells, float extraLift)
{
    float step = 1.0f / (float)cells;
    int stride = cells + 1;
    Vector3 off = Vector3Scale(n, extraLift);

    rlColor4ub(255, 255, 255, 255);
    for (int j = 0; j < cells; j++)
    {
        float t0 = step * (float)j;
        float t1 = t0 + step;
        for (int i = 0; i < cells; i++)
        {
            float s0 = step * (float)i;
            float s1 = s0 + step;
            Vector3 a = Vector3Add(s_runeVerts[j * stride + i], off);
            Vector3 b = Vector3Add(s_runeVerts[j * stride + i + 1], off);
            Vector3 c = Vector3Add(s_runeVerts[(j + 1) * stride + i + 1], off);
            Vector3 d = Vector3Add(s_runeVerts[(j + 1) * stride + i], off);

            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s0, t0); rlVertex3f(a.x, a.y, a.z);
            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s1, t0); rlVertex3f(b.x, b.y, b.z);
            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s1, t1); rlVertex3f(c.x, c.y, c.z);
            rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(s0, t1); rlVertex3f(d.x, d.y, d.z);
        }
    }
}

static void Rune_SetCommonUniforms(const RuneVariantDef *def,
                                   float fade, float glowGain,
                                   float ignite, float igniteSpike,
                                   float timeWrapped, float seed,
                                   float passMode, float layerMode,
                                   float layerRot)
{
    Vector4 deep = ColorNormalize(def->deep);
    Vector4 mid  = ColorNormalize(def->mid);
    Vector4 core = ColorNormalize(def->core);
    SetShaderValue(s_runeFx.shader, s_runeFx.deepColor, &deep, SHADER_UNIFORM_VEC4);
    SetShaderValue(s_runeFx.shader, s_runeFx.midColor,  &mid,  SHADER_UNIFORM_VEC4);
    SetShaderValue(s_runeFx.shader, s_runeFx.coreColor, &core, SHADER_UNIFORM_VEC4);

    float params[4] = { fade, glowGain, ignite, igniteSpike };
    float style[4]  = { timeWrapped, seed, passMode, layerMode };
    SetShaderValue(s_runeFx.shader, s_runeFx.params,   params,    SHADER_UNIFORM_VEC4);
    SetShaderValue(s_runeFx.shader, s_runeFx.style,    style,     SHADER_UNIFORM_VEC4);
    SetShaderValue(s_runeFx.shader, s_runeFx.layerRot, &layerRot, SHADER_UNIFORM_FLOAT);
    if (s_runeFx.noiseTex >= 0 && s_runeNoiseTex.id != 0)
        SetShaderValueTexture(s_runeFx.shader, s_runeFx.noiseTex, s_runeNoiseTex);
}

static void Rune_SubmitEmbers(Vector3 center, Vector3 u, Vector3 v, Vector3 n,
                              float radius, double time, float ignite,
                              float igniteSpike, float emberRatio)
{
    int count = (int)((float)RUNE_EMBER_COUNT * emberRatio);
    if (count <= 0) return;
    if (count > RUNE_EMBER_COUNT) count = RUNE_EMBER_COUNT;

    // Extract camera right/up from active view transform (ENGINE_LANDMINES 28)
    Matrix view = rlGetMatrixTransform();
    Vector3 camR = { view.m0, view.m4, view.m8 };
    Vector3 camU = { view.m1, view.m5, view.m9 };
    float rLen = Vector3Length(camR);
    float uLen = Vector3Length(camU);
    if (rLen > 1e-4f && uLen > 1e-4f)
    {
        camR = Vector3Scale(camR, 1.0f / rLen);
        camU = Vector3Scale(camU, 1.0f / uLen);
    }
    else
    {
        camR = u;
        camU = n;
    }

    float half = radius * RUNE_QUAD_EXT;
    float spd = s_runeSpin;
    float tF = (float)fmod(time, 1000.0);

    rlBegin(RL_QUADS);
    for (int i = 0; i < count; i++)
    {
        const RuneEmberSeed *e = &s_runeEmbersData[i];
        float life = Rune_Wrap01((double)e->phase + time * (double)e->lifeSpeed * (0.28 + (double)spd * 0.22));
        float fadeIn = life < 0.12f ? (life / 0.12f) : 1.0f;
        float fadeOut = powf(1.0f - life, 1.45f);
        float alpha = fadeIn * fadeOut;

        float a = e->angle + tF * (e->isSkim ? 0.12f : 0.18f * spd) * e->driftSign;
        float ca = cosf(a), sa = sinf(a);
        float rx = ca, ry = sa;
        float tx = -sa * e->driftSign, ty = ca * e->driftSign;

        Vector3 wp;
        if (!e->isSkim)
        {
            float outR = (e->radNorm + life * 0.095f) * half;
            float tang = life * (0.13f + igniteSpike * 0.18f) * half;
            float px = rx * outR + tx * tang;
            float py = ry * outR + ty * tang;
            float rise = RUNE_Y_LIFT + (0.015f + life * (0.20f + e->phase * 0.14f)) * radius;
            wp = Vector3Add(center,
                 Vector3Add(Vector3Add(Vector3Scale(u, px), Vector3Scale(v, py)),
                            Vector3Scale(n, rise)));
        }
        else
        {
            float outR = e->radNorm * half;
            float rise = RUNE_Y_LIFT + (0.008f + fabsf(sinf(tF * 3.0f + e->phase * 12.0f)) * 0.018f) * radius;
            wp = Vector3Add(center,
                 Vector3Add(Vector3Add(Vector3Scale(u, rx * outR), Vector3Scale(v, ry * outR)),
                            Vector3Scale(n, rise)));
        }

        float twinkle = 0.68f + 0.32f * sinf(tF * 11.0f + e->phase * RUNE_TAU * 10.0f);
        float heat01 = (0.75f + 0.35f * twinkle + igniteSpike * 0.4f) * (1.0f - life * 0.45f) / 1.3f;
        if (heat01 < 0.0f) heat01 = 0.0f; else if (heat01 > 1.0f) heat01 = 1.0f;
        float aFinal = alpha * twinkle * (0.25f + 0.75f * ignite);
        if (aFinal < 0.02f) continue;
        if (aFinal > 1.0f) aFinal = 1.0f;

        uint8_t hr = (uint8_t)(heat01 * 255.0f);
        uint8_t aa = (uint8_t)(aFinal * 255.0f);
        rlColor4ub(hr, 255, 255, aa);

        float sz = e->sizeNorm * half;
        Vector3 rVec = Vector3Scale(camR, sz);
        Vector3 uVec = Vector3Scale(camU, sz);
        Vector3 p0 = Vector3Subtract(Vector3Subtract(wp, rVec), uVec);
        Vector3 p1 = Vector3Subtract(Vector3Add(wp, rVec), uVec);
        Vector3 p2 = Vector3Add(Vector3Add(wp, rVec), uVec);
        Vector3 p3 = Vector3Add(Vector3Subtract(wp, rVec), uVec);

        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p0.x, p0.y, p0.z);
        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p1.x, p1.y, p1.z);
        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p2.x, p2.y, p2.z);
        rlNormal3f(n.x, n.y, n.z); rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p3.x, p3.y, p3.z);
    }
    rlEnd();
}

// ── Public Entry Points ─────────────────────────────────────────────────────
void VFX_ComposeRuneCircleVariant(Vector3 center, Vector3 normal,
                                  VFX_RuneCircleVariant variant,
                                  float radius, float t01, int ringCount)
{
    Rune_InitShared();
    Rune_InitShader();
    if (!Rune_HasShader()) return;

    if ((int)variant < 0 || (int)variant >= VFX_RUNE_VARIANT_COUNT)
        variant = VFX_RUNE_PHAN_THIEN;

    Rune_EnsureNoiseTex();
    Rune_EnsureVariantTextures(variant);

    if (radius <= 0.0f) radius = 1.0f;
    if (t01 < 0.0f) t01 = 0.0f;
    if (t01 > 1.0f) t01 = 1.0f;
    if (ringCount < 1) ringCount = 1;
    if (ringCount > 4) ringCount = 4;

    // Symmetric ignite sweep (0.00..0.24) + cooldown spike (0.24..0.42) + fade out (0.86..1.00)
    float ignite = 1.0f;
    float igniteSpike = 0.0f;
    if (t01 < 0.24f)
    {
        float u = t01 / 0.24f;
        ignite = 1.0f - powf(1.0f - u, 2.3f);
        igniteSpike = sinf(u * RUNE_PI) * 0.90f;
    }
    else if (t01 < 0.42f)
    {
        float c = (t01 - 0.24f) / 0.18f;
        igniteSpike = (1.0f - c) * (1.0f - c) * 0.45f;
    }

    float fade = 1.0f;
    if (t01 < 0.03f) fade = t01 / 0.03f;
    else if (t01 > 0.86f) fade = (1.0f - t01) / 0.14f;
    if (fade <= 0.001f) return;

    const RuneVariantDef *def = &s_runeDefs[variant];
    const RuneTexCache *tex = &s_runeCache[variant];

    Vector3 u, v;
    Rune_PlaneBasis(normal, &u, &v);
    Vector3 n = Vector3Normalize(normal);

    double time = (double)TimeFX_Elapsed();
    float timeWrapped = (float)fmod(time, 500.0);
    double spd = (double)s_runeSpin * (1.0 + (double)igniteSpike * 1.4);
    float rot1 = Rune_WrapTau(time * (double)def->rot[0] * spd);
    float rot2 = Rune_WrapTau(time * (double)def->rot[1] * spd);
    float rot3 = Rune_WrapTau(time * (double)def->rot[2] * spd);

    float pH = s_runeParallax * (radius / 2.2f);
    float lift1 = 0.012f * pH;
    float lift2 = 0.036f * pH;
    float lift3 = 0.062f * pH;
    float seedVal = (float)def->seed * 0.017f;
    float seedPulse = (float)def->seed * 0.031f;
    float glowBase = s_runeGlow * (0.78f + 0.08f * (float)ringCount);

    s_runeCells = Rune_BuildSurface(center, u, v, n, radius);

    // ── Pass 0: BODY (BLEND_ALPHA) — pigment ink for bright-background readability
    {
        VFXRenderScope scope = VFXRender_BeginDraw(
            VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);

        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();

        SkillManager_BeginShader(s_runeFx.shader);

        // Layer 1 (inner core + main script)
        Rune_SetCommonUniforms(def, fade * 0.94f, glowBase * 1.08f,
                               ignite, igniteSpike, timeWrapped, seedVal,
                               0.0f, 0.0f, rot1);
        rlSetTexture(tex->innerTex.id);
        rlBegin(RL_QUADS);
        Rune_SubmitSurface(n, s_runeCells, lift1);
        rlEnd();
        rlDrawRenderBatchActive();

        // Layer 2 (outer counter-rotating script + geometric motifs)
        if (ringCount >= 2)
        {
            Rune_SetCommonUniforms(def, fade * 0.90f, glowBase * 0.86f,
                                   ignite, igniteSpike, timeWrapped, seedVal,
                                   0.0f, 0.0f, rot2);
            rlSetTexture(tex->outerTex.id);
            rlBegin(RL_QUADS);
            Rune_SubmitSurface(n, s_runeCells, lift2);
            rlEnd();
            rlDrawRenderBatchActive();
        }

        rlSetTexture(0);
        SkillManager_EndShader();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }

    // ── Pass 1: EMISSION (BLEND_ALPHA_PREMULTIPLY) — 3 blackbody layers + embers
    {
        VFXRenderScope scope = VFXRender_BeginDraw(
            VFX_RENDER_PASS_EMISSION, VFX_SURFACE_PREMULTIPLIED, false);

        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlDrawRenderBatchActive();

        SkillManager_BeginShader(s_runeFx.shader);

        // Layer 1: Inner molten core + main script
        Rune_SetCommonUniforms(def, fade, glowBase * 1.08f,
                               ignite, igniteSpike, timeWrapped, seedVal,
                               1.0f, 0.0f, rot1);
        rlSetTexture(tex->innerTex.id);
        rlBegin(RL_QUADS);
        Rune_SubmitSurface(n, s_runeCells, lift1);
        rlEnd();
        rlDrawRenderBatchActive();

        // Layer 2: Outer counter-rotating script + geometric motifs
        if (ringCount >= 2)
        {
            Rune_SetCommonUniforms(def, fade, glowBase * 0.86f,
                                   ignite, igniteSpike, timeWrapped, seedVal,
                                   1.0f, 0.0f, rot2);
            rlSetTexture(tex->outerTex.id);
            rlBegin(RL_QUADS);
            Rune_SubmitSurface(n, s_runeCells, lift2);
            rlEnd();
            rlDrawRenderBatchActive();
        }

        // Layer 3: Perimeter energy pulse & sharp filaments
        if (ringCount >= 2)
        {
            Rune_SetCommonUniforms(def, fade, glowBase * 0.95f,
                                   ignite, igniteSpike, timeWrapped, seedPulse,
                                   1.0f, 1.0f, rot3);
            rlSetTexture(0);
            rlBegin(RL_QUADS);
            Rune_SubmitSurface(n, s_runeCells, lift3);
            rlEnd();
            rlDrawRenderBatchActive();
        }

        // Layer 4: Pinpoint orbital embers (1 batched draw)
        if (s_runeEmbers > 0.01f)
        {
            Rune_SetCommonUniforms(def, fade, glowBase,
                                   ignite, igniteSpike, timeWrapped, seedVal,
                                   1.0f, 2.0f, 0.0f);
            rlSetTexture(0);
            Rune_SubmitEmbers(center, u, v, n, radius, time,
                              ignite, igniteSpike, s_runeEmbers);
            rlDrawRenderBatchActive();
        }

        rlSetTexture(0);
        SkillManager_EndShader();
        rlEnableBackfaceCulling();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
}

// ── Live Inspector Config (Sandbox NEW FX Parameters Panel) ──────────────────
static VFX_RuneCircleConfig s_runeLiveConfig = {
    .variant   = VFX_RUNE_PHAN_THIEN,
    .posture   = VFX_RUNE_POSTURE_GROUND,
    .radius    = 2.2f,
    .spinSpeed = 1.0f,
    .glow      = 1.0f,
    .embers    = 1.0f,
    .parallax  = 1.0f,
};

VFX_RuneCircleConfig *VFX_RuneCircle_GetLiveConfig(void)
{
    return &s_runeLiveConfig;
}

int VFX_RuneCircle_GetParams(VFX_RuneCircleConfig *cfg, VFX_ParamDef *out, int maxOut)
{
    if (!cfg || !out || maxOut <= 0) return 0;
    static const char * const s_variantNames[VFX_RUNE_VARIANT_COUNT] = {
        "PHAN THIEN", "BAT QUAI", "TRU TIEN", "HUYET SAT", "U MINH", "TINH TU"
    };
    static const char * const s_postureNames[VFX_RUNE_POSTURE_COUNT] = {
        "Ground", "Standing Portal"
    };
    int n = 0;
    if (n < maxOut) out[n++] = (VFX_ParamDef){
        .name = "Variant", .group = "Seal", .type = VFX_PARAM_ENUM,
        .valPtr = &cfg->variant, .minInt = 0, .maxInt = VFX_RUNE_VARIANT_COUNT - 1,
        .enumNames = s_variantNames, .enumCount = VFX_RUNE_VARIANT_COUNT
    };
    if (n < maxOut) out[n++] = (VFX_ParamDef){
        .name = "Posture", .group = "Seal", .type = VFX_PARAM_ENUM,
        .valPtr = &cfg->posture, .minInt = 0, .maxInt = VFX_RUNE_POSTURE_COUNT - 1,
        .enumNames = s_postureNames, .enumCount = VFX_RUNE_POSTURE_COUNT
    };
    if (n < maxOut) out[n++] = (VFX_ParamDef){
        .name = "Radius", .group = "Seal", .type = VFX_PARAM_FLOAT,
        .valPtr = &cfg->radius, .minFloat = 0.6f, .maxFloat = 4.5f, .stepFloat = 0.2f
    };
    if (n < maxOut) out[n++] = (VFX_ParamDef){
        .name = "Spin Speed", .group = "Tune", .type = VFX_PARAM_FLOAT,
        .valPtr = &s_runeSpin, .minFloat = 0.0f, .maxFloat = 3.0f, .stepFloat = 0.1f
    };
    if (n < maxOut) out[n++] = (VFX_ParamDef){
        .name = "Glow", .group = "Tune", .type = VFX_PARAM_FLOAT,
        .valPtr = &s_runeGlow, .minFloat = 0.4f, .maxFloat = 2.0f, .stepFloat = 0.05f
    };
    if (n < maxOut) out[n++] = (VFX_ParamDef){
        .name = "Embers", .group = "Tune", .type = VFX_PARAM_FLOAT,
        .valPtr = &s_runeEmbers, .minFloat = 0.0f, .maxFloat = 1.0f, .stepFloat = 0.05f
    };
    if (n < maxOut) out[n++] = (VFX_ParamDef){
        .name = "Parallax 3D", .group = "Tune", .type = VFX_PARAM_FLOAT,
        .valPtr = &s_runeParallax, .minFloat = 0.0f, .maxFloat = 2.5f, .stepFloat = 0.1f
    };
    return n;
}

void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat,
                           float radius, float t01, int ringCount)
{
    VFX_ComposeRuneCircleVariant(center, normal,
                                 VFX_RuneCircle_VariantForMaterial(mat),
                                 radius, t01, ringCount);
}
