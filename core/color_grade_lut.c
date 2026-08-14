#include "core/color_grade_lut.h"

#include <stddef.h>

static Texture2D s_lut = {0};
static bool s_initialized = false;
static bool s_isNeutral = true;

#define LUT_SIZE   COLOR_GRADE_LUT_SIZE
#define LUT_WIDTH  (COLOR_GRADE_LUT_SIZE * COLOR_GRADE_LUT_SIZE)
#define LUT_HEIGHT (COLOR_GRADE_LUT_SIZE)

// The identity strip. Mirrored exactly by core/tests/color_grade_lut_test.c —
// if this addressing changes, that test's round-trip fails, which is the point.
static Image ColorGradeLut_BuildNeutralImage(void)
{
    Image img = GenImageColor(LUT_WIDTH, LUT_HEIGHT, BLACK);
    const float denom = (float)(LUT_SIZE - 1);
    for (int slice = 0; slice < LUT_SIZE; ++slice) {
        for (int g = 0; g < LUT_SIZE; ++g) {
            for (int r = 0; r < LUT_SIZE; ++r) {
                // 255.0f/denom rather than a 0..1 round-trip: the quantised
                // value has to land on the same byte the shader's inverse
                // mapping expects, or "neutral" is off by one code value and
                // the whole frame picks up a faint cast.
                unsigned char cr = (unsigned char)((float)r * 255.0f / denom + 0.5f);
                unsigned char cg = (unsigned char)((float)g * 255.0f / denom + 0.5f);
                unsigned char cb = (unsigned char)((float)slice * 255.0f / denom + 0.5f);
                ImageDrawPixel(&img, slice * LUT_SIZE + r, g,
                               (Color){cr, cg, cb, 255});
            }
        }
    }
    return img;
}

static void ColorGradeLut_Adopt(Image img, bool neutral)
{
    Texture2D uploaded = LoadTextureFromImage(img);
    if (uploaded.id == 0) {
        TraceLog(LOG_WARNING, "LUT: upload failed, keeping previous grade");
        return;
    }
    // BILINEAR is safe with this addressing: red maps to texel CENTRES inside a
    // tile (0.5 .. size-0.5), so filtering never reaches across a tile edge into
    // the neighbouring blue slice. POINT here would quantise the grade into
    // visible banding on smooth gradients.
    SetTextureFilter(uploaded, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(uploaded, TEXTURE_WRAP_CLAMP);
    if (s_lut.id != 0) UnloadTexture(s_lut);
    s_lut = uploaded;
    s_isNeutral = neutral;
}

void ColorGradeLut_Init(void)
{
    if (s_initialized) return;
    Image img = ColorGradeLut_BuildNeutralImage();
    ColorGradeLut_Adopt(img, true);
    UnloadImage(img);
    s_initialized = true;

    // Asset-optional, same shape as the audio system: dropping a strip at the
    // conventional path is the ENTIRE integration — no code change, no rebuild.
    // Announce which one won, because "my LUT did nothing" and "my LUT loaded
    // and is subtle" are indistinguishable on screen.
    // FileExists first: "no graded strip supplied" is the DEFAULT state, not a
    // problem, and a WARNING on the default state trains everyone to ignore the
    // log — which is where the real load failures get announced.
    if (FileExists(COLOR_GRADE_LUT_DEFAULT_PATH) &&
        ColorGradeLut_Load(COLOR_GRADE_LUT_DEFAULT_PATH)) return;
    TraceLog(LOG_INFO, "LUT: neutral %dx%d strip (%d^3) — drop a graded strip at "
                       "%s to grade the frame",
             LUT_WIDTH, LUT_HEIGHT, LUT_SIZE, COLOR_GRADE_LUT_DEFAULT_PATH);
}

void ColorGradeLut_Unload(void)
{
    if (s_lut.id != 0) UnloadTexture(s_lut);
    s_lut = (Texture2D){0};
    s_initialized = false;
    s_isNeutral = true;
}

bool ColorGradeLut_Load(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    ColorGradeLut_Init();
    if (!FileExists(path)) {
        TraceLog(LOG_WARNING, "LUT: '%s' not found, keeping current grade", path);
        return false;
    }
    Image img = LoadImage(path);
    if (img.data == NULL) {
        TraceLog(LOG_WARNING, "LUT: '%s' failed to decode, keeping current grade", path);
        return false;
    }
    if (img.width != LUT_WIDTH || img.height != LUT_HEIGHT) {
        // Naming the expected size is the difference between a one-line fix and
        // an afternoon: the usual mistake is a 32^3 strip from another engine.
        TraceLog(LOG_WARNING,
                 "LUT: '%s' is %dx%d, expected %dx%d (%d^3 strip) — keeping current grade",
                 path, img.width, img.height, LUT_WIDTH, LUT_HEIGHT, LUT_SIZE);
        UnloadImage(img);
        return false;
    }
    ColorGradeLut_Adopt(img, false);
    UnloadImage(img);
    TraceLog(LOG_INFO, "LUT: loaded '%s'", path);
    return true;
}

void ColorGradeLut_UseNeutral(void)
{
    if (s_isNeutral && s_lut.id != 0) return;
    Image img = ColorGradeLut_BuildNeutralImage();
    ColorGradeLut_Adopt(img, true);
    UnloadImage(img);
    s_initialized = true;
}

Texture2D ColorGradeLut_Texture(void) { return s_lut; }

bool ColorGradeLut_IsNeutral(void) { return s_isNeutral; }
