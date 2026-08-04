// COLOUR PROBE — does what a fragment shader writes actually arrive?
//
// WHY THIS EXISTS. trail_volume.fs's debug views write vec4(q, q, q, 1.0),
// which has R == G == B by construction. On screen they came out solid BLUE and
// per-facet MAGENTA. Grey cannot be blue, so either the shader running is not
// the one on disk, or something between the fragment and the framebuffer is
// changing the colour. Until that is settled, every reading taken from a debug
// view this session is worthless — including the ones that redirected the whole
// investigation.
//
// So: stop reasoning, measure the instrument. This draws two quads with KNOWN
// constant colours — one through raylib's default shader, one through the
// volume shader — then reads the pixels back and prints the numbers. No eyes
// involved, no interpretation.
//
// HOW TO READ THE RESULT
//   default grey OK, volume grey OK    -> the pipe is honest; the fault is in
//                                         the values the shader computes
//   default grey OK, volume grey WRONG -> the fault is inside trail_volume.fs
//                                         or its uniforms (a mis-packed
//                                         u_volDebug would do it)
//
// FIRST RUN, and the reason the shader now returns its debug colours at the top
// of main(): rows 3 and 4 came back as the BACKGROUND, not as a wrong colour.
// The fragments were being discarded before the debug branch was reached, so
// the probe was measuring a discard while claiming to measure a colour — and so
// was every debug view taken before it.
//   both WRONG                         -> a layer/post-process is transforming
//                                         the colour, and every debug view read
//                                         this session must be thrown away
//
// The probe deliberately does NOT use the effect's geometry. A quad has no
// silhouette, no deform and no texture, so nothing it shows can be blamed on
// them.

#include "sandbox/colour_probe.h"

#include "core/trails/trail_system.h"
#include "raylib.h"
#include "rlgl.h"

#include <math.h>
#include <stdlib.h>

// Two constants chosen so every failure mode is distinguishable:
//   GREY  — R == G == B, so ANY channel swap or per-channel curve shows up
//   RED   — one channel only, so a swizzle (R<->B) is unmistakable
static const Color PROBE_GREY = {128, 128, 128, 255};
static const Color PROBE_RED = {255, 0, 0, 255};

// Screen rects, in pixels from the top-left. Kept small and in a corner so the
// probe never covers what is being looked at.
#define PROBE_W 48
#define PROBE_H 48
#define PROBE_X0 16
#define PROBE_Y0 16

static bool s_armed = false;
static bool s_pending = false;

void ColourProbe_Arm(void) { s_armed = true; }

void ColourProbe_Draw2D(void)
{
    if (!s_armed) return;
    s_armed = false;
    s_pending = true;

    // 1. DEFAULT SHADER, flat colour. The control: if this one is wrong, the
    //    fault is downstream of every shader in the engine.
    DrawRectangle(PROBE_X0, PROBE_Y0, PROBE_W, PROBE_H, PROBE_GREY);
    DrawRectangle(PROBE_X0 + PROBE_W + 8, PROBE_Y0, PROBE_W, PROBE_H, PROBE_RED);

    // 2. THE VOLUME SHADER, in its constant-colour debug modes. Same geometry,
    //    same blend, same everything — only the program differs, so a
    //    difference between rows 1 and 2 localises the fault to this shader.
    Shader vol = Trail_GetVolumeShader();
    if (vol.id == 0)
    {
        TraceLog(LOG_WARNING, "[PROBE] volume shader not loaded — row 2 skipped");
        return;
    }
    int dbgLoc = GetShaderLocation(vol, "u_volDebug");
    if (dbgLoc < 0)
        TraceLog(LOG_WARNING, "[PROBE] u_volDebug has no location — row 2 will "
                              "show whatever the default branch computes");

    BeginShaderMode(vol);
    float m8 = 8.0f; // constant grey, written by the shader itself
    if (dbgLoc >= 0) SetShaderValue(vol, dbgLoc, &m8, SHADER_UNIFORM_FLOAT);
    DrawRectangle(PROBE_X0, PROBE_Y0 + PROBE_H + 8, PROBE_W, PROBE_H, WHITE);
    float m9 = 9.0f; // constant red
    if (dbgLoc >= 0) SetShaderValue(vol, dbgLoc, &m9, SHADER_UNIFORM_FLOAT);
    DrawRectangle(PROBE_X0 + PROBE_W + 8, PROBE_Y0 + PROBE_H + 8, PROBE_W, PROBE_H, WHITE);
    EndShaderMode();
}

static void ReportCell(Image img, const char *label, int cx, int cy,
                       Color want)
{
    if (cx < 0 || cy < 0 || cx >= img.width || cy >= img.height)
    {
        TraceLog(LOG_WARNING, "[PROBE] %s: sample point off screen", label);
        return;
    }
    Color got = GetImageColor(img, cx, cy);
    int dr = abs((int)got.r - (int)want.r);
    int dg = abs((int)got.g - (int)want.g);
    int db = abs((int)got.b - (int)want.b);
    const char *verdict = (dr <= 12 && dg <= 12 && db <= 12) ? "OK" : "WRONG";
    TraceLog(LOG_INFO, "[PROBE] %-22s muon (%3d,%3d,%3d)  duoc (%3d,%3d,%3d)  %s",
             label, want.r, want.g, want.b, got.r, got.g, got.b, verdict);
}

void ColourProbe_Readback(void)
{
    if (!s_pending) return;
    s_pending = false;

    Image img = LoadImageFromScreen();
    // Sample the CENTRE of each cell: an edge pixel could be antialiased
    // against the background and that would be a second thing to explain.
    int c = PROBE_W / 2;
    TraceLog(LOG_INFO, "[PROBE] ---- mot fragment mau da biet co toi framebuffer khong ----");
    ReportCell(img, "mac dinh / xam", PROBE_X0 + c, PROBE_Y0 + c, PROBE_GREY);
    ReportCell(img, "mac dinh / do", PROBE_X0 + PROBE_W + 8 + c, PROBE_Y0 + c, PROBE_RED);
    ReportCell(img, "trail_volume / xam", PROBE_X0 + c, PROBE_Y0 + PROBE_H + 8 + c,
               PROBE_GREY);
    ReportCell(img, "trail_volume / do", PROBE_X0 + PROBE_W + 8 + c,
               PROBE_Y0 + PROBE_H + 8 + c, PROBE_RED);
    TraceLog(LOG_INFO, "[PROBE] hai dong DAU sai  -> co thu gi do doi mau sau shader");
    TraceLog(LOG_INFO, "[PROBE] chi hai dong SAU sai -> loi trong trail_volume.fs / uniform cua no");
    UnloadImage(img);

    // And the picture, cropped to the probe itself, for the record.
    Image shot = LoadImageFromScreen();
    ImageCrop(&shot, (Rectangle){PROBE_X0 - 8, PROBE_Y0 - 8,
                                 PROBE_W * 2 + 24, PROBE_H * 2 + 24});
    if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
    ExportImage(shot, "autotest_output/colour_probe.png");
    UnloadImage(shot);
    TraceLog(LOG_INFO, "[PROBE] -> autotest_output/colour_probe.png");
}
