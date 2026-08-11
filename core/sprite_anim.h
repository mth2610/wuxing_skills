#ifndef SPRITE_ANIM_H
#define SPRITE_ANIM_H

#include "raylib.h"
#include <stdbool.h>

typedef enum {
  ANIM_ONCE = 0,
  ANIM_LOOP,
  ANIM_RANDOM_START,
  ANIM_PING_PONG,
} AnimPlayMode;

// Optional per-frame crop emitted by the flipbook bake. `crop` is expressed in
// the frame's own 0..1 space, not in atlas space. Keeping the source frame's
// centre fixed while sampling only its occupied rectangle removes transparent
// fill-rate without making a drifting simulation visibly jump between cells.
// A NULL metadata pointer preserves the full-cell behaviour used by all legacy
// sheets.
typedef struct {
  Rectangle crop;
} SpriteAnimFrameMeta;

// UV plus the geometry correction that corresponds to a cropped frame. The
// renderer scales its billboard by `scale` and moves its centre by `offset` in
// source-frame units, so crop metadata is a render optimisation, not a change
// to a puff's world-space size or pivot.
typedef struct {
  Rectangle uv;
  Vector2 offset;
  Vector2 scale;
} SpriteAnimFrameSample;

typedef struct {
  int rows, cols;         // Layout cấu trúc lưới của Atlas
  int frameCount;         // Tổng số frame hợp lệ (<= rows * cols)
  float fps;
  AnimPlayMode playMode;
  
  // Trạng thái nội bộ (dùng cho cấu trúc mẫu template)
  float _timer;
  int   _currentFrame;
  bool  _finished;

  // Optional, immutable bake metadata. The caller owns this static array.
  const SpriteAnimFrameMeta *frameMeta;
  int frameMetaCount;
} SpriteAnim;

void      SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode);
void      SpriteAnim_Update(SpriteAnim *anim, float dt);
Rectangle SpriteAnim_GetUVRect(const SpriteAnim *anim);
bool      SpriteAnim_IsFinished(const SpriteAnim *anim);
void      SpriteAnim_Reset(SpriteAnim *anim);
void      SpriteAnim_SetFrameMetadata(SpriteAnim *anim,
                                      const SpriteAnimFrameMeta *meta,
                                      int metaCount);

// Hàm tính toán nhanh UV cho Particle sử dụng biến nội bộ nhẹ (Tránh phình to Particle struct)
Rectangle SpriteAnim_CalculateUV(const SpriteAnim *template, float age, int *outFrame);

// Đợt E / E4 — the SAME frame plus the one after it, and how far between them.
//
// WHY: SpriteAnim_CalculateUV snaps to whole frames. A 64-frame sheet played over
// a 2 s life runs at 32 fps against a 60 fps render, so every atlas frame is held
// ~2 render frames and then JUMPS to a different simulation state. On a soft
// radial blob that is invisible; on an authored, turbulent sheet it reads as the
// sprites flipping back and forth — reported on the F2 smoke flipbook.
//
// The caller draws the quad twice, cross-fading A->B by `outBlend` (the standard
// flipbook cross-fade; rlgl's immediate batch carries only position/texcoord/
// colour, so there is no spare attribute to hand a second UV set to a shader and
// blend it in one draw).
//
// At the last frame `outNext` == the returned rect and `outBlend` == 0, so a
// clamped ANIM_ONCE animation does not blend into a wrapped-around frame 0.
Rectangle SpriteAnim_CalculateUVBlend(const SpriteAnim *template, float age,
                                      Rectangle *outNext, float *outBlend);

// Metadata-aware counterpart used by particle renderers. Existing users that
// need only UVs can keep calling SpriteAnim_CalculateUVBlend unchanged.
SpriteAnimFrameSample SpriteAnim_CalculateFrameSampleBlend(
    const SpriteAnim *template, float age, SpriteAnimFrameSample *outNext,
    float *outBlend);

#endif // SPRITE_ANIM_H
