/* ADAPTIVE RENDER SCALE — see render_scale.h for the why and the measured
 * prices. This file is only the controller: it decides a scale, reports the
 * size, and never touches a render target itself. */
#include "render_scale.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "raylib.h"

/* The ladder is coarse on purpose. Every rung change reallocates the scene chain
 * and the post chain (~35 MB at 720p) and rebuilds six framebuffers — a hitch of
 * tens of milliseconds and a window for the compositor to present a blank frame.
 * So the rung is treated as a per-map constant, not a per-second variable.
 *
 * v1 walked the ladder continuously and was wrong twice over in play: it
 * oscillated 60 -> 30 fps (a 1.5 ms step is enough to move the frame across the
 * 16.67 ms refresh boundary, and the controller then reacted to the boundary
 * flip it had just caused) and flashed the screen white on every step.
 *
 * v2 measured the frame for the first second of a map and then froze. That is
 * also wrong, and the capture harness showed why: the first second of a map is
 * not the steady state (assets and effects ramp in), so the estimate said
 * "11.6 ms at 1.0" where the steady state was 16.8 ms, and the controller
 * immediately had to walk back down — the same hitch it was built to avoid.
 *
 * v3 (this file): start at 0.85, ignore the first ~2 s, and then only move for a
 * SUSTAINED reason — one up-step after ~5 s of clearly-cheap frames, and a
 * down-step only after ~2 s of genuine overload. A failed up-step is remembered
 * and never retried on that map. Between those, the rung is a constant. */
static const float kLadder[] = {1.00f, 0.95f, 0.90f, 0.85f, 0.80f, 0.75f};
#define LADDER_COUNT ((int)(sizeof(kLadder) / sizeof(kLadder[0])))

#define START_RUNG 3                /* 0.85 — measured sweet spot on this GPU, and
                                       the rung most likely to be kept, so the
                                       common case never resizes at all */
#define WINDOW_FRAMES 20            /* ~0.33 s at 60 Hz */
#define SETTLE_WINDOWS 6            /* ~2 s: map load and effect ramp-in are not
                                       representative of the steady state */
#define UP_WINDOWS 15               /* ~5 s of sustained headroom before stepping up */
#define UP_MS 10.5f                 /* "clearly cheap": 24 % below the target */
#define EMERGENCY_MS 16.0f          /* submit this high cannot make the refresh */
#define EMERGENCY_WINDOWS 6         /* ... sustained ~2 s, not one hitch frame */
#define EMERGENCY_LIMIT 4           /* per map — enough to walk to the floor;
                                       a step still needs 2 s of real overload */
#define UP_LIMIT 1                  /* per map */
#define FLOOR_DEFAULT 0.80f

static int s_level = START_RUNG;
static int s_floorLevel = LADDER_COUNT - 1;
static bool s_adaptive = true;
static int s_displayW = 0, s_displayH = 0;
static int s_outW = 0, s_outH = 0;
static int s_pendingW = 0, s_pendingH = 0;
static bool s_resizePending = false;

static double s_accumMs = 0.0;
static int s_windowFrames = 0;
static int s_settleWindows = SETTLE_WINDOWS;
static int s_underWindows = 0;
static int s_overWindows = 0;
static int s_upSteps = 0;
static int s_emergencySteps = 0;
static bool s_noMoreUp = false;
static double s_lastUpTime = -1000.0;

static int ClampToDisplay(float scale, int displayW, int displayH, int *outW, int *outH)
{
  if (displayW < 16) displayW = 16;
  if (displayH < 16) displayH = 16;
  int w = (int)lroundf((float)displayW * scale);
  int h = (int)lroundf((float)displayH * scale);
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

static void ResetCounters(void)
{
  s_accumMs = 0.0;
  s_windowFrames = 0;
  s_settleWindows = SETTLE_WINDOWS;
  s_underWindows = 0;
  s_overWindows = 0;
  s_upSteps = 0;
  s_emergencySteps = 0;
  s_noMoreUp = false;
  s_lastUpTime = -1000.0;
}

void RenderScale_Init(int displayWidth, int displayHeight)
{
  s_displayW = displayWidth;
  s_displayH = displayHeight;

  /* Fixed-scale override for A/B work (WUXING_RENDER_SCALE=0.9), a start rung
   * (WUXING_RENDER_SCALE_START=1.0) and a floor for the ladder
   * (WUXING_RENDER_SCALE_MIN=0.85). Read once: these decide allocations, and a
   * value that changes under a running frame is a trap, not a feature. */
  const char *fixed = getenv("WUXING_RENDER_SCALE");
  const char *startEnv = getenv("WUXING_RENDER_SCALE_START");
  const char *floorEnv = getenv("WUXING_RENDER_SCALE_MIN");

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

  s_level = START_RUNG;
  s_adaptive = true;
  if (startEnv != NULL)
  {
    float v = (float)atof(startEnv);
    if (v >= 0.5f && v <= 1.0f)
    {
      int best = 0;
      for (int i = 0; i < LADDER_COUNT; i++)
        if (fabsf(kLadder[i] - v) < fabsf(kLadder[best] - v))
          best = i;
      s_level = best;
    }
  }
  if (fixed != NULL)
  {
    float v = (float)atof(fixed);
    if (v >= 0.5f && v <= 1.0f)
    {
      int best = 0;
      for (int i = 0; i < LADDER_COUNT; i++)
        if (fabsf(kLadder[i] - v) < fabsf(kLadder[best] - v))
          best = i;
      s_level = best;
      s_adaptive = false;
    }
  }
  if (s_floorLevel < s_level)
    s_floorLevel = s_level;

  ResetCounters();
  s_outW = 0;
  s_outH = 0;
  Publish(s_displayW, s_displayH);

  TraceLog(LOG_INFO,
           "RENDER_SCALE: %s, rung %.2f (floor %.2f), %dx%d — frozen per map, "
           "steps only on sustained load",
           s_adaptive ? "adaptive" : "fixed", kLadder[s_level],
           kLadder[s_floorLevel], s_outW, s_outH);
}

/* A map switch (or entering gameplay) is the only place the rung may be
 * re-decided: the screen is already changing, so the one-time resize hitch has
 * nothing to ruin. Keeps the current rung as the starting point, so the common
 * case costs no resize at all. */
void RenderScale_NotifyMapChange(void)
{
  if (!s_adaptive)
    return;
  ResetCounters();
}

void RenderScale_Frame(float submitMs, float intervalMs, int displayW, int displayH)
{
  (void)intervalMs;   /* see the header: the delivered interval saturates under
                         SetTargetFPS(60) and cannot distinguish a 9 ms frame
                         from a 16.5 ms one. Submit time is the signal. */

  if (displayW > 0 && displayH > 0 &&
      (displayW != s_displayW || displayH != s_displayH))
  {
    s_displayW = displayW;
    s_displayH = displayH;
    Publish(s_displayW, s_displayH);
  }

  if (!s_adaptive)
    return;

  s_accumMs += (double)submitMs;
  if (++s_windowFrames < WINDOW_FRAMES)
    return;

  const float avgMs = (float)(s_accumMs / (double)s_windowFrames);
  s_accumMs = 0.0;
  s_windowFrames = 0;

  if (s_settleWindows > 0)
    s_settleWindows--;

  const bool settling = (s_settleWindows > 0);

  if (avgMs > EMERGENCY_MS)
  {
    /* A down-step is allowed even while settling: an overloaded map should be
     * fixed ~2 s after it loads, not 4 s. Only the UP path waits for the settle
     * (its whole point is that early frames are not representative). */
    s_overWindows++;
    s_underWindows = 0;
  }
  else if (!settling && avgMs < UP_MS)
  {
    s_underWindows++;
    s_overWindows = 0;
  }
  else
  {
    s_overWindows = 0;
    s_underWindows = 0;
  }

  /* DOWN on sustained overload. Never on a single window: one hitch frame is
   * not a reason to reallocate 35 MB of targets. */
  if (s_overWindows >= EMERGENCY_WINDOWS && s_emergencySteps < EMERGENCY_LIMIT &&
      s_level < s_floorLevel)
  {
    const int prev = s_level;
    s_level++;
    s_emergencySteps++;
    s_overWindows = 0;
    s_underWindows = 0;
    /* A down-step shortly after an up-step proves the up-step was wrong: never
     * try again on this map (that pair is exactly the 60 -> 30 sawtooth). */
    if (GetTime() - s_lastUpTime < 15.0)
      s_noMoreUp = true;
    Publish(s_displayW, s_displayH);
    TraceLog(LOG_INFO, "RENDER_SCALE: sustained %.1f ms -> %.2f -> %.2f "
                       "(down %d/%d)",
             avgMs, kLadder[prev], kLadder[s_level],
             s_emergencySteps, EMERGENCY_LIMIT);
    return;
  }

  /* UP only for a long, uninterrupted run of clearly-cheap frames, and only once
   * per map. 5 s is longer than any load spike or effect ramp. */
  if (s_underWindows >= UP_WINDOWS && s_level > 0 && !s_noMoreUp &&
      s_upSteps < UP_LIMIT)
  {
    const int prev = s_level;
    s_level--;
    s_upSteps++;
    s_underWindows = 0;
    s_lastUpTime = GetTime();
    Publish(s_displayW, s_displayH);
    TraceLog(LOG_INFO, "RENDER_SCALE: %.1f ms sustained under budget -> %.2f -> %.2f",
             avgMs, kLadder[prev], kLadder[s_level]);
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
