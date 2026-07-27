#include "core/sprite_anim.h"
#include <math.h>

void SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode) {
  anim->rows = rows;
  anim->cols = cols;
  anim->frameCount = (frameCount <= rows * cols) ? frameCount : (rows * cols);
  anim->fps = fps;
  anim->playMode = mode;
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
