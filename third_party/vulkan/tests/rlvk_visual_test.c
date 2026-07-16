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

    struct P { float posSize[4]; float color[4]; } items[3] = {
        { {-2,0,0, 1.2f}, {1,0.5f,0.1f,1} },       // orange left
        { { 0,0,0, 1.2f}, {0.2f,1,0.3f,1} },       // green center
        { { 2,0,0, 1.2f}, {0.3f,0.4f,1,1} },       // blue right
    };
    unsigned int ssbo = rlLoadShaderBuffer(sizeof(items), items, RL_DYNAMIC_COPY);

    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground((Color){0,0,60,255});
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

// ---- runner ----------------------------------------------------------------------

typedef struct { const char *name; const char *(*fn)(void); } Scenario;
static const Scenario SCENARIOS[] = {
    { "clear",          sc_clear },
    { "batch_alpha",    sc_batch_alpha },
    { "additive3d",     sc_additive3d },
    { "shader_uniform", sc_shader_uniform },
    { "depth",          sc_depth },
    { "depth_rt",       sc_depth_rt },
    { "winding_rt",     sc_winding_rt },
    { "instanced",      sc_instanced },
    { "ssbo_vs",        sc_ssbo_vs },
    { "readback",       sc_readback },
    { "stress",         sc_stress },
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
