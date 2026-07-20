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
//   shadow_ortho   manual ortho depth-capture FBO + R32F copy + custom mat4 uniform
//                  sample, mirroring environment/env_shadow.c (shadow-map class)
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

// Shadow-map ortho capture + R32F depth-copy + custom-mat4 sample, mirroring
// environment/env_shadow.c's exact pipeline (manual depth FBO, rlOrtho
// capture projection, shadow_copy.fs-style passthrough, u_lightVP sampled in
// a receiver shader). The occluder and both test points are placed AWAY
// from the world origin and from the light's own position, with a tilted
// (non-degenerate) light direction — this is deliberate: it's exactly the
// shape of scene that exposed the REAL_SHADING_P6_NOTES.md "shadow position/
// size drifts with distance from arena center" bug, where `vec*mat` (i.e.
// transpose(u_lightVP)*v) only coincidentally matched `mat*vec` near the
// light's aim point and diverged elsewhere. A regression back to vec*mat
// would flip point A from shadowed to lit (or corrupt its projected UV
// enough to sample the wrong texel), making this scenario fail.
static const char *sc_shadow_ortho(void)
{
    const int SZ = 256;
    Vector3 AIM = { 5.0f, 0.0f, -4.0f };            // off-origin "arena center" analog
    Vector3 lightDir = Vector3Normalize((Vector3){ 0.3f, -0.8f, 0.5f }); // tilted, non-degenerate
    float distance = 20.0f, halfExtent = 6.0f;
    Vector3 lightPos = Vector3Subtract(AIM, Vector3Scale(lightDir, distance));
    Matrix lightView = MatrixLookAt(lightPos, AIM, (Vector3){0,1,0});
    Matrix lightProj = MatrixOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent, 0.1, distance*2.0);
    Matrix lightVP = MatrixMultiply(lightView, lightProj); // == env_shadow.c's ComputeLightVP()

    // Point A: directly under the occluder cube -> must end up SHADOWED.
    // Point B: off to the side, still in-frustum, nothing above it -> must stay LIT.
    Vector3 pointA = AIM;
    Vector3 pointB = Vector3Add(AIM, (Vector3){ 4.0f, 0.0f, 4.0f });

    // ---- depth FBO (color+depth attachment, matches env_shadow.c/screen_distort.c) ----
    unsigned int colorTex = rlLoadTexture(NULL, SZ, SZ, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    unsigned int depthTex = rlLoadTextureDepth(SZ, SZ, false);
    unsigned int fbo = rlLoadFramebuffer();
    rlEnableFramebuffer(fbo);
    rlFramebufferAttach(fbo, colorTex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(fbo, depthTex, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(fbo);
    rlDisableFramebuffer();
    if (!complete) return "depth FBO incomplete";

    const char *DEPTH_VS = "#version 330\nin vec3 vertexPosition; uniform mat4 mvp;\n"
        "void main(){ gl_Position = mvp*vec4(vertexPosition,1.0); }\n";
    const char *DEPTH_FS = "#version 330\nout vec4 finalColor;\nvoid main(){ finalColor=vec4(1.0); }\n";
    Shader depthSh = LoadShaderFromMemory(DEPTH_VS, DEPTH_FS);

    // ---- depth -> R32F copy target (texture0 route, same as shadow_copy.fs) ----
    Texture2D depthTex2D = { depthTex, SZ, SZ, 1, 19 };
    RenderTexture2D copyRT = { 0 };
    copyRT.id = rlLoadFramebuffer();
    copyRT.texture.id = rlLoadTexture(NULL, SZ, SZ, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    copyRT.texture.width = SZ; copyRT.texture.height = SZ; copyRT.texture.mipmaps = 1;
    copyRT.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R32;
    rlEnableFramebuffer(copyRT.id);
    rlFramebufferAttach(copyRT.id, copyRT.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    bool copyComplete = rlFramebufferComplete(copyRT.id);
    rlDisableFramebuffer();
    if (!copyComplete) return "R32F copy FBO incomplete";
    SetTextureFilter(copyRT.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(copyRT.texture, TEXTURE_WRAP_CLAMP);

    const char *COPY_FS = "#version 330\nin vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform sampler2D texture0;\nvoid main(){ finalColor = vec4(texture(texture0, fragTexCoord).r,0,0,1); }\n";
    Shader copySh = LoadShaderFromMemory(NULL, COPY_FS);

    // ---- receiver: mat*vec against u_lightVP, sampled at points A and B ----
    const char *RECV_FS = "#version 330\nin vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform mat4 u_lightVP; uniform vec3 uWorldPos;\n"
        "void main(){\n"
        "  vec4 posLS = u_lightVP * vec4(uWorldPos, 1.0);\n"
        "  vec3 proj = posLS.xyz/posLS.w * 0.5 + 0.5;\n"
        "  if (proj.x<0.0||proj.x>1.0||proj.y<0.0||proj.y>1.0||proj.z>1.0) { finalColor=vec4(0,1,0,1); return; }\n"
        "  float d = texture(texture0, proj.xy).r;\n"
        "  float shadowed = (proj.z - 0.002 > d) ? 1.0 : 0.0;\n"
        "  finalColor = vec4(shadowed, 1.0-shadowed, 0.0, 1.0); }\n";
    Shader recvSh = LoadShaderFromMemory(NULL, RECV_FS);
    int locVP = GetShaderLocation(recvSh, "u_lightVP");
    int locWP = GetShaderLocation(recvSh, "uWorldPos");

    // Everything (capture FBO, copy pass, receiver draw) happens INSIDE one
    // BeginDrawing/EndDrawing per frame — mirrors both sc_depth_rt here and
    // the real game (EnvShadow_BeginCapture/EndCapture run as a pre-pass
    // inside the same frame as MyBeginMode3D, never straddling a present).
    //
    // MODELVIEW is set with a direct rlLoadIdentity()+rlMultMatrixf(), NOT
    // rlPushMatrix()/rlPopMatrix() — bisected finding (2026-07-20): under
    // rlvk, bracketing a MODELVIEW change with rlPushMatrix()/rlPopMatrix()
    // while a non-swapchain framebuffer is bound corrupts a LATER, unrelated
    // DrawMesh/DrawMeshInstanced call elsewhere in the same process (repro'd
    // down to zero draws between the push and pop — this scenario originally
    // broke `instanced`/`ui_after_rt` purely from the push/pop bracket, with
    // no cube ever drawn). Root cause not fully isolated (needs RenderDoc/
    // Xcode GPU capture per HANDOFF §6); direct rlLoadIdentity() (matching
    // this scenario's own reset afterward, no stack push at all) reproducibly
    // avoids it and is what environment/env_shadow.c now does too. The
    // PROJECTION stack's push/pop (rlOrtho) is unaffected — bisected clean.
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLUE);

        rlDrawRenderBatchActive();
        rlEnableDepthTest(); rlEnableDepthMask();
        rlEnableFramebuffer(fbo);
        rlViewport(0, 0, SZ, SZ);
        rlClearScreenBuffers();
        rlMatrixMode(RL_PROJECTION); rlPushMatrix(); rlLoadIdentity();
        rlOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent, 0.1, distance*2.0);
        rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
        rlMultMatrixf(MatrixToFloat(lightView));

        BeginShaderMode(depthSh);
        DrawCube(Vector3Add(AIM, (Vector3){0,1.0f,0}), 1.6f, 1.6f, 1.6f, WHITE); // occluder over point A only
        EndShaderMode();
        rlDrawRenderBatchActive();
        rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
        rlMatrixMode(RL_PROJECTION); rlPopMatrix();
        rlDisableFramebuffer();
        rlViewport(0, 0, GetScreenWidth(), GetScreenHeight());

        BeginTextureMode(copyRT);
        BeginShaderMode(copySh);
        DrawTextureRec(depthTex2D, (Rectangle){0,0,(float)SZ,-(float)SZ}, (Vector2){0,0}, WHITE);
        EndShaderMode();
        EndTextureMode();

        // Draw the shadow-copy texture itself as the quad (DrawTexturePro
        // binds its own argument as texture0 internally, so no manual
        // rlSetTexture juggling is needed here).
        BeginShaderMode(recvSh);
        SetShaderValueMatrix(recvSh, locVP, lightVP);
        SetShaderValue(recvSh, locWP, &pointA, SHADER_UNIFORM_VEC3);
        DrawTexturePro(copyRT.texture, (Rectangle){0,0,(float)SZ,(float)SZ},
                        (Rectangle){W/4.0f-40,H/2.0f-40,80,80}, (Vector2){0,0}, 0, WHITE);
        rlDrawRenderBatchActive();
        SetShaderValue(recvSh, locWP, &pointB, SHADER_UNIFORM_VEC3);
        DrawTexturePro(copyRT.texture, (Rectangle){0,0,(float)SZ,(float)SZ},
                        (Rectangle){3*W/4.0f-40,H/2.0f-40,80,80}, (Vector2){0,0}, 0, WHITE);
        rlDrawRenderBatchActive();
        EndShaderMode();

        EndDrawing();
    }

    Image im = snap();
    Color a = at(im, W/4, H/2), b = at(im, 3*W/4, H/2);
    UnloadImage(im); UnloadShader(recvSh); UnloadShader(copySh); UnloadShader(depthSh);
    UnloadRenderTexture(copyRT);
    rlUnloadFramebuffer(fbo); rlUnloadTexture(colorTex); rlUnloadTexture(depthTex);
    if (a.g > 100) return "point A (under occluder) not shadowed: matrix/capture regression";
    if (b.r > 100) return "point B (open ground) falsely shadowed";
    return NULL;
}

// Reproduces maps/toolkit/ground_shadow.c's exact technique: a CUSTOM VS (only
// vertexPosition+vertexColor, no texcoord — matching ground_shadow.vs) + CUSTOM FS
// sampling `texture0` at a FIXED uv (matching ground_shadow.fs's light-space-projected
// coordinate, not a mesh UV), bound via `rlSetTexture()` immediately before a raw
// `rlBegin(RL_TRIANGLES)` immediate-mode draw — NOT DrawTexturePro/DrawMesh. A PRECEDING
// unrelated RL_TRIANGLES draw (default texture) runs first so `rlBegin()`'s mode-change
// texture-capture branch is already "warm" when the real draw happens, matching the game
// (floor plate draws after zone-disc/other geometry, all RL_TRIANGLES). Real-world context:
// REAL_SHADING_P6_NOTES.md session-4 part 5 — the ground_shadow shader's raw-visualized
// texture0 sample came back uniform white/unbound in-game despite `rlSetTexture` being
// called right before the draw, exactly this scenario's shape.
static const char *sc_custom_vs_texture(void)
{
    Image mi = GenImageColor(4, 4, (Color){255, 0, 255, 255}); // distinctive magenta, unused elsewhere
    Texture2D marker = LoadTextureFromImage(mi); UnloadImage(mi);

    const char *VS = "#version 330\nin vec3 vertexPosition; in vec4 vertexColor;\n"
        "uniform mat4 mvp; out vec4 fragColor;\n"
        "void main(){ fragColor = vertexColor; gl_Position = mvp*vec4(vertexPosition,1.0); }\n";
    const char *FS = "#version 330\nin vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0;\n"
        "void main(){ finalColor = texture(texture0, vec2(0.5, 0.5)); }\n"; // fixed uv, like ground_shadow.fs's projected coord
    Shader sh = LoadShaderFromMemory(VS, FS);

    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginMode3D(cam);
            // Preceding unrelated RL_TRIANGLES draw at the default texture — puts the batch's
            // current draw-call entry into "RL_TRIANGLES, default tex" before the real draw,
            // same as the game's prior floor/prop draws.
            rlSetTexture(0);
            rlBegin(RL_TRIANGLES);
                rlColor4ub(80,80,80,255);
                rlVertex3f(-3,-2,1); rlVertex3f(3,-2,1); rlVertex3f(0,-1.5f,1);
            rlEnd();

            BeginShaderMode(sh);
            rlSetTexture(marker.id); // the exact binding route ground_shadow.c uses
            rlBegin(RL_TRIANGLES);
                rlColor4ub(255,255,255,255);
                rlVertex3f(-2,-1.5f,0); rlVertex3f(2,-1.5f,0); rlVertex3f(0,2,0);
            rlEnd();
            rlSetTexture(0);
            EndShaderMode();
        EndMode3D();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2);
    UnloadImage(im); UnloadTexture(marker); UnloadShader(sh);
    if (!near3(c, 255, 0, 255, 20))
        return "texture0 not bound for custom-VS immediate-mode draw (read as unbound/default)";
    return NULL;
}

// Same as sc_custom_vs_texture, but the sampled texture is RENDERED TO earlier in the SAME
// frame (BeginTextureMode/EndTextureMode fill) instead of a static LoadTextureFromImage —
// matching env_shadow.c's real timing: EnvShadow_BeginCapture/EndCapture (a render-to-texture
// pass) runs as a pre-pass BEFORE the main 3D draw, then ground_shadow.fs samples that same
// texture later in the SAME frame via rlSetTexture + immediate mode. sc_custom_vs_texture
// alone passed (static texture, never rendered-to) — this isolates whether the render-to-
// texture step is what breaks the later immediate-mode sample.
static const char *sc_render_then_immediate_sample(void)
{
    RenderTexture2D rt = LoadRenderTexture(8, 8);
    BeginTextureMode(rt); ClearBackground((Color){255, 0, 255, 255}); EndTextureMode();

    const char *VS = "#version 330\nin vec3 vertexPosition; in vec4 vertexColor;\n"
        "uniform mat4 mvp; out vec4 fragColor;\n"
        "void main(){ fragColor = vertexColor; gl_Position = mvp*vec4(vertexPosition,1.0); }\n";
    const char *FS = "#version 330\nin vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0;\n"
        "void main(){ finalColor = texture(texture0, vec2(0.5, 0.5)); }\n";
    Shader sh = LoadShaderFromMemory(VS, FS);

    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginMode3D(cam);
            rlSetTexture(0);
            rlBegin(RL_TRIANGLES);
                rlColor4ub(80,80,80,255);
                rlVertex3f(-3,-2,1); rlVertex3f(3,-2,1); rlVertex3f(0,-1.5f,1);
            rlEnd();

            BeginShaderMode(sh);
            rlSetTexture(rt.texture.id);
            rlBegin(RL_TRIANGLES);
                rlColor4ub(255,255,255,255);
                rlVertex3f(-2,-1.5f,0); rlVertex3f(2,-1.5f,0); rlVertex3f(0,2,0);
            rlEnd();
            rlSetTexture(0);
            EndShaderMode();
        EndMode3D();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2);
    UnloadImage(im); UnloadRenderTexture(rt); UnloadShader(sh);
    if (!near3(c, 255, 0, 255, 20))
        return "texture0 not bound after render-to-texture + custom-VS immediate-mode sample";
    return NULL;
}

// Same idea as sc_render_then_immediate_sample, but the sampled texture is built EXACTLY like
// env_shadow.c's s_copyRT: a manually-constructed R32F color-only framebuffer (rlLoadFramebuffer
// + rlLoadTexture(..., RL_PIXELFORMAT_UNCOMPRESSED_R32, ...), NOT LoadRenderTexture), filled by a
// copy-style shader pass (not a plain ClearBackground), with POINT filter + CLAMP wrap set
// afterward (R32F isn't linear-filterable on many GL3.3 drivers — same reasoning as
// screen_distort.c/env_shadow.c). Isolates whether the single-channel R32F format specifically
// is what breaks the later custom-VS immediate-mode `.r` sample, since sc_render_then_immediate_
// sample (a normal RGBA8 RenderTexture2D) passed clean.
static const char *sc_r32f_immediate_sample(void)
{
    const int SZ = 8;
    unsigned int copyFbo = rlLoadFramebuffer();
    RenderTexture2D copyRT = { 0 };
    copyRT.id = copyFbo;
    copyRT.texture.id = rlLoadTexture(NULL, SZ, SZ, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    copyRT.texture.width = SZ; copyRT.texture.height = SZ; copyRT.texture.mipmaps = 1;
    copyRT.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R32;
    rlEnableFramebuffer(copyFbo);
    rlFramebufferAttach(copyFbo, copyRT.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(copyFbo);
    rlDisableFramebuffer();
    if (!complete) return "R32F FBO incomplete";
    SetTextureFilter(copyRT.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(copyRT.texture, TEXTURE_WRAP_CLAMP);

    // Fill it to a known value (0.777) via a fullscreen-quad copy-style shader pass, exactly
    // shadow_copy.fs's shape (a constant instead of sampling another texture — simpler, same
    // write path).
    const char *FILL_FS = "#version 330\nout vec4 finalColor;\nvoid main(){ finalColor = vec4(0.777, 0.0, 0.0, 1.0); }\n";
    Shader fillSh = LoadShaderFromMemory(NULL, FILL_FS);
    Image wi = GenImageColor(4, 4, WHITE); Texture2D white = LoadTextureFromImage(wi); UnloadImage(wi);
    BeginTextureMode(copyRT);
    BeginShaderMode(fillSh);
    DrawTexturePro(white, (Rectangle){0,0,4,4}, (Rectangle){0,0,(float)SZ,(float)SZ}, (Vector2){0,0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
    UnloadTexture(white); UnloadShader(fillSh);

    const char *VS = "#version 330\nin vec3 vertexPosition; in vec4 vertexColor;\n"
        "uniform mat4 mvp; out vec4 fragColor;\n"
        "void main(){ fragColor = vertexColor; gl_Position = mvp*vec4(vertexPosition,1.0); }\n";
    const char *FS = "#version 330\nin vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0;\n"
        "void main(){ float d = texture(texture0, vec2(0.5, 0.5)).r; finalColor = vec4(d, d, d, 1.0); }\n";
    Shader sh = LoadShaderFromMemory(VS, FS);

    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginMode3D(cam);
            rlSetTexture(0);
            rlBegin(RL_TRIANGLES);
                rlColor4ub(80,80,80,255);
                rlVertex3f(-3,-2,1); rlVertex3f(3,-2,1); rlVertex3f(0,-1.5f,1);
            rlEnd();

            BeginShaderMode(sh);
            rlSetTexture(copyRT.texture.id);
            rlBegin(RL_TRIANGLES);
                rlColor4ub(255,255,255,255);
                rlVertex3f(-2,-1.5f,0); rlVertex3f(2,-1.5f,0); rlVertex3f(0,2,0);
            rlEnd();
            rlSetTexture(0);
            EndShaderMode();
        EndMode3D();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2);
    UnloadImage(im); UnloadRenderTexture(copyRT); UnloadShader(sh);
    // 0.777*255 ~= 198
    if (!near3(c, 198, 198, 198, 20))
        return "R32F texture0 not sampled correctly for custom-VS immediate-mode draw";
    return NULL;
}

// Adds the ONE structural piece the earlier three synthetic scenarios (custom_vs_texture,
// render_then_immediate_sample, r32f_immediate_sample) were all missing: main.c wraps the ENTIRE
// main 3D scene — including the ground/GroundShadow draw — in `ScreenDistort_Begin()`, which is
// just `BeginTextureMode(renderTex)` (a SEPARATE offscreen render target, composited to the
// screen afterward). So `GroundShadow_Begin()`'s `rlSetTexture(shadowMap)` runs while a
// DIFFERENT, unrelated FBO (the main scene's own render target) is the ACTIVE render target —
// sampling FBO A's texture while FBO B is bound for writing. None of the earlier scenarios
// nested a second FBO around the sampling draw; this one does, mirroring env_shadow.c's capture
// (own FBO) -> ScreenDistort_Begin (a second, different FBO) -> ground draw (samples the first
// FBO's texture while the second is active) -> composite chain exactly.
static const char *sc_nested_rt_sample(void)
{
    const int SZ = 8;
    unsigned int copyFbo = rlLoadFramebuffer();
    RenderTexture2D copyRT = { 0 };
    copyRT.id = copyFbo;
    copyRT.texture.id = rlLoadTexture(NULL, SZ, SZ, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    copyRT.texture.width = SZ; copyRT.texture.height = SZ; copyRT.texture.mipmaps = 1;
    copyRT.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R32;
    rlEnableFramebuffer(copyFbo);
    rlFramebufferAttach(copyFbo, copyRT.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    bool complete = rlFramebufferComplete(copyFbo);
    rlDisableFramebuffer();
    if (!complete) return "R32F FBO incomplete";
    SetTextureFilter(copyRT.texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(copyRT.texture, TEXTURE_WRAP_CLAMP);

    const char *FILL_FS = "#version 330\nout vec4 finalColor;\nvoid main(){ finalColor = vec4(0.777, 0.0, 0.0, 1.0); }\n";
    Shader fillSh = LoadShaderFromMemory(NULL, FILL_FS);
    Image wi = GenImageColor(4, 4, WHITE); Texture2D white = LoadTextureFromImage(wi); UnloadImage(wi);
    BeginTextureMode(copyRT);
    BeginShaderMode(fillSh);
    DrawTexturePro(white, (Rectangle){0,0,4,4}, (Rectangle){0,0,(float)SZ,(float)SZ}, (Vector2){0,0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
    UnloadTexture(white); UnloadShader(fillSh);

    const char *VS = "#version 330\nin vec3 vertexPosition; in vec4 vertexColor;\n"
        "uniform mat4 mvp; out vec4 fragColor;\n"
        "void main(){ fragColor = vertexColor; gl_Position = mvp*vec4(vertexPosition,1.0); }\n";
    const char *FS = "#version 330\nin vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0;\n"
        "void main(){ float d = texture(texture0, vec2(0.5, 0.5)).r; finalColor = vec4(d, d, d, 1.0); }\n";
    Shader sh = LoadShaderFromMemory(VS, FS);

    RenderTexture2D sceneRT = LoadRenderTexture(W, H); // stands in for main.c's ScreenDistort renderTex
    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);

        BeginTextureMode(sceneRT); // == ScreenDistort_Begin()
        ClearBackground((Color){10,10,10,255});
        BeginMode3D(cam);
            rlSetTexture(0);
            rlBegin(RL_TRIANGLES);
                rlColor4ub(80,80,80,255);
                rlVertex3f(-3,-2,1); rlVertex3f(3,-2,1); rlVertex3f(0,-1.5f,1);
            rlEnd();

            BeginShaderMode(sh);
            rlSetTexture(copyRT.texture.id); // == GroundShadow_Begin()'s rlSetTexture(map.id)
            rlBegin(RL_TRIANGLES);
                rlColor4ub(255,255,255,255);
                rlVertex3f(-2,-1.5f,0); rlVertex3f(2,-1.5f,0); rlVertex3f(0,2,0);
            rlEnd();
            rlSetTexture(0);
            EndShaderMode();
        EndMode3D();
        EndTextureMode(); // == ScreenDistort_End()

        DrawTextureRec(sceneRT.texture, (Rectangle){0,0,W,-H}, (Vector2){0,0}, WHITE); // composite
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2);
    UnloadImage(im); UnloadRenderTexture(copyRT); UnloadRenderTexture(sceneRT); UnloadShader(sh);
    if (!near3(c, 198, 198, 198, 20))
        return "texture0 not sampled correctly when the sampling draw is nested inside a second FBO scope";
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
    { "shadow_ortho",   sc_shadow_ortho },
    { "custom_vs_texture", sc_custom_vs_texture },
    { "render_then_immediate_sample", sc_render_then_immediate_sample },
    { "r32f_immediate_sample", sc_r32f_immediate_sample },
    { "nested_rt_sample", sc_nested_rt_sample },
    { "winding_rt",     sc_winding_rt },
    { "instanced",      sc_instanced },
    { "ssbo_vs",        sc_ssbo_vs },
    { "readback",       sc_readback },
    { "ui_after_rt",    sc_ui_after_rt },
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
