# Fix main.c to generate a proper trail texture
sed -i '' '/TrailSystem_SetGlobalTexture(globalParticleTex);/c\
  Image trailImg = {\
      .data = malloc(64 * sizeof(Color)),\
      .width = 64,\
      .height = 1,\
      .mipmaps = 1,\
      .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8\
  };\
  Color *trailPixels = (Color *)trailImg.data;\
  for(int i=0; i<64; i++) {\
      float u = i / 63.0f;\
      float dist = fabsf(u - 0.5f) * 2.0f;\
      float alpha = 1.0f - dist;\
      trailPixels[i] = (Color){255, 255, 255, (unsigned char)(255 * alpha)};\
  }\
  Texture2D globalTrailTex = LoadTextureFromImage(trailImg);\
  UnloadImage(trailImg);\
  TrailSystem_SetGlobalTexture(globalTrailTex);\
' /Users/mth2610/Desktop/c_games/wuxing_skills/main.c

# Fix visual_composer.c to increase particle rate
sed -i '' 's/.onLiveEmitRate = 40.0f/.onLiveEmitRate = 120.0f/g' /Users/mth2610/Desktop/c_games/wuxing_skills/core/composition/visual_composer.c
