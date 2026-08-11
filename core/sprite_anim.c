#include "core/sprite_anim.h"
#include <math.h>
#include <stddef.h>

void SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode) {
  anim->rows = rows;
  anim->cols = cols;
  anim->frameCount = (frameCount <= rows * cols) ? frameCount : (rows * cols);
  anim->fps = fps;
  anim->playMode = mode;
  anim->frameMeta = NULL;
  anim->frameMetaCount = 0;
  SpriteAnim_Reset(anim);
}

void SpriteAnim_Update(SpriteAnim *anim, float dt) {
  if (anim->_finished && anim->playMode == ANIM_ONCE) return;

  anim->_timer += dt;
  float frameDuration = 1.0f / anim->fps;
  
  while (anim->_timer >= frameDuration) {
    anim->_timer -= frameDuration;
    
    if (anim->playMode == ANIM_ONCE) {
      if (anim->_currentFrame < anim->frameCount - 1) {
        anim->_currentFrame++;
      } else {
        anim->_finished = true;
      }
    } else if (anim->playMode == ANIM_LOOP || anim->playMode == ANIM_RANDOM_START) {
      anim->_currentFrame = (anim->_currentFrame + 1) % anim->frameCount;
    } else if (anim->playMode == ANIM_PING_PONG) {
      // Thực thi ping-pong cơ bản qua toán học ảo hóa chu kỳ
      static int dir = 1;
      anim->_currentFrame += dir;
      if (anim->_currentFrame >= anim->frameCount) {
        anim->_currentFrame = anim->frameCount - 2;
        dir = -1;
      }
      if (anim->_currentFrame < 0) {
        anim->_currentFrame = (anim->frameCount > 1) ? 1 : 0;
        dir = 1;
      }
    }
  }
}

Rectangle SpriteAnim_GetUVRect(const SpriteAnim *anim) {
  float cellWidth = 1.0f / anim->cols;
  float cellHeight = 1.0f / anim->rows;
  
  int r = anim->_currentFrame / anim->cols;
  int c = anim->_currentFrame % anim->cols;
  
  return (Rectangle){
    (float)c * cellWidth,
    (float)r * cellHeight,
    cellWidth,
    cellHeight
  };
}

bool SpriteAnim_IsFinished(const SpriteAnim *anim) {
  return anim->_finished;
}

void SpriteAnim_Reset(SpriteAnim *anim) {
  anim->_timer = 0.0f;
  anim->_finished = false;
  if (anim->playMode == ANIM_RANDOM_START) {
    anim->_currentFrame = GetRandomValue(0, anim->frameCount - 1);
  } else {
    anim->_currentFrame = 0;
  }
}

void SpriteAnim_SetFrameMetadata(SpriteAnim *anim,
                                 const SpriteAnimFrameMeta *meta,
                                 int metaCount)
{
  anim->frameMeta = meta;
  anim->frameMetaCount = (meta != NULL && metaCount > 0) ? metaCount : 0;
}

static SpriteAnimFrameSample SpriteAnim_FrameSample(const SpriteAnim *anim,
                                                    int frame)
{
  const float cellWidth = 1.0f / anim->cols;
  const float cellHeight = 1.0f / anim->rows;
  const int r = frame / anim->cols;
  const int c = frame % anim->cols;
  SpriteAnimFrameSample sample = {
      .uv = {(float)c * cellWidth, (float)r * cellHeight, cellWidth, cellHeight},
      .offset = {0.0f, 0.0f},
      .scale = {1.0f, 1.0f},
  };

  if (anim->frameMeta != NULL && frame < anim->frameMetaCount)
  {
    Rectangle crop = anim->frameMeta[frame].crop;
    // Malformed external metadata must never invert UVs or make a sprite
    // disappear. Fall back to the legacy full cell for that frame instead.
    if (crop.width > 0.0f && crop.height > 0.0f && crop.x >= 0.0f &&
        crop.y >= 0.0f && crop.x + crop.width <= 1.0f &&
        crop.y + crop.height <= 1.0f)
    {
      sample.uv.x += crop.x * cellWidth;
      sample.uv.y += crop.y * cellHeight;
      sample.uv.width = crop.width * cellWidth;
      sample.uv.height = crop.height * cellHeight;
      sample.offset = (Vector2){crop.x + crop.width * 0.5f - 0.5f,
                                crop.y + crop.height * 0.5f - 0.5f};
      sample.scale = (Vector2){crop.width, crop.height};
    }
  }
  return sample;
}

Rectangle SpriteAnim_CalculateUV(const SpriteAnim *template, float age, int *outFrame) {
  float cellWidth = 1.0f / template->cols;
  float cellHeight = 1.0f / template->rows;
  
  int totalFramesNeeded = (int)(age * template->fps);
  int frame = 0;
  
  if (template->playMode == ANIM_ONCE) {
    frame = (totalFramesNeeded >= template->frameCount) ? (template->frameCount - 1) : totalFramesNeeded;
  } else if (template->playMode == ANIM_LOOP || template->playMode == ANIM_RANDOM_START) {
    frame = totalFramesNeeded % template->frameCount;
  } else if (template->playMode == ANIM_PING_PONG) {
    int period = (template->frameCount - 1) * 2;
    if (period <= 0) period = 1;
    int cycle = totalFramesNeeded % period;
    if (cycle < template->frameCount) {
      frame = cycle;
    } else {
      frame = period - cycle;
    }
  }
  
  if (outFrame) *outFrame = frame;
  
  int r = frame / template->cols;
  int c = frame % template->cols;
  
  return (Rectangle){ (float)c * cellWidth, (float)r * cellHeight, cellWidth, cellHeight };
}

Rectangle SpriteAnim_CalculateUVBlend(const SpriteAnim *template, float age,
                                      Rectangle *outNext, float *outBlend)
{
  float cellWidth = 1.0f / template->cols;
  float cellHeight = 1.0f / template->rows;

  // Fractional frame position — the whole point of this variant. The integer
  // part picks the frame, the fraction is how far into the NEXT one we are.
  float exact = age * template->fps;
  if (exact < 0.0f) exact = 0.0f;
  int frame = (int)exact;
  float blend = exact - (float)frame;

  int next = frame + 1;

  if (template->playMode == ANIM_ONCE) {
    if (frame >= template->frameCount - 1) {
      // Clamped at the end: hold the last frame and stop blending, or the sheet
      // would cross-fade into frame 0 and the puff would appear to restart.
      frame = template->frameCount - 1;
      next = frame;
      blend = 0.0f;
    }
  } else {
    frame = frame % template->frameCount;
    next  = (frame + 1) % template->frameCount;
  }
  if (next >= template->frameCount) next = template->frameCount - 1;

  int r  = frame / template->cols, c  = frame % template->cols;
  int rn = next  / template->cols, cn = next  % template->cols;

  if (outNext)
    *outNext = (Rectangle){ (float)cn * cellWidth, (float)rn * cellHeight,
                            cellWidth, cellHeight };
  if (outBlend) *outBlend = blend;

  return (Rectangle){ (float)c * cellWidth, (float)r * cellHeight,
                      cellWidth, cellHeight };
}

SpriteAnimFrameSample SpriteAnim_CalculateFrameSampleBlend(
    const SpriteAnim *template, float age, SpriteAnimFrameSample *outNext,
    float *outBlend)
{
  // Keep the established UV state machine as the single source of truth for
  // once/loop semantics. Reconstruct frame indices from its returned cells;
  // they are exact grid multiples generated by this module.
  Rectangle nextUV;
  float blend;
  Rectangle uv = SpriteAnim_CalculateUVBlend(template, age, &nextUV, &blend);
  const int frame = (int)(uv.y * template->rows + 0.5f) * template->cols +
                    (int)(uv.x * template->cols + 0.5f);
  const int next = (int)(nextUV.y * template->rows + 0.5f) * template->cols +
                   (int)(nextUV.x * template->cols + 0.5f);
  if (outNext) *outNext = SpriteAnim_FrameSample(template, next);
  if (outBlend) *outBlend = blend;
  return SpriteAnim_FrameSample(template, frame);
}
