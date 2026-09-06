#include "core/camera_fx.h"
#include "core/audio_system.h"
#include "core/decals/decal_system.h"
#include "core/fluid/fluid_impact.h"
#include "core/fluid/fluid_surface.h"
#include "core/gas/gas_system.h"
#include "core/metaball_fx.h"
#include "core/particles/particle_manager.h"
#include "core/post_fx.h"
#include "sandbox/sandbox_core.h"
#include "core/scene_targets.h"
#include "core/screen_distort.h"
#include "core/vfx_render.h"
#include "core/surface_material.h"
#include "core/gfx_quality.h"
#include "core/atmosphere.h"
#include "sandbox/skill_debugger.h"
#include "core/skill_manager.h"
#include "core/trails/trail_system.h"
#include "sandbox/ui_panel.h"
#include "core/vfx_light.h"
#include "sandbox/vfx_test.h" // MỚI: Chỉ giữ duy nhất file test này để điều phối
#include "core/resource_manager.h"
#include "core/skill_helper.h"
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "core/tuning.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "maps/toolkit/ground_shadow.h"
#include "core/map_manager.h"
#include "skills/taiji/core_test/core_test_skill.h"
#include "sandbox/auto_test.h"
#include "sandbox/auto_test_cases.h"
#include "sandbox/visual_verify.h"
#include "sandbox/pool_stats.h"
#include "sandbox/gradient_probe.h"
#include "core/status_vfx.h"
#include "core/afterimage.h"
#include "game/game_screen.h"
#include "entities/entities.h"
#include "combat/combat.h"
#include "control/control.h"
#include "boss/boss_system.h"
#include "skills/taiji/taiji_phong/taiji_phong_skill.h"
#include "game/game_rules.h"
#include "ai/ai.h"
#include "ui/ui.h"
#include "net/net_transport.h"
#include "formations/formation_system.h"
#include "net/net.h"
#include <stdio.h>

// Biến camera toàn cục
Camera3D camera = {0};
PlayerEntity player = {0};

// Capture coordinates are explicit world metres, independent of arena layout.
static bool ParseCaptureVector(const char *text, Vector3 *out)
{
    char trailing;
    return sscanf(text, "%f,%f,%f%c", &out->x, &out->y, &out->z, &trailing) == 3 &&
           isfinite(out->x) && isfinite(out->y) && isfinite(out->z);
}

static void MyBeginMode3D(Camera3D camera) {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION);
  rlPushMatrix();
  rlLoadIdentity();
  float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();

  if (camera.projection == CAMERA_PERSPECTIVE) {
    // near/far real-world-scaled (root CLAUDE.md "Standard coordinates &
    // scale") — NOT a straight ÷100 of the old 10.0/15000.0. Empirically,
    // near values below ~1.0 render a fully blank scene in this project's
    // rlFrustum() setup (bisected via autotest screenshots; root cause not
    // identified — suspected precision issue at very small frustum extents,
    // not a near/far *ratio* problem since the same ratio at 0.1/150 also
    // failed). sandbox_core.c's g_camDist is clamped with margin above this
    // near plane — keep core/screen_distort.c's SOFT_PARTICLE_SCENE_NEAR/FAR
    // in sync if this ever changes.
    double top = 1.0 * tan(camera.fovy * 0.5 * DEG2RAD);
    double right = top * aspect;
    rlFrustum(-right, right, -top, top, 1.0, 1000.0);
  } else if (camera.projection == CAMERA_ORTHOGRAPHIC) {
    double top = camera.fovy / 2.0;
    double right = top * aspect;
    rlOrtho(-right, right, -top, top, 0.0001, 150.0);
  }

  rlMatrixMode(RL_MODELVIEW);
  rlPushMatrix();
  rlLoadIdentity();
  Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
  rlMultMatrixf(MatrixToFloat(matView));
  rlEnableDepthTest();
}

static void MyEndMode3D(void) {
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION);
  rlPopMatrix();
  rlMatrixMode(RL_MODELVIEW);
  rlPopMatrix();
  rlLoadIdentity();
  rlDisableDepthTest();
}

// Smoke test for the autotest harness itself (sandbox/auto_test.h) — proves
// the whole pipeline (env var -> headless window -> fixed-dt frames ->
// registration -> step -> log -> summary -> exit code) works end to end.
// Real-Shading debug toggles, laid out as TAP TARGETS. Android has no keyboard, so the two status
// labels double as buttons (raylib reports a touch as mouse button 0), which is the only way to
// exercise the shadow path on device — EnvShadow ships DISABLED and `J` was its only switch.
// PLACEMENT is the whole difficulty on a phone. The bottom-left belongs to the virtual joystick
// (sandbox_core.c: centre {150*s, h-150*s}, radius 65*s — and `s` scales up on big screens, so
// "just above it" is not safe either), the bottom-right to attack/dash/jump, the top-right to the
// camera buttons, and the top 84 px is the OS's mandatory gesture strip (ENGINE_LANDMINES §5,
// verified on the A33: a finger there never reaches the app, though `adb tap` does — so this is
// NOT something device testing via adb will catch). That leaves the top-left, below y=90.
//   index 0 = frame-time readout, 1 = GFX tier button, 2 = SHADOW button
static Rectangle DebugToggleRect(int index) {
  const float y[3] = { 96.0f, 122.0f, 148.0f };
  return (Rectangle){ 10.0f, y[index], 300.0f, 24.0f }; // generous width: a finger is not a cursor
}

/* Manager-owned VFX uses the common semantic passes. Standalone skills and
 * compositions own VFXRender scopes, so main remains render-graph
 * orchestration rather than an effect list. */
static void DrawDecalVFXLayers(Camera3D camera)
{
  if (!g_debugHideDecals) {
    int activeDecals = 0;
    DecalSystem_GetStats(&activeDecals, NULL);
    if (activeDecals == 0) return;
    DecalSystem_SetCamera(camera);

    // Pigment/material coverage belongs to BODY. Emissive cracks/runes are
    // submitted separately below so their blend and contrast policy cannot
    // contaminate the material pass.
    VFXRender_BeginPass(VFX_RENDER_PASS_BODY);
    DecalSystem_DrawBody();
    VFXRender_EndPass();

    if (DecalSystem_HasEmission()) {
      VFXRender_BeginPass(VFX_RENDER_PASS_EMISSION);
      DecalSystem_DrawEmission();
      VFXRender_EndPass();
    }
  }
}

static void DrawParticleTrailVFXLayers(Camera3D camera, Texture2D particleTexture)
{
  /* Skip this pass only when both debug categories are hidden. */
  if (g_debugHideParticles && g_debugHideTrails) return;
  ParticleManagerStats particleStats = {0};
  ParticleManager_GetStats(&particleStats);
  bool hasTrails = !g_debugHideTrails && GetActiveTrailCount() > 0;
  bool hasParticles = !g_debugHideParticles &&
                      (particleStats.activeCpuParticles > 0 || particleStats.activeGpuParticles > 0);
  bool hasEmissionParticles = hasParticles && ParticleManager_HasEmissionParticles();
  bool hasEmissionTrails = hasTrails;
  if (!hasTrails && !hasParticles) return;

  // Particle/trail bodies share BODY semantics with decals, water and
  // afterimages. Edge shaping happens in each producer before alpha-over.
  VFXRender_BeginPass(VFX_RENDER_PASS_BODY);
  if (hasTrails) DrawTrailEntitiesBody(camera);
  if (hasParticles) ParticleManager_DrawBody(camera, particleTexture);
  VFXRender_EndPass();

  // Trail/particle radiance is a separate semantic layer. Keeping HDR out of
  // BODY preserves soft smoke edges and lets energy trails carry a yellow
  // core over their darker material body.
  if (hasEmissionTrails || hasEmissionParticles) {
    VFXRender_BeginPass(VFX_RENDER_PASS_EMISSION);
    if (hasEmissionTrails) DrawTrailEntitiesEmission(camera);
    if (hasEmissionParticles)
      ParticleManager_DrawEmission(camera, particleTexture);
  }
  if (hasEmissionTrails || hasEmissionParticles) VFXRender_EndPass();
}

/* Post-scene VFX that need screen-space preparation before their body pass. */
static void CompositeScreenSpaceVFX(Camera3D camera)
{
  /* SSF producers submit before the pending check. They must never be gated
   * by decal or ordinary-particle passes: an airborne water orb has neither. */
  FluidImpact_Draw();
  VFX_Compose_SubmitScreenSpaceVFX();
  bool hasFluid = FluidSurface_HasPending();
  bool hasMetaballs = MetaballFX_HasRegisteredBlobs();
  bool hasGas = GasSystem_HasPending();
  if (!hasFluid && !hasMetaballs && !hasGas) return;
  if (hasFluid) FluidSurface_Capture(camera);
  if (hasMetaballs) MetaballFX_Prepare(camera, ELEMENT_COLOR_WATER, 0.3f, 0.12f);
  if (hasGas) GasSystem_Prepare(camera);
  VFXRender_BeginPass(VFX_RENDER_PASS_BODY);
  if (hasFluid) FluidSurface_Composite();
  if (hasMetaballs) MetaballFX_Composite();
  if (hasGas) GasSystem_Composite();
  VFXRender_EndPass();
}

int main(int argc, char **argv) {
  // Widened/heightened from 1200x700 so the sandbox tuning panel
  // (sandbox/ui_panel.c) has room for multi-column tunable layouts as skills
  // gain more sandbox-tunable parameters (CORE_ISSUES.md Item 34 follow-up).
  // Non-const: on Android these are reassigned to the native display size after
  // InitWindow so raylib does NOT upscale (render size == display size). A
  // render≠display mismatch makes raylib blit through an offscreen target that
  // renders black on some devices (Mali), so the whole app (even 2D menu) shows
  // black. Everything downstream (RTs, camera, UI) reads these, so it adapts.
  int screenWidth = 1280;
  int screenHeight = 720;

  // --render-vfx <index> [--warmup <frames>] [--out <path>]
  // Renders NEWFX tab entry <index> headlessly, saves PNG, exits.
  bool        renderVFXMode   = false;
  int         renderVFXIndex  = 0;
  int         renderVFXWarmup = 90;
  const char *renderVFXOut    = "autotest_output/vfx_eval.png";
  Vector3 captureOrigin = {6.0f, 0.0f, 4.4f};
  Vector3 captureEye = {0};
  bool captureEyeSet = false;
  bool captureExportFailed = false;
  bool captureNeutralSmoke = false;
  int         netHostPort     = 0;      // --host [port]
  const char *netJoinIp       = NULL;   // --join <ip> [port]
  int         netJoinPort     = NET_DEFAULT_PORT;
  bool        netHostOnline   = false;  // --host-online (EOS, prints join code)
  const char *netJoinCode     = NULL;   // --join-online <code>
  for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--host") == 0)
          netHostPort = (i + 1 < argc && argv[i+1][0] != '-') ? atoi(argv[++i]) : NET_DEFAULT_PORT;
      else if (strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
          netJoinIp = argv[++i];
          if (i + 1 < argc && argv[i+1][0] != '-') netJoinPort = atoi(argv[++i]);
      }
      else if (strcmp(argv[i], "--host-online") == 0)
          netHostOnline = true;
      else if (strcmp(argv[i], "--join-online") == 0 && i + 1 < argc)
          netJoinCode = argv[++i];
      else if (strcmp(argv[i], "--render-vfx") == 0 && i + 1 < argc)
          { renderVFXIndex = atoi(argv[++i]); renderVFXMode = true; }
      else if (strcmp(argv[i], "--render-neutral-smoke") == 0)
          { captureNeutralSmoke = true; renderVFXMode = true; }
      else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
          renderVFXWarmup = atoi(argv[++i]);
      else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
          renderVFXOut = argv[++i];
      else if (strcmp(argv[i], "--origin") == 0 || strcmp(argv[i], "--eye") == 0) {
          bool eye = strcmp(argv[i], "--eye") == 0;
          Vector3 value;
          if (i + 1 >= argc || !ParseCaptureVector(argv[++i], &value)) {
              fprintf(stderr, "Capture coordinates must be finite x,y,z world metres.\n");
              return 2;
          }
          if (eye) { captureEye = value; captureEyeSet = true; }
          else captureOrigin = value;
      }
  }

  if (renderVFXMode && (renderVFXWarmup < 1 ||
      (captureEyeSet && (Vector3Distance(captureEye,
          Vector3Add(captureOrigin, (Vector3){0, 0.2f, 0})) < 1.0f ||
          hypotf(captureEye.x - captureOrigin.x, captureEye.z - captureOrigin.z) < 0.001f)))) {
      fprintf(stderr, "Capture needs positive warmup, eye >= 1 metre from target and a nonvertical view.\n");
      return 2;
  }

  bool autoTestMode     = AutoTest_IsEnabled();
  bool visualVerifyMode = VisualVerify_IsEnabled();
  bool headlessMode     = autoTestMode || visualVerifyMode || renderVFXMode;

  unsigned int configFlags = 0;
  if (headlessMode) {
      // Off-screen SetWindowPosition was tried first, but produced the exact
      // same GetWorldToScreen() output as FLAG_WINDOW_HIDDEN below (proving
      // the odd coordinates aren't a window-position artifact) — kept
      // FLAG_WINDOW_HIDDEN since it doesn't touch window placement at all,
      // closer to normal interactive behavior.
      configFlags |= FLAG_WINDOW_HIDDEN;
  }
  SetConfigFlags(configFlags);
  InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");

  // Second half of making a headless capture reproducible (the first is the
  // pinned timestep — see TimeFX_SetRawDelta below). raylib seeds its RNG from
  // time(NULL) inside InitWindow, so Random01() — every particle's jitter,
  // lifetime and size variation — returns a different sequence every run. With
  // the timestep pinned but the RNG free, particle-heavy fixtures still varied
  // enough between two identical runs to swamp the parameter under test.
  //
  // Only headless: normal play must keep its variety. The seed value is
  // arbitrary and only has to be STABLE, since its whole job is that two runs
  // of one fixture produce the same image.
  if (headlessMode) {
      SetRandomSeed(20260814u);   // raylib's own generator (Random01 -> GetRandomValue)
      // ...and libc's, which is a SEPARATE stream that SetRandomSeed does not
      // touch. InitWindow seeds it from time(NULL), and several places draw
      // from it directly (core/atmosphere.c's motes and stars,
      // core/skill_helper.c, galaxy_spiral_skill). Seeding only raylib's left
      // scattered sprites landing a few pixels apart between two runs — small
      // in area, but up to 166/255 per pixel, which is not a noise floor you
      // can measure a parameter against.
      srand(20260814u);
  }
  // Third leg: load-shedding gates whose input is wall clock decide differently
  // between two runs, which changes what is DRAWN. They consult this and admit
  // the work while capturing.
  TimeFX_SetDeterministic(headlessMode);

  // Audio device + SFX/music framework (no-op & silent until the user drops
  // assets under assets/audio/). Headless autotest/render modes skip it.
  if (!headlessMode) Audio_Init();

  rlSetClipPlanes(0.001f, 150.0f);

#ifndef PLATFORM_ANDROID
  // Tự động sinh các texture cơ bản nếu thiếu trong thư mục assets/textures
  if (!FileExists("assets/textures/noise.png")) {
      Image noiseImg = GenImagePerlinNoise(256, 256, 0, 0, 16.0f);
      ExportImage(noiseImg, "assets/textures/noise.png");
      UnloadImage(noiseImg);
  }
  if (!FileExists("assets/textures/flare.png")) {
      Image flareImg = GenImageGradientRadial(128, 128, 0.0f, WHITE, BLANK);
      ExportImage(flareImg, "assets/textures/flare.png");
      UnloadImage(flareImg);
  }
  if (!FileExists("assets/textures/crack.png")) {
      Image crackImg = GenImageCellular(256, 256, 32);
      ExportImage(crackImg, "assets/textures/crack.png");
      UnloadImage(crackImg);
  }
  if (!FileExists("assets/textures/water_caustics.png")) {
      Image causticsImg = GenImageCellular(256, 256, 16);
      ExportImage(causticsImg, "assets/textures/water_caustics.png");
      UnloadImage(causticsImg);
  }
#endif

  // -----------------------------------------------------------------
  // KHỞI TẠO CÁC HỆ THỐNG ĐỒ HỌA VFX
  // -----------------------------------------------------------------
  ParticleManager_Init();
  InitTrailSystem();
  VFXLight_Init();
  DecalSystem_Init();
  /* Scene targets FIRST: PostFX_Init reads SceneTargets_IsHDR(), and the distort
     pass reads the colour target this creates. */
  SceneTargets_Init(screenWidth, screenHeight);
  ScreenDistort_Init();
  FluidSurface_Init(screenWidth, screenHeight);
  PostFX_Init(screenWidth, screenHeight);
  SurfaceMaterial_Init(); // G2 — must precede InitSandbox (CharacterModel_Load applies it)
  GfxQuality_Set(GfxQuality_Default()); // Real Shading P0 — platform-appropriate tier
  GasSystem_Init(screenWidth, screenHeight);
  Atmosphere_Init();      // G3 — ambient dust motes over the arena
  Atmosphere_Configure((Vector3){6.0f, 3.0f, 4.4f}, (Vector3){15.0f, 5.0f, 15.0f},
                       340, (Color){160, 190, 235, 255});
  MetaballFX_Init(screenWidth, screenHeight);
  /* THE DEFAULT PARTICLE SPRITE.
   *
   * Was GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK) — raylib's built-in,
   * whose alpha falls off LINEARLY. Two things were wrong with it and the second
   * is the one that matters:
   *
   *   64 px, magnified. A large billboard stretches 64 texels, which is why
   *   BILINEAR had to be forced below (the note there records the hard square
   *   edges it produced on Android).
   *
   *   A LINEAR CONE HAS NO CORE. Alpha falling off evenly makes the bright
   *   region WIDE AND DIM. A glowing particle needs the opposite — a small
   *   region hot enough to cross the bloom threshold (1.25 scene-referred,
   *   BRIGHT_BACKGROUND_VFX_SPEC.md §7.6) sitting inside a much fainter halo.
   *   No emissiveBoost can fix the shape: it scales the whole disc together.
   *
   * The formula below is the one scripts/gen_default_particle_sprite.py wrote
   * and nothing ever loaded — a compact Gaussian core, a faint wide halo, and a
   * smoothstep edge. It is generated here rather than shipped as an asset
   * because this IS the fallback: a fallback that can fail to load is not one.
   *
   * RGB stays white through the entire falloff on purpose. Baking a dark rim
   * turns alpha-blended fire into a dirty brown ring on coloured backgrounds;
   * the particle's own tint decides the colour. */
  const int kPartTexSize = 256;
  Image img = GenImageColor(kPartTexSize, kPartTexSize, BLANK);
  {
    const float half = kPartTexSize * 0.5f;
    for (int y = 0; y < kPartTexSize; y++) {
      for (int x = 0; x < kPartTexSize; x++) {
        float px = ((float)x + 0.5f - half) / half;
        float py = ((float)y + 0.5f - half) / half;
        float r2 = px * px + py * py;
        float r = sqrtf(r2);
        /* smoothstep(0.66, 0.94, r), inverted: the sprite is gone by its own
           edge, so a square texel never shows at the quad's corner. */
        float t = (r - 0.66f) / (0.94f - 0.66f);
        t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
        float edge = 1.0f - t * t * (3.0f - 2.0f * t);
        float core = expf(-r2 / 0.034f);   /* compact — this is what blooms */
        float halo = expf(-r2 / 0.20f);    /* wide and faint — this is the glow */
        float cov = edge * (0.92f * core + 0.08f * halo);
        if (cov < 0.0f) cov = 0.0f; else if (cov > 1.0f) cov = 1.0f;
        ImageDrawPixel(&img, x, y, (Color){255, 255, 255, (unsigned char)(255.0f * cov)});
      }
    }
  }
  Texture2D globalParticleTex = LoadTextureFromImage(img);
  // BILINEAR bắt buộc: mặc định raylib là POINT (GL_NEAREST) — hạt billboard
  // phóng to (vd Fire) stretch texel 64x64 thành khối vuông cứng lộ viền rõ
  // (thấy trên path CPU/VBO Android; path COMPUTE desktop ít lộ hơn do hạt
  // thường nhỏ hơn). Cùng quy ước với atmosphere/flow_map/metaball_fx/post_fx.
  SetTextureFilter(globalParticleTex, TEXTURE_FILTER_BILINEAR);
  Image trailImg = GenImageColor(64, 64, BLANK);
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      float u = x / 63.0f;
      float dist = fabsf(u - 0.5f) * 2.0f;
      float alpha = fmaxf(0.0f, 1.0f - dist * dist);
      ImageDrawPixel(&trailImg, x, y, (Color){255, 255, 255, (unsigned char)(255 * alpha)});
    }
  }
  Texture2D globalTrailTex = LoadTextureFromImage(trailImg);
  UnloadImage(trailImg);
  SetTextureFilter(globalTrailTex, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(globalTrailTex, TEXTURE_WRAP_CLAMP);
  TrailSystem_SetGlobalTexture(globalTrailTex);
  UnloadImage(img);

  Image atlasImg = GenImageColor(128, 128, BLANK);
  ImageDrawCircle(&atlasImg, 32, 32, 20, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 12, 12, 40, 40, WHITE);
  ImageDrawCircle(&atlasImg, 32, 96, 12, WHITE);
  ImageDrawRectangle(&atlasImg, 64 + 16, 64 + 16, 32, 32, WHITE);
  Texture2D testAtlasTex = LoadTextureFromImage(atlasImg);
  UnloadImage(atlasImg);

  ResourceManager_Init();
  /* WUXING_TUNING pins the live-tuning file for a measurement run.
   *
   * tuning.cfg PERSISTS ACROSS SESSIONS and silently rescales what any capture
   * shows. It is loaded here, before the headless branch, so --render-vfx and
   * scripts/render_vfx_matrix.sh inherit whatever it happens to hold — and the
   * harness recorded none of it, which means §11b's "objective oracle" was not
   * reproducible. Found parked mid-sweep at bloom_threshold = 0.9: below 1.0
   * every diffuse surface blooms itself and veils the frame, which costs every
   * effect chroma no matter how the effect is authored
   * (BRIGHT_BACKGROUND_VFX_SPEC.md §7.3 — the threshold must sit ABOVE the
   * brightest expected background in exposed space).
   *
   *   unset      -> tuning.cfg, the interactive behaviour, unchanged
   *   <path>     -> that file instead
   *   "none"     -> no file at all: code defaults, i.e. the SHIPPING values
   */
  const char *tuningPath = getenv("WUXING_TUNING");
  if (tuningPath == NULL) {
    tuningPath = "tuning.cfg";
  } else {
    if (strcmp(tuningPath, "none") == 0) tuningPath = "";
    TraceLog(LOG_INFO, "TUNING: pinned to '%s' via WUXING_TUNING%s",
             tuningPath, tuningPath[0] ? "" : " (code defaults)");
  }
  Tuning_Init(tuningPath);
  InitSkillManager(screenWidth, screenHeight);
  if (autoTestMode) AutoTestCases_Register(&player);
  DamageVolume_Init();
  EmitterSystem_Init();
  Afterimage_Init();
  PoolStats_Init();
  RegisterStaticOccluder((Vector3){4.0f, 0.0f, 3.2f}, 0.25f, 0.625f);
  RegisterStaticOccluder((Vector3){8.0f, 0.0f, 5.2f}, 0.3f, 0.75f);
  RegisterStaticOccluder((Vector3){6.0f, 0.0f, 2.6f}, 0.2f, 0.5f);
  InitUIPanel();
  SkillDebugger_Init();
  Environment_Init();
  EnvShadow_Init(); // Real Shading P6 — depth-only shadow map, OFF until toggled
  MapManager_Init();
  Combat_Init();

  EnemyEntity enemy;
  InitSandbox(&player, &enemy);
  GameScreen_Init(&player);

  // --host / --join (ENet, LAN) or --host-online / --join-online (EOS,
  // internet): bring the endpoint up and drop into the LOBBY screen (Đợt
  // A2 — the room gathers there; the host's BAT DAU moves everyone into
  // the match together).
  bool netRequested = false;
  char roomCode[16] = { 0 }; // shown in the lobby + match HUD (host only)
  if (netHostPort > 0) netRequested = Net_StartHost(netHostPort);
  else if (netJoinIp != NULL) netRequested = Net_StartClient(netJoinIp, netJoinPort);
  else if (netHostOnline) {
      netRequested = Net_StartHostOnline(roomCode, (int)sizeof(roomCode));
      if (netRequested) {
          GameScreen_SetOnlineCode(roomCode); // HUD shows it while waiting

          // The one line the host reads to their friend — keep it loud.
          printf("\n==============================\n"
                 "  WUXING ONLINE — JOIN CODE: %s\n"
                 "  (ban be: ./wuxing --join-online %s)\n"
                 "==============================\n\n", roomCode, roomCode);
      }
  }
  else if (netJoinCode != NULL) netRequested = Net_JoinOnline(netJoinCode);
  if ((netHostPort > 0 || netJoinIp || netHostOnline || netJoinCode) && !netRequested) {
      TraceLog(LOG_WARNING, "[NET] failed to start %s",
               (netHostPort > 0 || netHostOnline) ? "host" : "client");
  }

  UIPanelState uiState = {0};
  uiState.activeSkillIndex = 0;
  uiState.currentParams.level = 1;
  uiState.currentParams.milestone = 1;
  uiState.currentParams.sizeScale = 1.0f;
  uiState.currentParams.quantity = 3;
  uiState.currentParams.anchorType = CAST_ANCHOR_TARGET;
  uiState.currentParams.pathType = CAST_PATH_PROJECTILE;
  uiState.currentParams.showPortal = true;
  uiState.currentParams.damage = 100.0f;
  uiState.isPanelOpen = false;

  PostFXConfig postFXConfig = {.bloomEnabled = true,
                               // Diffuse surfaces live in the 0..1 range. Keeping the
                               // bloom threshold above that range prevents a bright map
                               // from feeding its own full-screen haze into the composite;
                               // HDR emissive VFX still exceed it and bloom normally.
                               .bloomThreshold = 1.25f,
                               .bloomIntensity = 0.12f,
                               .chromaticEnabled = true,
                               .chromaticStrength = 0.15f,
                               .vignetteEnabled = true,
                               .vignetteRadius = 0.85f,
                               .vignetteSoftness = 0.45f,
                               .colorGradeEnabled = true,
                               .contrast = 1.12f,
                               /* 1.55, up from 1.28 on 19/08/2026. ACES desaturates, and
                                  this is now the ONLY thing correcting for it: the
                                  tone map's hue restoration shipped at 0.6 and was turned
                                  off (see core/post_fx.c), because it bought chroma by
                                  subtracting the non-peak channels, which fakes occlusion
                                  on bright scenery and costs ~23% of internal structure
                                  everywhere. Saturation buys the same chroma for about an
                                  eighth of the structure, and multiplies all three channels
                                  around luma instead of pulling two of them down. */
                               .saturation = 1.55f,
                               .colorTint = {1.0f, 1.0f, 1.0f},
                               // Split-tone: cool moonlit shadows, warm highlights (Moonlight Blade mood).
                               .shadowTint = {0.90f, 0.97f, 1.12f},
                               /* MAX CHANNEL 1.0, not 1.10 (19/08/2026). This is a
                                  multiply applied AFTER the tone map, so any channel
                                  above 1.0 spends display range the curve had already
                                  allocated: at 1.10, R reached white at scene-referred
                                  2.0 against ACES's own 7.2, and 2.0 / 5.0 / 8.0 / 12.0
                                  all came out the same colour. Measured with REF BANDS
                                  (fixture 20) — see §7.6b. The warmth now comes from
                                  LOWERING G and B rather than raising R, so the tint
                                  redistributes colour instead of amplifying it. B lands
                                  at 0.90, the same value it already had. */
                               .highlightTint = {1.00f, 0.96f, 0.90f},
                               // Đợt G1 — cinematic tone mapping on by default.
                               .tonemapEnabled = true,
                               .exposure = 1.00f,
                               // Đợt G5 — LUT grading. On by default, but it is
                               // a no-op (and the shader branch stays disabled)
                               // until a graded strip exists at
                               // assets/luts/grade.png. See core/color_grade_lut.h.
                               .lutEnabled = true,
                               .lutStrength = 1.00f};

  if (visualVerifyMode) {
      VisualVerify_Init(Skill_GetIndexByName(VisualVerify_GetSkillName()));
  }

#if !defined(PERFORMANCE_CAPTURE)
  /* renderVFXMode is a headless capture path like the other two: pacing it to 60
   * makes its frame time unmeasurable, since every frame is padded to 16.7 ms. */
  if (!autoTestMode && !visualVerifyMode && !renderVFXMode) SetTargetFPS(60);
#endif // -DWUXING_PERF_CAPTURE=ON: no cap, so the HUD reports real frame time (see CMakeLists)

  bool g_gamePaused = false;
  bool g_stepNextFrame = false;
  bool g_slowMotion = false;

  float g_totalElapsed = 0.0f;

  typedef enum {
      SCREEN_MAIN_MENU,
      SCREEN_SKILL_SANDBOX,
      SCREEN_VFX_TESTER,
      SCREEN_GAME,
      SCREEN_LOBBY   // net room (Đợt A2) — waits for the host's BAT DAU
  } GameScreen;
  GameScreen currentScreen = SCREEN_MAIN_MENU;
  if (netRequested) currentScreen = SCREEN_LOBBY; // PvP run: gather in the room

  // Main-menu online (EOS) UI state — TAO PHONG / NHAP MA buttons. Actions
  // are queued one frame (menuOnlinePending) so the "DANG KET NOI" overlay
  // renders before the blocking EOS setup call freezes the thread.
  char menuOnlineMsg[64] = "";
  char menuJoinInput[8]  = "";
  int  menuJoinLen       = 0;
  bool menuJoinOpen      = false;
  int  menuOnlinePending = 0; // 0 none, 1 host, 2 join
  // Headless modes never click through the menu — drop straight into the
  // sandbox screen so AutoTest_RunFrame/VisualVerify actually tick (the menu
  // branch `continue;`s past them, which used to hang autotest forever).
  if (autoTestMode || visualVerifyMode) currentScreen = SCREEN_SKILL_SANDBOX;
  // Dev: WUXING_MAP=<name substring> forces the active map (case-insensitive)
  // so headless verify/screenshot runs can target a specific world.
  {
    const char *wantMap = getenv("WUXING_MAP");
    if (wantMap != NULL && wantMap[0] != '\0') {
      for (int i = 0; i < MapManager_GetCount(); i++) {
        if (strcasestr(MapManager_GetName(i), wantMap) != NULL) {
          MapManager_SetActiveIndex(i);
          TraceLog(LOG_INFO, "WUXING_MAP: active map -> %s", MapManager_GetName(i));
          break;
        }
      }
    }
  }
  int renderVFXFrame = 0;
  if (renderVFXMode) {
      currentScreen    = SCREEN_VFX_TESTER;
      player.position  = captureOrigin;
      if (captureNeutralSmoke) VFXTest_SetNeutralSmokeRenderTarget(player.position);
      else VFXTest_SetRenderTarget(renderVFXIndex, player.position);
      TraceLog(LOG_INFO, "CAPTURE: fixture=%s index=%d",
          captureNeutralSmoke ? "neutral-smoke" : "newfx",
          captureNeutralSmoke ? -1 : renderVFXIndex);
      TraceLog(LOG_INFO, "CAPTURE: map=%s origin=%.3f,%.3f,%.3f warmup=%d tuning=%s",
          getenv("WUXING_MAP") ? getenv("WUXING_MAP") : "default",
          captureOrigin.x, captureOrigin.y, captureOrigin.z, renderVFXWarmup,
          getenv("WUXING_TUNING") ? getenv("WUXING_TUNING") : "tuning.cfg");
  }
  while (autoTestMode     ? !AutoTest_IsFinished()      :
         visualVerifyMode ? !VisualVerify_IsFinished()  :
         renderVFXMode    ? (renderVFXFrame <= renderVFXWarmup) :
         !WindowShouldClose()) {
    // The headless paths pin dt so a capture is reproducible. That pin only ever
    // covered THIS variable, while VFX/composition code re-read GetFrameTime()
    // on its own — free-running here, since headless also skips SetTargetFPS —
    // so every capture landed on a different animation phase. Publishing the raw
    // delta through TimeFX is what makes the pin actually reach them; see
    // TimeFX_RawDelta in core/time_fx.h.
    const bool headlessFixedStep = (autoTestMode || visualVerifyMode || renderVFXMode);
    float rawDt = headlessFixedStep ? (1.0f / 60.0f) : GetFrameTime();
    TimeFX_SetRawDelta(rawDt);
    float dt = headlessFixedStep ? rawDt : TimeFX_Apply(rawDt);
    g_totalElapsed += dt;

    // Android EGL present diagnostic (WUXING_PRESENT_TEST=1): bypasses ALL game
    // rendering and draws only a solid color + shapes. RESOLVED 14/07/2026:
    // the "black + uncapped fps" this test exposed was raylib built with
    // -DCUSTOMIZE_BUILD=ON (EndDrawing neither swapped nor paced — see
    // Makefile.Android compile_raylib_android + ANDROID_NOTICES.md §D2).
    // Kept as a cheap present-path sanity toggle.
    if (getenv("WUXING_PRESENT_TEST")) {
        BeginDrawing();
        ClearBackground(RED);
        DrawCircle(GetScreenWidth()/2, GetScreenHeight()/2, 200, WHITE);
        DrawText("PRESENT OK", 60, 60, 60, GREEN);
        EndDrawing();
        continue;
    }

    // -------------------------------------------------------------------------
    // TIME CONTROL FOR DEBUGGING / SCREENSHOTTING
    // -------------------------------------------------------------------------
    if (IsKeyPressed(KEY_V)) g_gamePaused = !g_gamePaused;
    if (IsKeyPressed(KEY_B)) g_stepNextFrame = true;
    if (IsKeyPressed(KEY_M)) g_slowMotion = !g_slowMotion;
    if (IsKeyPressed(KEY_K)) {
        int nextMap = (MapManager_GetActiveIndex() + 1) % MapManager_GetCount();
        MapManager_SetActiveIndex(nextMap);
    }
    if (IsKeyPressed(KEY_L)) {
        GfxQuality_Set((GfxQuality)((GfxQuality_Get() + 1) % 4)); // Real Shading — cycle UNLIT..HIGH
    }
    if (IsKeyPressed(KEY_J)) {
        EnvShadow_SetEnabled(!EnvShadow_IsEnabled()); // Real Shading P6 — toggle real shadow map
    }
    // Same two toggles by TOUCH, for Android (no keyboard). Only while the labels are actually
    // drawn (they are hidden on SCREEN_GAME), so gameplay taps are never swallowed.
    if (currentScreen != SCREEN_GAME && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 tap = GetMousePosition();
        if (CheckCollisionPointRec(tap, DebugToggleRect(1))) {
            GfxQuality_Set((GfxQuality)((GfxQuality_Get() + 1) % 4));
        } else if (CheckCollisionPointRec(tap, DebugToggleRect(2))) {
            EnvShadow_SetEnabled(!EnvShadow_IsEnabled());
        }
    }
    if (IsKeyPressed(KEY_H) && EnvShadow_IsEnabled()) {
        EnvShadow_DebugDump(player.position); // P6 diag — numeric shadow-map readback (see notes)
    }

    if (g_gamePaused) {
        if (g_stepNextFrame) {
            dt = 1.0f / 60.0f; // Force exactly 1 frame of time
            g_stepNextFrame = false;
        } else {
            dt = 0.0f;
        }
    } else if (g_slowMotion) {
        dt *= 0.1f; // Slow motion 10% speed
    }
    
    SkillDebugger_CheckInput();
    if (g_isDebuggerCapturing) {
        dt = 0.0f; // Freeze time during automated screenshot capture
    }

    // Combat event frame boundary: last frame's clash events stay peekable
    // through this frame's skill updates, then get cleared here.
    Combat_BeginFrame();

    Audio_Update(dt); // streams the music bed (SFX are fire-and-forget)

    if (currentScreen == SCREEN_MAIN_MENU) {
        // Execute the online action queued LAST frame — its "DANG KET NOI"
        // overlay is already on screen, because EOS setup (login + lobby)
        // blocks the thread for a few seconds.
        if (menuOnlinePending != 0) {
            int action = menuOnlinePending;
            menuOnlinePending = 0;
            if (action == 1) { // TAO PHONG
                if (Net_StartHostOnline(roomCode, (int)sizeof(roomCode))) {
                    GameScreen_SetOnlineCode(roomCode);
                    currentScreen = SCREEN_LOBBY;
                    continue;
                }
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "TAO PHONG THAT BAI — XEM LOG TERMINAL");
            } else {           // NHAP MA -> join
                if (Net_JoinOnline(menuJoinInput)) {
                    roomCode[0] = '\0'; // khách không cần hiện mã
                    currentScreen = SCREEN_LOBBY;
                    continue;
                }
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "KHONG VAO DUOC PHONG %s", menuJoinInput);
            }
        }

        Vector2 mousePos = GetMousePosition();

        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        Rectangle btnSandbox = { sw/2 - 150, sh/2 - 60, 300, 50 };
        Rectangle btnVFX = { sw/2 - 150, sh/2 + 20, 300, 50 };
        Rectangle btnGame = { sw/2 - 150, sh/2 + 100, 300, 50 };
        Rectangle btnHost = { sw/2 - 150, sh/2 + 180, 300, 50 };
        Rectangle btnJoin = { sw/2 - 150, sh/2 + 260, 300, 50 };

        // Robust Android tap: arm the button under the touch while it's DOWN, fire on RELEASE if
        // the release settles over the same button. IsMouseButtonPressed (down-edge) is unreliable
        // here — on the down frame GetMousePosition can still be the stale/previous position, so the
        // over-button test misses even though the button highlights. Same fix pattern as the sandbox
        // top buttons (sandbox/ui_panel.c). Buttons are screen-centered, so no top-edge gesture zone.
        Rectangle menuBtns[5] = { btnSandbox, btnVFX, btnGame, btnHost, btnJoin };
        bool downNow = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        int overIdx = -1;
        for (int i = 0; i < 5; i++)
            if (CheckCollisionPointRec(mousePos, menuBtns[i])) { overIdx = i; break; }
        static int s_menuArmed = -1;
        if (downNow && overIdx >= 0) s_menuArmed = overIdx;
        int fired = -1;
        if (!downNow && s_menuArmed >= 0) {
            if (overIdx == s_menuArmed) fired = s_menuArmed;
            s_menuArmed = -1;
        }

        if (fired == 0) {
            currentScreen = SCREEN_SKILL_SANDBOX;
        }
        if (fired == 1) {
            currentScreen = SCREEN_VFX_TESTER;
        }
        if (fired == 2) {
            GameScreen_SetMode(GAME_MODE_BOSS); // offline entry — boss match
            currentScreen = SCREEN_GAME;
        }
        if (fired == 3) {
            if (!Net_OnlineAvailable())
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "BUILD CHUA BAT EOS — cmake -DWUXING_EOS=ON");
            else if (Net_GetMode() == NET_MODE_OFF) {
                menuOnlineMsg[0] = '\0';
                menuJoinOpen = false;
                menuOnlinePending = 1;
            }
        }
        if (fired == 4) {
            if (!Net_OnlineAvailable())
                snprintf(menuOnlineMsg, sizeof(menuOnlineMsg), "BUILD CHUA BAT EOS — cmake -DWUXING_EOS=ON");
            else {
                menuJoinOpen = !menuJoinOpen;
                menuOnlineMsg[0] = '\0';
            }
        }

        // Room-code entry (open while the NHAP MA button is toggled on).
        if (menuJoinOpen) {
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                if (ch >= 'a' && ch <= 'z') ch -= 32;
                if (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) &&
                    menuJoinLen < 5) {
                    menuJoinInput[menuJoinLen++] = (char)ch;
                    menuJoinInput[menuJoinLen] = '\0';
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE) && menuJoinLen > 0)
                menuJoinInput[--menuJoinLen] = '\0';
            if (IsKeyPressed(KEY_ENTER) && menuJoinLen >= 3 &&
                Net_GetMode() == NET_MODE_OFF) {
                menuOnlineMsg[0] = '\0';
                menuOnlinePending = 2;
            }
        }

        BeginDrawing();
        ClearBackground(DARKGRAY);

        const char* title = "WUXING SKILLS TESTBED";
        int titleW = MeasureText(title, 30);
        DrawText(title, sw/2 - titleW/2, sh/2 - 150, 30, WHITE);

        DrawRectangleRounded(btnSandbox, 0.2f, 10, CheckCollisionPointRec(mousePos, btnSandbox) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnSandbox, 0.2f, 10, WHITE);
        DrawText("1. ENTER SKILL SANDBOX", (int)btnSandbox.x + 30, (int)btnSandbox.y + 15, 20, BLACK);

        DrawRectangleRounded(btnVFX, 0.2f, 10, CheckCollisionPointRec(mousePos, btnVFX) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnVFX, 0.2f, 10, WHITE);
        DrawText("2. ENTER VFX PREFAB TESTER", (int)btnVFX.x + 10, (int)btnVFX.y + 15, 20, BLACK);

        DrawRectangleRounded(btnGame, 0.2f, 10, CheckCollisionPointRec(mousePos, btnGame) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnGame, 0.2f, 10, WHITE);
        DrawText("3. ENTER GAME", (int)btnGame.x + 90, (int)btnGame.y + 15, 20, BLACK);

        // Online (EOS) — greyed-out label when the build carries the stub.
        bool onlineUp = Net_OnlineAvailable();
        Color onlineTxt = onlineUp ? BLACK : (Color){ 90, 90, 90, 255 };
        DrawRectangleRounded(btnHost, 0.2f, 10, CheckCollisionPointRec(mousePos, btnHost) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnHost, 0.2f, 10, WHITE);
        DrawText("4. TAO PHONG ONLINE", (int)btnHost.x + 45, (int)btnHost.y + 15, 20, onlineTxt);

        DrawRectangleRounded(btnJoin, 0.2f, 10, (menuJoinOpen || CheckCollisionPointRec(mousePos, btnJoin)) ? GRAY : LIGHTGRAY);
        DrawRectangleRoundedLines(btnJoin, 0.2f, 10, WHITE);
        DrawText("5. NHAP MA VAO PHONG", (int)btnJoin.x + 40, (int)btnJoin.y + 15, 20, onlineTxt);

        if (menuJoinOpen) {
            // Entry box to the right of the join button: MA: ABC_ + ENTER hint.
            Rectangle box = { btnJoin.x + btnJoin.width + 16, btnJoin.y, 190, 50 };
            DrawRectangleRec(box, (Color){ 25, 25, 35, 255 });
            DrawRectangleLinesEx(box, 2, (Color){ 240, 220, 120, 255 });
            const char *entry = TextFormat("MA: %s%s", menuJoinInput,
                                           (((int)(g_totalElapsed * 2.0f)) & 1) ? "_" : " ");
            DrawText(entry, (int)box.x + 12, (int)box.y + 8, 24, (Color){ 240, 220, 120, 255 });
            DrawText("ENTER DE VAO", (int)box.x + 12, (int)box.y + 34, 12, (Color){ 180, 180, 190, 255 });
        }
        if (menuOnlineMsg[0] != '\0') {
            int mw = MeasureText(menuOnlineMsg, 18);
            DrawText(menuOnlineMsg, sw/2 - mw/2, (int)btnJoin.y + 60, 18, (Color){ 235, 140, 90, 255 });
        }
        if (menuOnlinePending != 0) {
            // This frame queued a blocking EOS call — tell the user before
            // the window freezes for the few seconds it takes.
            const char *t = (menuOnlinePending == 1) ? "DANG TAO PHONG QUA EPIC..."
                                                     : "DANG TIM PHONG QUA EPIC...";
            int tw2 = MeasureText(t, 26);
            DrawRectangle(sw/2 - tw2/2 - 20, sh/2 - 230, tw2 + 40, 44, (Color){ 15, 15, 25, 230 });
            DrawText(t, sw/2 - tw2/2, sh/2 - 220, 26, (Color){ 240, 220, 120, 255 });
        }

        EndDrawing();
        continue;
    }

    if (currentScreen == SCREEN_LOBBY) {
        // Room screen (Đợt A2). Net_Tick must keep pumping here — peers
        // join/leave and the roster updates while everyone waits.
        Net_Tick(dt);

        // Dev: WUXING_LOBBY_AUTOSTART=<sec> — headless/scripted runs can't
        // click BAT DAU; the host fires it automatically after N seconds.
        static float s_lobbyElapsed = 0.0f;
        s_lobbyElapsed += dt;
        const char *autoStart = getenv("WUXING_LOBBY_AUTOSTART");
        if (autoStart != NULL && Net_GetMode() == NET_MODE_HOST &&
            s_lobbyElapsed >= (float)atoi(autoStart)) {
            // WUXING_LOBBY_BOTS=n — headless runs can't click the bot
            // slots either; drop n bots on side 1 right before starting.
            const char *botEnv = getenv("WUXING_LOBBY_BOTS");
            for (int b = 0; botEnv != NULL && b < atoi(botEnv); b++)
                Net_HostAddBot(1);
            Net_HostStartMatch();
            s_lobbyElapsed = -1e9f; // fire once
        }

        if (Net_ConsumeMatchStart()) {
            // Net rooms play team battle (Đợt A3); WUXING_NET_BOSS=1 keeps
            // the old invasion-vs-boss run for dev/testing.
            GameScreen_SetMode(getenv("WUXING_NET_BOSS") != NULL
                                   ? GAME_MODE_BOSS : GAME_MODE_TEAM_BATTLE);
            GameScreen_Init(&player); // fresh match state for everyone
            currentScreen = SCREEN_GAME;
            continue;
        }

        BeginDrawing();
        ClearBackground((Color){ 12, 12, 20, 255 });
        UILobbyAction act = UI_LobbyUpdateDraw(roomCode,
                                               Net_GetMode() == NET_MODE_HOST);
        EndDrawing();

        if (act == UI_LOBBY_START) {
            Net_HostStartMatch(); // ConsumeMatchStart picks it up next frame
        } else if (act == UI_LOBBY_LEAVE ||
                   (Net_GetMode() == NET_MODE_OFF)) { // host vanished / stopped
            Net_Stop();
            GameScreen_SetOnlineCode(NULL);
            roomCode[0] = '\0';
            currentScreen = SCREEN_MAIN_MENU;
        }
        continue;
    }

    Vector3 mouseTarget3D = {0};

    // Edge-triggered: true only on the frame the VFX Tester screen is entered
    // (fresh launch or coming back from another screen), so the reset below
    // fires every time, not just once per process lifetime.
    static GameScreen s_prevScreenForVFXReset = SCREEN_MAIN_MENU;
    bool enteredVFXTester = (currentScreen == SCREEN_VFX_TESTER &&
                             s_prevScreenForVFXReset != SCREEN_VFX_TESTER);
    s_prevScreenForVFXReset = currentScreen;

    // VFX Tester "hoàn toàn tối" mode: N toggles ambient/sun to black and skips
    // MapManager_DrawActive() (ground + skybox), isolating whatever a fixture
    // emits on its own — the other setting is the normal lit scene, unchanged.
    static bool s_vfxDarkMode = false;
    static bool s_vfxLightingSaved = false;
    static Color s_vfxSavedAmbient, s_vfxSavedSun;

    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        UpdateUIPanel(GetMousePosition(), &uiState);
        if (uiState.requestedBackToMenu) currentScreen = SCREEN_MAIN_MENU;

        UpdateSandbox(&player, &enemy, dt, &uiState, &mouseTarget3D);
        CameraFX_Update(&camera, dt);
    } else if (currentScreen == SCREEN_VFX_TESTER) {
        static float vfxCameraAngle = 0.0f;
        static float vfxCamDist = 8.4f;

        // Most convenient default for testing: pivot at the arena centre (root
        // CLAUDE.md), not wherever the player last stood — fixtures spawn at
        // this pivot (vfx_test.c: s_prefabStartPos = playerPos), so this is
        // also where every new fixture appears. Re-fires on R so the view can
        // be recovered after WASD/QE/scroll drift without leaving the screen.
        if (enteredVFXTester || IsKeyPressed(KEY_R)) {
            player.position = (Vector3){6.0f, 0.0f, 4.4f};
            vfxCameraAngle = 0.6f;
            vfxCamDist = 6.0f;
        }

        if (IsKeyPressed(KEY_N)) {
            s_vfxDarkMode = !s_vfxDarkMode;
            if (s_vfxDarkMode) {
                if (!s_vfxLightingSaved) {
                    s_vfxSavedAmbient = Environment_GetAmbientColor();
                    s_vfxSavedSun = Environment_GetSunColor();
                    s_vfxLightingSaved = true;
                }
                Environment_SetAmbientColor(BLACK);
                Environment_SetSunColor(BLACK);
            } else if (s_vfxLightingSaved) {
                Environment_SetAmbientColor(s_vfxSavedAmbient);
                Environment_SetSunColor(s_vfxSavedSun);
            }
        }

        float speed = 20.0f;
        float s = sinf(vfxCameraAngle);
        float c = cosf(vfxCameraAngle);

        if (IsKeyDown(KEY_W)) { player.position.x -= s * speed * dt; player.position.z -= c * speed * dt; }
        if (IsKeyDown(KEY_S)) { player.position.x += s * speed * dt; player.position.z += c * speed * dt; }
        if (IsKeyDown(KEY_A)) { player.position.x -= c * speed * dt; player.position.z += s * speed * dt; }
        if (IsKeyDown(KEY_D)) { player.position.x += c * speed * dt; player.position.z -= s * speed * dt; }

        if (IsKeyDown(KEY_Q)) vfxCameraAngle -= 2.5f * dt;
        if (IsKeyDown(KEY_E)) vfxCameraAngle += 2.5f * dt;

        vfxCamDist -= GetMouseWheelMove() * 0.5f;
        if (vfxCamDist < 2.0f) vfxCamDist = 2.0f;
        if (vfxCamDist > 30.0f) vfxCamDist = 30.0f;

        camera.target = (Vector3){ player.position.x, player.position.y + 0.2f, player.position.z };
        camera.position = (Vector3){
            player.position.x + sinf(vfxCameraAngle) * vfxCamDist,
            player.position.y + vfxCamDist * 0.8f,
            player.position.z + cosf(vfxCameraAngle) * vfxCamDist
        };

        // WUXING_VFX_TOPDOWN=1 — steep overhead framing so the GROUND fills the
        // frame instead of the sky. Judging anything that lives on a surface
        // (decals above all) needs the receiver to BE the destination; the
        // default orbit puts most of the frame on skybox, which is why a decal
        // there is judged against the wrong background entirely.
        {
            static bool read = false, on = false;
            if (!read) { read = true;
                const char *v = getenv("WUXING_VFX_TOPDOWN");
                on = (v != NULL && *v && *v != '0'); }
            if (on) {
                camera.position = (Vector3){ player.position.x + 0.001f,
                                             player.position.y + vfxCamDist,
                                             player.position.z + 0.001f };
                camera.target = (Vector3){ player.position.x, player.position.y,
                                           player.position.z };
            }
        }

        if (renderVFXMode && captureEyeSet) {
            camera.position = captureEye;
            camera.target = Vector3Add(captureOrigin, (Vector3){0, 0.2f, 0});
        }

        // Intersect against the flat Y=0 plane first (cheap, works for the
        // common flat-map case), then snap the result's Y to the ACTIVE
        // map's real ground height — on a heightmap map (e.g. VERDANT_PATH)
        // Y=0 is only correct on the plateau interior; anywhere else (near
        // the cliff falloff) this used to leave mouseTarget3D.y wrong,
        // which broke ground-hugging/multi-point NEWFX effects that assume
        // it's the real surface (e.g. FISSURE's start point).
        Ray mouseRay = GetScreenToWorldRay(GetMousePosition(), camera);
        float t = -mouseRay.position.y / mouseRay.direction.y;
        float mtX = mouseRay.position.x + mouseRay.direction.x * t;
        float mtZ = mouseRay.position.z + mouseRay.direction.z * t;
        mouseTarget3D = (Vector3){ mtX, MapManager_GetGroundHeightAt(mtX, mtZ), mtZ };
        if (renderVFXMode) mouseTarget3D = captureOrigin;

        if (VFXTest_UpdateAndHandleInput(player.position, mouseTarget3D, testAtlasTex, globalParticleTex)) {
            currentScreen = SCREEN_MAIN_MENU;
        }
        if (!renderVFXMode) CameraFX_Update(&camera, dt);
    } else if (currentScreen == SCREEN_GAME) {
        // Spatial-audio ears follow the local player; night bed loops (both
        // no-ops until assets/audio/ has files — Audio_PlayMusic self-guards
        // against restarting the same track).
        Audio_SetListener(player.position);
        Audio_PlayMusic(MUS_ARENA_NIGHT);
        GameScreen_Update(&player, &camera, dt);
        // Dev: WUXING_TEAM_TEST=1 — scripted team-battle round on the host
        // for headless net verification (no inputs available): 10s into
        // FIGHTING wipe side 1 (elimination fires → VICTORY/DEFEAT sync),
        // 6s later run the exact ENTER-rematch path (respawn + reset).
        if (getenv("WUXING_TEAM_TEST") != NULL &&
            GameScreen_GetMode() == GAME_MODE_TEAM_BATTLE &&
            Net_GetMode() == NET_MODE_HOST) {
            static float s_ttClock = 0.0f;
            static bool s_ttWiped = false, s_ttRematched = false;
            s_ttClock += dt;
            if (!s_ttWiped && s_ttClock >= 10.0f &&
                GameScreen_GetState() == GAME_FIGHTING) {
                for (int i = 0; i < MAX_AGENTS; i++) {
                    const Agent *a = Entity_GetAgent(i);
                    if (a != NULL && a->archetype == ARCH_HERO && a->team == TEAM_ENEMY)
                        Entity_ApplyDamage(i, 1e9f, (Vector3){ 0 });
                }
                s_ttWiped = true;
                TraceLog(LOG_INFO, "[TEAMTEST] wiped side 1");
            }
            if (s_ttWiped && !s_ttRematched && s_ttClock >= 16.0f) {
                Net_HostRespawnPeerHeroes();
                GameScreen_Init(&player);
                s_ttRematched = true;
                TraceLog(LOG_INFO, "[TEAMTEST] rematch fired");
            }
        }
        if (GameScreen_RequestedBackToMenu()) {
            currentScreen = SCREEN_MAIN_MENU;
            // Leaving a net match tears the session down (EOS: closes P2P +
            // leaves the lobby) so TAO PHONG / NHAP MA work again from the menu.
            Net_Stop();
            GameScreen_SetOnlineCode(NULL);
            // The match pinned VERDANT_PATH + its ring-out bounds — hand the
            // sandbox its DEFAULT_ARENA world back (bounds are global; a
            // sandbox player outside the match circle would fall forever).
            for (int i = 0; i < MapManager_GetCount(); i++) {
                if (strcmp(MapManager_GetName(i), "DEFAULT_ARENA") == 0) {
                    MapManager_SetActiveIndex(i);
                    break;
                }
            }
            Entity_SetArenaBounds((Vector3){ 6.0f, 0.0f, 4.4f }, 18.0f);
            player.position = (Vector3){ -11.0f, 0.0f, 4.4f }; // sandbox home
            Entity_SetPosition(player.agentId, player.position);
        }
        CameraFX_Update(&camera, dt);
    }

    // Boss AI then Đấu Pháp resolve — boss casts submit through skills into
    // the combat registry, so Boss_Update runs first; Combat_Update last,
    // after all skill updates submitted this frame's projectile colliders
    // (immediate mode). Both tick for every screen; no boss / no
    // submissions = no-op. (Module 7 game/ will own this ordering.)
    // Net transport pump — host applies remote intents / broadcasts
    // snapshots; a connected client mirrors the host pool instead of
    // simulating (Net_ClientDrivesWorld skips the local gameplay ticks).
    Net_Tick(dt);

    // Entities tick — owned HERE for every screen (it used to live inside
    // UpdateSandbox only, so in SCREEN_GAME timers never ticked: one dash
    // arm froze movement forever, jumps never landed, mana never regened,
    // knockback/ring-out physics were dead). Runs after the screen updates
    // (positions pushed) and before AI/boss/combat consume fresh state.
    if (!Net_ClientDrivesWorld()) {
        Entity_Update(dt);
        AI_Update(dt);
        Boss_Update(dt);
        Formation_Update(dt);
        Combat_Update(dt);
    }

    // Hero-bot casts → net mirror (Đợt A5): ai/ is net-blind, so main.c
    // ferries its cast events to connected clients (no-op offline).
    {
        HeroBotCast botCasts[16];
        int nCasts = AI_PollHeroCasts(botCasts, 16);
        for (int ci = 0; ci < nCasts; ci++)
            Net_HostNotifyCast(botCasts[ci].agentId, botCasts[ci].skillIndex,
                               botCasts[ci].aim);
    }

    // Minion self-destruct VFX — ai/ is pure logic and reports explosions
    // as events; the composition layer draws them (element-matched preset).
    {
        MinionExplosion booms[8];
        int nBooms = AI_PollExplosions(booms, 8);
        for (int bi = 0; bi < nBooms; bi++) {
            // F0 purge: VFX_ComposeImpact is gone. Its successor is the E6
            // package — one call, tuned as a unit, severity as the single dial.
            VC_MaterialId mat =
                (booms[bi].element == 0) ? VC_MAT_WATER :
                (booms[bi].element == 1) ? VC_MAT_WOOD :
                (booms[bi].element == 2) ? VC_MAT_FIRE :
                (booms[bi].element == 3) ? VC_MAT_EARTH :
                                           VC_MAT_METAL;
            VFX_ComposeImpactPackage(booms[bi].pos, (Vector3){0.0f, 1.0f, 0.0f},
                                     mat, 0.8f, 0.40f);
            Audio_PlaySFXAt(SFX_EXPLOSION, booms[bi].pos);
        }
    }

    // Cảnh Giới Thái Cực → monochrome world (Module 6). Any live taiji
    // agent (player via balanced loadout, boss below 30% HP) fades the
    // whole canvas to black-and-white; fades back out on exit.
    {
        static float s_taijiMono = 0.0f;
        bool anyTaiji = Entity_IsTaijiActive(Control_GetAgentId()) ||
                        (Boss_IsAlive() && Entity_IsTaijiActive(Boss_GetAgentId()));
        float target = anyTaiji ? 1.0f : 0.0f;
        float speed = 2.5f * dt; // ~0.4s fade
        if (s_taijiMono < target)      { s_taijiMono += speed; if (s_taijiMono > target) s_taijiMono = target; }
        else if (s_taijiMono > target) { s_taijiMono -= speed; if (s_taijiMono < target) s_taijiMono = target; }
        PostFX_SetMonochrome(s_taijiMono);
    }

    static bool isDragging = false;
    static int pathCount = 0;
    static Vector3 pathPoints[32];

    if (currentScreen == SCREEN_SKILL_SANDBOX && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !uiState.clickedOnUI) {
      isDragging = true;
      pathCount = 1;
      pathPoints[0] = mouseTarget3D;
    }

    if (isDragging) {
      // Add points if distance > 5.0f
      if (pathCount < 32) {
        if (Vector3Distance(mouseTarget3D, pathPoints[pathCount - 1]) > 5.0f) {
          pathPoints[pathCount++] = mouseTarget3D;
        }
      }

      // Draw the drag path for visual feedback
      for (int i = 0; i < pathCount - 1; i++) {
        DrawLine3D(pathPoints[i], pathPoints[i + 1], GREEN);
      }

      if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        isDragging = false;
        uiState.currentParams.pathPointCount = pathCount;
        for (int i = 0; i < pathCount; i++) {
          uiState.currentParams.pathPoints[i] = pathPoints[i];
        }
        // Cast anim gated on the bool result — no flourish when the mana
        // gate (or bounds check) rejected the cast. Flourish length is the
        // skill's own registered duration, not a fixed number.
        if (CastSkill(uiState.activeSkillIndex, player.agentId, player.position,
                      mouseTarget3D, uiState.currentParams)) {
          CharacterModel_TriggerAttackTimed(&player.anim, CHAR_ANIM_CAST,
                                            Skill_GetCastAnimSeconds(uiState.activeSkillIndex));
          Sandbox_FacePlayerToward(&player, mouseTarget3D); // quay về hướng đánh
        }
      }
    }

    Tuning_Update();
    UpdateSkillManager(dt, enemy.position, 0.35f);
    DamageVolume_Update(dt);
    VFX_Compose_Update(dt);
    EmitterSystem_Update(dt);
    StatusVFX_Update(dt);
    Afterimage_Update(dt);
    ParticleManager_Update(dt);
    FluidImpact_Update(dt);
    GasSystem_Update(dt);
    UpdateTrailSystem(dt);
    VFXLight_Update(dt);
    // Đợt E1a — decay any live radial burst and project its focal point.
    // Must use the SAME camera the scene is rendered with, and must run
    // before PostFX_Draw.
    PostFX_UpdateTransient(camera, dt);
    VFXLight_DebugTestLight(player.position);   // tuning.cfg → vfx_light_test = 1
    DecalSystem_Update(dt);
    ScreenDistort_Update(dt);
    Environment_Update(dt);

    // WUXING_VFX_DAYLIGHT=1 keeps the map and floods it with light instead of
    // replacing the sky. Needed for anything that lives ON a surface: a decal is
    // a conformal stamp projected onto a receiver, so WUXING_VFX_BG — which
    // skips MapManager_DrawActive to get past the skybox — deletes the very
    // thing it is stamped on, and the "washed out decal" it shows is the
    // harness, not the effect. Bright ground, real receiver, judgeable decal.
    static bool s_vfxDayRead = false, s_vfxDayOn = false;
    if (!s_vfxDayRead) {
        s_vfxDayRead = true;
        const char *d = getenv("WUXING_VFX_DAYLIGHT");
        s_vfxDayOn = (d != NULL && *d && *d != '0');
        if (s_vfxDayOn)
            TraceLog(LOG_INFO, "VFX DAYLIGHT: map kept, sun/ambient flooded");
    }
    if (s_vfxDayOn && currentScreen == SCREEN_VFX_TESTER) {
        // Pushed every frame: the time-of-day system rewrites these, so setting
        // them once at startup would be silently undone a frame later.
        Environment_SetSunColor((Color){255, 250, 240, 255});
        Environment_SetAmbientColor((Color){205, 214, 230, 255});
    }

    MapManager_Update(dt);
    Atmosphere_Update(dt, camera); // G3 — drift dust motes

    SkillDebugger_PreRender();

    BeginDrawing();

    // Real Shading P6 — depth-only shadow caster pre-pass, off by default.
    // Re-invokes the same (pure, no-side-effect) draw functions used for the
    // real scene below, but into the light's depth target with the depth-only
    // shader; overlay draws (HP bars, decals) get swept in too since these
    // functions aren't split into geometry-only vs. UI layers — harmless
    // (they just contribute stray depth), not visually wrong.
    // PERF NOTE (2026-07-22): half-rate capture was tried here and REVERTED the same day. It did
    // buy ~1.5 ms (the capture's second scene traversal is the bulk of the shadow cost), but it
    // cost two things that matter more:
    //   1. Visible SHIMMER on moving casters — the shadow updates at 30 Hz against 60 Hz motion,
    //      and lower wherever the frame rate is lower (on Mali it landed around 15-25 Hz). Note
    //      this is NOT shadow "swimming": ComputeLightVP's frustum is world-static, so the texel
    //      grid never moves. It is purely the update rate.
    //   2. Alternating frame cost (capture frames ~19 ms, skipped ~14 ms), which under vsync means
    //      every other frame misses the deadline — judder at a 16 ms average.
    // Both are temporal artifacts, and the ms it saved is available from resolution instead, which
    // is a purely spatial trade. Do not re-introduce it without solving the update rate.
    if (EnvShadow_IsEnabled()) {
        EnvShadow_BeginCapture();
        Model charModel = CharacterModel_GetModel();
        if (CharacterModel_IsLoaded()) {
            SurfaceMaterial_BeginShadowCast(charModel, EnvShadow_GetDepthShader());
        }
        if (!g_isDebuggerCapturing && currentScreen == SCREEN_SKILL_SANDBOX) {
            DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
        }
        if (currentScreen == SCREEN_GAME) {
            GameScreen_Draw3D(&player);
            Boss_Draw();
            Formation_Draw();
        }
        if (CharacterModel_IsLoaded()) {
            SurfaceMaterial_EndShadowCast(charModel);
        }
        EnvShadow_EndCapture();
    }

    // ── BRIGHT-BACKGROUND HARNESS ────────────────────────────────────────────
    //
    //   WUXING_VFX_BG=0xRRGGBBFF   (e.g. 0xC8D2E6FF for an overcast sky)
    //
    // Clears to that colour and skips the map + skybox, the mirror image of the
    // N-key dark mode above. It exists because "the VFX looks like a milky film
    // over a bright background" was, for a long time, a complaint no test could
    // reproduce: the tester's default sky is dark, and an effect that can only
    // ADD light looks fine against dark and washes out against bright. Judging
    // that class of defect needs a bright destination, and eyeballing the game
    // is not a measurement. Pair it with --render-vfx for a headless A/B.
    static bool  s_vfxBgRead = false;
    static bool  s_vfxBgOn   = false;
    static Color s_vfxBgColor;
    if (!s_vfxBgRead) {
        s_vfxBgRead = true;
        const char *bg = getenv("WUXING_VFX_BG");
        if (bg && *bg) {
            s_vfxBgOn = true;
            s_vfxBgColor = GetColor((unsigned int)strtoul(bg, NULL, 0));
            TraceLog(LOG_INFO, "VFX BG override: 0x%08X (map + skybox skipped)",
                     (unsigned)strtoul(bg, NULL, 0));
        }
    }
    const bool vfxBgActive = s_vfxBgOn && currentScreen == SCREEN_VFX_TESTER;

    SceneTargets_Begin();
    if (vfxBgActive) {
        ClearBackground(s_vfxBgColor);
    } else if (g_isDebuggerCapturing || (currentScreen == SCREEN_VFX_TESTER && s_vfxDarkMode)) {
        ClearBackground(BLACK);
    } else {
        ClearBackground(GetColor(0x111111FF));
    }

    MyBeginMode3D(camera);
    // Đợt E / E2 — one place binds the VFX light pool to every registered lit
    // shader (characters, ground, path, props). Doing it per-surface inside each
    // draw function did not scale: a new ground shader meant hunting for its
    // per-frame hook, and the one that got missed stayed dark with nothing to
    // show why. A shader now opts in with #include + one call in the .fs and
    // VFXLight_RegisterShader at load — no per-frame code of its own.
    //
    // MUST be inside the 3D pass and after MyBeginMode3D: the upload converts
    // the lights into the space the surfaces actually compute in, and it reads
    // that space off rlGetMatrixTransform(), which only holds the view matrix
    // once MyBeginMode3D has pushed it. See VFXLight_ShaderSpaceMatrix.
    VFXLight_BindAll(GfxQuality_Get() >= GFX_HIGH ? 4 : (GfxQuality_Get() >= GFX_MED ? 2 : 0));
    SurfaceMaterial_UpdateFrame(camera); // G2 — push sun/ambient/fog to lit models
    GroundShadow_UpdateFrame(); // Real Shading P6 — push shadow map to raw-immediate ground draws
    if (!(currentScreen == SCREEN_VFX_TESTER && s_vfxDarkMode) && !vfxBgActive) {
        // Skipping the map is not optional for the bright harness: the skybox
        // paints over ClearBackground, so clearing alone leaves the same dark
        // sky and the test silently measures nothing.
        MapManager_DrawActive();
    }
    if (!g_isDebuggerCapturing && currentScreen == SCREEN_SKILL_SANDBOX) {
        DrawSandbox3D(&player, &enemy, mouseTarget3D, &uiState);
    }

    DrawDecalVFXLayers(camera);

    if (!g_debugHideMeshes) {
        DrawSkillManagerWorld3D();
    }

    // Shared composition pools (including one-shot LightningArc) must render
    // in every 3D game scene, not only the VFX tester and skill sandbox.
    // Their body/emission submissions own their target switches internally.
    // NEVER take the refraction scene snapshot in here: raylib's EndTextureMode
    // resets the projection to screen ortho, so a copy made inside this 3D pass
    // corrupts everything drawn after it (engine landmine #15 — the glass
    // shield once vanished entirely this way). The snapshot is taken at 2D
    // time after MyEndMode3D, and refractive draws run in a dedicated
    // post-pass (VFX_ShieldShell_DrawRefraction) so they see the complete
    // scene — see the block after SceneTargets_SnapshotDepth() below.
    /* BACKGROUND LUMINANCE, captured here and nowhere else: the world is drawn,
       no VFX are yet, so this is what the effects are standing in front of. One
       line later and an effect would be measuring its own light.
       Bracketed by MyEndMode3D/MyBeginMode3D because the capture must use
       BeginTextureMode (nothing else renders into a target under rlvk) and
       EndTextureMode resets the camera — see SceneTargets_CaptureBackgroundLuma. */
    MyEndMode3D();
    SceneTargets_CaptureBackgroundLuma();
    MyBeginMode3D(camera);

    VFX_Compose_Draw3D(camera);

    // =========================================================================
    // MỚI: TOÀN BỘ PHẦN TRUY XUẤT VÀ VẼ KHỐI CẦU DEBUG LIGHT ĐÃ ĐƯỢC BỐC SANG ĐÂY
    // + ĐƯỢC BỔ SUNG THÊM VIỆC VẼ MESH TỪ PREFAB TESTER
    // =========================================================================
    if (currentScreen == SCREEN_VFX_TESTER) {
        VFXTest_SetCamera(camera); // để phím P chụp đúng vùng hiệu ứng
        VFXTest_Draw3D();

        if (!renderVFXMode && !VFXTest_ShouldHideCharacterRef()) {
            Environment_DrawSmartShadow(player.position, ENV_SHAPE_SPHERE, 0.25f, 0.25f);
            DrawCharacter3D(player.position, 0.25f, GetColor(0xFFD39BFF), GetColor(0x3B5998FF), GetColor(0xCCCCCCFF), true, mouseTarget3D);
        }

        VFXLight_DrawDebug();   // tuning.cfg → vfx_light_debug = 1
    }

    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        VFXLight_DrawDebug();   // tuning.cfg → vfx_light_debug = 1
    }

    if (currentScreen == SCREEN_GAME) {
        GameScreen_Draw3D(&player);
        Boss_Draw();
        Formation_Draw();
    }
    Afterimage_Draw();
    DrawParticleTrailVFXLayers(camera, globalParticleTex);

    Atmosphere_Draw(camera); // G3 — owns its emission layer, including debug-only frames

    MyEndMode3D();
    SceneTargets_End();
    SceneTargets_SnapshotDepth(); // soft particles: snapshot this frame's depth for next frame's sampling
    // Glass shields refract the COMPLETE scene (characters, trails, atmosphere
    // all drawn by now). Copy it at 2D time while renderTex is still only a
    // source — the copy must not happen inside the 3D pass (landmine #15) —
    // then draw the shields in a dedicated world-space pass in copy-then-draw
    // order.
    SceneTargets_SnapshotScene();
    MyBeginMode3D(camera);
    VFX_ShieldShell_DrawRefraction(camera);
    VFX_FlowShield_DrawRefraction(camera);
    MyEndMode3D();
    CompositeScreenSpaceVFX(camera);

    /* THE DISTORT COPY IS SKIPPED WHEN NOTHING WOULD DISTORT. With no live
     * shockwave source, ScreenDistort_Draw is an identity copy of the scene
     * target into PostFX's own full-resolution HDR target — a read and a write
     * of ~15 MB at 720p, every frame, for no change. The composite is pointed
     * straight at the scene target instead.
     *
     * The gradient probe forces the copy back on, because it DRAWS into that
     * target: it has to sit inside the post chain to be worth anything (it
     * exists to answer "is the banding the effect or the pipeline"), and there
     * is nowhere to put it on the direct path. */
    const bool needsDistortCopy =
        ScreenDistort_HasLiveSources() || GradientProbe_IsActive();
    if (needsDistortCopy)
    {
        PostFX_Begin();
        ClearBackground(BLACK);
        ScreenDistort_Draw(camera);
        /* Gradient probe (phím G / WUXING_GRADIENT_PROBE=1). Phải nằm TRONG đây: đích
         * là target HDR mà VFX ghi vào, nên nó ăn nguyên chuỗi bloom -> tone map ->
         * grade -> LUT -> vignette -> dither -> FXAA. Vẽ sau ScreenDistort_Draw để
         * đè hẳn lên cảnh. Xem sandbox/gradient_probe.c. */
        GradientProbe_DrawScene();
        PostFX_End();
    }
    else
    {
        /* The Begin/End pair was also doing the job EndTextureMode does for
           free: returning to the default framebuffer and its viewport. Nothing
           else in the frame guarantees that — SceneTargets_EndVFXLayer, for
           one, leaves the SCENE target bound on purpose — so the direct path
           has to say it. Without this the composite renders into whatever was
           last bound and the screen stays black. */
        rlDrawRenderBatchActive();
        rlDisableFramebuffer();
        rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
        PostFX_UseDirectSource(SceneTargets_GetSceneTexture());
    }
    /* Announce on CHANGE, not once at startup: the mode flips with gameplay, and
     * "did my skip actually engage" is otherwise unanswerable without a profiler
     * (same reasoning as PostFX_ApplyQualityTier's tier line). */
    {
        static int s_lastCopyMode = -1;
        if ((int)needsDistortCopy != s_lastCopyMode)
        {
            s_lastCopyMode = (int)needsDistortCopy;
            TraceLog(LOG_INFO, "POSTFX source: %s",
                     needsDistortCopy ? "distort copy (a source is live)"
                                      : "scene target DIRECT (copy skipped)");
        }
    }

    ClearBackground(BLACK);
    /* AUTO EXPOSURE (§7.5) — 2D time, so BeginTextureMode is legal, and before
       the composite that consumes it. Metered from the pre-VFX background, so a
       spell cannot drive the exposure applied to itself. */
    SceneTargets_UpdateExposure(dt, 0.18f, 0.10f, 2.5f, 0.8f);

    PostFX_Draw(&postFXConfig);
    /* Chứng: CÙNG dải màu đó, tính bằng CPU qua đường cong ACES per-channel, vẽ
     * SAU post nên không đi qua gì cả. Chênh lệch giữa hai dải chính là phần
     * đường ống thêm vào. */
    GradientProbe_DrawControl();

    // These are dev/debug overlays — skip them entirely on SCREEN_GAME so it
    // reads as a real production screen, not a test environment. Untouched
    // for every other screen. On the VFX Tester specifically, TAB
    // (VFXTest_ShouldHideDebugOverlays) also hides them by default — pure
    // clutter while judging how an effect looks.
    if (currentScreen != SCREEN_GAME &&
        !(currentScreen == SCREEN_VFX_TESTER && VFXTest_ShouldHideDebugOverlays())) {
        DrawSkillManagerOverlay();
        DrawCoreTestSkillDebugHUD();
        PoolStats_DrawOverlay(); // CORE_ISSUES.md Item 3 test — on-screen depth readback (press L)
    }

    if (!renderVFXMode && currentScreen != SCREEN_GAME && currentScreen != SCREEN_VFX_TESTER) {
        Vector2 enemyScreenHead = GetWorldToScreen(
            (Vector3){enemy.position.x, enemy.position.y + 0.55f, enemy.position.z},
            camera);
        DrawText("ENEMY", (int)enemyScreenHead.x - 22, (int)enemyScreenHead.y, 12,
                 WHITE);
    }

    if (!g_isDebuggerCapturing && !renderVFXMode) {
        if (currentScreen == SCREEN_SKILL_SANDBOX) {
            DrawUIPanel(&uiState);
            DrawSandboxTouchControls(&player);
            if (uiState.isPanelOpen) {
                DrawSandboxHUD();
            }
        } else if (currentScreen == SCREEN_VFX_TESTER && !renderVFXMode) {
            VFXTest_DrawHUD();
        } else if (currentScreen == SCREEN_GAME) {
            GameScreen_DrawHUD(&player);
        }

        // Frame-time readout. FPS alone cannot be tuned with: it is a reciprocal, so equal FPS
        // steps are unequal amounts of work (60->50 is 3.3 ms, 30->25 is 6.7 ms), and under vsync
        // it snaps to refresh divisors and hides every partial win. Milliseconds are LINEAR in the
        // work done - a 2 ms saving reads as 2 ms whether you are at 60 FPS or at 40. Budget for
        // 60 FPS is 16.6 ms; the colour is that budget (green under, yellow under 33, red over).
        {
            static float s_msAvg = 0.0f, s_msWorst = 0.0f, s_worstWindow = 0.0f;
            float dt = GetFrameTime();
            float msNow = dt * 1000.0f;
            s_msAvg += (msNow - s_msAvg) * 0.05f;   // ~20-frame smoothing: steady enough to read
            if (msNow > s_msWorst) s_msWorst = msNow;
            s_worstWindow += dt;
            if (s_worstWindow >= 1.0f) { s_worstWindow = 0.0f; s_msWorst = msNow; } // worst of the last second
            Color budget = (s_msAvg < 16.6f) ? GREEN : (s_msAvg < 33.3f) ? YELLOW : RED;
            const char *line = TextFormat("%.1f ms  %d FPS   worst %.1f ms", s_msAvg, GetFPS(), s_msWorst);
            Rectangle r = DebugToggleRect(0);   // screen-relative: lands correctly on a phone too
#if defined(PERFORMANCE_CAPTURE)
            DrawText(line, (int)r.x, (int)r.y, 20, budget);   // measurement build: show it in-game too
#else
            if (currentScreen != SCREEN_GAME) DrawText(line, (int)r.x, (int)r.y, 20, budget);
#endif
        }

        // Two separate lines, each its own tap target (see DebugToggleRect): on Android these ARE
        // the only way to switch tier / turn the real shadow on, since there is no keyboard.
        if (currentScreen != SCREEN_GAME) {
            static const char *gfxTierName[4] = { "UNLIT", "LOW", "MED", "HIGH" };
            Rectangle rg = DebugToggleRect(1), rs = DebugToggleRect(2);
            DrawText(TextFormat("GFX [L/tap]: %s", gfxTierName[GfxQuality_Get()]),
                     (int)rg.x, (int)rg.y, 20, SKYBLUE);
            DrawText(TextFormat("SHADOW [J/tap]: %s", EnvShadow_IsEnabled() ? "ON" : "OFF"),
                     (int)rs.x, (int)rs.y, 20, EnvShadow_IsEnabled() ? GREEN : SKYBLUE);
            // Thái Cực state — without it on screen, "[U] did nothing" and
            // "[U] worked and the skill is still gated" look identical, which is
            // exactly the confusion the toggle exists to remove.
            bool taiji = Entity_IsTaijiActive(player.agentId);
            Rectangle rt = DebugToggleRect(3);
            DrawText(TextFormat("THAI CUC [U]: %s", taiji ? "ON" : "OFF"),
                     (int)rt.x, (int)rt.y, 20, taiji ? GOLD : SKYBLUE);
        }

        // Real Shading P6 — debug preview of the raw shadow-map texture.
        // OFF by default: set WUXING_SHADOW_DEBUG=1 to show it.
        // PERF (2026-07-22): this is NOT free. It minifies the whole 2048x2048 R32F shadow map
        // (16 MB) into a 220px box with POINT filtering and no mipmaps, so every preview pixel is a
        // scattered, cache-missing texel fetch. Because it was gated on EnvShadow_IsEnabled() it ran
        // only after pressing J — i.e. it was part of the "shadow costs 30 FPS" measurement itself.
        // Keep it behind the env var; never leave it on while judging shadow performance.
        // If the box is a flat solid color the depth CAPTURE isn't producing real per-pixel data;
        // a recognizable arena/character silhouette means the capture is fine and any bug is in the
        // comparison math instead.
        if (EnvShadow_IsEnabled() && getenv("WUXING_SHADOW_DEBUG")) {
            int previewSize = 220;
            int px = GetScreenWidth() - previewSize - 10;
            int py = GetScreenHeight() - previewSize - 10;
            Texture2D shadowTex = EnvShadow_GetShadowMap();
            DrawRectangle(px - 2, py - 2, previewSize + 4, previewSize + 4, BLACK);
            DrawTexturePro(shadowTex,
                           (Rectangle){ 0, 0, (float)shadowTex.width, (float)shadowTex.height },
                           (Rectangle){ (float)px, (float)py, (float)previewSize, (float)previewSize },
                           (Vector2){ 0, 0 }, 0.0f, WHITE);
            DrawText("SHADOW MAP DEBUG", px, py - 20, 14, YELLOW);
        }
    }
             
    if (currentScreen == SCREEN_SKILL_SANDBOX) {
        SkillDebugger_PostRender(uiState.activeSkillIndex, player.position, mouseTarget3D);
    }

    GradientProbe_Readback();

    EndDrawing();

    if (autoTestMode)     AutoTest_RunFrame();
    if (visualVerifyMode) VisualVerify_RunFrame(g_totalElapsed);
    if (renderVFXMode) {
        renderVFXFrame++;
        if (renderVFXFrame >= renderVFXWarmup) {
            Color captureAmbient = Environment_GetAmbientColor();
            Color captureSun = Environment_GetSunColor();
            Vector3 captureLight = Environment_GetSunDirection();
            TraceLog(LOG_INFO, "CAPTURE: ambient=%d,%d,%d sun=%d,%d,%d lightTravel=%.4f,%.4f,%.4f",
                captureAmbient.r, captureAmbient.g, captureAmbient.b,
                captureSun.r, captureSun.g, captureSun.b,
                captureLight.x, captureLight.y, captureLight.z);
            TraceLog(LOG_INFO, "CAPTURE: eye=%.3f,%.3f,%.3f target=%.3f,%.3f,%.3f shadow=%d output=%s",
                camera.position.x, camera.position.y, camera.position.z,
                camera.target.x, camera.target.y, camera.target.z,
                EnvShadow_IsEnabled(), renderVFXOut);
            if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
            Image img = LoadImageFromScreen();
            captureExportFailed = !ExportImage(img, renderVFXOut);
            UnloadImage(img);
            break;
        }
    }
  }

  int exitCode = captureExportFailed ? 1 : 0;
  if (autoTestMode) {
      AutoTest_PrintSummary();
      exitCode = AutoTest_GetExitCode();
  }
  if (visualVerifyMode) {
      exitCode = VisualVerify_GetExitCode();
  }

  UnloadTexture(globalParticleTex);
  UnloadTexture(testAtlasTex);
  ParticleManager_Unload();
  UnloadTrailSystem();
  DecalSystem_Unload();
  ScreenDistort_Unload();
  GasSystem_Unload();
  SceneTargets_Unload();
  FluidSurface_Unload();
  Atmosphere_Unload();
  MetaballFX_Unload();
  UnloadSkillManager();
  DamageVolume_Unload();
  EmitterSystem_Unload();
  ResourceManager_Unload();
  MapManager_Unload();
  Audio_Shutdown();
  CloseWindow();

  return exitCode;
}
