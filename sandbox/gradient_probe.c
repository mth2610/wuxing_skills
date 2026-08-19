// GRADIENT PROBE — the pipeline on trial, with the effect removed.
//
// THE CLAIM UNDER TEST. The ShieldShell's silhouette and its ground-contact line
// are authored as one continuous white -> yellow -> orange ramp and arrive as
// distinct colour patches with edges between them. The shell computes that ramp out
// of a fresnel, a matcap, a Beer-Lambert wall density and a depth-gap contact term,
// so the banding could belong to any of them — or to none of them.
//
// So draw the SIMPLEST THING THAT CAN BAND: a rectangle whose colour is an analytic
// function of x. No mesh, no normals, no depth, no texture, no time. If the ramp
// still comes out in patches, nothing the shell does can be responsible, and no
// amount of authoring inside glass_shell.fs will fix it.
//
// WHY IT IS DRAWN WHERE IT IS. The rectangle goes into the HDR scene target inside
// PostFX_Begin/End — the same buffer a VFX writes into — so it takes the whole chain:
// bloom, exposure, tone map (post_process.fs toneMapScene, incl. postfx_hue_restore),
// colour grade, LUT, vignette, dither, FXAA. The control strip at the bottom is the
// SAME ramp evaluated on the CPU through the plain per-channel ACES curve and drawn
// AFTER PostFX_Draw, so it skips all of it. Band-for-band, the difference between the
// two IS what the pipeline adds.
//
// A ramp is the right probe and a flat patch is not: sandbox/colour_probe.c already
// proves constant colours arrive intact. Banding is by definition a defect of the
// DERIVATIVE, and only a gradient exposes it.
//
// USE
//   in the VFX tester, press G                  (arms it for one frame)
//   WUXING_GRADIENT_PROBE=1 ./build/wuxing ...  (arms on the first frame, headless-safe)
// Both print the numbers to the log and write autotest_output/gradient_probe.png.
//
// READING THE NUMBERS. The input ramp is monotone rising by construction, so on an
// honest pipeline every channel is monotone non-decreasing. `reversals` counts steps
// where a channel goes DOWN, `worst_drop` is how far below its own running maximum a
// channel ever falls. A colour band is exactly that: a channel dips and recovers,
// which puts an edge on both sides of the dip. One dip = one visible ring.

#include "sandbox/gradient_probe.h"

#include "raylib.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GP_BANDS 5
#define GP_BAND_PX 88                       /* per band, HDR strip */
#define GP_STRIP_PX (GP_BANDS * GP_BAND_PX) /* total HDR strip height */
#define GP_CTRL_BAND_PX 36
#define GP_CTRL_PX (GP_BANDS * GP_CTRL_BAND_PX)

/* MEASURE THE MIDDLE, NOT THE EDGES. Chromatic aberration and the vignette are radial:
 * at x = 0 and x = W they put a blue fringe and a 40% darkening on top of whatever the
 * ramp is doing, and the first run of this probe counted both as banding. Band 4 exists
 * to show what is left inside this window; keep the window and the reference together. */
#define GP_X_LO 0.20f
#define GP_X_HI 0.80f

/* Must match probe_gradient.fs. The shader is the ground truth for the LOOK; these
 * exist so the CPU control strip evaluates the identical function. */
static const float GP_MAX_HDR = 12.0f;   /* top of the level ramp, scene-linear */
static const float GP_MIN_HDR = 0.05f;   /* bottom of it (a log ramp cannot start at 0) */
static const float GP_FLAT_LEVEL = 3.0f; /* the constant level of bands 3 and 4 */
static const float GP_ORANGE[3] = {1.00f, 0.55f, 0.18f};
static const float GP_YELLOW[3] = {1.00f, 0.85f, 0.35f};
static const float GP_WHITE[3] = {1.00f, 1.00f, 1.00f};

static bool s_armed = false;
static bool s_active = false;  /* drawing this frame */
static bool s_pending = false; /* readback owed at end of this frame */
static Shader s_shader = {0};
static bool s_tried = false;
static Texture2D s_white = {0};
static Texture2D s_control = {0};
static bool s_envChecked = false;

void GradientProbe_Arm(void) { s_armed = true; }
bool GradientProbe_IsActive(void) { return s_active; }

static void EnsureShader(void)
{
    if (s_tried) return;
    s_tried = true;
    s_shader = LoadShader(0, "core/shaders/probe_gradient.fs");
    if (s_shader.id == 0)
        TraceLog(LOG_WARNING, "[GRADIENT] probe_gradient.fs failed to load");

    /* DrawTexturePro is what makes fragTexCoord span 0..1 across the destination
     * rectangle; DrawRectangle would hand the shader the shapes atlas's own texcoords
     * (a single white texel) and every fragment would see the same t. The texture
     * itself is never sampled by the shader — only its rect drives the interpolator. */
    Image w = GenImageColor(1, 1, WHITE);
    s_white = LoadTextureFromImage(w);
    UnloadImage(w);
}

/* ── the ramps, CPU side ─────────────────────────────────────────────────────── */

static void GP_Wyo(float t, float out[3])
{
    const float *a, *b;
    float u;
    if (t < 0.5f) { a = GP_ORANGE; b = GP_YELLOW; u = t / 0.5f; }
    else          { a = GP_YELLOW; b = GP_WHITE;  u = (t - 0.5f) / 0.5f; }
    for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * u;
}

static void GP_BandColor(int band, float t, float out[3])
{
    const float level = expf(logf(GP_MIN_HDR) +
                             (logf(GP_MAX_HDR) - logf(GP_MIN_HDR)) * t);
    switch (band) {
        case 0: out[0] = out[1] = out[2] = level; break;
        case 1: for (int i = 0; i < 3; i++) out[i] = GP_ORANGE[i] * level; break;
        case 2: GP_Wyo(t, out); for (int i = 0; i < 3; i++) out[i] *= level; break;
        case 3: GP_Wyo(t, out); for (int i = 0; i < 3; i++) out[i] *= GP_FLAT_LEVEL; break;
        default: for (int i = 0; i < 3; i++) out[i] = GP_ORANGE[i] * GP_FLAT_LEVEL; break;
    }
}

/* The PER-CHANNEL ACES fit, i.e. post_process.fs's curve with u_hueRestore = 0. The
 * control strip is deliberately this and not the shipping curve: the shipping curve is
 * the thing on trial, and a control that contains the accused proves nothing. */
static float GP_Aces(float x)
{
    float v = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static void EnsureControlTexture(int width)
{
    if (s_control.id != 0 && s_control.width == width) return;
    if (s_control.id != 0) UnloadTexture(s_control);

    Image img = GenImageColor(width, GP_CTRL_PX, BLACK);
    for (int band = 0; band < GP_BANDS; band++) {
        for (int y = 2; y < GP_CTRL_BAND_PX - 2; y++) {
            for (int x = 0; x < width; x++) {
                float t = (width > 1) ? (float)x / (float)(width - 1) : 0.0f;
                float lin[3];
                GP_BandColor(band, t, lin);
                Color c = {(unsigned char)(GP_Aces(lin[0]) * 255.0f + 0.5f),
                           (unsigned char)(GP_Aces(lin[1]) * 255.0f + 0.5f),
                           (unsigned char)(GP_Aces(lin[2]) * 255.0f + 0.5f), 255};
                ImageDrawPixel(&img, x, band * GP_CTRL_BAND_PX + y, c);
            }
        }
    }
    s_control = LoadTextureFromImage(img);
    UnloadImage(img);
}

/* ── draw ────────────────────────────────────────────────────────────────────── */

void GradientProbe_DrawScene(void)
{
    if (!s_envChecked) {
        s_envChecked = true;
        const char *e = getenv("WUXING_GRADIENT_PROBE");
        if (e && e[0] == '1') s_armed = true;
    }
    if (!s_armed) return;
    s_armed = false;
    s_active = true;
    s_pending = true;

    EnsureShader();
    if (s_shader.id == 0) return;

    const float w = (float)GetScreenWidth();

    int bandsLoc = GetShaderLocation(s_shader, "u_bands");
    int maxLoc = GetShaderLocation(s_shader, "u_maxHDR");
    int minLoc = GetShaderLocation(s_shader, "u_minHDR");
    int flatLoc = GetShaderLocation(s_shader, "u_flatLevel");
    float bandsF = (float)GP_BANDS;

    BeginShaderMode(s_shader);
    if (bandsLoc >= 0) SetShaderValue(s_shader, bandsLoc, &bandsF, SHADER_UNIFORM_FLOAT);
    if (maxLoc >= 0) SetShaderValue(s_shader, maxLoc, &GP_MAX_HDR, SHADER_UNIFORM_FLOAT);
    if (minLoc >= 0) SetShaderValue(s_shader, minLoc, &GP_MIN_HDR, SHADER_UNIFORM_FLOAT);
    if (flatLoc >= 0) SetShaderValue(s_shader, flatLoc, &GP_FLAT_LEVEL, SHADER_UNIFORM_FLOAT);

    /* OPAQUE, not blended: the probe must REPLACE whatever the scene put here, or the
     * measurement is of the ramp plus the arena behind it. rlDisableColorBlend is
     * flush-scoped on both backends, so the batch is drawn inside the disabled window
     * (core/post_fx.c's composite has the same note, and the bug it describes). */
    rlDisableColorBlend();
    DrawTexturePro(s_white, (Rectangle){0, 0, 1, 1},
                   (Rectangle){0, 0, w, (float)GP_STRIP_PX}, (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndShaderMode();
}

void GradientProbe_DrawControl(void)
{
    if (!s_active) return;

    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    EnsureControlTexture(w);
    if (s_control.id == 0) return;

    rlDisableColorBlend();
    DrawTexturePro(s_control, (Rectangle){0, 0, (float)w, (float)GP_CTRL_PX},
                   (Rectangle){0, (float)(h - GP_CTRL_PX), (float)w, (float)GP_CTRL_PX},
                   (Vector2){0, 0}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
}

/* ── measurement ─────────────────────────────────────────────────────────────── */

typedef struct {
    int reversals[3];   /* steps where the channel goes DOWN on a rising input */
    int worstDrop[3];   /* furthest a channel ever falls below its own running max */
    int slopeDips[3];   /* slope collapses to near zero and then RE-ACCELERATES */
} BandStats;

/* Averaged over several rows: the pipeline's last step is a +/- 1 LSB dither, and a
 * single scanline would report that noise as reversals. Averaging kills the dither and
 * leaves any real dip intact, since a dip is hundreds of pixels wide.
 *
 * WHAT slopeDips COUNTS, and why it is the metric that matters. A ramp can be perfectly
 * monotone and still show a visible edge: what the eye reads as "a band" is a place
 * where the rate of change collapses and then picks back up, because that is a plateau
 * with a wall on each side. `reversals` only catches the stronger case where the channel
 * actually goes backwards. Both are reported; the tone map's restoration weight rises
 * and falls, so it produces both, depending on level.
 */
static void ScanBand(Image img, int y0, int y1, int width, BandStats *st, char *strip,
                     size_t stripSz, char *slopeOut, size_t slopeSz)
{
    const int rows = (y1 - y0);
    static float acc[4096][3];
    if (width > 4096 || rows <= 0) return;

    const int xLo = (int)(width * GP_X_LO);
    const int xHi = (int)(width * GP_X_HI);

    for (int x = 0; x < width; x++) {
        float sum[3] = {0, 0, 0};
        for (int y = y0; y < y1; y++) {
            Color c = GetImageColor(img, x, y);
            sum[0] += c.r; sum[1] += c.g; sum[2] += c.b;
        }
        for (int i = 0; i < 3; i++) acc[x][i] = sum[i] / (float)rows;
    }

    /* Slope over a 16 px baseline, not 1 px: at 1 px the step of a smooth ramp is a
     * fraction of an LSB and the sign is quantisation, not shape. */
    const int SPAN = 16;
    for (int ch = 0; ch < 3; ch++) {
        int rev = 0;
        float peak = acc[xLo][ch], drop = 0.0f;
        for (int x = xLo + 1; x <= xHi; x++) {
            if (acc[x][ch] - acc[x - 1][ch] < -0.5f) rev++;
            if (acc[x][ch] > peak) peak = acc[x][ch];
            if (peak - acc[x][ch] > drop) drop = peak - acc[x][ch];
        }
        st->reversals[ch] = rev;
        st->worstDrop[ch] = (int)(drop + 0.5f);

        /* mean slope over the window, then count collapse-and-recover events */
        float mean = 0.0f; int n = 0;
        for (int x = xLo; x + SPAN <= xHi; x += SPAN) { mean += acc[x + SPAN][ch] - acc[x][ch]; n++; }
        if (n > 0) mean /= (float)n;
        int dips = 0; bool inDip = false;
        if (mean > 0.5f) {
            for (int x = xLo; x + SPAN <= xHi; x += SPAN) {
                float sl = acc[x + SPAN][ch] - acc[x][ch];
                if (!inDip && sl < mean * 0.25f) inDip = true;
                else if (inDip && sl > mean * 0.75f) { inDip = false; dips++; }
            }
        }
        st->slopeDips[ch] = dips;
    }

    /* Value strip and G-slope profile, both sampled ACROSS THE MEASURED WINDOW only. */
    strip[0] = 0; slopeOut[0] = 0;
    const int CELLS = 14;
    int step = (xHi - xLo) / CELLS;
    if (step < 1) step = 1;
    for (int x = xLo; x <= xHi; x += step) {
        char cell[24];
        snprintf(cell, sizeof(cell), "%3d,%3d,%3d ", (int)(acc[x][0] + 0.5f),
                 (int)(acc[x][1] + 0.5f), (int)(acc[x][2] + 0.5f));
        if (strlen(strip) + strlen(cell) + 1 >= stripSz) break;
        strcat(strip, cell);
    }
    for (int x = xLo; x + step <= xHi; x += step) {
        char cell[16];
        snprintf(cell, sizeof(cell), "%+4d ", (int)(acc[x + step][1] - acc[x][1]));
        if (strlen(slopeOut) + strlen(cell) + 1 >= slopeSz) break;
        strcat(slopeOut, cell);
    }
}

void GradientProbe_Readback(void)
{
    if (!s_pending) return;
    s_pending = false;
    s_active = false;

    Image img = LoadImageFromScreen();
    const int w = img.width, h = img.height;
    /* LoadImageFromScreen returns the REAL render target size, which is not always the
     * logical screen size (rlvk on Android renders at native resolution). Scale the
     * layout the same way rather than assuming 1:1. */
    const float sy = (float)h / (float)GetScreenHeight();

    TraceLog(LOG_INFO, "[GRADIENT] ---- probe qua TOAN BO duong ong (HDR -> post) ----");
    TraceLog(LOG_INFO, "[GRADIENT] band 0=xam | 1=MOT mau cam | 2=cam->vang->trang sang dan | 3=cam->vang->trang sang CO DINH | 4=PHANG (chuan vi tri)");

    char strip[1024], slope[512];
    for (int band = 0; band < GP_BANDS; band++) {
        int y0 = (int)((band * GP_BAND_PX + GP_BAND_PX * 0.30f) * sy);
        int y1 = (int)((band * GP_BAND_PX + GP_BAND_PX * 0.70f) * sy);
        if (y1 <= y0 || y1 > h) continue;
        BandStats st = {0};
        ScanBand(img, y0, y1, w, &st, strip, sizeof(strip), slope, sizeof(slope));
        TraceLog(LOG_INFO,
                 "[GRADIENT] HDR   band %d  dao chieu R/G/B %d/%d/%d  tut %d/%d/%d  hut-doc-roi-vot-lai %d/%d/%d",
                 band, st.reversals[0], st.reversals[1], st.reversals[2],
                 st.worstDrop[0], st.worstDrop[1], st.worstDrop[2],
                 st.slopeDips[0], st.slopeDips[1], st.slopeDips[2]);
        TraceLog(LOG_INFO, "[GRADIENT]     mau  %s", strip);
        TraceLog(LOG_INFO, "[GRADIENT]     dG   %s", slope);
    }

    for (int band = 0; band < GP_BANDS; band++) {
        int base = h - (int)(GP_CTRL_PX * sy) + (int)(band * GP_CTRL_BAND_PX * sy);
        int y0 = base + (int)(GP_CTRL_BAND_PX * 0.30f * sy);
        int y1 = base + (int)(GP_CTRL_BAND_PX * 0.70f * sy);
        if (y1 <= y0 || y1 > h) continue;
        BandStats st = {0};
        ScanBand(img, y0, y1, w, &st, strip, sizeof(strip), slope, sizeof(slope));
        TraceLog(LOG_INFO,
                 "[GRADIENT] CHUNG band %d  dao chieu R/G/B %d/%d/%d  tut %d/%d/%d  hut-doc-roi-vot-lai %d/%d/%d",
                 band, st.reversals[0], st.reversals[1], st.reversals[2],
                 st.worstDrop[0], st.worstDrop[1], st.worstDrop[2],
                 st.slopeDips[0], st.slopeDips[1], st.slopeDips[2]);
        TraceLog(LOG_INFO, "[GRADIENT]     mau  %s", strip);
        TraceLog(LOG_INFO, "[GRADIENT]     dG   %s", slope);
    }

    if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
    ExportImage(img, "autotest_output/gradient_probe.png");
    UnloadImage(img);
    TraceLog(LOG_INFO, "[GRADIENT] -> autotest_output/gradient_probe.png");
}
