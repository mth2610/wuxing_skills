#include "post_process/color_grade_lut.h"

#include <stddef.h>
#include <stdlib.h>

static Texture2D s_lut = {0};
static bool s_initialized = false;
static bool s_isNeutral = true;

#define LUT_SIZE COLOR_GRADE_LUT_SIZE
#define LUT_WIDTH (COLOR_GRADE_LUT_SIZE * COLOR_GRADE_LUT_SIZE)
#define LUT_HEIGHT (COLOR_GRADE_LUT_SIZE)

static void ColorGradeLut_FillRamp(unsigned char ramp[LUT_SIZE])
{
    const float denom = (float)(LUT_SIZE - 1);
    for (int i = 0; i < LUT_SIZE; ++i)
    {
        ramp[i] = (unsigned char)((float)i * 255.0f / denom + 0.5f);
    }
}

static bool ColorGradeLut_CheckImageIsNeutral(Image img)
{
    if (img.data == NULL || img.width != LUT_WIDTH || img.height != LUT_HEIGHT ||
        img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        return false;
    }

    unsigned char ramp[LUT_SIZE];
    ColorGradeLut_FillRamp(ramp);
    const Color *pixels = (const Color *)img.data;

    for (int g = 0; g < LUT_SIZE; ++g)
    {
        const unsigned char cg = ramp[g];
        const Color *row = pixels + g * LUT_WIDTH;
        for (int slice = 0; slice < LUT_SIZE; ++slice)
        {
            const unsigned char cb = ramp[slice];
            const Color *tile = row + slice * LUT_SIZE;
            for (int r = 0; r < LUT_SIZE; ++r)
            {
                const Color px = tile[r];
                if (px.r != ramp[r] || px.g != cg || px.b != cb)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

// The identity strip. Mirrored exactly by core/tests/color_grade_lut_test.c —
// if this addressing changes, that test's round-trip fails, which is the point.
static Image ColorGradeLut_BuildNeutralImage(void)
{
    Color *pixels = (Color *)RL_MALLOC((size_t)LUT_WIDTH * (size_t)LUT_HEIGHT * sizeof(Color));
    if (pixels == NULL)
    {
        return (Image){0};
    }

    unsigned char ramp[LUT_SIZE];
    ColorGradeLut_FillRamp(ramp);

    for (int g = 0; g < LUT_SIZE; ++g)
    {
        const unsigned char cg = ramp[g];
        Color *row = pixels + g * LUT_WIDTH;
        for (int slice = 0; slice < LUT_SIZE; ++slice)
        {
            const unsigned char cb = ramp[slice];
            Color *tile = row + slice * LUT_SIZE;
            for (int r = 0; r < LUT_SIZE; ++r)
            {
                tile[r] = (Color){ramp[r], cg, cb, 255};
            }
        }
    }

    Image img = {
        .data = pixels,
        .width = LUT_WIDTH,
        .height = LUT_HEIGHT,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    return img;
}

static bool ColorGradeLut_Adopt(Image img, bool neutral)
{
    if (img.data == NULL)
    {
        return false;
    }
    Texture2D uploaded = LoadTextureFromImage(img);
    if (uploaded.id == 0)
    {
        TraceLog(LOG_WARNING, "LUT: upload failed, keeping previous grade");
        return false;
    }
    // BILINEAR is safe with this addressing: red maps to texel CENTRES inside a
    // tile (0.5 .. size-0.5), so filtering never reaches across a tile edge into
    // the neighbouring blue slice.
    SetTextureFilter(uploaded, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(uploaded, TEXTURE_WRAP_CLAMP);
    if (s_lut.id != 0)
        UnloadTexture(s_lut);
    s_lut = uploaded;
    s_isNeutral = neutral;
    return true;
}

static bool ColorGradeLut_LoadInternal(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (!FileExists(path))
    {
        TraceLog(LOG_WARNING, "LUT: '%s' not found, keeping current grade", path);
        return false;
    }
    Image img = LoadImage(path);
    if (img.data == NULL)
    {
        TraceLog(LOG_WARNING, "LUT: '%s' failed to decode, keeping current grade", path);
        return false;
    }
    if (img.width != LUT_WIDTH || img.height != LUT_HEIGHT)
    {
        TraceLog(LOG_WARNING,
                 "LUT: '%s' is %dx%d, expected %dx%d (%d^3 strip) — keeping current grade",
                 path, img.width, img.height, LUT_WIDTH, LUT_HEIGHT, LUT_SIZE);
        UnloadImage(img);
        return false;
    }
    if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }
    const bool isNeutral = ColorGradeLut_CheckImageIsNeutral(img);
    const bool ok = ColorGradeLut_Adopt(img, isNeutral);
    UnloadImage(img);
    if (ok)
    {
        TraceLog(LOG_INFO, "LUT: loaded '%s'%s", path, isNeutral ? " (neutral identity)" : "");
    }
    return ok;
}

void ColorGradeLut_Init(void)
{
    if (s_initialized && s_lut.id != 0)
        return;

    // Ưu tiên nạp file LUT mặc định trước (nếu tồn tại) để tránh cấp phát rồi hủy ngay
    // texture neutral trung gian trên Vulkan trong cùng một frame khởi tạo.
    if (FileExists(COLOR_GRADE_LUT_DEFAULT_PATH) &&
        ColorGradeLut_LoadInternal(COLOR_GRADE_LUT_DEFAULT_PATH))
    {
        s_initialized = true;
        return;
    }

    Image img = ColorGradeLut_BuildNeutralImage();
    if (ColorGradeLut_Adopt(img, true))
    {
        s_initialized = true;
    }
    UnloadImage(img);

    TraceLog(LOG_INFO, "LUT: neutral %dx%d strip (%d^3) — drop a graded strip at "
                       "%s to grade the frame",
             LUT_WIDTH, LUT_HEIGHT, LUT_SIZE, COLOR_GRADE_LUT_DEFAULT_PATH);
}

void ColorGradeLut_Unload(void)
{
    if (s_lut.id != 0)
        UnloadTexture(s_lut);
    s_lut = (Texture2D){0};
    s_initialized = false;
    s_isNeutral = true;
}

bool ColorGradeLut_Load(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return false;
    if (ColorGradeLut_LoadInternal(path))
    {
        s_initialized = true;
        return true;
    }
    if (!s_initialized || s_lut.id == 0)
    {
        ColorGradeLut_Init();
    }
    return false;
}

void ColorGradeLut_UseNeutral(void)
{
    if (s_isNeutral && s_lut.id != 0)
        return;
    Image img = ColorGradeLut_BuildNeutralImage();
    if (ColorGradeLut_Adopt(img, true))
    {
        s_initialized = true;
    }
    UnloadImage(img);
}

Texture2D ColorGradeLut_Texture(void) { return s_lut; }

bool ColorGradeLut_IsNeutral(void) { return s_isNeutral; }