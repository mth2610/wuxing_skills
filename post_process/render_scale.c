/* ADAPTIVE RENDER SCALE — see render_scale.h for the why and the measured
 * prices. This file is only the controller: it decides a scale, reports the
 * size, and never touches a render target itself. */
#include "render_scale.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "raylib.h"

/* The ladder is coarse on purpose. Every step reallocates the scene chain and
 * the post chain (~20 MB at 720p), which is a one-frame hitch; a fine ladder
 * would trade that hitch for nothing, because the frame cost is not that
 * sensitive between adjacent steps (0.95 -> 0.90 measured ~0.4 ms). */
static const float kLadder[] = {1.00f, 0.95f, 0.90f, 0.85f, 0.80f};
#define LADDER_COUNT ((int)(sizeof(kLadder) / sizeof(kLadder[0])))

#define WINDOW_FRAMES 20      /* ~0.33 s at 60 Hz: fast enough to react to a
                                 skill going off, long enough that one hitch
                                 frame does not move the ladder */
#define UP_WINDOWS 4          /* four clean windows (~1.3 s) before stepping up */
#define MIN_SECONDS_BETWEEN_STEPS 1.0
/* 60 Hz is 16.67 ms, and the SUBMIT measurement below stops just before
 * EndDrawing/present, which costs ~1.2 ms on this path (measured: submit 15.4
 * against a 16.61 ms delivered frame). 15.0 therefore lands at ~16.2 ms
 * delivered — under the refresh with the slack a missed vsync needs. */
#define BUDGET_DEFAULT_MS 15.0f
#define FLOOR_DEFAULT 0.80f
#define BUDGET_SLACK 0.80f          /* step up only below 80 % of budget */

static int s_level = 0;
static int s_floorLevel = LADDER_COUNT - 1;
static bool s_adaptive = true;
static float s_budgetMs = BUDGET_DEFAULT_MS;
static int s_displayW = 0, s_displayH = 0;
static int s_outW = 0, s_outH = 0;
static int s_pendingW = 0, s_pendingH = 0;
static bool s_resizePending = false;

static double s_accumMs = 0.0;
static double s_accumIntervalMs = 0.0;
static int s_windowFrames = 0;
static int s_cleanWindows = 0;
static int s_badWindows = 0;
static double s_lastStepTime = -100.0;
static bool s_loggedOnce = false;

static int ClampToDisplay(float scale, int displayW, int displayH, int *outW, int *outH)
{
  if (displayW < 16) displayW = 16;
  if (displayH < 16) displayH = 16;
  int w = (int)lroundf((float)displayW * scale);
  int h = (int)lroundf((float)displayH * scale);
  /* Never let rounding produce a size that upscales unevenly in only one axis:
   * both come from the same scale, so clamp each against the display. */
  if (w > displayW) w = displayW;
  if (h > displayH) h = displayH;
  if (w < 16) w = 16;
  if (h < 16) h = 16;
  *outW = w;
  *outH = h;
  return w * h;
}

static void Publish(int displayW, int displayH)
{
  int w = 0, h = 0;
  ClampToDisplay(kLadder[s_level], displayW, displayH, &w, &h);
  if (w != s_outW || h != s_outH)
  {
    s_pendingW = w;
    s_pendingH = h;
    s_resizePending = true;
    s_outW = w;
    s_outH = h;
  }
}

void RenderScale_Init(int displayWidth, int displayHeight)
{
  s_displayW = displayWidth;
  s_displayH = displayHeight;

  /* Fixed-scale override for A/B work (WUXING_RENDER_SCALE=0.9) and a floor
   * for the adaptive ladder (WUXING_RENDER_SCALE_MIN=0.85). Read once: these
   * decide allocations, and a value that changes under a running frame is a
   * trap, not a feature. */
  const char *fixed = getenv("WUXING_RENDER_SCALE");
  const char *floorEnv = getenv("WUXING_RENDER_SCALE_MIN");
  const char *budgetEnv = getenv("WUXING_RENDER_SCALE_BUDGET");

  float floorScale = FLOOR_DEFAULT;
  if (floorEnv != NULL)
  {
    float v = (float)atof(floorEnv);
    if (v >= 0.5f && v <= 1.0f)
      floorScale = v;
  }
  s_floorLevel = LADDER_COUNT - 1;
  for (int i = 0; i < LADDER_COUNT; i++)
  {
    if (kLadder[i] + 1e-4f >= floorScale)
      s_floorLevel = i;   /* last rung still at or above the requested floor */
  }

  if (budgetEnv != NULL)
  {
    float v = (float)atof(budgetEnv);
    if (v > 4.0f && v < 100.0f)
      s_budgetMs = v;
  }

  s_level = 0;
  s_adaptive = true;
  if (fixed != NULL)
  {
    float v = (float)atof(fixed);
    if (v >= 0.5f && v <= 1.0f)
    {
      /* Snap the fixed request onto the ladder so the resize path stays the
       * same one the adaptive controller uses. */
      int best = 0;
      for (int i = 0; i < LADDER_COUNT; i++)
        if (fabsf(kLadder[i] - v) < fabsf(kLadder[best] - v))
          best = i;
      s_level = best;
      s_adaptive = false;
    }
  }

  s_outW = 0;
  s_outH = 0;
  Publish(s_displayW, s_displayH);

  TraceLog(LOG_INFO, "RENDER_SCALE: %s, scale %.2f, budget %.1f ms, floor %.2f, %dx%d",
           s_adaptive ? "adaptive" : "fixed", kLadder[s_level], s_budgetMs,
           kLadder[s_floorLevel], s_outW, s_outH);
}

void RenderScale_Frame(float submitMs, float intervalMs, int displayW, int displayH)
{
  if (displayW > 0 && displayH > 0 &&
      (displayW != s_displayW || displayH != s_displayH))
  {
    /* Window/display size changed: the same scale means a different pixel
     * count, so republish before anything else looks at the size. */
    s_displayW = displayW;
    s_displayH = displayH;
    Publish(s_displayW, s_displayH);
  }

  if (!s_adaptive)
  {
    s_accumMs = 0.0;
    s_accumIntervalMs = 0.0;
    s_windowFrames = 0;
    return;
  }

  s_accumMs += (double)submitMs;
  s_accumIntervalMs += (double)intervalMs;
  if (++s_windowFrames < WINDOW_FRAMES)
    return;

  const float avgMs = (float)(s_accumMs / (double)s_windowFrames);
  const float avgInterval = (float)(s_accumIntervalMs / (double)s_windowFrames);
  s_accumMs = 0.0;
  s_accumIntervalMs = 0.0;
  s_windowFrames = 0;

  const double now = GetTime();
  const float refreshMs = (avgInterval > 2.0f) ? avgInterval : 16.667f;

  /* "Over" can come from either signal: the submit time is the real cost, and
   * an interval at twice the refresh proves the frame missed vsync even if the
   * submit measurement was optimistic (deep driver pipelining). */
  const bool over = (avgMs > s_budgetMs) || (avgInterval > refreshMs * 1.40f);
  const bool clearlyUnder = (avgMs < s_budgetMs * BUDGET_SLACK) &&
                            (avgInterval <= refreshMs * 1.05f);

  if (over)
  {
    s_badWindows++;
    s_cleanWindows = 0;
  }
  else if (clearlyUnder)
  {
    s_cleanWindows++;
    s_badWindows = 0;
  }
  else
  {
    /* Between the two: hold. This band is what stops the ladder oscillating
     * on a scene whose cost sits right at the budget. */
    s_cleanWindows = 0;
    s_badWindows = 0;
  }

  if (over && s_level < s_floorLevel && (now - s_lastStepTime) >= MIN_SECONDS_BETWEEN_STEPS)
  {
    const int prev = s_level;
    s_level = (s_level + 1 <= s_floorLevel) ? s_level + 1 : s_floorLevel;
    s_lastStepTime = now;
    s_cleanWindows = 0;
    Publish(s_displayW, s_displayH);
    TraceLog(LOG_INFO, "RENDER_SCALE: %.2f -> %.2f (submit %.1f ms, interval %.1f ms)",
             kLadder[prev], kLadder[s_level], avgMs, avgInterval);
    return;
  }

  /* Stepping UP is the dangerous direction, so it needs a run of clean windows
   * and a wider margin than a single window provides. */
  if (s_level > 0 && s_cleanWindows >= UP_WINDOWS &&
      (now - s_lastStepTime) >= MIN_SECONDS_BETWEEN_STEPS)
  {
    const int prev = s_level;
    s_level--;
    s_lastStepTime = now;
    s_cleanWindows = 0;
    Publish(s_displayW, s_displayH);
    TraceLog(LOG_INFO, "RENDER_SCALE: %.2f -> %.2f (clean: submit %.1f ms, interval %.1f ms)",
             kLadder[prev], kLadder[s_level], avgMs, avgInterval);
    return;
  }

  if (!s_loggedOnce)
  {
    s_loggedOnce = true;
    TraceLog(LOG_INFO, "RENDER_SCALE: scale %.2f holding (submit %.1f ms, interval %.1f ms)",
             kLadder[s_level], avgMs, avgInterval);
  }
}

bool RenderScale_TakeResize(int *width, int *height)
{
  if (!s_resizePending)
    return false;
  s_resizePending = false;
  if (width != NULL) *width = s_pendingW;
  if (height != NULL) *height = s_pendingH;
  return true;
}

int RenderScale_GetWidth(void) { return s_outW; }
int RenderScale_GetHeight(void) { return s_outH; }
float RenderScale_GetScale(void) { return kLadder[s_level]; }
bool RenderScale_IsAdaptive(void) { return s_adaptive; }

bool RenderScale_AdaptiveForced(void)
{
  /* Cached: this is read once per frame in the capture paths, and getenv() is a
     linear scan of the environment block. */
  static int forced = -1;
  if (forced < 0)
    forced = (getenv("WUXING_RENDER_SCALE_ADAPTIVE") != NULL) ? 1 : 0;
  return forced == 1;
}
