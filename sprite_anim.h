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

typedef struct {
  int rows, cols;         // Layout cấu trúc lưới của Atlas
  int frameCount;         // Tổng số frame hợp lệ (<= rows * cols)
  float fps;
  AnimPlayMode playMode;
  
  // Trạng thái nội bộ (dùng cho cấu trúc mẫu template)
  float _timer;
  int   _currentFrame;
  bool  _finished;
} SpriteAnim;

void      SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode);
void      SpriteAnim_Update(SpriteAnim *anim, float dt);
Rectangle SpriteAnim_GetUVRect(const SpriteAnim *anim);
bool      SpriteAnim_IsFinished(const SpriteAnim *anim);
void      SpriteAnim_Reset(SpriteAnim *anim);

// Hàm tính toán nhanh UV cho Particle sử dụng biến nội bộ nhẹ (Tránh phình to Particle struct)
Rectangle SpriteAnim_CalculateUV(const SpriteAnim *template, float age, int *outFrame);

#endif // SPRITE_ANIM_H