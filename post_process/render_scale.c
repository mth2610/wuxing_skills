/* ADAPTIVE RENDER SCALE — controller for resolution scaling. */
#include "render_scale.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "raylib.h"

static const float kLadder[] = {1.00f, 0.92f, 0.85f, 0.78f, 0.72f, 0.68f};
#define LADDER_COUNT ((int)(sizeof(kLadder) / sizeof(kLadder[0])))

#define START_RUNG 0        /* 1.00 — Native resolution by default; quality tiers are explicitly chosen via GfxQuality */
#define WINDOW_FRAMES 20    /* ~0.33 s at 60 Hz */
#define SETTLE_WINDOWS 6    /* ~2 s ổn định đầu map */
#define UP_WINDOWS 12       /* ~4 s dư dả hiệu năng rõ rệt trước khi nâng scale */
#define UP_MS 12.2f         /* Nâng lên 0.85 khi thực sự nhẹ (< 12.2 ms) */
#define COMFORT_MS 14.2f    /* Ngưỡng an toàn để xóa bộ đếm quá tải */
#define EMERGENCY_MS 16.0f  /* Vượt ngưỡng này sẽ rớt khỏi mốc 60 Hz */
#define EMERGENCY_WINDOWS 4 /* ~1.3 s quá tải liên tục là hạ ngay để cứu FPS */
#define EMERGENCY_LIMIT 5   /* Đủ để đi hết thang xuống mức sàn */
#define UP_LIMIT 1          /* Tối đa 1 lần nâng mỗi map */
#define FLOOR_DEFAULT 0.68f /* Mở sàn xuống 0.68 khi bật WUXING_RENDER_SCALE_ADAPTIVE=1 */

static int s_level = START_RUNG;
static int s_floorLevel = LADDER_COUNT - 1;
static bool s_adaptive = false;
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
  if (displayW < 16)
    displayW = 16;
  if (displayH < 16)
    displayH = 16;
  // Làm tròn về bội số của 4 để tương thích tốt nhất với tháp Bloom (1/4) và alignment GPU
  int w = ((int)lroundf((float)displayW * scale) + 2) & ~3;
  int h = ((int)lroundf((float)displayH * scale) + 2) & ~3;
  if (w > displayW)
    w = displayW;
  if (h > displayH)
    h = displayH;
  if (w < 16)
    w = 16;
  if (h < 16)
    h = 16;
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
      s_floorLevel = i;
  }

  s_level = START_RUNG;
  const char *adaptiveEnv = getenv("WUXING_RENDER_SCALE_ADAPTIVE");
  const char *captureEnv = getenv("WUXING_CAPTURE_FRAMES");
  if (captureEnv != NULL && adaptiveEnv == NULL && fixed == NULL && startEnv == NULL)
  {
    /* Headless captures default to native 1.00 fixed scale for reproducibility
     * unless explicitly overridden via WUXING_RENDER_SCALE* env vars. */
    s_level = 0;
    s_adaptive = false;
  }
  else if (adaptiveEnv != NULL)
  {
    s_adaptive = (strcmp(adaptiveEnv, "1") == 0 || strcmp(adaptiveEnv, "true") == 0);
  }
  else
  {
    /* Native 1.00 fixed resolution by default; users select UNLIT/LOW/MED/HIGH/ULTRA explicitly. */
    s_adaptive = false;
  }
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

void RenderScale_NotifyMapChange(void)
{
  if (!s_adaptive)
    return;
  ResetCounters();
}

void RenderScale_Frame(float submitMs, float intervalMs, int displayW, int displayH)
{
  (void)intervalMs;

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
    // Nếu frame quá nặng (> 24 ms ~ dưới 42 FPS), tăng tốc độ hạ bậc thêm 1 đơn vị
    s_overWindows += (avgMs > 24.0f) ? 2 : 1;
    s_underWindows = 0;
  }
  else if (!settling && avgMs < UP_MS)
  {
    s_underWindows++;
    s_overWindows = 0;
  }
  else
  {
    // Giảm dần bộ đếm quá tải thay vì xóa trắng về 0 ngay khi gặp 1 cửa sổ 15.x ms
    if (avgMs < COMFORT_MS)
      s_overWindows = 0;
    else if (s_overWindows > 0)
      s_overWindows--;
    s_underWindows = 0;
  }

  if (s_overWindows >= EMERGENCY_WINDOWS && s_emergencySteps < EMERGENCY_LIMIT &&
      s_level < s_floorLevel)
  {
    const int prev = s_level;
    s_level++;
    s_emergencySteps++;
    s_overWindows = 0;
    s_underWindows = 0;
    if (GetTime() - s_lastUpTime < 20.0)
      s_noMoreUp = true;
    Publish(s_displayW, s_displayH);
    TraceLog(LOG_INFO, "RENDER_SCALE: sustained %.1f ms -> %.2f -> %.2f "
                       "(down %d/%d)",
             avgMs, kLadder[prev], kLadder[s_level],
             s_emergencySteps, EMERGENCY_LIMIT);
    return;
  }

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
  if (width != NULL)
    *width = s_pendingW;
  if (height != NULL)
    *height = s_pendingH;
  return true;
}

int RenderScale_GetWidth(void) { return s_outW; }
int RenderScale_GetHeight(void) { return s_outH; }
float RenderScale_GetScale(void) { return kLadder[s_level]; }
bool RenderScale_IsAdaptive(void) { return s_adaptive; }

bool RenderScale_AdaptiveForced(void)
{
  static int forced = -1;
  if (forced < 0)
    forced = (getenv("WUXING_RENDER_SCALE_ADAPTIVE") != NULL) ? 1 : 0;
  return forced == 1;
}