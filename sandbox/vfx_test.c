#include "vfx_test.h"
#include "core/camera_fx.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/screen_distort.h"
#include "core/trail_system.h"
#include "core/vfx_light.h"
#include "raymath.h"
#include <stddef.h> // Đảm bảo định nghĩa từ khóa NULL chuẩn xác

static int g_activeCountCache = 0;

void VFXTest_UpdateAndHandleInput(Vector3 playerPos, Texture2D testAtlasTex,
                                  Texture2D globalParticleTex) {
  if (IsKeyPressed(KEY_T)) {
    CameraFX_Shake(0.5f);
    ScreenDistort_Add(playerPos, 120.0f, 0.8f, 1.2f, 250.0f);

    VFXLight_Spawn(playerPos, (Color){255, 180, 50, 255}, 150.0f, 9999.0f);
    DecalSystem_Add(playerPos, (float)GetRandomValue(0, 360), 40.0f,
                globalParticleTex, 3.0f, ORANGE);

    static ColorGradient g;
    static bool gradientInit = false;
    if (!gradientInit) {
      ColorGradient_AddStop(&g, 0.0f, RED);
      ColorGradient_AddStop(&g, 0.25f, ORANGE);
      ColorGradient_AddStop(&g, 0.5f, YELLOW);
      ColorGradient_AddStop(&g, 0.75f, GREEN);
      ColorGradient_AddStop(&g, 1.0f, BLUE);
      gradientInit = true;
    }

    static SpriteAnim anim;
    static bool animInit = false;
    if (!animInit) {
      SpriteAnim_Init(&anim, 2, 2, 4, 8.0f, ANIM_LOOP);
      animInit = true;
    }

    static ParticleConfig deathChildConfig;
    deathChildConfig.velocity = (Vector3){0.0f, 0.0f, 0.0f};
    deathChildConfig.colorStart = BLUE;
    deathChildConfig.colorEnd = BLACK;
    deathChildConfig.radius = 25.0f;
    deathChildConfig.lifetime = 1.5f;
    deathChildConfig.gradient = &g;
    deathChildConfig.forceField = NULL;
    deathChildConfig.spriteAnim = NULL;
    deathChildConfig.onLiveEmit = NULL;
    deathChildConfig.onDeathEmit = NULL;

    static ParticleConfig liveChildConfig;
    liveChildConfig.velocity = (Vector3){0.0f, 10.0f, 0.0f};
    liveChildConfig.colorStart = ORANGE;
    liveChildConfig.colorEnd = RED;
    liveChildConfig.radius = 30.0f;
    liveChildConfig.lifetime = 0.8f;
    liveChildConfig.gradient = &g;
    liveChildConfig.forceField = NULL;
    liveChildConfig.spriteAnim = NULL;
    liveChildConfig.onLiveEmit = NULL;
    liveChildConfig.onDeathEmit = NULL;

    ParticleConfig motherConfig = {0};
    motherConfig.position =
        Vector3Add(playerPos, (Vector3){-60.0f, 15.0f, 0.0f});
    motherConfig.velocity = (Vector3){120.0f, 0.0f, 0.0f};
    motherConfig.radius = 40.0f;
    motherConfig.lifetime = 1.5f;
    motherConfig.colorStart = WHITE;
    motherConfig.colorEnd = YELLOW;
    motherConfig.gradient = &g;
    motherConfig.onLiveEmit = &liveChildConfig;
    motherConfig.onLiveEmitRate = 35.0f;
    motherConfig.onDeathEmit = &deathChildConfig;
    motherConfig.onDeathEmitCount = 12;

    SpawnParticle(motherConfig);

    TrailConfig tConfig = {0};
    tConfig.type = TRAIL_TYPE_PROJECTILE;
    tConfig.pos = Vector3Add(playerPos, (Vector3){75.0f, 30.0f, 0.0f});
    tConfig.vel = (Vector3){220.0f, 0.0f, 0.0f};
    tConfig.len = 50.0f;
    tConfig.thick = 6.0f;
    tConfig.trailLength = 80.0f;
    tConfig.life = 4.0f;
    tConfig.gradient = &g;
    tConfig.spriteAnim = &anim;
    tConfig.tex = testAtlasTex;
    SpawnTrailEntity(tConfig);
  }
}

void VFXTest_DrawDebugLights3D(void) {
  VFXLightData activeLights[MAX_VFX_LIGHTS];
  VFXLight_GetActive(activeLights, &g_activeCountCache, MAX_VFX_LIGHTS);
  for (int i = 0; i < g_activeCountCache; i++) {
    DrawSphereWires(activeLights[i].position, activeLights[i].radius, 8, 8,
                    ColorAlpha(activeLights[i].color, 0.2f));
    DrawSphere(activeLights[i].position, 5.0f, activeLights[i].color);
  }
}

void VFXTest_DrawHUD(void) {
  VFXLightData activeLights[MAX_VFX_LIGHTS];
  int activeCount = 0;
  VFXLight_GetActive(activeLights, &activeCount, MAX_VFX_LIGHTS);
  DrawText(TextFormat("Active VFX Lights: %d / 8", activeCount), 10, 610,
           20, ORANGE);
}