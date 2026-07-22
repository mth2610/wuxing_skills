// rlvk windowed visual test suite - one tiny self-checking scenario per backend area.
// Run all: rlvk_visual_test        Run one: rlvk_visual_test <name>     List: --list
// Each scenario prints exactly one PASS/FAIL line; exit code = number of failures.
// Build/run via scripts/run_rlvk_visual_test.sh (caches a Vulkan-patched raylib in /tmp).
//
// Scenario map (bug-class each one guards):
//   clear          swapchain clear + present + readback
//   batch_alpha    2D batch, texture alpha blending (particle sprite class)
//   additive3d     3D billboard, BLEND_ADDITIVE + opaque radial tex (glow class)
//   shader_uniform custom fragment shader + SetShaderValue-driven alpha (VFX shader class)
//   depth          batch-path occlusion: near opaque drawn FIRST, far bright drawn
//                  SECOND additive+maskoff must NOT overlay it (black-hole swirl class)
//   depth_rt       the same occlusion inside a render texture (PostFX scene path)
//   winding_rt     front-face triangle visible on screen AND inside a render texture
//                  (flip-Y winding/culling class - mesh see-through)
//   instanced      DrawMeshInstanced + instanceTransform attribute
//   readback       LoadImageFromScreen after EndDrawing, then keep rendering
//                  (frameConsumed/present lifecycle class - GPU-fault regression)
//   stress         many additive billboards x frames: arena-exhaustion mid-frame
//                  flush path (fire_pillar crash class) - survival test
//   ubo_arena      per-draw uniform changes until the arena is spent: the UBO push must
//                  never be skipped (stale mvp/uniforms = smoke-column "shuffled
//                  rectangles" class, HANDOFF 7.28) - correctness test
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 400
#define H 300

static Image snap(void) { return LoadImageFromScreen(); }
static Color at(Image im, int x, int y) { return ((Color *)im.data)[y*im.width + x]; }
static bool near3(Color c, int r, int g, int b, int tol)
{
    return (abs(c.r - r) <= tol) && (abs(c.g - g) <= tol) && (abs(c.b - b) <= tol);
}

static Texture2D radialAlphaTex(void)   // white core, alpha falls 255->0 outward (RGBA)
{
    int S = 64; Color *px = (Color *)MemAlloc(S*S*sizeof(Color));
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++)
    {
        float dx = (x - S/2)/(float)(S/2), dy = (y - S/2)/(float)(S/2);
        float a = 1.0f - sqrtf(dx*dx + dy*dy); a = Clamp(a, 0.0f, 1.0f);
        px[y*S + x] = (Color){ 255, 255, 255, (unsigned char)(a*255) };
    }
    Image img = { px, S, S, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D t = LoadTextureFromImage(img); MemFree(px); return t;
}

static Camera3D cam3d(void)
{
    Camera3D c = { 0 };
    c.position = (Vector3){ 0, 0, 6 }; c.target = (Vector3){ 0, 0, 0 };
    c.up = (Vector3){ 0, 1, 0 }; c.fovy = 45.0f; c.projection = CAMERA_PERSPECTIVE;
    return c;
}

// ---- scenarios (return NULL on pass, short reason string on fail) ----------------

static const char *sc_clear(void)
{
    for (int f = 0; f < 3; f++) { BeginDrawing(); ClearBackground(RED); EndDrawing(); }
    Image im = snap(); Color c = at(im, W/2, H/2); UnloadImage(im);
    return near3(c, 230, 41, 55, 10) ? NULL : "center is not the clear color";
}

static const char *sc_batch_alpha(void)
{
    Texture2D tex = radialAlphaTex();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLUE);
        DrawTexturePro(tex, (Rectangle){0,0,64,64}, (Rectangle){W/2-64,H/2-64,128,128}, (Vector2){0,0}, 0, WHITE);
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2), e = at(im, 8, 8); UnloadImage(im); UnloadTexture(tex);
    if (!near3(c, 255, 255, 255, 12)) return "sprite core not white";
    if (!near3(e, 0, 121, 241, 12))   return "corner lost bg: texture alpha not blending";
    return NULL;
}

static const char *sc_additive3d(void)
{
    Image gi = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);   // opaque alpha everywhere
    Texture2D tex = LoadTextureFromImage(gi); UnloadImage(gi);
    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground((Color){10,10,20,255});
        BeginMode3D(cam);
            BeginBlendMode(BLEND_ADDITIVE);
            DrawBillboard(cam, tex, (Vector3){0,0,0}, 3.0f, WHITE);
            EndBlendMode();
        EndMode3D();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2), e = at(im, 8, 8); UnloadImage(im); UnloadTexture(tex);
    if (c.r < 200) return "glow core not bright: additive blend broken";
    if (!near3(e, 10, 10, 20, 10)) return "corner changed: black edge not additive-transparent";
    return NULL;
}

static const char *sc_shader_uniform(void)
{
    const char *FS = "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
        "uniform vec4 uColor; uniform float uAlpha;\n"
        "void main(){ float e = 1.0-clamp(length(fragTexCoord-vec2(0.5))*2.0,0.0,1.0);\n"
        "  finalColor = vec4(uColor.rgb, uAlpha*e); }\n";
    Shader sh = LoadShaderFromMemory(NULL, FS);
    Vector4 col = { 0.1f, 1.0f, 0.2f, 1.0f }; float alpha = 0.85f;
    SetShaderValue(sh, GetShaderLocation(sh, "uColor"), &col, SHADER_UNIFORM_VEC4);
    SetShaderValue(sh, GetShaderLocation(sh, "uAlpha"), &alpha, SHADER_UNIFORM_FLOAT);
    Image wi = GenImageColor(8, 8, WHITE); Texture2D white = LoadTextureFromImage(wi); UnloadImage(wi);
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLUE);
        BeginShaderMode(sh);
        DrawTexturePro(white, (Rectangle){0,0,8,8}, (Rectangle){W/2-70,H/2-70,140,140}, (Vector2){0,0}, 0, WHITE);
        EndShaderMode();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2), e = at(im, 8, 8);
    UnloadImage(im); UnloadTexture(white); UnloadShader(sh);
    if (c.g < 150 || c.r > 90) return "center wrong: custom uniforms not delivered";
    if (!near3(e, 0, 121, 241, 12)) return "corner lost bg: shader alpha not blending";
    return NULL;
}

// Near opaque cube drawn FIRST, far bright wall drawn SECOND (additive, depth mask off,
// depth TEST still on). Depth test must keep the wall behind the cube (black-hole class).
static void drawOcclusionScene(void)
{
    ClearBackground((Color){12,12,16,255});
    DrawCube((Vector3){0,0,2}, 1.6f, 1.6f, 1.6f, (Color){40,40,48,255});   // near, opaque, writes depth
    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    DrawCube((Vector3){0,0,-2}, 8.0f, 6.0f, 0.05f, (Color){255,220,60,255}); // far bright wall
    rlEnableDepthMask();
    EndBlendMode();
}

static const char *sc_depth(void)
{
    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing();
        BeginMode3D(cam); drawOcclusionScene(); EndMode3D();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2), s = at(im, 20, H/2); UnloadImage(im);
    if (c.r > 130) return "far bright wall overlays near cube: depth test not occluding";
    if (s.r < 150) return "far wall missing entirely";
    return NULL;
}

static const char *sc_depth_rt(void)
{
    RenderTexture2D rt = LoadRenderTexture(W, H);
    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginTextureMode(rt);
            BeginMode3D(cam); drawOcclusionScene(); EndMode3D();
        EndTextureMode();
        DrawTextureRec(rt.texture, (Rectangle){0, 0, W, -H}, (Vector2){0, 0}, WHITE);
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2), s = at(im, 20, H/2); UnloadImage(im); UnloadRenderTexture(rt);
    if (c.r > 130) return "occlusion broken INSIDE render texture (PostFX scene path)";
    if (s.r < 150) return "far wall missing inside render texture";
    return NULL;
}

// Sample a render texture's DEPTH attachment in a shader and linearize it, exactly like the
// game's depth_copy.fs / soft-particle path. Under Caps.noSampledDepth the attachment depth has
// no SAMPLED usage; rlvk must route the sample through the sampleable shadow-copy twin (§7.1),
// else the sample falls back to the white default (depth==1.0 => "infinitely far" everywhere),
// which is exactly the hard-edged, no-fade soft-particle symptom.
static const char *sc_soft_depth(void)
{
    const char *FS =
        "#version 330\n"
        "in vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
        "uniform float uNear; uniform float uFar;\n"
        "void main(){\n"
        "  float ndc = texture(texture0, fragTexCoord).r * 2.0 - 1.0;\n"
        "  float lin = (2.0*uNear*uFar) / (uFar + uNear - ndc*(uFar - uNear));\n"
        "  float v = clamp(lin/20.0, 0.0, 1.0);\n"  // near cube (~4) -> 0.2; cleared far (=uFar) -> 1.0
        "  finalColor = vec4(v, v, v, 1.0); }\n";
    Shader sh = LoadShaderFromMemory(NULL, FS);
    float nearV = 0.01f, farV = 1000.0f;  // raylib BeginMode3D defaults
    SetShaderValue(sh, GetShaderLocation(sh, "uNear"), &nearV, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, GetShaderLocation(sh, "uFar"),  &farV,  SHADER_UNIFORM_FLOAT);
    RenderTexture2D rt = LoadRenderTexture(W, H);
    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginTextureMode(rt);
            ClearBackground(BLACK);                     // clears RT depth to 1.0 (far)
            BeginMode3D(cam);
                DrawCube((Vector3){0,0,2}, 1.6f, 1.6f, 1.6f, WHITE); // near, fills center, writes depth
            EndMode3D();
        EndTextureMode();
        BeginShaderMode(sh);
            DrawTexturePro(rt.depth, (Rectangle){0,0,W,H}, (Rectangle){0,0,W,H}, (Vector2){0,0}, 0, WHITE);
        EndShaderMode();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2), e = at(im, 8, 8); UnloadImage(im);
    UnloadRenderTexture(rt); UnloadShader(sh);
    // center covers the near cube -> real depth ~ dist 4 -> dark; corner is cleared far -> white
    if (c.r > 150) return "RT depth sample reads far everywhere: soft-particle depth not sampleable (no shadow-copy twin)";
    if (e.r < 200) return "cleared-far background not white: depth sample wrong";
    return NULL;
}

// One soft-particle billboard straddling a ground plane: the half over open sky stays bright,
// the half sunk into the ground fades out (the game's soft-particle behaviour). Also a depth
// ORIENTATION test — a Y-flipped depth twin would fade the WRONG half. Set RLVK_SOFT_DUMP=path
// to export the frame as a PNG for eyeballing.
static const char *sc_soft_ground(void)
{
    const char *FS =
        "#version 330\n"
        "in vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform sampler2D texture0;\n"             // = rt.depth (raw NDC scene depth of the ground)
        "uniform vec2 uRes; uniform float uNear; uniform float uFar; uniform float uFade;\n"
        "float lin(float d){ float n=d*2.0-1.0; return (2.0*uNear*uFar)/(uFar+uNear-n*(uFar-uNear)); }\n"
        "void main(){\n"
        "  float sceneL = lin(texture(texture0, gl_FragCoord.xy/uRes).r);\n"
        "  float fragL  = lin(gl_FragCoord.z);\n"
        "  float soft = clamp((sceneL - fragL)/uFade, 0.0, 1.0);\n"  // 0 where ground is in front/close
        "  float g = 1.0 - clamp(length(fragTexCoord-vec2(0.5))*2.0, 0.0, 1.0);\n"
        "  finalColor = vec4(vec3(0.35,0.75,1.0)*g*soft, 1.0); }\n";
    Shader sh = LoadShaderFromMemory(NULL, FS);
    float res[2] = {(float)W,(float)H}, nearV = 0.01f, farV = 1000.0f, fade = 0.6f;
    SetShaderValue(sh, GetShaderLocation(sh,"uRes"),  res,   SHADER_UNIFORM_VEC2);
    SetShaderValue(sh, GetShaderLocation(sh,"uNear"), &nearV,SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, GetShaderLocation(sh,"uFar"),  &farV, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, GetShaderLocation(sh,"uFade"), &fade, SHADER_UNIFORM_FLOAT);
    Mesh quad = GenMeshPlane(2.6f, 2.6f, 1, 1);           // XZ plane; stood upright below
    Material mat = LoadMaterialDefault(); mat.shader = sh;
    RenderTexture2D rt = LoadRenderTexture(W, H);
    mat.maps[MATERIAL_MAP_DIFFUSE].texture = rt.depth;    // texture0 = scene depth
    Camera3D cam = { 0 };
    cam.position = (Vector3){0,3,5}; cam.target = (Vector3){0,0,0};
    cam.up = (Vector3){0,1,0}; cam.fovy = 45.0f; cam.projection = CAMERA_PERSPECTIVE;
    Matrix stand = MatrixRotateX(PI*0.5f);                // XZ plane -> upright XY, facing the camera
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground((Color){10,12,24,255});
        BeginTextureMode(rt);
            ClearBackground((Color){10,12,24,255});
            BeginMode3D(cam);
                DrawCube((Vector3){0,0,0}, 30.0f, 0.1f, 30.0f, (Color){30,34,44,255}); // ground slab at y=0
            EndMode3D();
        EndTextureMode();
        DrawTextureRec(rt.texture, (Rectangle){0,0,W,-H}, (Vector2){0,0}, WHITE);       // show the ground
        BeginMode3D(cam);
            BeginBlendMode(BLEND_ADDITIVE);
            rlDisableDepthTest();
            DrawMesh(quad, mat, stand);                    // soft-faded glow through the ground
            rlEnableDepthTest();
            EndBlendMode();
        EndMode3D();
        EndDrawing();
    }
    Image im = snap();
    if (getenv("RLVK_SOFT_DUMP")) ExportImage(im, getenv("RLVK_SOFT_DUMP"));
    // Above the ground line = open sky behind -> bright glow; below = ground in front -> faded.
    Color hi = at(im, W/2, H/2 - 55), lo = at(im, W/2, H/2 + 70);
    UnloadImage(im); UnloadRenderTexture(rt); UnloadShader(sh); UnloadMesh(quad);
    if (hi.b < 90)  return "glow above ground missing: soft fade or depth sample wrong";
    if (lo.b > hi.b) return "glow brighter BELOW ground than above: depth twin Y-flipped";
    return NULL;
}

static const char *sc_winding_rt(void)
{
    Camera3D cam = cam3d();
    RenderTexture2D rt = LoadRenderTexture(W, H);
    // CCW (front-facing) triangle covering the screen center; backface culling is on by default
    Vector3 a = {-2,-1.5f,0}, b = {2,-1.5f,0}, c3 = {0,2,0};
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        // direct draw first (whole screen), then the RT copy overlays the right half
        BeginMode3D(cam); DrawTriangle3D(a, b, c3, GREEN); EndMode3D();
        BeginTextureMode(rt);
            ClearBackground(BLACK);
            BeginMode3D(cam); DrawTriangle3D(a, b, c3, GREEN); EndMode3D();
        EndTextureMode();
        DrawTexturePro(rt.texture, (Rectangle){0,0,W,-H}, (Rectangle){W/2,0,W/2,H}, (Vector2){0,0}, 0, WHITE);
        EndDrawing();
    }
    // triangle spans the center: left half shows the direct draw, right half the RT copy
    Image im = snap();
    Color direct = at(im, W/2 - 30, H/2), inRt = at(im, W/2 + 100, H/2);
    UnloadImage(im); UnloadRenderTexture(rt);
    if (direct.g < 120) return "front-face triangle culled on the swapchain path";
    if (inRt.g < 120)   return "triangle culled INSIDE render texture: flip-Y winding mismatch";
    return NULL;
}

static const char *sc_instanced(void)
{
    const char *VS = "#version 330\n"
        "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor; in mat4 instanceTransform;\n"
        "uniform mat4 mvp; out vec4 fragColor;\n"
        "void main(){ fragColor = vertexColor; gl_Position = mvp*instanceTransform*vec4(vertexPosition,1.0); }\n";
    const char *FS = "#version 330\n"
        "in vec4 fragColor; uniform vec4 colDiffuse; out vec4 finalColor;\n"
        "void main(){ finalColor = vec4(1.0, 0.6, 0.1, 1.0); }\n";
    Mesh cube = GenMeshCube(0.8f, 0.8f, 0.8f);
    Material mat = LoadMaterialDefault();
    Shader sh = LoadShaderFromMemory(VS, FS);
    sh.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(sh, "instanceTransform");
    mat.shader = sh;
    Matrix xf[3] = { MatrixTranslate(-2,0,0), MatrixTranslate(0,0,0), MatrixTranslate(2,0,0) };
    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLUE);
        BeginMode3D(cam); DrawMeshInstanced(cube, mat, xf, 3); EndMode3D();
        EndDrawing();
    }
    // fovy 45 at z=6: half-height = 6*tan(22.5deg) = 2.485, half-width = 2.485*(400/300) = 3.314
    // x = +/-2 projects to center +/- (2/3.314)*(W/2) = +/-120.7 px
    Image im = snap();
    Color l = at(im, W/2 - 121, H/2), m = at(im, W/2, H/2), r = at(im, W/2 + 121, H/2);
    Color gapL = at(im, W/2 - 60, H/2), gapR = at(im, W/2 + 60, H/2);
    UnloadImage(im); UnloadMesh(cube); UnloadShader(sh);
    if (m.r < 180)                       return "center instance missing";
    if (l.r < 180 || r.r < 180)          return "side instances missing: per-instance transform not applied";
    if (gapL.r > 120 || gapR.r > 120)    return "instances smeared: instance rate/stride wrong";
    return NULL;
}

static const char *sc_readback(void)
{
    for (int f = 0; f < 2; f++) { BeginDrawing(); ClearBackground(PURPLE); EndDrawing(); }
    Image mid = snap();                                   // read AFTER present (no active frame)
    bool okMid = near3(at(mid, W/2, H/2), 200, 122, 255, 12); UnloadImage(mid);
    for (int f = 0; f < 3; f++) { BeginDrawing(); ClearBackground(LIME); EndDrawing(); }  // must keep presenting
    Image after = snap();
    bool okAfter = near3(at(after, W/2, H/2), 0, 158, 47, 12); UnloadImage(after);
    if (!okMid)   return "post-present readback returned wrong pixels";
    if (!okAfter) return "rendering broke after readback (frame lifecycle desync)";
    return NULL;
}

// The game's screen composite: render 3D into an RT, blit that RT full-screen onto the
// default framebuffer (PostFX_Draw), THEN draw 2D UI (DrawRectangle/DrawText) on top of it.
// On Android the whole 2D UI/HUD vanished on in-game screens while the blitted 3D scene stayed
// visible - i.e. 2D primitives issued to the default framebuffer AFTER an RT pass + a
// full-screen textured quad were being lost. The main menu (plain 2D, no RT pass) was fine.
// depth_rt already proves 3D-after-blit works; this guards specifically the 2D-after-blit path.
static const char *sc_ui_after_rt(void)
{
    Camera3D cam = cam3d();
    RenderTexture2D rt = LoadRenderTexture(W, H);
    Rectangle panel = { W/2 - 60, H/2 - 40, 120, 80 };
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        // 1) scene into the RT
        BeginTextureMode(rt);
            ClearBackground((Color){ 20, 40, 120, 255 });     // distinctive "scene" blue
            BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 2.0f, 2.0f, 2.0f, (Color){40,60,140,255}); EndMode3D();
        EndTextureMode();
        // 2) blit the scene full-screen onto the default framebuffer (PostFX_Draw analogue)
        DrawTextureRec(rt.texture, (Rectangle){0,0,W,-H}, (Vector2){0,0}, WHITE);
        // 3) 2D UI on top (the vanishing HUD) - opaque red panel + white text
        DrawRectangleRec(panel, RED);
        DrawText("UI", (int)panel.x + 40, (int)panel.y + 28, 30, WHITE);
        EndDrawing();
    }
    Image im = snap();
    Color center = at(im, W/2, H/2);                          // inside the red panel
    Color scene  = at(im, 20, H/2);                           // far-left: only the blitted scene
    UnloadImage(im); UnloadRenderTexture(rt);
    if (!near3(scene, 20, 40, 120, 40)) return "blitted scene missing: RT->screen blit itself lost";
    if (center.r < 150 || center.g > 90) return "2D UI panel missing on top of RT blit (Android HUD-vanish class)";
    return NULL;
}

// GPU-particle draw path: vertex shader reads a per-instance SSBO by gl_InstanceID
// (mirrors core/shaders/particles.vs + compute/gpu_particle_system.c's draw half).
// Guards: graphics set0 SSBO bindings, rlvkRebaseStorageBuffers, rlvkBindShaderSsbos.
static const char *sc_ssbo_vs(void)
{
    const char *VS =
        "#version 430\n"
        "layout(location=0) in vec3 vertexPosition;\n"
        "struct P { vec4 posSize; vec4 color; };\n"
        "layout(std430, binding=0) buffer Buf { P items[]; };\n"
        "uniform mat4 mvp;\n"
        "out vec4 fragColor;\n"
        "void main(){\n"
        "  P p = items[gl_InstanceID];\n"
        "  fragColor = p.color;\n"
        "  gl_Position = mvp * vec4(p.posSize.xyz + vertexPosition*p.posSize.w, 1.0);\n"
        "}\n";
    const char *FS =
        "#version 430\n"
        "in vec4 fragColor; out vec4 finalColor;\n"
        "void main(){ finalColor = fragColor; }\n";
    Shader sh = LoadShaderFromMemory(VS, FS);
    int mvpLoc = GetShaderLocation(sh, "mvp");

    float quad[] = { -0.5f,-0.5f,0, 0.5f,-0.5f,0, 0.5f,0.5f,0,  -0.5f,-0.5f,0, 0.5f,0.5f,0, -0.5f,0.5f,0 };
    unsigned int vao = rlLoadVertexArray();
    rlEnableVertexArray(vao);
    unsigned int vbo = rlLoadVertexBuffer(quad, sizeof(quad), false);
    rlSetVertexAttribute(0, 3, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(0);
    rlDisableVertexArray();

    // NULL-init + mid-frame rlUpdateShaderBuffer = the real GPU-particle spawn pattern.
    // Guards the in-stream upload barrier (transfer write -> shader STORAGE read): with
    // attribute/index-only visibility this data stays stale and every instance is culled.
    struct P { float posSize[4]; float color[4]; } items[3] = {
        { {-2,0,0, 1.2f}, {1,0.5f,0.1f,1} },       // orange left
        { { 0,0,0, 1.2f}, {0.2f,1,0.3f,1} },       // green center
        { { 2,0,0, 1.2f}, {0.3f,0.4f,1,1} },       // blue right
    };
    unsigned int ssbo = rlLoadShaderBuffer(sizeof(items), NULL, RL_DYNAMIC_DRAW);

    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground((Color){0,0,60,255});
        rlUpdateShaderBuffer(ssbo, items, sizeof(items), 0);   // spawn-style mid-frame write
        BeginMode3D(cam);
            Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
            rlEnableShader(sh.id);
            rlSetUniformMatrix(mvpLoc, mvp);
            rlBindShaderBuffer(ssbo, 0);
            rlEnableVertexArray(vao);
            rlDrawVertexArrayInstanced(0, 6, 3);
            rlDisableVertexArray();
            rlDisableShader();
        EndMode3D();
        EndDrawing();
    }
    // fovy 45 at z=6: x = +/-2 -> center +/- 121 px (see sc_instanced)
    Image im = snap();
    Color l = at(im, W/2 - 121, H/2), m = at(im, W/2, H/2), r = at(im, W/2 + 121, H/2);
    UnloadImage(im);
    rlUnloadShaderBuffer(ssbo); rlUnloadVertexBuffer(vbo); rlUnloadVertexArray(vao); UnloadShader(sh);
    if (m.g < 150)             return "center instance missing: VS did not read the SSBO";
    if (l.r < 150 || r.b < 150) return "side instances missing/wrong color: gl_InstanceID indexing broken";
    return NULL;
}

static const char *sc_stress(void)
{
    Image gi = GenImageGradientRadial(32, 32, 0.0f, WHITE, BLACK);
    Texture2D tex = LoadTextureFromImage(gi); UnloadImage(gi);
    Camera3D cam = cam3d();
    for (int f = 0; f < 90; f++)
    {
        float t = f*0.016f;
        BeginDrawing(); ClearBackground((Color){8,8,16,255});
        BeginMode3D(cam);
            BeginBlendMode(BLEND_ADDITIVE); rlDisableDepthMask();
            for (int i = 0; i < 2000; i++)
            {
                float a = i*0.137f + t;
                Vector3 p = { cosf(a)*2.5f*((i%17)/17.0f), ((i%23)/23.0f - 0.5f)*3.0f, sinf(a)*2.5f*((i%13)/13.0f) };
                DrawBillboard(cam, tex, p, 0.2f, (Color){255,120,30,180});
            }
            rlEnableDepthMask(); EndBlendMode();
        EndMode3D();
        EndDrawing();
    }
    Image im = snap(); Color e = at(im, 4, 4); UnloadImage(im); UnloadTexture(tex);
    if (!near3(e, 8, 8, 16, 14)) return "frame corrupt after arena-flush stress";
    return NULL;   // surviving 90 heavy frames without device loss is the main assertion
}

// Real-Shading-P6 shadow projection convention probe. The game builds a light
// view-proj on the CPU (MatrixLookAt + MatrixOrtho), uploads it as a CUSTOM
// `uniform mat4 u_lightVP` via SetShaderValueMatrix, and a fragment shader
// projects a world point into shadow-map UV. THREE sessions of the game bug
// hunt could not settle whether the shader must use `M * v` or `v * M` to
// reproduce the CPU projection — because that depends on rlvk's SPIR-V mat4
// decoration for a reflected custom uniform, which the game can't show numer-
// ically. This does: it has the shader OUTPUT the projected UV as a color,
// reads it back, and compares BOTH multiply orders against the CPU formula
// that is known to match the captured shadow texels (env_shadow.c ProjectLS).
// The order whose readback matches the CPU is the correct one, full stop.
static void ls_cpu_proj(Matrix vp, Vector3 wp, float *ox, float *oy)
{
    float x = wp.x*vp.m0 + wp.y*vp.m4 + wp.z*vp.m8  + vp.m12;
    float y = wp.x*vp.m1 + wp.y*vp.m5 + wp.z*vp.m9  + vp.m13;
    float w = wp.x*vp.m3 + wp.y*vp.m7 + wp.z*vp.m11 + vp.m15;
    if (w == 0.0f) w = 1.0f;
    *ox = (x/w)*0.5f + 0.5f;
    *oy = (y/w)*0.5f + 0.5f;
}
static void ls_render_proj(const char *mulExpr, Matrix vp, Vector3 wp, float *rx, float *ry)
{
    char fs[640];
    snprintf(fs, sizeof(fs),
        "#version 330\n"
        "in vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
        "uniform mat4 u_lightVP; uniform vec3 uWorldPos;\n"
        "void main(){\n"
        "  vec4 p = %s;\n"
        "  vec3 proj = p.xyz / p.w * 0.5 + 0.5;\n"
        "  finalColor = vec4(clamp(proj.xy, 0.0, 1.0), 0.0, 1.0);\n"
        "}\n", mulExpr);
    Shader sh = LoadShaderFromMemory(NULL, fs);
    int locVP = GetShaderLocation(sh, "u_lightVP");
    int locWP = GetShaderLocation(sh, "uWorldPos");
    Image wi = GenImageColor(8, 8, WHITE); Texture2D white = LoadTextureFromImage(wi); UnloadImage(wi);
    for (int f = 0; f < 3; f++) {
        BeginDrawing(); ClearBackground(BLACK);
        BeginShaderMode(sh);
        SetShaderValueMatrix(sh, locVP, vp);                       // exactly the game's upload
        SetShaderValue(sh, locWP, &wp, SHADER_UNIFORM_VEC3);
        DrawTexturePro(white, (Rectangle){0,0,8,8}, (Rectangle){0,0,W,H}, (Vector2){0,0}, 0, WHITE);
        EndShaderMode();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2); UnloadImage(im);
    UnloadTexture(white); UnloadShader(sh);
    *rx = c.r / 255.0f; *ry = c.g / 255.0f;
}
static const char *sc_shadow_proj(void)
{
    // Replicate env_shadow.c's ComputeLightVP (verdant_path sun).
    Vector3 center = { 6.0f, 1.0f, 4.4f };
    Vector3 sun = Vector3Normalize((Vector3){ 0.5f, -0.7f, -0.3f });
    float dist = 18.0f + 6.0f;
    Vector3 lightPos = Vector3Subtract(center, Vector3Scale(sun, dist));
    Matrix view = MatrixLookAt(lightPos, center, (Vector3){0,1,0});
    float he = 18.0f + 2.0f;
    Matrix proj = MatrixOrtho(-he, he, -he, he, 0.1f, dist*2.0f);
    Matrix vp = MatrixMultiply(view, proj);

    // A world point well off the arena center, where a W/translation transpose
    // bug diverges most (the game's "drift ∝ distance from center" signature).
    Vector3 wp = { 14.0f, 0.8f, 9.0f };
    float cx, cy; ls_cpu_proj(vp, wp, &cx, &cy);
    float mx, my; ls_render_proj("u_lightVP * vec4(uWorldPos, 1.0)", vp, wp, &mx, &my);
    float vx, vy; ls_render_proj("vec4(uWorldPos, 1.0) * u_lightVP", vp, wp, &vx, &vy);

    float dM = fabsf(mx-cx) + fabsf(my-cy);
    float dV = fabsf(vx-cx) + fabsf(vy-cy);
    printf("  [shadow_proj] CPU=(%.3f,%.3f)  M*v=(%.3f,%.3f) dPix=%.3f  v*M=(%.3f,%.3f) dPix=%.3f\n",
           cx, cy, mx, my, dM, vx, vy, dV);
    const float TOL = 0.02f; // ~5 LSB of 8-bit readback
    bool mOK = dM < TOL, vOK = dV < TOL;
    if (mOK && !vOK) return NULL;                 // convention is M*v (game shaders should use it)
    if (vOK && !mOK) return "convention is v*M, not M*v (see printed deltas)";
    if (mOK && vOK)  return "AMBIGUOUS: both match (worldPos too central?)";
    return "NEITHER order matches CPU — capture/upload path differs (see deltas)";
}

// Real-Shading-P6 END-TO-END shadow test: does the whole chain actually put a
// DARK patch on the ground? shadow_proj only proved the projection UV; this
// reproduces the game faithfully — capture a real occluder from the light
// (exactly EnvShadow_BeginCapture's manual rlOrtho + rlMultMatrixf(view) dance),
// store NDC depth in a color RT (the game's copy result), then draw the ground
// as IMMEDIATE-MODE triangles bound to that RT via rlSetTexture (exactly
// default_arena + GroundShadow_Begin), sampling with M*v. Renders once with the
// shadow ON and once OFF and asserts a localized cluster of ground pixels
// darkened. RLVK_SHADOW_DUMP=path exports the ON frame as a PNG to eyeball.
// GAME-FAITHFUL light params: exactly env_shadow.c ComputeLightVP (ARENA_CENTER
// (6,1,4.4), halfExtent 20, dist 24), so this reproduces the real frustum SCALE
// — a character-sized caster in a 40 m box on a 2048 map, i.e. a tiny shadow.
static Vector3 g_center = { 6.0f, 1.0f, 4.4f };
static void ls_light(Matrix *view, Matrix *proj, Matrix *vp, float *he_out, float *dist_out)
{
    Vector3 sun = Vector3Normalize((Vector3){ 0.5f, -0.7f, -0.3f }); // verdant_path sun
    float dist = 18.0f + 6.0f;
    Vector3 lp = Vector3Subtract(g_center, Vector3Scale(sun, dist));
    *view = MatrixLookAt(lp, g_center, (Vector3){0,1,0});
    float he = 18.0f + 2.0f;                             // game halfExtent (whole arena)
    *proj = MatrixOrtho(-he, he, -he, he, 0.1f, dist*2.0f);
    *vp = MatrixMultiply(*view, *proj);
    *he_out = he; *dist_out = dist;
}
static const char *sc_shadow_cast(void)
{
    const int SM = 2048;                                 // game desktop resolution
    Matrix lview, lproj, lvp; float he, dist; ls_light(&lview, &lproj, &lvp, &he, &dist);

    // Capture: store the occluder's light-space NDC depth in R (game copy result).
    Shader depthSh = LoadShaderFromMemory(
        "#version 330\nin vec3 vertexPosition; uniform mat4 mvp;\n"
        "void main(){ gl_Position = mvp * vec4(vertexPosition,1.0); }\n",
        "#version 330\nout vec4 finalColor;\n"
        "void main(){ finalColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0); }\n");

    // Ground receiver: immediate-mode attribs (position+color), sample via M*v.
    Shader groundSh = LoadShaderFromMemory(
        "#version 330\nin vec3 vertexPosition; in vec4 vertexColor; uniform mat4 mvp;\n"
        "out vec4 fc; out vec3 fwp;\n"
        "void main(){ fc=vertexColor; fwp=vertexPosition; gl_Position=mvp*vec4(vertexPosition,1.0); }\n",
        "#version 330\nin vec4 fc; in vec3 fwp; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform mat4 u_lightVP; uniform float u_on; uniform float u_texel;\n"
        "float sf(vec3 wp){\n"
        "  vec4 p = u_lightVP * vec4(wp,1.0);\n"                 // M*v (shadow_proj-proven)
        "  vec3 pr = p.xyz/p.w*0.5+0.5;\n"
        "  if(pr.z>1.0||pr.x<0.0||pr.x>1.0||pr.y<0.0||pr.y>1.0) return 1.0;\n"
        "  float s=0.0;\n"
        "  for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++){\n"
        "    float d=texture(texture0, pr.xy+vec2(x,y)*u_texel).r;\n"
        "    s += (pr.z-0.002 > d)?0.0:1.0; }\n"
        "  return s/9.0; }\n"
        "void main(){ float sh = (u_on>0.5)? sf(fwp):1.0;\n"
        "  finalColor = vec4(fc.rgb*mix(0.30,1.0,sh), 1.0); }\n");
    int gLocVP = GetShaderLocation(groundSh, "u_lightVP");
    int gLocOn = GetShaderLocation(groundSh, "u_on");
    int gLocTx = GetShaderLocation(groundSh, "u_texel");

    RenderTexture2D sm = LoadRenderTexture(SM, SM);
    Vector3 occ = { 10.0f, 0.9f, 6.0f };                 // character-scale caster, off-center in the arena
    Camera3D cam = { 0 };
    cam.position=(Vector3){10,5,14}; cam.target=(Vector3){10,0.3f,6};
    cam.up=(Vector3){0,1,0}; cam.fovy=45.0f; cam.projection=CAMERA_PERSPECTIVE;
    float texel = 1.0f/(float)SM;

    Image imOn = {0}, imOff = {0};
    for (int pass = 0; pass < 2; pass++) {
        float on = (pass==0) ? 1.0f : 0.0f;
        for (int f = 0; f < 3; f++) {
            BeginDrawing(); ClearBackground((Color){40,44,70,255});

            // --- capture the occluder into the shadow map (light POV) ---
            BeginTextureMode(sm);
                ClearBackground(WHITE);                         // far = 1.0
                rlEnableDepthTest(); rlEnableDepthMask();
                rlMatrixMode(RL_PROJECTION); rlPushMatrix(); rlLoadIdentity();
                rlOrtho(-he, he, -he, he, 0.1, dist*2.0);
                rlMatrixMode(RL_MODELVIEW); rlPushMatrix(); rlLoadIdentity();
                rlMultMatrixf(MatrixToFloat(lview));
                BeginShaderMode(depthSh);
                    DrawCube(occ, 0.6f, 1.8f, 0.6f, WHITE); // character-scale caster
                EndShaderMode();
                rlDrawRenderBatchActive();
                rlMatrixMode(RL_PROJECTION); rlPopMatrix();
                rlMatrixMode(RL_MODELVIEW);  rlPopMatrix();
            EndTextureMode();

            // --- main view: draw the ground as immediate-mode tris, sampling the SM ---
            BeginMode3D(cam);
                BeginShaderMode(groundSh);
                    SetShaderValueMatrix(groundSh, gLocVP, lvp);
                    SetShaderValue(groundSh, gLocOn, &on,   SHADER_UNIFORM_FLOAT);
                    SetShaderValue(groundSh, gLocTx, &texel,SHADER_UNIFORM_FLOAT);
                    rlSetTexture(sm.texture.id);                // bind SM as texture0 (game path)
                    rlBegin(RL_TRIANGLES);
                        rlColor4ub(120,130,160,255);
                        // arena-covering ground quad at y=0 around the caster
                        float x0=g_center.x-20, x1=g_center.x+20, z0=g_center.z-20, z1=g_center.z+20;
                        rlVertex3f(x0,0,z0); rlVertex3f(x0,0,z1); rlVertex3f(x1,0,z1);
                        rlVertex3f(x0,0,z0); rlVertex3f(x1,0,z1); rlVertex3f(x1,0,z0);
                    rlEnd();
                    rlSetTexture(0);
                EndShaderMode();
            EndMode3D();
            EndDrawing();
        }
        Image im = snap();
        if (pass==0) imOn = im; else imOff = im;
    }
    if (getenv("RLVK_SHADOW_DUMP")) ExportImage(imOn, getenv("RLVK_SHADOW_DUMP"));

    // Count ground pixels that DARKENED when the shadow is on.
    int darker = 0, total = imOn.width*imOn.height;
    for (int i = 0; i < total; i++) {
        Color a = ((Color*)imOn.data)[i], b = ((Color*)imOff.data)[i];
        if ((int)b.r - (int)a.r > 18 && (int)b.g - (int)a.g > 18) darker++;
    }
    UnloadImage(imOn); UnloadImage(imOff);
    UnloadRenderTexture(sm); UnloadShader(depthSh); UnloadShader(groundSh);
    printf("  [shadow_cast] darkened ground pixels = %d / %d\n", darker, total);
    if (darker < 150)          return "NO shadow reaches the ground (sample/compare/bind broken)";
    if (darker > total*6/10)   return "whole ground darkened (shadow not localized)";
    return NULL;
}

// FAITHFUL reproduction of the GAME's exact capture path that shadow_cast
// skipped: render occluder depth into a real DEPTH ATTACHMENT (manual FBO, like
// env_shadow.c), then COPY depth->R32F through a fullscreen blit (the rlvk
// noSampledDepth twin path), then sample. Runs the copy blit with the Y-flip
// (game's negative-height DrawTextureRec) AND without, and reports which
// orientation actually lands the shadow — settling the "occluder stored at a
// different UV than the sample reads" orientation bug headlessly.
static int ls_darkcount(Image on, Image off)
{
    int d = 0, n = on.width*on.height;
    for (int i = 0; i < n; i++) {
        Color a = ((Color*)on.data)[i], b = ((Color*)off.data)[i];
        if ((int)b.r-(int)a.r > 18 && (int)b.g-(int)a.g > 18) d++;
    }
    return d;
}
static int sc_pipeline_run(int flip)   // returns darkened ground pixels
{
    const int SM = 2048;
    Matrix lview, lproj, lvp; float he, dist; ls_light(&lview, &lproj, &lvp, &he, &dist);

    // --- manual depth-attachment FBO, exactly env_shadow.c EnvShadow_Init ---
    unsigned int colTex = rlLoadTexture(NULL, SM, SM, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    unsigned int depTex = rlLoadTextureDepth(SM, SM, false);
    unsigned int fbo    = rlLoadFramebuffer();
    rlEnableFramebuffer(fbo);
    rlFramebufferAttach(fbo, colTex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(fbo, depTex, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferComplete(fbo);
    rlDisableFramebuffer();
    Texture2D depthTex2D = { depTex, SM, SM, 1, 19 };            // DEPTH_COMPONENT_24BIT, like env_shadow
    // R32F copy target — EXACTLY env_shadow.c (was RGBA8 LoadRenderTexture,
    // which masked the game bug: sampling an R32F color texture as texture0 in
    // an immediate-mode 3D draw is the suspect).
    RenderTexture2D copyRT = { 0 };
    copyRT.id = rlLoadFramebuffer();
    int cfmt = getenv("RLVK_DIAG_RGBA8") ? RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 : RL_PIXELFORMAT_UNCOMPRESSED_R32;
    copyRT.texture.id = rlLoadTexture(NULL, SM, SM, cfmt, 1);
    copyRT.texture.width = SM; copyRT.texture.height = SM;
    copyRT.texture.mipmaps = 1; copyRT.texture.format = cfmt;
    rlEnableFramebuffer(copyRT.id);
    rlFramebufferAttach(copyRT.id, copyRT.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferComplete(copyRT.id);
    rlDisableFramebuffer();
    SetTextureFilter(copyRT.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(copyRT.texture, TEXTURE_WRAP_CLAMP);
    Shader depthSh = LoadShaderFromMemory(
        "#version 330\nin vec3 vertexPosition; uniform mat4 mvp;\n"
        "void main(){ gl_Position = mvp*vec4(vertexPosition,1.0); }\n",
        "#version 330\nout vec4 c; void main(){ c=vec4(1.0); }\n"); // depth written by rasterizer
    Shader copySh = LoadShaderFromMemory(NULL,
        "#version 330\nin vec2 fragTexCoord; out vec4 c; uniform sampler2D texture0;\n"
        "void main(){ float d=texture(texture0,fragTexCoord).r; c=vec4(d,0.0,0.0,1.0); }\n");
    Shader groundSh = LoadShaderFromMemory(
        "#version 330\nin vec3 vertexPosition; in vec4 vertexColor; uniform mat4 mvp;\n"
        "out vec4 fc; out vec3 fwp;\n"
        "void main(){ fc=vertexColor; fwp=vertexPosition; gl_Position=mvp*vec4(vertexPosition,1.0);}\n",
        "#version 330\nin vec4 fc; in vec3 fwp; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform mat4 u_lightVP; uniform float u_on;\n"
        "void main(){ float sh=1.0;\n"
        "  if(u_on>0.5){ vec4 p=u_lightVP*vec4(fwp,1.0); vec3 pr=p.xyz/p.w*0.5+0.5;\n"
        "    if(!(pr.z>1.0||pr.x<0.0||pr.x>1.0||pr.y<0.0||pr.y>1.0)){\n"
        "      float d=texture(texture0,pr.xy).r; sh=(pr.z-0.002>d)?0.0:1.0; } }\n"
        "  finalColor=vec4(fc.rgb*mix(0.30,1.0,sh),1.0); }\n");
    int gVP=GetShaderLocation(groundSh,"u_lightVP"), gOn=GetShaderLocation(groundSh,"u_on");

    Vector3 occ = { 10.0f, 0.9f, 6.0f };
    Camera3D cam = {0};
    cam.position=(Vector3){10,5,14}; cam.target=(Vector3){10,0.3f,6};
    cam.up=(Vector3){0,1,0}; cam.fovy=45.0f; cam.projection=CAMERA_PERSPECTIVE;

    Image imOn={0}, imOff={0};
    for (int pass=0; pass<2; pass++) {
        float on=(pass==0)?1.0f:0.0f;
        for (int f=0; f<3; f++) {
            BeginDrawing(); ClearBackground((Color){40,44,70,255});
            // capture occluder depth into the depth attachment (env_shadow BeginCapture dance)
            rlEnableDepthTest(); rlEnableDepthMask();
            rlEnableFramebuffer(fbo); rlViewport(0,0,SM,SM); rlClearScreenBuffers();
            rlMatrixMode(RL_PROJECTION); rlPushMatrix(); rlLoadIdentity();
            rlOrtho(-he,he,-he,he,0.1,dist*2.0);
            rlMatrixMode(RL_MODELVIEW); rlPushMatrix(); rlLoadIdentity();
            rlMultMatrixf(MatrixToFloat(lview));
            BeginShaderMode(depthSh); DrawCube(occ,0.6f,1.8f,0.6f,WHITE); EndShaderMode();
            rlDrawRenderBatchActive();
            rlMatrixMode(RL_PROJECTION); rlPopMatrix();
            rlMatrixMode(RL_MODELVIEW);  rlPopMatrix();
            rlDisableFramebuffer();
            // copy depth -> R (with/without Y-flip)
            BeginTextureMode(copyRT);
                BeginShaderMode(copySh);
                float hh = flip ? -(float)SM : (float)SM;
                DrawTextureRec(depthTex2D, (Rectangle){0,0,(float)SM,hh}, (Vector2){0,0}, WHITE);
                EndShaderMode();
            EndTextureMode();
            rlViewport(0,0,GetScreenWidth(),GetScreenHeight());
            // main: draw ground sampling copyRT
            BeginMode3D(cam);
                // Pollute batch/texture state like a real game frame: default-shader
                // 3D draws (bind default white tex) + a shader switch BEFORE the ground.
                // The trigger: any prior 3D draw in the main render pass. In the game this is
                // all the other scene geometry drawn before the ground receiver.
                DrawCube((Vector3){occ.x-3,1.0f,occ.z}, 1.0f,2.0f,1.0f, (Color){80,80,90,255});
                DrawCube((Vector3){occ.x+3,1.0f,occ.z}, 1.0f,2.0f,1.0f, (Color){80,80,90,255});
                BeginShaderMode(groundSh);
                    SetShaderValueMatrix(groundSh,gVP,lvp);
                    SetShaderValue(groundSh,gOn,&on,SHADER_UNIFORM_FLOAT);
                    rlSetTexture(copyRT.texture.id);
                    rlBegin(RL_TRIANGLES); rlColor4ub(120,130,160,255);
                        float x0=g_center.x-20,x1=g_center.x+20,z0=g_center.z-20,z1=g_center.z+20;
                        rlVertex3f(x0,0,z0); rlVertex3f(x0,0,z1); rlVertex3f(x1,0,z1);
                        rlVertex3f(x0,0,z0); rlVertex3f(x1,0,z1); rlVertex3f(x1,0,z0);
                    rlEnd();
                    rlSetTexture(0);
                EndShaderMode();
            EndMode3D();
            EndDrawing();
        }
        Image im=snap(); if(pass==0) imOn=im; else imOff=im;
    }
    if (flip && getenv("RLVK_SHADOW_DUMP")) ExportImage(imOn, getenv("RLVK_SHADOW_DUMP"));
    if (flip && getenv("RLVK_SHADOW_RB")) {
        float *px = (float*)rlReadTexturePixels(copyRT.texture.id, SM, SM, RL_PIXELFORMAT_UNCOMPRESSED_R32);
        if (px) { float mn=9,mx=-9; int occ=0; for(int i=0;i<SM*SM;i++){float d=px[i]; if(d<mn)mn=d; if(d>mx)mx=d; if(d<0.99f)occ++;}
                  printf("  [rb] copyRT min=%.3f max=%.3f occluderTexels=%d\n",mn,mx,occ); RL_FREE(px); }
    }
    int darker = ls_darkcount(imOn, imOff);
    UnloadImage(imOn); UnloadImage(imOff);
    rlUnloadFramebuffer(copyRT.id); rlUnloadTexture(copyRT.texture.id);
    UnloadShader(depthSh); UnloadShader(copySh); UnloadShader(groundSh);
    rlUnloadFramebuffer(fbo); rlUnloadTexture(colTex); rlUnloadTexture(depTex);
    return darker;
}
static const char *sc_shadow_pipeline(void)
{
    int df = sc_pipeline_run(1);   // game's current Y-flip
    int dn = sc_pipeline_run(0);   // no flip
    printf("  [shadow_pipeline] darkened: flip(game)=%d  noflip=%d\n", df, dn);
    // Guards the §7.26 fix: DrawCube uses rlPushMatrix/rlPopMatrix (MODELVIEW). Because BeginMode3D
    // leaves a PROJECTION push outstanding, the old rlPopMatrix reset (gated on the SHARED
    // stackCounter==0) never fired for that balanced MODELVIEW push/pop, leaking
    // transformRequired=true + currentMatrix=&transform into the following custom-UBO ground draw
    // and corrupting its uniform delivery (u_lightVP) -> shadow projected off into the far/clear
    // region, sampling 1.0 = "no occluder". It looked like a dropped texture push (it is NOT: the
    // copyRT is bound, populated, SHADER_READ_ONLY). Fixed by tracking MODELVIEW push depth on its
    // own counter (rlvk_matrix.inl). Removing the two DrawCubes also makes it pass (no push/pop).
    if (df < 150 && dn < 150)  return "REGRESSION §7.26: rlPushMatrix/rlPopMatrix leaks transformRequired into the ground draw";
    if (df >= 150) return NULL;
    return "game's Y-FLIP copy misses; NO-FLIP lands the shadow -> remove the -height in env_shadow copy";
}

// Per-draw uniform changes under arena pressure (smoke-column class).
// A VFX that changes a uniform between instances forces one batch flush per instance; every flush
// snapshots the shader's UBO block into the per-frame bump arena. rlvkAppendUboWrites cannot drain
// the arena, so when it filled up it SKIPPED the push and the draw silently inherited the previous
// push - stale mvp AND stale uniforms, i.e. quads painted in another quad's color / another quad's
// place ("the picture got cut into rectangles and shuffled"). Fix: the call sites that CAN drain
// (batch flush, mesh draw) reserve the UBO block up front, so the skip path is unreachable.
// Detection is ADDITIVE on purpose: each stripe accumulates exactly 10 x 0.1 of its own channel,
// so a single stale push (which paints the pressure quads' black instead) leaves that stripe one
// increment short FOREVER - later correct draws cannot repair it. A last-draw-wins pixel check
// would only catch corruption in the final draw and pass by luck.
static const char *sc_ubo_arena(void)
{
    const char *FS = "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
        "uniform vec4 uColor;\n"
        // A fat (zero-filled, never set) uniform block widens the failure window on purpose: once
        // the arena has less than one block left, EVERY following flush skips its push while the
        // much smaller vertex payload still fits - dozens of stale draws in a row, not a lucky one.
        "uniform vec4 uPad[512];\n"
        "void main(){ finalColor = vec4((uColor.rgb + uPad[511].rgb) * 0.1, 1.0); }\n";
    Shader sh = LoadShaderFromMemory(NULL, FS);
    int loc = GetShaderLocation(sh, "uColor");

    const int STRIPES = 12, ROUNDS = 250;
    const int SW = W / STRIPES;
    Vector4 white = { 1.0f, 1.0f, 1.0f, 1.0f }; // the pressure color: it contaminates EVERY channel of a stripe that inherits it
    Vector4 col[12];
    for (int s = 0; s < STRIPES; s++) // one pure channel per stripe: contamination is unmistakable
        col[s] = (Vector4){ (s % 3 == 0) ? 1.0f : 0.0f, (s % 3 == 1) ? 1.0f : 0.0f, (s % 3 == 2) ? 1.0f : 0.0f, 1.0f };

    BeginDrawing();
    ClearBackground(BLACK);
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(sh);
    for (int r = 0; r < ROUNDS; r++)
    {
        // Pressure: one flush (= one UBO snapshot) per tiny black quad, until the arena is spent
        for (int i = 0; i < 50; i++)
        {
            rlDrawRenderBatchActive();
            SetShaderValue(sh, loc, &white, SHADER_UNIFORM_VEC4);
            DrawRectangle(0, H - 2, 2, 2, WHITE);
        }
        // Payload: one stripe per uniform change, at every arena offset the pressure loop leaves
        for (int s = 0; s < STRIPES; s++)
        {
            rlDrawRenderBatchActive();
            SetShaderValue(sh, loc, &col[s], SHADER_UNIFORM_VEC4);
            DrawRectangle(s * SW, 40, SW, H - 100, WHITE);
        }
    }
    EndShaderMode();
    EndBlendMode();
    EndDrawing();

    Image im = snap();
    const char *why = NULL;
    static char buf[160];
    for (int s = 0; s < STRIPES && !why; s++)
    {
        Color c = at(im, s * SW + SW / 2, H / 2);
        int own = (s % 3 == 0) ? c.r : (s % 3 == 1) ? c.g : c.b;
        int f1 = (s % 3 == 0) ? c.g : c.r;
        int f2 = (s % 3 == 2) ? c.g : c.b;
        if (own < 245 || f1 > 15 || f2 > 15)
        {
            snprintf(buf, sizeof(buf), "stripe %d is (%u,%u,%u): a UBO push was skipped, that draw ran on stale uniforms", s, c.r, c.g, c.b);
            why = buf;
        }
    }
    UnloadImage(im);
    UnloadShader(sh);
    return why;
}

// ---- runner ----------------------------------------------------------------------

typedef struct { const char *name; const char *(*fn)(void); } Scenario;
static const Scenario SCENARIOS[] = {
    { "clear",          sc_clear },
    { "batch_alpha",    sc_batch_alpha },
    { "additive3d",     sc_additive3d },
    { "shader_uniform", sc_shader_uniform },
    { "depth",          sc_depth },
    { "depth_rt",       sc_depth_rt },
    { "soft_depth",     sc_soft_depth },
    { "soft_ground",    sc_soft_ground },
    { "winding_rt",     sc_winding_rt },
    { "instanced",      sc_instanced },
    { "ssbo_vs",        sc_ssbo_vs },
    { "readback",       sc_readback },
    { "ui_after_rt",    sc_ui_after_rt },
    { "stress",         sc_stress },
    { "shadow_proj",    sc_shadow_proj },
    { "shadow_cast",    sc_shadow_cast },
    { "shadow_pipeline",sc_shadow_pipeline },
    { "ubo_arena",      sc_ubo_arena },
};
#define N_SCENARIOS (int)(sizeof(SCENARIOS)/sizeof(SCENARIOS[0]))

int main(int argc, char **argv)
{
    const char *only = (argc > 1) ? argv[1] : NULL;
    if (only && (strcmp(only, "--list") == 0))
    {
        for (int i = 0; i < N_SCENARIOS; i++) printf("%s\n", SCENARIOS[i].name);
        return 0;
    }
    SetTraceLogLevel(LOG_WARNING);      // keep output tiny: PASS/FAIL lines only
    InitWindow(W, H, "rlvk_visual_test");
    int fails = 0, ran = 0;
    for (int i = 0; i < N_SCENARIOS; i++)
    {
        if (only && (strcmp(only, SCENARIOS[i].name) != 0)) continue;
        const char *why = SCENARIOS[i].fn();
        ran++;
        if (why) { fails++; printf("FAIL %-14s %s\n", SCENARIOS[i].name, why); }
        else     printf("PASS %s\n", SCENARIOS[i].name);
        fflush(stdout);
    }
    CloseWindow();
    if (only && ran == 0) { printf("unknown scenario '%s' (use --list)\n", only); return 2; }
    printf("%d/%d passed\n", ran - fails, ran);
    return fails;
}
