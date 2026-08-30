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
//   depth_mask_clear depth-mask-off ClearBackground preserves shared RT depth (VFX layer path)
//   fbo_switch     FBO-to-FBO scope switch makes the outgoing colour sampleable
//   winding_rt     front-face triangle visible on screen AND inside a render texture
//                  (flip-Y winding/culling class - mesh see-through)
//   gas_projection raymarched world point survives RT display inversion and prior
//                  MODELVIEW push/pop pollution at the GetWorldToScreen position
//   instanced      DrawMeshInstanced + instanceTransform attribute
//   imm_normal     immediate-mode rlNormal3f round-trip + what matModel really is
//                  inside MyBeginMode3D (volume-tube |N.V| inversion class)
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
#include <stdint.h>
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

static float halfToFloat(uint16_t h)
{
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t exp = ((uint32_t)h >> 10) & 0x1Fu;
    uint32_t mant = (uint32_t)h & 0x03FFu;
    uint32_t bits;
    if (exp == 0)
    {
        if (mant == 0) bits = sign;
        else
        {
            exp = 1;
            while ((mant & 0x0400u) == 0) { mant <<= 1; exp--; }
            mant &= 0x03FFu;
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    }
    else if (exp == 0x1Fu)
        bits = sign | 0x7F800000u | (mant << 13);
    else
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

// Only ever fed values in [0,1] (an 8-bit readback widened for the shared metrics), so
// the subnormal/overflow branches a general converter needs are deliberately absent.
static uint16_t floatToHalf(float f)
{
    uint32_t b; memcpy(&b, &f, sizeof(b));
    uint32_t sign = (b >> 16) & 0x8000u;
    int exp = (int)((b >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = (b >> 13) & 0x03FFu;
    if (exp <= 0) return (uint16_t)sign;              /* flush to zero */
    if (exp >= 0x1F) return (uint16_t)(sign | 0x7C00u);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | mant);
}

static void sampleHalfRGBA(const uint16_t *pixels, int width, int x, int y, float out[4])
{
    size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4u;
    for (int c = 0; c < 4; c++) out[c] = halfToFloat(pixels[i + (size_t)c]);
}

// docs/BRIGHT_BACKGROUND_VFX_SPEC.md 8.2 metric primitives. rgbDistance is the
// MAX-ABS-CHANNEL distance the spec defines, not a Euclidean one - the first
// version of bright_vfx used sqrt() here, which silently changed what its 0.10
// threshold meant.
static float rgbDistanceMax(const float a[3], const float b[3])
{
    float d = 0.0f;
    for (int c = 0; c < 3; c++) { float v = fabsf(a[c] - b[c]); if (v > d) d = v; }
    return d;
}
static float chromaOf(const float a[3])
{
    float hi = a[0], lo = a[0];
    for (int c = 1; c < 3; c++) { if (a[c] > hi) hi = a[c]; if (a[c] < lo) lo = a[c]; }
    return hi - lo;
}
static float lumaOf(const float a[3]) { return 0.2126f*a[0] + 0.7152f*a[1] + 0.0722f*a[2]; }

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

// shaderc may assign texture0 to a non-zero descriptor binding once another
// sampler exists. The draw-call texture must still land in texture0, while the
// explicitly bound second sampler remains independent (soft-particle contract).
static const char *sc_sampler_pair(void)
{
    const char *FS = "#version 330\n"
        "in vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform sampler2D u_cameraDepthTex;\n"
        "void main(){ vec4 a=texture(texture0,fragTexCoord); vec4 b=texture(u_cameraDepthTex,fragTexCoord); finalColor=vec4(a.r,b.g,0.0,1.0); }\n";
    Shader sh = LoadShaderFromMemory(NULL, FS);
    Image ri = GenImageColor(8, 8, RED), gi = GenImageColor(8, 8, GREEN);
    Texture2D red = LoadTextureFromImage(ri), green = LoadTextureFromImage(gi);
    UnloadImage(ri); UnloadImage(gi);
    SetShaderValueTexture(sh, GetShaderLocation(sh, "u_cameraDepthTex"), green);
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginShaderMode(sh);
            DrawTexturePro(red, (Rectangle){0,0,8,8}, (Rectangle){W/2-60,H/2-60,120,120}, (Vector2){0,0}, 0, WHITE);
        EndShaderMode();
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2);
    UnloadImage(im); UnloadTexture(red); UnloadTexture(green); UnloadShader(sh);
    return (c.r > 200 && c.g > 90 && c.b < 30) ? NULL : "two samplers crossed: texture0 or soft-depth sampler is unbound";
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

// MSAA on an OFFSCREEN render target (rlvkSetFramebufferSamples). FLAG_MSAA_4X_HINT /
// rlvkSetMsaaSamples only reach the swapchain, so an engine that rasterizes its scene into an
// HDR render texture and composites it has binary coverage on every silhouette no matter what
// the window was configured with. Measured in the game before this existed: a 64/255 luma jump
// across ONE pixel at a mesh edge, with a smooth bloom ramp either side of it.
//
// Two independent things are asserted, because the colour half can work while the depth half is
// silently dead:
//   1. COVERAGE — a flat-white sphere on black must produce partially-covered pixels along its
//      silhouette. At 1 sample there are none at all (that is the bug), so this half is red
//      before the fix by construction.
//   2. DEPTH RESOLVE — the attached 1x depth texture must still read back real scene depth
//      afterwards. The multisample depth is unreadable by anything downstream (vkCmdResolveImage
//      is colour-only, vkCmdCopyImage rejects samples > 1), so without the
//      VK_KHR_depth_stencil_resolve subpass resolve the soft-particle depth snapshot gets
//      garbage — the failure most likely to ship unnoticed, since nothing about the picture
//      changes until a soft particle meets geometry.
extern int rlvkSetFramebufferSamples(unsigned int fbId, int samples);
static const char *sc_msaa_rt(void)
{
    const char *DEPTH_FS =
        "#version 330\n"
        "in vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
        "uniform float uNear; uniform float uFar;\n"
        "void main(){\n"
        "  float ndc = texture(texture0, fragTexCoord).r * 2.0 - 1.0;\n"
        "  float lin = (2.0*uNear*uFar) / (uFar + uNear - ndc*(uFar - uNear));\n"
        "  float v = clamp(lin/20.0, 0.0, 1.0);\n"
        "  finalColor = vec4(v, v, v, 1.0); }\n";
    Shader dsh = LoadShaderFromMemory(NULL, DEPTH_FS);
    float nearV = 0.01f, farV = 1000.0f;
    SetShaderValue(dsh, GetShaderLocation(dsh, "uNear"), &nearV, SHADER_UNIFORM_FLOAT);
    SetShaderValue(dsh, GetShaderLocation(dsh, "uFar"), &farV, SHADER_UNIFORM_FLOAT);

    RenderTexture2D rt = LoadRenderTexture(W, H);
    int samples = rlvkSetFramebufferSamples(rt.id, 4);
    Camera3D cam = cam3d();

    // Pass 1: the coverage half. A flat-shaded sphere (raylib's default shader does no lighting,
    // so it is a solid white disc) over black — every non-black non-white pixel is AA coverage.
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginTextureMode(rt);
            ClearBackground(BLACK);
            BeginMode3D(cam);
                DrawSphereEx((Vector3){0,0,0}, 1.6f, 24, 24, WHITE);
            EndMode3D();
        EndTextureMode();
        DrawTextureRec(rt.texture, (Rectangle){0,0,W,-H}, (Vector2){0,0}, WHITE);
        EndDrawing();
    }
    int partial = 0;
    {
        Image im = snap();
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                Color c = at(im, x, y);
                if ((c.r > 24) && (c.r < 231)) partial++;   // neither background nor full coverage
            }
        UnloadImage(im);
    }

    // Pass 2: the depth-resolve half, same shape as soft_depth — a near cube fills the centre
    // (real depth ~4 units -> dark), the corners keep the cleared far value (-> white).
    int centre = 0, corner = 0;
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginTextureMode(rt);
            ClearBackground(BLACK);
            BeginMode3D(cam);
                DrawCube((Vector3){0,0,2}, 1.6f, 1.6f, 1.6f, WHITE);
            EndMode3D();
        EndTextureMode();
        BeginShaderMode(dsh);
            DrawTexturePro(rt.depth, (Rectangle){0,0,W,H}, (Rectangle){0,0,W,H}, (Vector2){0,0}, 0, WHITE);
        EndShaderMode();
        EndDrawing();
    }
    {
        Image im = snap();
        centre = at(im, W/2, H/2).r;
        corner = at(im, 8, 8).r;
        UnloadImage(im);
    }
    UnloadRenderTexture(rt); UnloadShader(dsh);

    printf("  [msaa_rt] samples=%d partialCoverage=%d depth(centre=%d corner=%d)\n",
           samples, partial, centre, corner);
    if (samples != 4)
        return NULL;    // device declined offscreen MSAA (Caps.msaa4x / Caps.depthResolve): not a regression
    if (partial < 200)
        return "offscreen MSAA reported active but the silhouette is still binary coverage";
    if (centre > 150)
        return "MSAA depth was not resolved: the 1x depth texture reads far where geometry is";
    if (corner < 200)
        return "MSAA depth resolve clobbered the cleared-far background";
    return NULL;
}

// A VFX colour layer shares the scene RT depth so particles still occlude correctly.
// It clears its own colour target with depth writes disabled.  The clear must honour
// that mask; otherwise every later VFX draw sees a freshly-cleared depth buffer.
static const char *sc_depth_mask_clear(void)
{
    RenderTexture2D rt = LoadRenderTexture(W, H);
    Camera3D cam = cam3d();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginTextureMode(rt);
            ClearBackground(BLACK);
            BeginMode3D(cam);
                DrawCube((Vector3){0,0,2}, 1.6f, 1.6f, 1.6f, (Color){40,40,48,255});
                rlDrawRenderBatchActive();
                rlDisableDepthMask();
                ClearBackground(BLANK); // colour only: preserve the near cube's depth
                BeginBlendMode(BLEND_ADDITIVE);
                    DrawCube((Vector3){0,0,-2}, 8.0f, 6.0f, 0.05f, (Color){255,220,60,255});
                EndBlendMode();
                rlDrawRenderBatchActive();
                rlEnableDepthMask();
            EndMode3D();
        EndTextureMode();
        DrawTextureRec(rt.texture, (Rectangle){0,0,W,-H}, (Vector2){0,0}, WHITE);
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2); UnloadImage(im); UnloadRenderTexture(rt);
    return (c.r < 80 && c.g < 80) ? NULL : "depth-mask-off ClearBackground erased shared RT depth";
}

// The VFX compositor renders the scene, switches to a colour layer, then back
// to the scene before sampling that layer. A scope switch must transition the
// outgoing layer colour to SHADER_READ_ONLY; ending only its render pass leaves
// a legal-looking but unreadable attachment on Vulkan.
static const char *sc_fbo_switch(void)
{
    RenderTexture2D scene = LoadRenderTexture(W, H);
    RenderTexture2D layer = {0};
    layer.id = rlLoadFramebuffer();
    layer.texture.id = rlLoadTexture(NULL, W, H, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    layer.texture.width = W; layer.texture.height = H;
    layer.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8; layer.texture.mipmaps = 1;
    rlEnableFramebuffer(layer.id);
    rlFramebufferAttach(layer.id, layer.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(layer.id, scene.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
    for (int f = 0; f < 3; f++)
    {
        BeginDrawing(); ClearBackground(BLACK);
        BeginTextureMode(scene); ClearBackground(BLUE);
        rlEnableFramebuffer(layer.id); ClearBackground(RED);
        rlEnableFramebuffer(scene.id);
        EndTextureMode();
        DrawTextureRec(layer.texture, (Rectangle){0,0,W,-H}, (Vector2){0,0}, WHITE);
        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2); UnloadImage(im);
    rlEnableFramebuffer(layer.id);
    rlFramebufferAttach(layer.id, 0, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
    rlUnloadTexture(layer.texture.id); rlUnloadFramebuffer(layer.id);
    UnloadRenderTexture(scene);
    // Screen capture on colour-managed macOS can map literal RED to roughly
    // (230,41,55). The invariant is sampleability: red must dominate strongly,
    // distinguishing the layer from the blue scene and white/black fallback.
    bool sampledRed = c.r >= 180 && c.r > c.g * 3 && c.r > c.b * 3;
    return sampledRed ? NULL : "outgoing FBO colour was not transitioned for sampling";
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

static const char *sc_gas_projection(void)
{
    const char *FS =
        "#version 330\n"
        "in vec2 fragTexCoord; out vec4 finalColor;\n"
        "uniform mat4 uInvProj; uniform mat4 uViewToWorld;\n"
        "uniform vec3 uCamera; uniform vec3 uSource; uniform int uFlipY;\n"
        "void main(){\n"
        " vec2 uv=fragTexCoord; if(uFlipY!=0) uv.y=1.0-uv.y;\n"
        " vec4 clip=vec4(uv*2.0-1.0,1.0,1.0);\n"
        " vec4 v=uInvProj*clip; v.xyz/=v.w;\n"
        " vec3 farWorld=(uViewToWorld*vec4(v.xyz,1.0)).xyz;\n"
        " vec3 d=normalize(farWorld-uCamera); vec3 oc=uCamera-uSource;\n"
        " float b=dot(oc,d), c=dot(oc,oc)-0.20*0.20;\n"
        " if(b*b-c<0.0) discard; finalColor=vec4(1.0,0.0,0.0,1.0);\n"
        "}\n";
    Camera3D cam = {0};
    cam.position = (Vector3){3.4f, 4.8f, 4.95f};
    cam.target = (Vector3){0.0f, 0.2f, 0.0f};
    cam.up = (Vector3){0.0f, 1.0f, 0.0f};
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    Vector3 source = {0.7f, 1.0f, -0.4f};
    Vector2 expected = GetWorldToScreen(source, cam);
    Matrix projection = MatrixPerspective(cam.fovy*DEG2RAD, (double)W/(double)H,
                                          1.0, 1000.0);
    Matrix invProjection = MatrixInvert(projection);
    Matrix viewToWorld = MatrixInvert(GetCameraMatrix(cam));
    Shader shader = LoadShaderFromMemory(NULL, FS);
    RenderTexture2D rt = LoadRenderTexture(W/4, H/4);
    Image wi = GenImageColor(4, 4, WHITE);
    Texture2D white = LoadTextureFromImage(wi);
    UnloadImage(wi);
    int invLoc = GetShaderLocation(shader, "uInvProj");
    int viewLoc = GetShaderLocation(shader, "uViewToWorld");
    int cameraLoc = GetShaderLocation(shader, "uCamera");
    int sourceLoc = GetShaderLocation(shader, "uSource");
    int flipLoc = GetShaderLocation(shader, "uFlipY");
    float error[2] = {9999.0f, 9999.0f};
    int pixels[2] = {0, 0};

    for (int flip = 0; flip < 2; ++flip)
    {
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(cam);
        DrawCube((Vector3){-2.0f, 0.0f, 0.0f}, 0.5f, 0.5f, 0.5f, BLUE);
        EndMode3D();
        BeginTextureMode(rt);
        ClearBackground(BLANK);
        BeginShaderMode(shader);
        SetShaderValueMatrix(shader, invLoc, invProjection);
        SetShaderValueMatrix(shader, viewLoc, viewToWorld);
        SetShaderValue(shader, cameraLoc, &cam.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, sourceLoc, &source, SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, flipLoc, &flip, SHADER_UNIFORM_INT);
        DrawTexturePro(white, (Rectangle){0,0,4,4},
                       (Rectangle){0,0,(float)rt.texture.width,(float)rt.texture.height},
                       (Vector2){0,0}, 0.0f, WHITE);
        EndShaderMode();
        EndTextureMode();
        DrawTexturePro(rt.texture, (Rectangle){0,0,(float)rt.texture.width,-(float)rt.texture.height},
                       (Rectangle){0,0,W,H}, (Vector2){0,0}, 0.0f, WHITE);
        EndDrawing();

        Image im = snap();
        double sx = 0.0, sy = 0.0;
        for (int y = 0; y < im.height; ++y) for (int x = 0; x < im.width; ++x)
        {
            Color c = at(im, x, y);
            if (c.r > 180 && c.g < 40 && c.b < 40)
            { sx += x; sy += y; pixels[flip]++; }
        }
        if (pixels[flip] > 0)
        {
            float cx = (float)(sx/pixels[flip]), cy = (float)(sy/pixels[flip]);
            error[flip] = Vector2Distance((Vector2){cx, cy}, expected);
        }
        UnloadImage(im);
    }
    printf("  [gas_projection] expected=(%.1f,%.1f) directErr=%.2f flipErr=%.2f pixels=%d/%d\n",
           expected.x, expected.y, error[0], error[1], pixels[0], pixels[1]);
    UnloadTexture(white);
    UnloadRenderTexture(rt);
    UnloadShader(shader);
    if (pixels[1] == 0) return "display-corrected gas ray produced no marker";
    if (error[1] > 6.0f) return "display-corrected gas ray does not match GetWorldToScreen";
    if (error[1] >= error[0]) return "direct gas ray unexpectedly matches better than the display-corrected ray";
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

// IMMEDIATE-MODE NORMAL/POSITION SPACE PROBE (added 06/08/2026).
//
// The game's volume-tube shading (core/trails/shaders/trail_volume.fs) measured
// |N.V| INVERTED on a plain cylinder and spent a whole session on it, ending on
// the conclusion "rlNormal3f does not deliver per-vertex normals through the
// immediate-mode batch" (core/docs/VOLUME_SHADING_HANDOFF.md). That conclusion
// was drawn from a GPU debug view, i.e. from the same suspect data path.
//
// This scenario sends a KNOWN normal and reads it back numerically, and it
// separates the two questions that view could not:
//   A. does the attribute ARRIVE? -> a shader that outputs raw `vertexNormal`
//   B. what does vs_header.glsl's `matModel * vec4(vertexNormal, 0)` produce?
//
// It reproduces main.c's MyBeginMode3D EXACTLY, and that is the whole point:
// MyBeginMode3D calls rlPushMatrix() in RL_MODELVIEW mode, which flips rlgl/rlvk
// into `transformRequired` and redirects the view matrix into State.transform
// (ENGINE_LANDMINES 9). From there, for an immediate-mode draw:
//   - rlVertex3f / rlNormal3f transform on the CPU by State.transform  -> the
//     batch buffer already holds VIEW-space positions and normals
//   - the batch flush uploads matModel = State.transform = the VIEW matrix
//     (rlgl.h:3082, mirrored at rlvk_core.inl:595)
//   - so `matModel * vertexNormal` in the VS applies the view rotation a
//     SECOND time.
// Expected result: A matches view*N, B matches view*view*N. If so, the
// attribute path is fine and vs_header double-transforms on this draw path.
static void immRotByTransform(Matrix m, Vector3 v, Vector3 *out)
{
    // Byte-for-byte rlNormal3f's arithmetic (rlgl.h:1612, rlvk_matrix.inl:300).
    out->x = m.m0*v.x + m.m4*v.y + m.m8 *v.z;
    out->y = m.m1*v.x + m.m5*v.y + m.m9 *v.z;
    out->z = m.m2*v.x + m.m6*v.y + m.m10*v.z;
}
static void immReadNormal(bool throughMatModel, Vector3 nWorld, Matrix matView, Vector3 *out)
{
    const char *vsRaw =
        "#version 330\n"
        "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;\n"
        "uniform mat4 mvp;\n"
        "out vec3 vN;\n"
        "void main(){ vN = vertexNormal; gl_Position = mvp*vec4(vertexPosition,1.0); }\n";
    // Character-for-character vs_header.glsl's VS_FinalOutput normal line.
    const char *vsModel =
        "#version 330\n"
        "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;\n"
        "uniform mat4 mvp; uniform mat4 matModel;\n"
        "out vec3 vN;\n"
        "void main(){ vN = normalize(vec3(matModel*vec4(vertexNormal,0.0)));\n"
        "             gl_Position = mvp*vec4(vertexPosition,1.0); }\n";
    const char *fs =
        "#version 330\n"
        "in vec3 vN; out vec4 finalColor;\n"
        "void main(){ finalColor = vec4(normalize(vN)*0.5+0.5, 1.0); }\n";
    Shader sh = LoadShaderFromMemory(throughMatModel ? vsModel : vsRaw, fs);

    Camera3D cam = { 0 };
    cam.position = (Vector3){ 5, 4, 7 }; cam.target = (Vector3){ 0, 0, 0 };
    cam.up = (Vector3){ 0, 1, 0 }; cam.fovy = 45.0f; cam.projection = CAMERA_PERSPECTIVE;

    for (int f = 0; f < 3; f++)
    {
        BeginDrawing();
        ClearBackground(BLACK);

        // ---- MyBeginMode3D (main.c:60), reproduced exactly ----
        rlDrawRenderBatchActive();
        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlLoadIdentity();
        double top = 1.0*tan(cam.fovy*0.5*DEG2RAD), right = top*((double)W/(double)H);
        rlFrustum(-right, right, -top, top, 1.0, 1000.0);
        rlMatrixMode(RL_MODELVIEW);
        rlPushMatrix();                       // <-- the line that arms transformRequired
        rlLoadIdentity();
        rlMultMatrixf(MatrixToFloat(matView));
        rlEnableDepthTest();

        BeginShaderMode(sh);
        rlDisableBackfaceCulling();           // the probe quad's winding is not the subject
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        for (int i = 0; i < 4; i++)
        {
            const float qx[4] = { -1.5f,  1.5f,  1.5f, -1.5f };
            const float qy[4] = { -1.5f, -1.5f,  1.5f,  1.5f };
            rlNormal3f(nWorld.x, nWorld.y, nWorld.z);   // THE KNOWN VALUE, same on every vertex
            rlTexCoord2f(0.0f, 0.0f);
            rlVertex3f(qx[i], qy[i], 0.0f);
        }
        rlEnd();
        EndShaderMode();
        rlEnableBackfaceCulling();

        // ---- MyEndMode3D ----
        rlDrawRenderBatchActive();
        rlMatrixMode(RL_PROJECTION); rlPopMatrix();
        rlMatrixMode(RL_MODELVIEW);  rlPopMatrix(); rlLoadIdentity();
        rlDisableDepthTest();

        EndDrawing();
    }
    Image im = snap(); Color c = at(im, W/2, H/2); UnloadImage(im);
    UnloadShader(sh);
    out->x = c.r/255.0f*2.0f - 1.0f;
    out->y = c.g/255.0f*2.0f - 1.0f;
    out->z = c.b/255.0f*2.0f - 1.0f;
}
static const char *sc_imm_normal(void)
{
    Matrix matView = MatrixLookAt((Vector3){5,4,7}, (Vector3){0,0,0}, (Vector3){0,1,0});
    Vector3 nWorld = Vector3Normalize((Vector3){ 0.30f, 0.90f, -0.32f });

    Vector3 once, twice;
    immRotByTransform(matView, nWorld, &once);          // what the CPU already baked in
    immRotByTransform(matView, once,   &twice);         // what the VS would add on top
    once  = Vector3Normalize(once);
    twice = Vector3Normalize(twice);

    Vector3 gotRaw, gotModel;
    immReadNormal(false, nWorld, matView, &gotRaw);
    immReadNormal(true,  nWorld, matView, &gotModel);

    float dRawOnce   = Vector3Length(Vector3Subtract(gotRaw,   once));
    float dRawWorld  = Vector3Length(Vector3Subtract(gotRaw,   nWorld));
    float dModelOnce = Vector3Length(Vector3Subtract(gotModel, once));
    float dModelTwice= Vector3Length(Vector3Subtract(gotModel, twice));

    printf("  [imm_normal] Nworld=(%.2f,%.2f,%.2f) view*N=(%.2f,%.2f,%.2f) view*view*N=(%.2f,%.2f,%.2f)\n",
           nWorld.x, nWorld.y, nWorld.z, once.x, once.y, once.z, twice.x, twice.y, twice.z);
    printf("  [imm_normal] raw vertexNormal=(%.2f,%.2f,%.2f) d(view*N)=%.3f d(world)=%.3f\n",
           gotRaw.x, gotRaw.y, gotRaw.z, dRawOnce, dRawWorld);
    printf("  [imm_normal] matModel*vertexNormal=(%.2f,%.2f,%.2f) d(view*N)=%.3f d(view*view*N)=%.3f\n",
           gotModel.x, gotModel.y, gotModel.z, dModelOnce, dModelTwice);

    const float TOL = 0.06f;    // 8-bit readback of a signed-encoded unit vector
    if (dRawOnce > TOL)
        return (dRawWorld < TOL) ? "attribute arrives UNTRANSFORMED (rlNormal3f CPU transform missing)"
                                 : "rlNormal3f attribute does NOT arrive (constant read back wrong)";
    if (dModelTwice > TOL)
        return (dModelOnce < TOL) ? "matModel is identity here - vs_header would be correct"
                                  : "matModel is neither identity nor the view matrix";
    return NULL;   // attribute path intact; matModel = view => vs_header double-transforms
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
// (mirrors the GPU particle draw half: core/particles/shaders/gpu/particle_gpu_ssbo.vs
// + compute/gpu_particle_system.c; the old core/shaders/particles.vs was deleted unused).
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

// ---- perf probes (skipped in a full run; ask for them by name) --------------------
// Measurement, not assertion: they always "pass". Run with RLVK_GPU_TRACE=1 and read the
// "VKGPU frames=512 scene=... present=..." line; the query pool prints every 512 frames, so
// each probe renders 520. Compare probes ACROSS PROCESSES - the averages are cumulative.
//
// What they discriminate: perf_rt2048 vs perf_rt256 is the same number of passes with 64x the
// pixels. If the extra cost scales with resolution it is per-PIXEL work (the Caps.noSampledDepth
// depth->buffer->twin bounce at scope close, LANDMINES 7.27); if 2048 ~= 256 it is per-PASS
// overhead (encoder/pass splits) and the twin is NOT the culprit.
static void perfFrame(RenderTexture2D *rt, Camera3D cam)
{
    BeginDrawing();
    ClearBackground((Color){8,8,16,255});
    if (rt)
    {
        BeginTextureMode(*rt);
            ClearBackground(BLACK);
            BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, RED); EndMode3D();
        EndTextureMode();
    }
    BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, BLUE); EndMode3D();
    EndDrawing();
}

static const char *perfRun(int rtSize)
{
    Camera3D cam = cam3d();
    RenderTexture2D rt = {0};
    if (rtSize) rt = LoadRenderTexture(rtSize, rtSize);
    for (int f = 0; f < 60; f++) perfFrame(rtSize ? &rt : NULL, cam);   // warm up: pipelines, arena growth
    double t0 = GetTime();
    const int N = 460;
    for (int f = 0; f < N; f++) perfFrame(rtSize ? &rt : NULL, cam);
    double ms = (GetTime() - t0) * 1000.0 / N;
    if (rtSize) UnloadRenderTexture(rt);
    printf("  [perf rt=%-4d] %.3f ms/frame (%.1f fps)\n", rtSize, ms, 1000.0 / ms);
    return NULL;
}

// A color-only render texture in an explicit format — core/post_fx.c's LoadRenderTextureWithFormat.
// No depth attachment, so no Caps.noSampledDepth twin is involved: this probe measures the postFX
// chain's own bandwidth, nothing else.
static RenderTexture2D fmtRT(int w, int h, int format)
{
    RenderTexture2D t = {0};
    t.id = rlLoadFramebuffer();
    if (t.id == 0) return t;
    rlEnableFramebuffer(t.id);
    t.texture.id = rlLoadTexture(NULL, w, h, format, 1);
    t.texture.width = w; t.texture.height = h; t.texture.format = format; t.texture.mipmaps = 1;
    rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
    SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
    return t;
}

// Screen-space fluid (core/fluid) additively blends per-splat thickness into an R32F
// target. That is optional hardware behaviour: the Vulkan spec's Mandatory Format Support
// tables require COLOR_ATTACHMENT_BLEND for R16_SFLOAT but NOT for R32_SFLOAT. A driver
// without it does not raise an error the caller can see - the effect just comes out wrong.
// So assert that Caps.floatBlendR32 agrees with what this device actually does. Three
// full-coverage additive white quads: only a float target that really blends holds 3.0.
// Declared here rather than by including rlvk.h: this suite links the patched raylib and
// has no Vulkan headers on its include path.
extern bool rlvkFormatSupportsBlend(int rlFormat);

static const char *sc_float_blend_rt(void)
{
    RenderTexture2D rt = fmtRT(64, 64, RL_PIXELFORMAT_UNCOMPRESSED_R32);
    if (rt.id == 0 || rt.texture.id == 0) return "could not create an R32F colour attachment";

    BeginDrawing();
        BeginTextureMode(rt);
            ClearBackground(BLANK);
            BeginBlendMode(BLEND_ADDITIVE);
                for (int i = 0; i < 3; i++) DrawRectangle(0, 0, 64, 64, WHITE);
            EndBlendMode();
        EndTextureMode();
    EndDrawing();

    float *px = (float *)rlReadTexturePixels(rt.texture.id, 64, 64, RL_PIXELFORMAT_UNCOMPRESSED_R32);
    if (px == NULL) { UnloadRenderTexture(rt); return "R32F readback returned nothing"; }
    float centre = px[32*64 + 32];
    RL_FREE(px);
    UnloadRenderTexture(rt);

    bool accumulated = fabsf(centre - 3.0f) < 0.05f;
    bool blendCap = rlvkFormatSupportsBlend(RL_PIXELFORMAT_UNCOMPRESSED_R32);
    printf("      R32F additive x3 -> %.3f (cap says blend=%d)\n", centre, (int)blendCap);
    if (blendCap && !accumulated)
        return "cap claims R32F blending but three additive writes did not accumulate";
    if (!blendCap && accumulated)
        return "cap denies R32F blending but the device accumulated anyway (cap is wrong)";
    return NULL;
}

// ── Bright-background VFX acceptance oracle ──────────────────────────────────────
// Implements docs/BRIGHT_BACKGROUND_VFX_SPEC.md §8: the same authored fixture over
// five background luminances, at three exposures, with bloom off (source
// readability) and on (optical quality), for every element hue.
//
// Two rules shape this scenario, both learned from its own first version:
//
//  1. THE ORACLE MUST TONE MAP WITH THE SHIPPING CURVE. v1 measured through a
//     Reinhard probe (`x/(1+x)`) while the game runs the ACES fit in
//     core/shaders/post_process.fs. Every §8.2 threshold is defined "after tone
//     mapping", and Reinhard is by far the more forgiving of the two — so the
//     gate was green on material ACES crushes. The composite below therefore
//     LOADS core/shaders/post_process.fs and the three real bloom shaders rather
//     than re-implementing any of them. That makes this scenario deliberately
//     non-hermetic: a Core change to the post chain can fail it. That is the
//     point — it is the acceptance oracle, not a backend unit test.
//  2. A SHADER LOADED BY PATH MUST BE GUARDED. raylib answers a missing file with
//     the default shader and a non-zero id (the perf_ssf_filter trap, PROGRESS.md).
//
// The FIXTURE stays a self-contained string (spec Task 1.1): it is the thing under
// test and must not drift with Core content. It carries the §5.4 spatial
// hierarchy in real pixels — 3 px core, 9 px body, 32 px halo — because a flat
// constant-colour quad (what v1 drew) cannot express, and therefore cannot test,
// any of the core/body/halo metrics the spec is actually about.
static void blit(Texture2D src, int dstW, int dstH);

static char s_brightWhy[256];
// The chart runs at the SHIPPING hue-restore strength, because an acceptance oracle that
// measures a configuration the game does not use is the drift this scenario exists to
// prevent (its first version tone mapped through Reinhard while the game ran ACES).
// BRIGHT_HUEFIX=<0..1> overrides it, which is how the strength was chosen.
// Keep in sync with core/post_fx.c's s_hueRestore default.
static float s_brightHueRestore = 0.6f;
// The scene/composite format the chart runs through. RGBA16F is the HDR path; the
// RGBA8 run is the MOBILE FALLBACK, where §7.1 claims "premultiplied coverage remains
// the primary bright-background mechanism even though radiance above 1.0 is
// unavailable". That claim had never been tested, on the path that ships to phones.
static int s_brightSceneFormat = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;

static Shader brightRepoShader(const char *rel)
{
    Shader s = { 0 };
    const char *root = getenv("RLVK_REPO_ROOT");
    if (root == NULL)
    {
        snprintf(s_brightWhy, sizeof(s_brightWhy),
                 "RLVK_REPO_ROOT not set (run via scripts/run_rlvk_visual_test.sh)");
        return s;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    s = LoadShader(NULL, path);
    if (s.id == 0 || s.id == rlGetShaderIdDefault())
    {
        snprintf(s_brightWhy, sizeof(s_brightWhy), "%s did not load (default-shader fallback)", rel);
        s.id = 0;
    }
    return s;
}

static void setF(Shader s, const char *n, float v)
{ int l = GetShaderLocation(s, n); if (l >= 0) SetShaderValue(s, l, &v, SHADER_UNIFORM_FLOAT); }
static void setV2(Shader s, const char *n, float x, float y)
{ float v[2] = { x, y }; int l = GetShaderLocation(s, n); if (l >= 0) SetShaderValue(s, l, v, SHADER_UNIFORM_VEC2); }
static void setV3(Shader s, const char *n, const float *v)
{ int l = GetShaderLocation(s, n); if (l >= 0) SetShaderValue(s, l, v, SHADER_UNIFORM_VEC3); }
static void setV4(Shader s, const char *n, float x, float y, float z, float w)
{ float v[4] = { x, y, z, w }; int l = GetShaderLocation(s, n); if (l >= 0) SetShaderValue(s, l, v, SHADER_UNIFORM_VEC4); }
static void setTex(Shader s, const char *n, Texture2D t)
{ int l = GetShaderLocation(s, n); if (l >= 0) SetShaderValueTexture(s, l, t); }

// The fixture. uMode: 0 premultiplied body+core (§5.2), 1 additive halo (§5.3),
// 2 legacy additive control, 3 exact HDR background fill, 4 flat premultiplied
// source (blend-law probe, both draw sites).
static const char *BRIGHT_FIXTURE_FS =
"#version 330\n"
"in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
"uniform sampler2D texture0;\n"
"uniform vec4 uBox;      // x,y,w,h of this draw in target pixels\n"
"uniform vec2 uCentre;   // fixture centre in target pixels\n"
"uniform vec4 uProfile;  // coreR, bodyR, haloR, emissionGain  (radii in pixels)\n"
"uniform vec2 uLevels;   // coverage, bodyIntensity\n"
"uniform vec3 uBody;\n"
"uniform vec3 uEmit;\n"
"uniform float uCorona;  // saturated radiance of the 3-9 px corona\n"
"uniform float uMode;\n"
"void main(){\n"
"    if (uMode > 3.5) { finalColor = vec4(uEmit, uLevels.x); return; }\n"
"    if (uMode > 2.5) { finalColor = vec4(uBody, 1.0); return; }\n"
"    vec2 px = uBox.xy + fragTexCoord * uBox.zw;\n"
"    float r = length(px - uCentre);\n"
"    float coverage = uLevels.x * (1.0 - smoothstep(uProfile.y - 1.0, uProfile.y, r));\n"
"    float coreMask = 1.0 - smoothstep(uProfile.x - 1.0, uProfile.x, r);\n"
"    float haloMask = 1.0 - smoothstep(0.0, uProfile.z, r);\n"
    // The halo is an ANNULUS, not a fill. Two measurements pin both edges of it:
    // a halo still at ~0.8 mask over the body adds back exactly the light the body's
    // coverage removed, so the body stops attenuating the background at all (the milky
    // film ShieldShell kept landing on) - while a halo that decays fast enough to clear
    // the body (a squared falloff) is down to 0.009 rgbDistance by r=11 and no longer
    // occupies the 8-32 px band §5.4 asks for at all. So: zero until just past the body
    // radius, then a LINEAR decay out to the halo radius.
"    haloMask *= smoothstep(uProfile.y * 0.9, uProfile.y * 2.0, r);\n"
"    float coronaMask = 1.0 - smoothstep(uProfile.y - 1.0, uProfile.y, r);\n"
"    if (uMode < 0.5) {\n"
"        // §5.2: coverage attenuates the background, emission is independent HDR.\n"
"        // THREE terms, not two. The 3-9 px corona needs its own SATURATED radiance:\n"
"        // coverage alone cannot hold hue, because a translucent body over a\n"
"        // complementary background mixes toward neutral (measured: warm body over\n"
"        // the bright-cool tile fell to chroma 0.05 with a body+core-only fixture).\n"
"        // The core stays whitened and narrow; the corona carries the element hue.\n"
"        finalColor = vec4(uBody * uLevels.y * coverage + uBody * coronaMask * uCorona\n"
"                          + uEmit * coreMask * uProfile.w, coverage);\n"
"    } else if (uMode < 1.5) {\n"
"        // §5.3: additive halo — do NOT pre-scale rgb by the mask the blend applies.\n"
"        // The halo carries the SATURATED BODY hue, never the whitened core hue.\n"
"        // Measured: an emission-coloured halo pumps light back into exactly the\n"
"        // channels the body's coverage darkened, and a blue effect over a blue sky\n"
"        // then measures rgbDistance 0.06 instead of 0.21.\n"
"        finalColor = vec4(uBody * uProfile.w, haloMask);\n"
"    } else {\n"
"        // The control: same energy, no coverage. Cannot attenuate the background.\n"
"        finalColor = vec4(uBody * uLevels.y + uBody * coronaMask * uCorona\n"
"                          + uEmit * coreMask * uProfile.w, coverage);\n"
"    }\n"
"}\n";

typedef struct {
    const char *name;
    float body[3];
    float emit[3];
    float chromaFloor;   // §8.2 default 0.12; per-hue exceptions are documented
    float mode;          // 0 = premultiplied archetype, 2 = additive control row
} BrightHue;

// Six element hues, not the spec's three: Kim/Thuy are the hard cases and testing
// only warm+blue hides them. NOTE ON `metal`: authored as deep gold, not the pale
// white-gold the art brief suggests. A pale (0.82,0.80,0.62) body measures
// chroma 0.02 and rgbDistance 0.04 against a white background at EV2 — i.e. it is
// genuinely invisible there, whatever the compositor does. Near-neutral emitters
// have no chroma headroom on bright ground; that is an authoring law, not a bug to
// tune around. Its floor is lowered to 0.10 for the same reason (§8.2 allows a
// deliberate, recorded metric change).
static const BrightHue BRIGHT_HUES[] = {
    { "thuy",    { 0.04f, 0.30f, 0.92f }, { 0.30f, 0.75f, 1.00f }, 0.12f, 0.0f },
    { "moc",     { 0.04f, 0.66f, 0.05f }, { 0.45f, 1.00f, 0.35f }, 0.12f, 0.0f },
    { "hoa",     { 0.92f, 0.24f, 0.03f }, { 1.00f, 0.55f, 0.14f }, 0.12f, 0.0f },
    { "tho",     { 0.60f, 0.30f, 0.04f }, { 1.00f, 0.76f, 0.30f }, 0.12f, 0.0f },
    { "kim",     { 0.56f, 0.40f, 0.02f }, { 1.00f, 0.92f, 0.62f }, 0.10f, 0.0f },
    { "taicuc",  { 0.52f, 0.07f, 0.92f }, { 0.82f, 0.48f, 1.00f }, 0.12f, 0.0f },
    { "additive-control", { 0.04f, 0.30f, 0.92f }, { 0.30f, 0.75f, 1.00f }, 0.0f, 2.0f },
};
#define BRIGHT_ROWS ((int)(sizeof(BRIGHT_HUES)/sizeof(BRIGHT_HUES[0])))

static const float BRIGHT_BG[5][3] = {     // §8.1, linear HDR
    { 0.02f, 0.02f, 0.02f }, { 0.18f, 0.18f, 0.18f }, { 1.00f, 1.00f, 1.00f },
    { 1.00f, 0.72f, 0.35f }, { 0.35f, 0.72f, 1.00f },
};
#define BRIGHT_COLS 5

#define BR_TILE_W 128
#define BR_TILE_H 112
#define BR_FW (BR_TILE_W * BRIGHT_COLS)
#define BR_FH (BR_TILE_H * BRIGHT_ROWS)

// §5.4 spatial hierarchy, in final screen pixels.
#define BR_CORE_R  1.5f     //  3 px wide
#define BR_BODY_R  4.5f     //  9 px wide
#define BR_HALO_R 16.0f     // 32 px wide
#define BR_GAIN    4.0f
#define BR_HALO_GAIN 0.9f   // §6.2: halo at 0.22x core energy
#define BR_COVERAGE 0.68f
#define BR_BODY_I   1.0f
#define BR_CORONA   1.6f    // §5.4: the saturated 3-9 px band, well below the core
#define BR_BOX ((int)(BR_HALO_R * 2.0f) + 8)

// Game defaults, main.c:426-442. Threshold/knee/maxEnergy/scatter must track
// PostFXConfig or this scenario stops describing the shipping look.
#define BR_BLOOM_THRESHOLD 1.25f
#define BR_BLOOM_INTENSITY 0.12f
#define BR_BLOOM_KNEE      0.15f
#define BR_BLOOM_MAXENERGY 12.0f
#define BR_BLOOM_SCATTER   0.70f

static void brightSample(const uint16_t *px, int fh, int x, int yDraw, bool flipped, float out[4])
{
    int y = flipped ? (fh - 1 - yDraw) : yDraw;
    sampleHalfRGBA(px, BR_FW, x, y, out);
}

static bool brightHasNonFinite(const uint16_t *px, int count)
{
    for (int i = 0; i < count; i++)
        if ((px[i] & 0x7C00u) == 0x7C00u) return true;   // half exponent all-ones = Inf/NaN
    return false;
}

// One full post chain at a given exposure. The bloom pyramid is scene -> prefilter
// (1/4) -> down (1/8) -> down (1/16) -> scatter-upsample back into 1/8. Every stage
// is a uv->uv full-target blit, so bloom[uv] is the bloom of scene[uv] by
// construction and no stage can silently mirror the pyramid against the scene.
static uint16_t *brightComposite(RenderTexture2D scene, RenderTexture2D l0, RenderTexture2D l1,
                                 RenderTexture2D l2, RenderTexture2D out,
                                 Shader bright, Shader down, Shader up, Shader post,
                                 Texture2D dummy, float exposure, bool bloomOn)
{
    BeginDrawing();
    if (bloomOn)
    {
        BeginTextureMode(l0);
            ClearBackground(BLANK);
            BeginShaderMode(bright);
                setF(bright, "u_threshold", BR_BLOOM_THRESHOLD);
                setF(bright, "u_exposure", exposure);
                setF(bright, "u_knee", BR_BLOOM_KNEE);
                setF(bright, "u_maxEnergy", BR_BLOOM_MAXENERGY);
                setV2(bright, "u_sourceTexelSize", 1.0f/BR_FW, 1.0f/BR_FH);
                rlDisableColorBlend();   // overwrite: see the note on the composite below
                blit(scene.texture, l0.texture.width, l0.texture.height);
                rlDrawRenderBatchActive();
                rlEnableColorBlend();
            EndShaderMode();
        EndTextureMode();
        BeginTextureMode(l1);
            ClearBackground(BLANK);
            BeginShaderMode(down);
                setV2(down, "u_texelSize", 1.0f/l0.texture.width, 1.0f/l0.texture.height);
                setF(down, "u_karis", 1.0f);            // firefly guard on the first hop only
                setF(down, "u_streakEnabled", 0.0f);
                rlDisableColorBlend();
                blit(l0.texture, l1.texture.width, l1.texture.height);
                rlDrawRenderBatchActive();
                rlEnableColorBlend();
            EndShaderMode();
        EndTextureMode();
        BeginTextureMode(l2);
            ClearBackground(BLANK);
            BeginShaderMode(down);
                setV2(down, "u_texelSize", 1.0f/l1.texture.width, 1.0f/l1.texture.height);
                setF(down, "u_karis", 0.0f);
                setF(down, "u_streakEnabled", 0.0f);
                rlDisableColorBlend();
                blit(l1.texture, l2.texture.width, l2.texture.height);
                rlDrawRenderBatchActive();
                rlEnableColorBlend();
            EndShaderMode();
        EndTextureMode();
        BeginTextureMode(l1);
            BeginShaderMode(up);
                // bloom_upsample.fs writes u_scatter into ALPHA and relies on the
                // hardware to compute mix(dst, tent, scatter) - so BLEND_ALPHA, not additive.
                setV2(up, "u_texelSize", 1.0f/l2.texture.width, 1.0f/l2.texture.height);
                setF(up, "u_scatter", BR_BLOOM_SCATTER);
                BeginBlendMode(BLEND_ALPHA);
                    blit(l2.texture, l1.texture.width, l1.texture.height);
                EndBlendMode();
            EndShaderMode();
        EndTextureMode();
    }

    BeginTextureMode(out);
        ClearBackground(BLANK);
        BeginShaderMode(post);
            setTex(post, "u_bloomTex", bloomOn ? l1.texture : dummy);
            setTex(post, "u_lutTex", dummy);
            setF(post, "u_bloomEnabled",     bloomOn ? 1.0f : 0.0f);
            setF(post, "u_bloomIntensity",   BR_BLOOM_INTENSITY);
            setF(post, "u_tonemapEnabled",   1.0f);
            setF(post, "u_exposure",         exposure);
            setF(post, "u_chromaticEnabled", 0.0f);
            setF(post, "u_vignetteEnabled",  0.0f);
            setF(post, "u_colorGradeEnabled",0.0f);
            setF(post, "u_lutEnabled",       0.0f);
            setF(post, "u_radialBlurEnabled",0.0f);
            // Gate 1: BRIGHT_HUEFIX=1 reruns the entire chart through the candidate
            // curve. Every metric stays enforced, so a run that passes proves the
            // candidate regresses nothing; BRIGHT_DEBUG then gives the delta table.
            setF(post, "u_hueRestore", s_brightHueRestore);
            // OVERWRITE, never alpha-blend. Additive VFX leave alpha ABOVE 1 in the
            // HDR scene target (BLEND_ADDITIVE accumulates alpha too), and
            // post_process.fs passes that alpha straight through - compositing with
            // BLEND_ALPHA then multiplies the tone-mapped image by ~1.5 and reads
            // back values above 1.0 that ACES can never produce. The game does the
            // same thing for the same reason (core/post_fx.c:709).
            rlDisableColorBlend();
            blit(scene.texture, BR_FW, BR_FH);
            rlDrawRenderBatchActive();   // the toggle only lands at FLUSH time
            rlEnableColorBlend();
        EndShaderMode();
    EndTextureMode();
    EndDrawing();

    if (s_brightSceneFormat == RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    {
        // Widen 8-bit to the same half-float buffer the rest of the chart reads, so one
        // set of metrics serves both paths and the LDR run cannot quietly use easier ones.
        unsigned char *px8 = (unsigned char *)rlReadTexturePixels(out.texture.id, BR_FW, BR_FH,
                                                RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        if (px8 == NULL) return NULL;
        uint16_t *wide = (uint16_t *)RL_MALLOC((size_t)BR_FW*BR_FH*4*sizeof(uint16_t));
        if (wide == NULL) { RL_FREE(px8); return NULL; }
        for (size_t i = 0; i < (size_t)BR_FW*BR_FH*4; i++) wide[i] = floatToHalf(px8[i] / 255.0f);
        RL_FREE(px8);
        return wide;
    }
    return (uint16_t *)rlReadTexturePixels(out.texture.id, BR_FW, BR_FH,
                                           RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
}

// The RGBA8 fallback held to the same contract as the HDR path. §7.1 asserts coverage
// still carries bright-background readability there; nothing had ever checked it, and it
// is the path that ships to phones. Emission above 1.0 clips BEFORE bloom on this path,
// so the compact core cannot be HDR — the source-readability metrics must pass anyway,
// on coverage alone, or the fallback is not a fallback.
static const char *sc_bright_vfx(void);

static const char *sc_bright_vfx_ldr(void)
{
    s_brightSceneFormat = RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    const char *why = sc_bright_vfx();
    s_brightSceneFormat = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    return why;
}

static const char *sc_bright_vfx(void)
{
    const float exposures[3] = { 0.5f, 1.0f, 2.0f };
    const char *why = NULL;
    RenderTexture2D scene = {0}, l0 = {0}, l1 = {0}, l2 = {0}, outRT = {0};
    Shader fx = {0}, bright = {0}, down = {0}, up = {0}, post = {0};
    Texture2D white = {0}, dummy = {0};
    uint16_t *px = NULL;
    float chromaNoBloom[3][BRIGHT_ROWS][BRIGHT_COLS];
    float lumaHaloNoBloom[3][BRIGHT_ROWS][BRIGHT_COLS];
    bool flipped = false;
    int prevLog = LOG_WARNING;
    const char *hueEnv = getenv("BRIGHT_HUEFIX");
    s_brightHueRestore = hueEnv ? (float)atof(hueEnv) : 0.6f;

    SetTraceLogLevel(LOG_ERROR);   // GetShaderLocation warns on optimised-out uniforms

    scene = fmtRT(BR_FW, BR_FH, s_brightSceneFormat);
    outRT = fmtRT(BR_FW, BR_FH, s_brightSceneFormat);
    l0    = fmtRT(BR_FW/4,  BR_FH/4,  RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    l1    = fmtRT(BR_FW/8,  BR_FH/8,  RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    l2    = fmtRT(BR_FW/16, BR_FH/16, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    if (!scene.id || !outRT.id || !l0.id || !l1.id || !l2.id)
    { why = "could not create the RGBA16F chart / bloom targets"; goto done; }

    fx = LoadShaderFromMemory(NULL, BRIGHT_FIXTURE_FS);
    if (fx.id == 0 || fx.id == rlGetShaderIdDefault())
    { why = "bright_vfx fixture shader fell back to default"; goto done; }

    s_brightWhy[0] = '\0';
    bright = brightRepoShader("core/shaders/bloom_bright.fs");
    down   = brightRepoShader("core/shaders/bloom_downsample.fs");
    up     = brightRepoShader("core/shaders/bloom_upsample.fs");
    post   = brightRepoShader("core/shaders/post_process.fs");
    if (!bright.id || !down.id || !up.id || !post.id) { why = s_brightWhy; goto done; }

    { Image wi = GenImageColor(8, 8, WHITE); white = LoadTextureFromImage(wi); UnloadImage(wi); }
    { Image di = GenImageColor(4, 4, BLACK); dummy = LoadTextureFromImage(di); UnloadImage(di); }

    // ---- the chart ---------------------------------------------------------------
    BeginDrawing();
    BeginTextureMode(scene);
        ClearBackground(BLACK);
        BeginShaderMode(fx);
        for (int r = 0; r < BRIGHT_ROWS; r++)
        {
            const BrightHue *h = &BRIGHT_HUES[r];
            for (int c = 0; c < BRIGHT_COLS; c++)
            {
                int tx = c*BR_TILE_W, ty = r*BR_TILE_H;
                float cx = tx + BR_TILE_W*0.5f, cy = ty + BR_TILE_H*0.5f;
                float bx = cx - BR_BOX*0.5f,    by = cy - BR_BOX*0.5f;

                // exact linear-HDR background (mode 3), not an 8-bit Color
                setF(fx, "uMode", 3.0f);
                setV3(fx, "uBody", BRIGHT_BG[c]);
                BeginBlendMode(BLEND_ALPHA);
                    DrawTexturePro(white, (Rectangle){0,0,(float)white.width,(float)white.height},
                                   (Rectangle){(float)tx,(float)ty,(float)BR_TILE_W,(float)BR_TILE_H},
                                   (Vector2){0,0}, 0.0f, WHITE);
                    rlDrawRenderBatchActive();
                EndBlendMode();

                setV4(fx, "uBox", bx, by, (float)BR_BOX, (float)BR_BOX);
                setV2(fx, "uCentre", cx, cy);
                setV2(fx, "uLevels", BR_COVERAGE, BR_BODY_I);
                setF(fx, "uCorona", BR_CORONA);
                setV3(fx, "uBody", h->body);
                setV3(fx, "uEmit", h->emit);
                Rectangle dst = { bx, by, (float)BR_BOX, (float)BR_BOX };
                Rectangle src = { 0, 0, (float)white.width, (float)white.height };

                setV4(fx, "uProfile", BR_CORE_R, BR_BODY_R, BR_HALO_R, BR_GAIN);
                setF(fx, "uMode", h->mode);
                BeginBlendMode(h->mode < 1.0f ? BLEND_ALPHA_PREMULTIPLY : BLEND_ADDITIVE);
                    DrawTexturePro(white, src, dst, (Vector2){0,0}, 0.0f, WHITE);
                    rlDrawRenderBatchActive();
                EndBlendMode();

                setV4(fx, "uProfile", BR_CORE_R, BR_BODY_R, BR_HALO_R, BR_HALO_GAIN);
                setF(fx, "uMode", 1.0f);
                BeginBlendMode(BLEND_ADDITIVE);
                    DrawTexturePro(white, src, dst, (Vector2){0,0}, 0.0f, WHITE);
                    rlDrawRenderBatchActive();
                EndBlendMode();
            }
        }
        EndShaderMode();
    EndTextureMode();
    EndDrawing();

    // ---- pass A: bloom OFF = source readability (§11 first clause) -----------------
    for (int e = 0; e < 3 && why == NULL; e++)
    {
        px = brightComposite(scene, l0, l1, l2, outRT, bright, down, up, post,
                             dummy, exposures[e], false);
        if (px == NULL) { why = "RGBA16F composite readback returned nothing"; goto done; }
        if (brightHasNonFinite(px, BR_FW*BR_FH*4)) { why = "composite contains NaN/Inf"; goto done; }

        if (e == 0)
        {
            // Resolve the readback origin instead of assuming it: row 1 (moc) is
            // green-dominant and its vertical mirror row 5 (taicuc) is blue-dominant,
            // so one sample decides it and a wrong guess cannot pass silently.
            int sx = (int)(0*BR_TILE_W + BR_TILE_W*0.5f) + 3;
            int sy = (int)(1*BR_TILE_H + BR_TILE_H*0.5f);
            float p[4];
            brightSample(px, BR_FH, sx, sy, false, p);
            flipped = !(p[1] > p[0] && p[1] > p[2]);
            brightSample(px, BR_FH, sx, sy, flipped, p);
            if (!(p[1] > p[0] && p[1] > p[2]))
            { why = "could not resolve readback orientation from the hue signature"; goto done; }
        }

        for (int r = 0; r < BRIGHT_ROWS && why == NULL; r++)
        {
            const BrightHue *h = &BRIGHT_HUES[r];
            bool control = (h->mode > 1.0f);
            for (int c = 0; c < BRIGHT_COLS && why == NULL; c++)
            {
                int cx = (int)(c*BR_TILE_W + BR_TILE_W*0.5f);
                int cy = (int)(r*BR_TILE_H + BR_TILE_H*0.5f);
                float smpB[4], smpS[4], smpC[4], smpH[4], corner[4];
                brightSample(px, BR_FH, cx + 52, cy, flipped, smpB);
                // S sits in the §5.4 saturated band (radius 1.5-4.5 px) at r=3.5: far
                // enough from the 3 px core that the core's own bloom does not
                // desaturate the sample, and inside the body's flat coverage region so
                // the sample measures the body rather than its taper. Both edges of that
                // window were found by measurement - at r=2.5 a near-white core washes
                // 0.07 of chroma off pale hues, and with a wider taper the background
                // leaks through and costs 0.02 of rgbDistance on white at EV2.
                brightSample(px, BR_FH, cx,      cy, flipped, smpC);
                brightSample(px, BR_FH, cx + 3,  cy, flipped, smpS);
                brightSample(px, BR_FH, cx + 11, cy, flipped, smpH);
                brightSample(px, BR_FH, cx + BR_BOX/2 - 1, cy + BR_BOX/2 - 1, flipped, corner);

                float dS = rgbDistanceMax(smpS, smpB), dH = rgbDistanceMax(smpH, smpB);
                if (getenv("BRIGHT_DEBUG"))
                    printf("      EV%.1f %-16s bg%d  B(%.3f %.3f %.3f) S(%.3f %.3f %.3f)"
                           " C(%.3f %.3f %.3f) H(%.3f %.3f %.3f) dS %.3f chroma %.3f\n",
                           exposures[e], h->name, c, smpB[0],smpB[1],smpB[2], smpS[0],smpS[1],smpS[2],
                           smpC[0],smpC[1],smpC[2], smpH[0],smpH[1],smpH[2], dS, chromaOf(smpS));
                chromaNoBloom[e][r][c] = chromaOf(smpS);
                lumaHaloNoBloom[e][r][c] = lumaOf(smpH);

                if (control)
                {
                    // The control carries the SAME authored energy on a pure additive
                    // surface, and proves the chart still measures the mechanism the
                    // spec is about. Two structural facts, neither of them tuned:
                    // additive can never pull a channel below the background (§4), and
                    // on bright ground it therefore has to buy visibility with white,
                    // which costs chroma against the premultiplied row of the same hue
                    // (row 0 is the same thuy body/emission).
                    // "Additive can never darken" holds only while the tone map is
                    // applied PER CHANNEL and is therefore monotonic per channel. The
                    // §12.1 candidate is not: it pulls the non-peak channels down to
                    // restore hue, so an additive effect bright enough to enter the
                    // shoulder CAN end up below the background in a channel (measured:
                    // control R 0.637 -> 0.572 against a 0.613 background). That is a
                    // real property of the candidate worth knowing - and it means §5.7's
                    // darkening budget stops proving "this has coverage" under it, so
                    // the invariant would have to be re-derived in scene-linear space.
                    float lo = 1.0f;
                    for (int k = 0; k < 3; k++) { float d = smpS[k] - smpB[k]; if (d < lo) lo = d; }
                    // Tolerance is the DITHER bound, not zero: post_process.fs adds up to
                    // 1 LSB per pixel, so two samples can differ by 2/255 = 0.008 with
                    // nothing having darkened. It only bites on the LDR path, where a
                    // clipped background makes dither the entire remaining difference.
                    if (s_brightHueRestore <= 0.0f && lo < -0.01f)
                    { why = "additive control darkened the background - the control is not additive"; break; }
                    if (c == 2 && chromaOf(smpS) >= chromaNoBloom[e][0][c])
                    { snprintf(s_brightWhy, sizeof(s_brightWhy),
                               "additive control held chroma %.3f vs premultiplied %.3f on white at EV%.1f"
                               " - the chart cannot detect the bug", chromaOf(smpS),
                               chromaNoBloom[e][0][c], exposures[e]); why = s_brightWhy; break; }
                    continue;
                }

                if (dS < 0.10f)
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "%s body lost contrast (rgbDistance %.3f < 0.10) on bg%d at EV%.1f",
                           h->name, dS, c, exposures[e]); why = s_brightWhy; break; }
                if (chromaOf(smpS) < h->chromaFloor)
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "%s body desaturated (chroma %.3f < %.2f) on bg%d at EV%.1f",
                           h->name, chromaOf(smpS), h->chromaFloor, c, exposures[e]); why = s_brightWhy; break; }
                if (c <= 1 && lumaOf(smpC) < lumaOf(smpS))
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "%s core is dimmer than its body on bg%d at EV%.1f", h->name, c, exposures[e]);
                  why = s_brightWhy; break; }
                // The halo must actually EXIST before "the halo is not the strongest
                // shape" means anything. With the §5.6 inner falloff applied it is easy
                // to tune the halo down to nothing and pass the ordering test vacuously;
                // the dark tile is where a low-energy additive surround has to show up.
                if (c == 0 && dH < 0.01f)
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "%s halo is below the measurement floor (dH %.4f) - the halo"
                           " ordering test would be vacuous", h->name, dH);
                  why = s_brightWhy; break; }
                if (dH >= dS)
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "%s halo is a stronger shape than the body on bg%d at EV%.1f",
                           h->name, c, exposures[e]); why = s_brightWhy; break; }

                // §5-additional: the DARKENING BUDGET, and it is deliberately a
                // STRUCTURAL test, not a magnitude one. rgbDistance already sets the
                // magnitude; what no §8.2 metric as written checks is the MECHANISM -
                // whether the effect actually attenuates the background (§4) or is
                // merely riding on added light, which is the failure that only shows
                // up once the scene gets brighter than the fixture it was tuned on.
                // An effect that never pulls a channel below the background is the
                // milky film ShieldShell kept landing on (PROGRESS.md 2026-08-16).
                if (c >= 2)
                {
                    float lo = 1.0f;
                    for (int k = 0; k < 3; k++)
                    { float d = smpS[k] - smpB[k]; if (d < lo) lo = d; }
                    if (lo >= -0.01f)
                    { snprintf(s_brightWhy, sizeof(s_brightWhy),
                               "%s never attenuates the background on bg%d at EV%.1f (min d %+.3f):"
                               " it is riding on added light only", h->name, c, exposures[e], lo);
                      why = s_brightWhy; break; }
                }

                // §8.2: the core may be white, but most of the body may not be.
                int whiteSamples = 0;
                for (int k = -4; k <= 4; k++)
                {
                    float b[4]; brightSample(px, BR_FH, cx + k, cy, flipped, b);
                    if (b[0] > 0.98f && b[1] > 0.98f && b[2] > 0.98f) whiteSamples++;
                }
                if (whiteSamples > 3)
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "%s blew out %d/9 body samples to white on bg%d at EV%.1f",
                           h->name, whiteSamples, c, exposures[e]); why = s_brightWhy; break; }

                // §8.2: a transparent sprite corner must not differ from its background.
                // §8.2 says 2/255. post_process.fs now dithers by up to 1 LSB per pixel,
                // so two samples can legitimately differ by 2/255 from dither alone;
                // 4/255 keeps the check meaningful (a real billboard border is far
                // larger) without making it a coin flip. Recorded metric change.
                if (rgbDistanceMax(corner, smpB) > 4.0f/255.0f)
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "%s billboard corner differs from the background on bg%d at EV%.1f",
                           h->name, c, exposures[e]); why = s_brightWhy; break; }
            }
        }
        RL_FREE(px); px = NULL;
    }
    if (why) goto done;

    // ---- pass B: bloom ON = optical quality ---------------------------------------
    for (int e = 0; e < 3 && why == NULL; e++)
    {
        px = brightComposite(scene, l0, l1, l2, outRT, bright, down, up, post,
                             dummy, exposures[e], true);
        if (px == NULL) { why = "RGBA16F bloom composite readback returned nothing"; goto done; }
        if (brightHasNonFinite(px, BR_FW*BR_FH*4)) { why = "bloom composite contains NaN/Inf"; goto done; }

        bool bloomLanded = false;
        for (int r = 0; r < BRIGHT_ROWS && why == NULL; r++)
        {
            if (BRIGHT_HUES[r].mode > 1.0f) continue;
            for (int c = 0; c < BRIGHT_COLS && why == NULL; c++)
            {
                int cx = (int)(c*BR_TILE_W + BR_TILE_W*0.5f);
                int cy = (int)(r*BR_TILE_H + BR_TILE_H*0.5f);
                float smpB[4], smpS[4], smpH[4];
                brightSample(px, BR_FH, cx + 52, cy, flipped, smpB);
                brightSample(px, BR_FH, cx + 3,  cy, flipped, smpS);
                brightSample(px, BR_FH, cx + 11, cy, flipped, smpH);

                if (lumaOf(smpH) > lumaHaloNoBloom[e][r][c] + 0.002f) bloomLanded = true;

                // §8.2's chroma-drop limit assumes bloom is driven by the SOURCE. Once
                // the background itself exposes above the bloom threshold it blooms too,
                // veils the whole frame and costs every effect chroma no matter how it
                // is authored - measured here at 0.06 on the bright tiles at EV2, where
                // a 1.0 background exposes to 2.0 against a 1.25 threshold. That is a
                // property of the threshold vs the scene, not of the effect, so the
                // limit is scoped to the non-self-blooming case and the drop is printed
                // instead. The shipping rule this implies: the bloom threshold must sit
                // ABOVE the brightest expected background in exposed space.
                float bgPeak = BRIGHT_BG[c][0];
                for (int k = 1; k < 3; k++) if (BRIGHT_BG[c][k] > bgPeak) bgPeak = BRIGHT_BG[c][k];
                bool backgroundSelfBlooms = (bgPeak * exposures[e] >= BR_BLOOM_THRESHOLD);
                if (backgroundSelfBlooms)
                {
                    if (getenv("BRIGHT_DEBUG"))
                        printf("      note: bg%d self-blooms at EV%.1f; %s chroma %.3f -> %.3f\n",
                               c, exposures[e], BRIGHT_HUES[r].name,
                               chromaNoBloom[e][r][c], chromaOf(smpS));
                }
                // §8.2 states this limit as an ABSOLUTE 0.05 chroma drop, which is not
                // scale-free: a richly saturated hue (chroma 0.80) losing 0.07 to its own
                // core bloom is 8% relative and looks fine, while a hue starting at 0.20
                // losing 0.06 is a third of its colour and looks washed. Deliberate,
                // recorded metric change (§8.2 permits it): the drop must clear BOTH an
                // absolute floor and a 15% relative share before it counts as a wash.
                else if (chromaNoBloom[e][r][c] - chromaOf(smpS) >
                         fmaxf(0.05f, 0.15f*chromaNoBloom[e][r][c]))
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "bloom washed %s out (chroma %.3f -> %.3f) on bg%d at EV%.1f",
                           BRIGHT_HUES[r].name, chromaNoBloom[e][r][c], chromaOf(smpS), c, exposures[e]);
                  why = s_brightWhy; break; }
                if (rgbDistanceMax(smpH, smpB) >= rgbDistanceMax(smpS, smpB))
                { snprintf(s_brightWhy, sizeof(s_brightWhy),
                           "bloom made the %s halo the strongest shape on bg%d at EV%.1f",
                           BRIGHT_HUES[r].name, c, exposures[e]); why = s_brightWhy; break; }
            }
        }
        // Self-check: if enabling bloom changed nothing anywhere, the pyramid is not
        // reaching the composite and every assertion above is vacuous.
        //
        // Except on the LDR fallback, where it is not a wiring fault but a property of
        // the path: an RGBA8 scene buffer clips at 1.0, so NOTHING can cross a 1.25
        // exposed threshold until exposure itself does. Measured consequence for mobile:
        // on the RGBA8 path bloom is INERT below exposure 1.25, however hot the effect is
        // authored — its emission was clipped away before the prefilter ever saw it.
        bool bloomPossible = (s_brightSceneFormat != RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
                             || (1.0f * exposures[e] >= BR_BLOOM_THRESHOLD);
        if (why == NULL && bloomPossible && !bloomLanded)
            why = "bloom on/off produced identical halo energy - the pyramid never reached the composite";
        RL_FREE(px); px = NULL;
    }
    if (why) goto done;

    // ---- blend law, both draw sites (spec Task 1.7 tolerance, Task 1.9 coverage) ---
    // Migrated VFX reach the backend through the 2D batch AND rlvkDrawMesh, which
    // build their pipelines independently, so both must be pinned to the same
    // equation. Each draw site gets its OWN pass over its own small target: sharing
    // one target would let the two quads overlap and double-blend, which reads as a
    // blend-law failure that is really a layout mistake.
    {
        const float srcRGB[3] = { 0.22f, 0.51f, 1.80f };
        const float srcA = 0.62f;
        RenderTexture2D probe = fmtRT(128, 128, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
        Mesh quad = GenMeshPlane(2.0f, 2.0f, 1, 1);
        Material mat = LoadMaterialDefault();
        mat.shader = fx;                       // borrowed, NOT owned - see the cleanup below
        Camera3D ortho = { 0 };
        ortho.position = (Vector3){ 0, 0, 6 }; ortho.target = (Vector3){ 0, 0, 0 };
        ortho.up = (Vector3){ 0, 1, 0 }; ortho.fovy = 3.0f;
        ortho.projection = CAMERA_ORTHOGRAPHIC;
        Matrix xform = MatrixRotateX(-PI*0.5f);   // GenMeshPlane lies in XZ; face the camera

        if (probe.id == 0) why = "could not create the blend-law probe target";
        for (int c = 0; c < 5 && why == NULL; c++)
        {
            for (int site = 0; site < 2 && why == NULL; site++)
            {
                BeginDrawing();
                BeginTextureMode(probe);
                    ClearBackground(BLANK);
                    BeginShaderMode(fx);
                        setF(fx, "uMode", 3.0f);
                        setV3(fx, "uBody", BRIGHT_BG[c]);
                        BeginBlendMode(BLEND_ALPHA);
                            DrawTexturePro(white, (Rectangle){0,0,8,8}, (Rectangle){0,0,128,128},
                                           (Vector2){0,0}, 0.0f, WHITE);
                            rlDrawRenderBatchActive();
                        EndBlendMode();
                        setF(fx, "uMode", 4.0f);
                        setV3(fx, "uEmit", srcRGB);
                        setV2(fx, "uLevels", srcA, 1.0f);
                        BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
                        if (site == 0)
                        {
                            DrawTexturePro(white, (Rectangle){0,0,8,8}, (Rectangle){0,0,128,128},
                                           (Vector2){0,0}, 0.0f, WHITE);
                            rlDrawRenderBatchActive();
                        }
                        else
                        {
                            BeginMode3D(ortho);
                                // No depth attachment on the probe target, and the plane's
                                // winding after the -90 rotation is not worth guessing:
                                // both are disabled so this measures BLENDING only.
                                rlDisableDepthMask();
                                rlDisableDepthTest();
                                rlDisableBackfaceCulling();
                                DrawMesh(quad, mat, xform);
                                rlDrawRenderBatchActive();
                                rlEnableBackfaceCulling();
                                rlEnableDepthTest();
                                rlEnableDepthMask();
                            EndMode3D();
                        }
                        EndBlendMode();
                    EndShaderMode();
                EndTextureMode();
                EndDrawing();

                px = (uint16_t *)rlReadTexturePixels(probe.texture.id, 128, 128,
                                                     RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
                if (px == NULL) { why = "blend-law readback returned nothing"; break; }
                float got[4];
                sampleHalfRGBA(px, 128, 64, 64, got);
                for (int k = 0; k < 3; k++)
                {
                    float expected = srcRGB[k] + BRIGHT_BG[c][k]*(1.0f - srcA);
                    if (fabsf(got[k] - expected) > 0.02f)
                    { snprintf(s_brightWhy, sizeof(s_brightWhy),
                               "%s premultiplied blend off on bg%d ch%d: %.3f vs %.3f",
                               site ? "mesh" : "batch", c, k, got[k], expected);
                      why = s_brightWhy; break; }
                }
                RL_FREE(px); px = NULL;
            }
        }
        // LoadMaterialDefault's maps are ours; its shader is NOT (we overwrote it with
        // fx, and UnloadMaterial would unload fx out from under the cleanup below).
        RL_FREE(mat.maps);
        UnloadMesh(quad);
        if (probe.id) UnloadRenderTexture(probe);
        if (why) goto done;
    }

done:
    SetTraceLogLevel(prevLog);
    if (px) RL_FREE(px);
    if (white.id) UnloadTexture(white);
    if (dummy.id) UnloadTexture(dummy);
    if (fx.id) UnloadShader(fx);
    if (bright.id) UnloadShader(bright);
    if (down.id) UnloadShader(down);
    if (up.id) UnloadShader(up);
    if (post.id) UnloadShader(post);
    if (l0.id) UnloadRenderTexture(l0);
    if (l1.id) UnloadRenderTexture(l1);
    if (l2.id) UnloadRenderTexture(l2);
    if (outRT.id) UnloadRenderTexture(outRT);
    if (scene.id) UnloadRenderTexture(scene);
    return why;
}

// Gate 0 for the hue-preserving tone map (BRIGHT_BACKGROUND_VFX_SPEC.md §12.1).
//
// REWRITTEN 18/08/2026, because the contract it guarded was traded away deliberately.
// It used to assert the change was BIT-IDENTICAL below exposed peak 1 and above peak 9 —
// a BUMP over the shipping curve, so the approval surface collapsed to the highlights.
// That bound is exactly what produced the "rainbow rim": a weight that transitions from
// zero to non-zero while the input is still climbing pulls the non-peak channels down and
// then releases them, which is a colour band with an edge on each side. Measured on a
// plain rectangle through the real pipeline (sandbox/gradient_probe.c), one hue at a
// rising level gave a G slope of +26 -> +9 -> -10 -> +9 on a perfectly smooth input.
// Searching the whole weight family showed the two properties are mutually exclusive, and
// that it is the LOWER bound that causes it, not the upper one — see post_process.fs
// toneMapScene() and root ENGINE_LANDMINES.md.
//
// So the shipping curve is now Candidate H (constant weight, monotone whitening) and this
// scenario asserts the contract that was chosen instead:
//   an ACHROMATIC surface   -> STILL BIT-IDENTICAL, at every level. Hue keeping is
//                              (x / peak) * f(peak), which for a grey IS the per-channel
//                              result, so this holds by construction — and it is what
//                              confines the change to saturated content.
//   saturated, below the
//   shoulder                -> allowed to move, up to a stated ceiling (worst case 0.206
//                              at peak 0.98, at twice the shipping strength). This is the
//                              part that was traded away, made explicit instead of assumed.
//   in the shoulder         -> chroma actually improves (the change is worth making)
//   across a dense ramp     -> NO CHANNEL EVER GOES BACKWARDS. This is the property the
//                              trade bought, and nothing guarded it before. Confirmed RED
//                              on the pre-H shader (58 reversals, worst drop 0.070) before
//                              being kept, per the "a guard that cannot fail on the
//                              pre-fix code is not a guard" rule.
// The dither in post_process.fs is a deterministic hash of gl_FragCoord, so it cancels
// exactly between two runs at the same pixel and these remain testable claims.
static const char *sc_tonemap_shoulder(void)
{
    // The first 9 entries are the original approval points and keep their meaning; the
    // rest are a dense LOG ramp 0.30 -> 14, which is what the monotonicity check reads.
    // Log, not linear: the shoulder does all of its work below peak ~9, and a linear ramp
    // to 14 spends most of its samples in the clipped region where nothing can move.
    enum { TMS_FIX = 9, TMS_RAMP = 40 };
    const int PW = 12, PH = 48, N = TMS_FIX + TMS_RAMP;
    float peaks[TMS_FIX + TMS_RAMP] = { 0.2f, 0.5f, 0.9f, 1.5f, 2.5f, 4.0f, 7.0f, 10.0f, 14.0f };
    for (int i = 0; i < TMS_RAMP; i++)
        peaks[TMS_FIX + i] = expf(logf(0.30f) + (logf(14.0f) - logf(0.30f)) * (float)i / (float)(TMS_RAMP - 1));
    const float hue[3] = { 1.0f, 0.35f, 0.08f };     // saturated warm; max channel is 1.0
    const float neutral[3] = { 1.0f, 1.0f, 1.0f };   // achromatic control row
    RenderTexture2D scene = {0}, out = {0};
    Shader fx = {0}, post = {0};
    Texture2D white = {0}, dummy = {0};
    uint16_t *px = NULL;
    float got[2][TMS_FIX + TMS_RAMP][4];      // [hueRestore off/on][patch][rgba], saturated row
    float gotN[2][TMS_FIX + TMS_RAMP][4];     // ...and the neutral control row
    const char *why = NULL;

    SetTraceLogLevel(LOG_ERROR);
    scene = fmtRT(PW*N, PH, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    out   = fmtRT(PW*N, PH, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    if (!scene.id || !out.id) { why = "could not create the ramp targets"; goto done; }
    fx = LoadShaderFromMemory(NULL, BRIGHT_FIXTURE_FS);
    if (fx.id == 0 || fx.id == rlGetShaderIdDefault()) { why = "fixture shader fell back to default"; goto done; }
    s_brightWhy[0] = '\0';
    post = brightRepoShader("core/shaders/post_process.fs");
    if (!post.id) { why = s_brightWhy; goto done; }
    { Image wi = GenImageColor(8, 8, WHITE); white = LoadTextureFromImage(wi); UnloadImage(wi); }
    { Image di = GenImageColor(4, 4, BLACK); dummy = LoadTextureFromImage(di); UnloadImage(di); }

    BeginDrawing();
    BeginTextureMode(scene);
        ClearBackground(BLACK);
        BeginShaderMode(fx);
            setF(fx, "uMode", 3.0f);
            // TWO ROWS. The top is the saturated warm hue; the bottom is NEUTRAL grey, and
            // it is the row that makes the new low-end assertion mean something: hue
            // keeping is (x / peak) * f(peak), which for a grey is identically the
            // per-channel result, so an achromatic surface CANNOT move no matter what the
            // weight does. That is what confines this whole-scene change to saturated
            // content, and it is checkable rather than argued.
            for (int row = 0; row < 2; row++)
            {
                const float *h = row ? neutral : hue;
                for (int i = 0; i < N; i++)
                {
                    float c[3] = { h[0]*peaks[i], h[1]*peaks[i], h[2]*peaks[i] };
                    setV3(fx, "uBody", c);
                    BeginBlendMode(BLEND_ALPHA);
                        DrawTexturePro(white, (Rectangle){0,0,8,8},
                                       (Rectangle){(float)(i*PW),(float)(row*(PH/2)),
                                                   (float)PW,(float)(PH/2)},
                                       (Vector2){0,0}, 0.0f, WHITE);
                        rlDrawRenderBatchActive();
                    EndBlendMode();
                }
            }
        EndShaderMode();
    EndTextureMode();
    EndDrawing();

    for (int variant = 0; variant < 2; variant++)
    {
        BeginDrawing();
        BeginTextureMode(out);
            ClearBackground(BLANK);
            BeginShaderMode(post);
                setTex(post, "u_bloomTex", dummy);
                setTex(post, "u_lutTex", dummy);
                setF(post, "u_bloomEnabled", 0.0f);
                setF(post, "u_tonemapEnabled", 1.0f);
                setF(post, "u_exposure", 1.0f);
                setF(post, "u_chromaticEnabled", 0.0f);
                setF(post, "u_vignetteEnabled", 0.0f);
                setF(post, "u_colorGradeEnabled", 0.0f);
                setF(post, "u_lutEnabled", 0.0f);
                setF(post, "u_radialBlurEnabled", 0.0f);
                setF(post, "u_hueRestore", variant ? 1.0f : 0.0f);
                rlDisableColorBlend();
                blit(scene.texture, PW*N, PH);
                rlDrawRenderBatchActive();
                rlEnableColorBlend();
            EndShaderMode();
        EndTextureMode();
        EndDrawing();
        px = (uint16_t *)rlReadTexturePixels(out.texture.id, PW*N, PH,
                                             RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
        if (px == NULL) { why = "ramp readback returned nothing"; goto done; }
        for (int i = 0; i < N; i++)
        {
            sampleHalfRGBA(px, PW*N, i*PW + PW/2, PH/4,     got[variant][i]);
            sampleHalfRGBA(px, PW*N, i*PW + PW/2, 3*(PH/4), gotN[variant][i]);
        }
        RL_FREE(px); px = NULL;
    }

    // (1) MAGNITUDE BOUND below the shoulder. Candidate H's weight is constant, so it no
    //     longer vanishes under peak 1 and this can never be zero again. What still has to
    //     hold is (a) that an ACHROMATIC surface still cannot move at all, and (b) that the
    //     saturated shift stays under a stated ceiling. Measured worst case at
    //     u_hueRestore = 1.0 — this scenario's value, i.e. TWICE the shipping 0.5 — is
    //     0.206 at peak 0.98, and the analytic maximum is 0.212 as peak -> 1
    //     (|aces(0.35) - 0.35*aces(1)|). The ceiling sits just above that with no slack to
    //     spare, so any reformulation that grows the change trips this immediately.
    const float TMS_SAT_LIMIT = 0.22f;
    // The neutral row must not move AT ALL. This is the invariant that keeps the change
    // confined to saturated content, and it is exact, not a tolerance.
    const float TMS_NEUTRAL_LIMIT = 1.0e-4f;
    for (int i = 0; i < N; i++)
    {
        float d  = rgbDistanceMax(got[0][i],  got[1][i]);
        float dn = rgbDistanceMax(gotN[0][i], gotN[1][i]);
        if (getenv("BRIGHT_DEBUG"))
            printf("      peak %5.1f  off(%.4f %.4f %.4f) on(%.4f %.4f %.4f) d %.5f  dNeutral %.5f  chroma %.4f -> %.4f\n",
                   peaks[i], got[0][i][0], got[0][i][1], got[0][i][2],
                   got[1][i][0], got[1][i][1], got[1][i][2], d, dn,
                   chromaOf(got[0][i]), chromaOf(got[1][i]));
        if (dn > TMS_NEUTRAL_LIMIT)
        {
            snprintf(s_brightWhy, sizeof(s_brightWhy),
                     "hue restoration moved an ACHROMATIC surface at peak %.2f (d %.5f):"
                     " the change is no longer confined to saturated content, which is the"
                     " only thing keeping it out of a full whole-scene re-approval",
                     peaks[i], dn);
            why = s_brightWhy; goto done;
        }
        if (peaks[i] < 1.0f && d > TMS_SAT_LIMIT)
        {
            snprintf(s_brightWhy, sizeof(s_brightWhy),
                     "hue restoration moves saturated material below the shoulder at peak %.2f"
                     " by %.5f (ceiling %.2f) — larger than the approved whole-scene shift",
                     peaks[i], d, TMS_SAT_LIMIT);
            why = s_brightWhy; goto done;
        }
    }
    // (2) It must also actually do something, or the trade bought nothing.
    {
        float gain = chromaOf(got[1][4]) - chromaOf(got[0][4]);   // peak 2.5, mid shoulder
        if (gain < 0.02f)
        {
            snprintf(s_brightWhy, sizeof(s_brightWhy),
                     "hue restoration changed nothing in the shoulder (chroma gain %.4f)", gain);
            why = s_brightWhy; goto done;
        }
    }
    // (3) MONOTONE. The point of the whole trade: on a strictly rising input, no channel
    //     may go backwards. A channel that dips and recovers is a colour band with an edge
    //     on each side — that is the "rainbow rim", and it is what this replaces the old
    //     bit-identity assertion with. The tolerance covers the output dither (one LSB,
    //     a position hash, so two neighbouring patches do not cancel) plus half-float
    //     readback; the defect it exists to catch measured 0.070, an order of magnitude up.
    {
        const float TMS_MONO_TOL = 0.012f;
        for (int i = TMS_FIX + 1; i < N; i++)
        {
            for (int c = 0; c < 3; c++)
            {
                float drop = got[1][i - 1][c] - got[1][i][c];
                if (drop > TMS_MONO_TOL)
                {
                    snprintf(s_brightWhy, sizeof(s_brightWhy),
                             "channel %d goes BACKWARDS across a rising ramp: peak %.2f -> %.2f"
                             " drops %.4f (tol %.3f) — a non-monotone channel on a monotone"
                             " input is a colour band",
                             c, peaks[i - 1], peaks[i], drop, TMS_MONO_TOL);
                    why = s_brightWhy; goto done;
                }
            }
        }
    }

done:
    SetTraceLogLevel(LOG_WARNING);
    if (px) RL_FREE(px);
    if (white.id) UnloadTexture(white);
    if (dummy.id) UnloadTexture(dummy);
    if (fx.id) UnloadShader(fx);
    if (post.id) UnloadShader(post);
    if (out.id) UnloadRenderTexture(out);
    if (scene.id) UnloadRenderTexture(scene);
    return why;
}

// rlDisableColorBlend() is FLUSH-SCOPED, and that is a footgun, not an rlvk quirk:
// the toggle only reaches the GPU when the batch is drawn, exactly as glDisable(GL_BLEND)
// does under GL. So the very natural
//     rlDisableColorBlend(); DrawTexturePro(...); rlEnableColorBlend();
// re-enables blending BEFORE the draw it was meant to protect ever flushes, and the draw
// lands blended. Found while building bright_vfx, whose composite came back multiplied by
// the scene's accumulated alpha (additive VFX push scene alpha above 1) and read back
// values above 1.0 that ACES can never produce. core/post_fx.c had the same shape in two
// places. This scenario pins both halves of the rule so neither can regress silently.
static const char *sc_colorblend_flush(void)
{
    const char *FS = "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform vec4 uSrc;\n"
        "void main(){ finalColor = uSrc; }\n";
    RenderTexture2D rt = fmtRT(64, 64, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    Shader sh = LoadShaderFromMemory(NULL, FS);
    Texture2D white = {0};
    const char *why = NULL;
    float got[2][4];
    if (rt.id == 0) { why = "could not create the probe target"; goto done; }
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault()) { why = "probe shader fell back to default"; goto done; }
    { Image wi = GenImageColor(8, 8, WHITE); white = LoadTextureFromImage(wi); UnloadImage(wi); }

    for (int variant = 0; variant < 2; variant++)
    {
        Vector4 src = { 1.0f, 0.0f, 0.0f, 0.25f };
        BeginDrawing();
        BeginTextureMode(rt);
            ClearBackground(BLANK);
            BeginShaderMode(sh);
                // ground truth to blend against: an opaque 0.5 grey, flushed on its own
                Vector4 grey = { 0.5f, 0.5f, 0.5f, 1.0f };
                SetShaderValue(sh, GetShaderLocation(sh, "uSrc"), &grey, SHADER_UNIFORM_VEC4);
                DrawTexturePro(white, (Rectangle){0,0,8,8}, (Rectangle){0,0,64,64},
                               (Vector2){0,0}, 0.0f, WHITE);
                rlDrawRenderBatchActive();
                SetShaderValue(sh, GetShaderLocation(sh, "uSrc"), &src, SHADER_UNIFORM_VEC4);
                rlDisableColorBlend();
                DrawTexturePro(white, (Rectangle){0,0,8,8}, (Rectangle){0,0,64,64},
                               (Vector2){0,0}, 0.0f, WHITE);
                if (variant == 0) rlDrawRenderBatchActive();   // flush INSIDE the window
                rlEnableColorBlend();
            EndShaderMode();
        EndTextureMode();
        EndDrawing();
        uint16_t *px = (uint16_t *)rlReadTexturePixels(rt.texture.id, 64, 64,
                                                       RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
        if (px == NULL) { why = "probe readback returned nothing"; goto done; }
        sampleHalfRGBA(px, 64, 32, 32, got[variant]);
        RL_FREE(px);
    }

    // flushed inside the window: a true overwrite, alpha 0.25 and all
    if (fabsf(got[0][0] - 1.0f) > 0.02f || fabsf(got[0][3] - 0.25f) > 0.02f)
    { why = "rlDisableColorBlend did not overwrite even with the draw flushed inside it"; goto done; }
    // re-enabled before the flush: the draw blends after all - 1.0*0.25 + 0.5*0.75
    if (fabsf(got[1][0] - 0.625f) > 0.02f)
    { why = "rlEnableColorBlend before the flush no longer restores blending (semantics changed)"; goto done; }

done:
    if (white.id) UnloadTexture(white);
    if (sh.id) UnloadShader(sh);
    if (rt.id) UnloadRenderTexture(rt);
    return why;
}

static void blit(Texture2D src, int dstW, int dstH);

// What the screen-space fluid surface's separable filter chain actually costs, and
// whether halving its internal resolution buys anything. core/fluid runs 4 rounds x 2
// passes of core/fluid/shaders/fluid_depth_narrow_range.fs over R32F depth at NATIVE
// resolution on the HIGH tier — 21 taps per pass at filterRadius 10. The early-out on
// empty pixels means the cost should scale with fluid COVERAGE rather than screen area;
// this measures whether that holds, by filtering the same synthetic blob at full and
// half resolution, LCG-interleaved in one run (perf traps: docs/PROGRESS.md).
static const char *sc_perf_ssf_filter(void)
{
    const int FW = 1280, FH = 720;
    /* The harness runs from its cache directory, so a relative repo path finds
     * nothing — and raylib does not fail when a shader file is missing, it
     * substitutes the DEFAULT shader and hands back a perfectly valid non-zero
     * id. This scenario spent its whole existence benchmarking that default
     * shader and reporting a native/half delta of -0.16 ms, which is exactly the
     * "no difference" answer a real measurement would never have given. Both
     * halves are needed: the absolute path, and a guard that can actually fire. */
    const char *repoRoot = getenv("RLVK_REPO_ROOT");
    if (!repoRoot) return "RLVK_REPO_ROOT not set (run via scripts/run_rlvk_visual_test.sh)";
    char filterPath[1024];
    snprintf(filterPath, sizeof(filterPath),
             "%s/core/fluid/shaders/fluid_depth_narrow_range.fs", repoRoot);
    Shader filter = LoadShader(NULL, filterPath);
    if (filter.id == 0 || filter.id == rlGetShaderIdDefault())
        return "fluid_depth_narrow_range.fs did not load (raylib fell back to the default shader)";

    RenderTexture2D src[3], a[3], b[3];
    for (int i = 0; i < 3; i++)
    {
        int w = (i == 1) ? FW/2 : FW, h = (i == 1) ? FH/2 : FH;   /* 0,2 native; 1 half */
        src[i] = fmtRT(w, h, RL_PIXELFORMAT_UNCOMPRESSED_R32);
        a[i]   = fmtRT(w, h, RL_PIXELFORMAT_UNCOMPRESSED_R32);
        b[i]   = fmtRT(w, h, RL_PIXELFORMAT_UNCOMPRESSED_R32);
        // A blob of "fluid depth" over ~12% of the frame; everything else stays far,
        // which is the branch the shader early-outs on.
        BeginTextureMode(src[i]);
            ClearBackground(WHITE);                      // 1.0 = empty
            DrawCircle(w/2, h/2, (float)h*0.20f, (Color){128, 0, 0, 255});   // 0.502 = surface
        EndTextureMode();
    }

    int locTexel = GetShaderLocation(filter, "u_texel");
    int locDir   = GetShaderLocation(filter, "u_direction");
    int locRange = GetShaderLocation(filter, "u_depthRange");
    int locKern  = GetShaderLocation(filter, "u_kernelRadius");
    int locRad   = GetShaderLocation(filter, "u_filterRadius");
    int locFill  = GetShaderLocation(filter, "u_fillHoles");
    int locProj  = GetShaderLocation(filter, "u_projection");
    int locInv   = GetShaderLocation(filter, "u_inverseProjection");
    Matrix proj = MatrixFrustum(-1, 1, -0.6, 0.6, 1.0, 1000.0);
    Matrix inv = MatrixInvert(proj);
    float depthRange = 0.022f*9.0f, kernelRadius = 0.022f;   /* kernelRadius varies per variant */

    unsigned int seed = 20260811u;
    double acc[3] = {0, 0, 0};
    int cnt[3] = {0, 0, 0};
    const int WARM = 40, N = 400;
    for (int f = 0; f < WARM + N; f++)
    {
        seed = seed*1103515245u + 12345u;
        /* 0 = native r10, 1 = half r5 (the same world footprint at a quarter of
         * the pixels), 2 = native r28 — the radius the HIGH tier actually ships.
         * Variant 2 has the SAME pass count as 0 and ~3x the fragment work, so it
         * separates "this is fragment-bound" from "this is per-pass overhead".
         * Without it the 0-vs-1 delta alone is unreadable: a wash could mean
         * either that half resolution does not help, or that nothing here is
         * measuring fragment cost at all. */
        int v = (int)(((seed >> 16) & 0xFFFFu) % 3u);
        int w = (v == 1) ? FW/2 : FW, h = (v == 1) ? FH/2 : FH;
        int filterRadius = (v == 1) ? 5 : ((v == 2) ? 28 : 10);
        Vector2 texel = { 1.0f/(float)w, 1.0f/(float)h };

        double t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        for (int round = 0; round < 4; round++)
        {
            Vector2 horizontal = {1, 0}, vertical = {0, 1};
            Texture2D source = round ? b[v].texture : src[v].texture;
            int fill = (round == 0);
            BeginTextureMode(a[v]);
                ClearBackground(WHITE);
                BeginShaderMode(filter);
                    SetShaderValue(filter, locTexel, &texel, SHADER_UNIFORM_VEC2);
                    SetShaderValue(filter, locDir, &horizontal, SHADER_UNIFORM_VEC2);
                    SetShaderValue(filter, locRange, &depthRange, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(filter, locKern, &kernelRadius, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(filter, locRad, &filterRadius, SHADER_UNIFORM_INT);
                    SetShaderValue(filter, locFill, &fill, SHADER_UNIFORM_INT);
                    SetShaderValueMatrix(filter, locProj, proj);
                    SetShaderValueMatrix(filter, locInv, inv);
                    DrawTextureRec(source, (Rectangle){0,0,(float)w,-(float)h}, (Vector2){0,0}, WHITE);
                EndShaderMode();
            EndTextureMode();
            fill = 0;
            BeginTextureMode(b[v]);
                ClearBackground(WHITE);
                BeginShaderMode(filter);
                    SetShaderValue(filter, locDir, &vertical, SHADER_UNIFORM_VEC2);
                    SetShaderValue(filter, locFill, &fill, SHADER_UNIFORM_INT);
                    DrawTextureRec(a[v].texture, (Rectangle){0,0,(float)w,-(float)h}, (Vector2){0,0}, WHITE);
                EndShaderMode();
            EndTextureMode();
        }
        blit(b[v].texture, W, H);
        EndDrawing();
        if (f >= WARM) { acc[v] += (GetTime() - t0)*1000.0; cnt[v]++; }
    }

    printf("  [perf ssf_filter] 8 passes | native %dx%d: %.3f ms | half %dx%d: %.3f ms"
           " | native 3x kernel: %.3f ms || resolution delta %.3f ms, kernel delta %.3f ms\n",
           FW, FH, acc[0]/cnt[0], FW/2, FH/2, acc[1]/cnt[1], acc[2]/cnt[2],
           acc[0]/cnt[0] - acc[1]/cnt[1], acc[2]/cnt[2] - acc[0]/cnt[0]);
    for (int i = 0; i < 3; i++) { UnloadRenderTexture(src[i]); UnloadRenderTexture(a[i]); UnloadRenderTexture(b[i]); }
    UnloadShader(filter);
    return NULL;
}

// What ONE compute dispatch costs, independent of the work inside it.
//
// core/fluid's PBD solver measured 4.4 ms in-game for 2,048 particles across 9
// dispatches — 72 workgroups of actual work, which no GPU takes milliseconds
// over. That points at per-CALL overhead rather than compute, and this file
// already documents the same shape for uploads (§ PROGRESS: ~0.5-0.65 ms per
// rlvkUploadBuffer CALL, because the render pass is torn down and rebuilt each
// time). If dispatches pay that too, it is a backend cost that every compute
// consumer pays — the GPU particle system included — and it belongs here, not
// in the fluid module.
//
// Same trivial kernel either way; only the NUMBER of dispatches differs, and the
// variants are LCG-interleaved because consecutive A/B blocks are worthless on
// this platform (see docs/PROGRESS.md perf traps).
static const char *sc_perf_dispatch_count(void)
{
    const char *cs =
        "#version 430\n"
        "layout(local_size_x = 256) in;\n"
        "layout(std430, binding = 0) buffer B { float v[]; };\n"
        "uniform float u_add;\n"
        "void main(){ uint i = gl_GlobalInvocationID.x; if (i < 2048u) v[i] += u_add; }\n";
    unsigned int shader = rlLoadShader(cs, RL_COMPUTE_SHADER);
    if (shader == 0) return "compute shader would not compile";
    unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    if (program == 0) return "compute program would not link";

    static float seedData[2048];
    unsigned int ssbo = rlLoadShaderBuffer(sizeof(seedData), seedData, RL_DYNAMIC_COPY);
    if (ssbo == 0) { rlUnloadShaderProgram(program); return "SSBO would not allocate"; }

    int loc = rlGetLocationUniform(program, "u_add");
    float add = 1.0f;
    unsigned int seed = 7771u;
    double acc[3] = {0, 0, 0};
    int cnt[3] = {0, 0, 0};
    const int WARM = 60, N = 500;
    const int few = 1, many = 9;              // 9 = the PBD solve's dispatch count

    for (int f = 0; f < WARM + N; f++)
    {
        seed = seed*1103515245u + 12345u;
        int variant = (int)((seed >> 16) & 1u);

        /* Variant 0 dispatches INSIDE the frame (rlvk splits the render pass per
         * call); variant 1 dispatches OUTSIDE it, which is where core/fluid's PBD
         * actually runs — main.c updates it before BeginDrawing, so every call
         * takes rlvk's one-shot path: allocate a pool, submit, vkQueueWaitIdle,
         * destroy. Same kernel, same count; only the frame scope differs. */
        double t0 = GetTime();
        if (variant)
        {
            rlEnableShader(program);
            rlBindShaderBuffer(ssbo, 0);
            for (int d = 0; d < many; d++)
            {
                rlSetUniform(loc, &add, RL_SHADER_UNIFORM_FLOAT, 1);
                rlComputeShaderDispatch(2048/256, 1, 1);
            }
            rlDisableShader();
        }
        BeginDrawing();
        ClearBackground(BLACK);
        if (!variant)
        {
            rlEnableShader(program);
            rlBindShaderBuffer(ssbo, 0);
            for (int d = 0; d < many; d++)
            {
                rlSetUniform(loc, &add, RL_SHADER_UNIFORM_FLOAT, 1);
                rlComputeShaderDispatch(2048/256, 1, 1);
            }
            rlDisableShader();
        }
        EndDrawing();
        if (f >= WARM) { acc[variant] += (GetTime() - t0)*1000.0; cnt[variant]++; }
    }

    double inFrame = acc[0]/cnt[0], outOfFrame = acc[1]/cnt[1];
    printf("  [perf dispatch_count] %d dispatches IN frame %.3f ms | OUTSIDE frame %.3f ms | one-shot penalty %.3f ms (%.3f ms per call)\n",
           many, inFrame, outOfFrame, outOfFrame - inFrame, (outOfFrame - inFrame)/(double)many);
    (void)few;
    rlUnloadShaderBuffer(ssbo);
    rlUnloadShaderProgram(program);
    return NULL;
}

static void blit(Texture2D src, int dstW, int dstH)
{
    DrawTexturePro(src, (Rectangle){0, 0, (float)src.width, (float)src.height},
                   (Rectangle){0, 0, (float)dstW, (float)dstH}, (Vector2){0, 0}, 0, WHITE);
}

// The postFX pipeline's shape at the game's real resolution: full-res HDR scene target, then the
// bloom pyramid (bright 1/4 -> 1/8 -> 1/16 -> back up) and a composite. `withBloom=false` isolates
// the full-res target alone, so the difference is what the pyramid costs.
static const char *perfPostFX(bool withBloom, int format, const char *label)
{
    const int FW = 1280, FH = 720;
    Camera3D cam = cam3d();
    RenderTexture2D main = fmtRT(FW, FH, format);
    RenderTexture2D bloom = fmtRT(FW / 4, FH / 4, format);
    RenderTexture2D df0 = fmtRT(FW / 8, FH / 8, format);
    RenderTexture2D df1 = fmtRT(FW / 16, FH / 16, format);

    for (int f = 0; f < 40 + 260; f++)
    {
        if (f == 40) { /* warm-up done */ }
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(main);
            ClearBackground((Color){8, 8, 16, 255});
            BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, RED); EndMode3D();
        EndTextureMode();
        if (withBloom)
        {
            BeginTextureMode(bloom); blit(main.texture,  FW/4,  FH/4);  EndTextureMode();
            BeginTextureMode(df0);   blit(bloom.texture, FW/8,  FH/8);  EndTextureMode();
            BeginTextureMode(df1);   blit(df0.texture,   FW/16, FH/16); EndTextureMode();
            BeginTextureMode(df0);   blit(df1.texture,   FW/8,  FH/8);  EndTextureMode();
            BeginTextureMode(bloom); blit(df0.texture,   FW/4,  FH/4);  EndTextureMode();
        }
        blit(main.texture, W, H);   // composite to the (small) window
        EndDrawing();
        if (f == 39) { /* start timing below */ }
    }
    // second, timed run
    double t0 = GetTime();
    const int N = 260;
    for (int f = 0; f < N; f++)
    {
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(main);
            ClearBackground((Color){8, 8, 16, 255});
            BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, RED); EndMode3D();
        EndTextureMode();
        if (withBloom)
        {
            BeginTextureMode(bloom); blit(main.texture,  FW/4,  FH/4);  EndTextureMode();
            BeginTextureMode(df0);   blit(bloom.texture, FW/8,  FH/8);  EndTextureMode();
            BeginTextureMode(df1);   blit(df0.texture,   FW/16, FH/16); EndTextureMode();
            BeginTextureMode(df0);   blit(df1.texture,   FW/8,  FH/8);  EndTextureMode();
            BeginTextureMode(bloom); blit(df0.texture,   FW/4,  FH/4);  EndTextureMode();
        }
        blit(main.texture, W, H);
        EndDrawing();
    }
    double ms = (GetTime() - t0) * 1000.0 / N;
    printf("  [perf %s] %.3f ms/frame (%.1f fps)\n", label, ms, 1000.0 / ms);

    UnloadRenderTexture(main); UnloadRenderTexture(bloom);
    UnloadRenderTexture(df0);  UnloadRenderTexture(df1);
    return NULL;
}

// Marginal cost of each EXTRA full-resolution offscreen pass (scene RT -> distort RT -> postFX RT
// ... every full-res target a frame bounces through). `passes` counts full-res targets: 1 = scene
// only, 2 = scene + one full-res copy, 3 = two copies.
static const char *perfFullResChain(int passes, const char *label)
{
    const int FW = 1280, FH = 720, FMT = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    Camera3D cam = cam3d();
    RenderTexture2D rt[3] = {0};
    for (int i = 0; i < passes && i < 3; i++) rt[i] = fmtRT(FW, FH, FMT);

    double t0 = 0; const int WARM = 40, N = 260;
    for (int f = 0; f < WARM + N; f++)
    {
        if (f == WARM) t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(rt[0]);
            ClearBackground((Color){8, 8, 16, 255});
            BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, RED); EndMode3D();
        EndTextureMode();
        for (int i = 1; i < passes && i < 3; i++)
        {
            BeginTextureMode(rt[i]); blit(rt[i-1].texture, FW, FH); EndTextureMode();
        }
        blit(rt[(passes < 3 ? passes : 3) - 1].texture, W, H);
        EndDrawing();
    }
    double ms = (GetTime() - t0) * 1000.0 / N;
    printf("  [perf %s] %.3f ms/frame (%.1f fps)\n", label, ms, 1000.0 / ms);
    for (int i = 0; i < passes && i < 3; i++) UnloadRenderTexture(rt[i]);
    return NULL;
}

// Dynamic-mesh re-upload cost — raylib's UpdateModelAnimation does CPU skinning and pushes the
// whole vertex buffer every frame, per character. In rlvk each upload also ENDS and re-opens the
// render pass (rlvkUploadBuffer). This times draw-only vs draw+re-upload of the same mesh IN ONE
// PROCESS, back to back: the machine's ±2 ms run-to-run variance cancels in the delta.
static const char *sc_perf_dynmesh(void)
{
    Camera3D cam = cam3d();
    Mesh mesh = GenMeshPlane(3.0f, 3.0f, 40, 40);   // ~9.6k vertices, the scale of a character mesh
    UploadMesh(&mesh, true);                         // dynamic
    Material mat = LoadMaterialDefault();
    Matrix xf = MatrixIdentity();
    size_t posBytes = (size_t)mesh.vertexCount * 3 * sizeof(float);

    // METHODOLOGY (learned the hard way, see PROGRESS.md): timing variant A as one block and
    // variant B as a second block is INVALID on this platform - whichever block ran first measured
    // ~10 ms and the second ~1.8 ms, with the SAME work, because presentation throttling changes
    // phase during the run. Interleave the variants frame by frame instead: every slow phase then
    // hits both variants equally and only the real difference survives.
    unsigned int seed = 22222u;
    double acc[3] = {0, 0, 0};
    int cnt[3] = {0, 0, 0};
    const int WARM = 60, N = 600;
    for (int f = 0; f < WARM + N; f++)
    {
        // NOT f&1: RLVK_FRAME_INDEX_COUNT is 2, so alternating aligns the variants with the frame
        // ring and each one systematically lands on the same slot (that bias produced "1 upload
        // costs 4.2 ms, 2 uploads cost 1.8 ms"). Decorrelate with a cheap LCG.
        seed = seed * 1103515245u + 12345u;
        int variant = (int)((seed >> 16) & 1u);
        double t0 = GetTime();
        if (variant)
        {
            for (int v = 0; v < mesh.vertexCount; v++) mesh.vertices[v*3 + 1] = 0.01f * (float)(v & 7);
            UpdateMeshBuffer(mesh, 0, mesh.vertices, (int)posBytes, 0);
            UpdateMeshBuffer(mesh, 2, mesh.normals,  (int)posBytes, 0);
        }
        BeginDrawing();
        ClearBackground((Color){8, 8, 16, 255});
        BeginMode3D(cam); DrawMesh(mesh, mat, xf); EndMode3D();
        EndDrawing();
        if (f >= WARM) { acc[variant] += (GetTime() - t0) * 1000.0; cnt[variant]++; }
    }
    double ms[2] = { acc[0] / cnt[0], acc[1] / cnt[1] };
    printf("  [perf dynmesh %d verts] draw-only %.3f ms | +re-upload %.3f ms | cost %.3f ms/mesh/frame\n",
           mesh.vertexCount, ms[0], ms[1], ms[1] - ms[0]);
    UnloadMesh(mesh);
    return NULL;
}

// The same mesh re-upload, but INSIDE an FBO scope whose depth twin is live (something sampled it,
// so sampleWanted is latched). rlvkUploadBuffer suspends the scope through the full
// rlDisableFramebuffer path, which runs the depth->buffer->twin bounce - so every upload inside a
// scene render target may be paying a whole-attachment depth round trip on top of the pass split.
// Compare the delta here against sc_perf_dynmesh's (same work on the swapchain, no twin).
static const char *sc_perf_upload_fbo(void)
{
    const int FW = 1280, FH = 720;
    Camera3D cam = cam3d();
    RenderTexture2D rt = LoadRenderTexture(FW, FH);   // has a depth attachment -> has a twin
    Mesh mesh = GenMeshPlane(3.0f, 3.0f, 40, 40);
    UploadMesh(&mesh, true);
    Material mat = LoadMaterialDefault();
    Matrix xf = MatrixIdentity();
    size_t posBytes = (size_t)mesh.vertexCount * 3 * sizeof(float);

    unsigned int seed = 9001u;
    double acc[3] = {0, 0, 0};
    int cnt[3] = {0, 0, 0};
    const int WARM = 60, N = 600;
    for (int f = 0; f < WARM + N; f++)
    {
        seed = seed * 1103515245u + 12345u;
        int variant = (int)((seed >> 16) & 1u);
        double t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(rt);
            ClearBackground((Color){8, 8, 16, 255});
            if (variant)
            {
                for (int v = 0; v < mesh.vertexCount; v++) mesh.vertices[v*3 + 1] = 0.01f * (float)(v & 7);
                UpdateMeshBuffer(mesh, 0, mesh.vertices, (int)posBytes, 0);
                UpdateMeshBuffer(mesh, 2, mesh.normals,  (int)posBytes, 0);
            }
            BeginMode3D(cam); DrawMesh(mesh, mat, xf); EndMode3D();
        EndTextureMode();
        blit(rt.texture, W, H);
        DrawTextureRec(rt.depth, (Rectangle){0,0,1,1}, (Vector2){0,0}, WHITE); // bind the depth twin: latches sampleWanted
        EndDrawing();
        if (f >= WARM) { acc[variant] += (GetTime() - t0) * 1000.0; cnt[variant]++; }
    }
    printf("  [perf upload-in-FBO %d verts] no-upload %.3f ms | +2 uploads %.3f ms | cost %.3f ms/frame\n",
           mesh.vertexCount, acc[0]/cnt[0], acc[1]/cnt[1], acc[1]/cnt[1] - acc[0]/cnt[0]);
    UnloadMesh(mesh); UnloadRenderTexture(rt);
    return NULL;
}

// Does the NUMBER of full-resolution offscreen targets matter? (scene RT -> distort RT -> postFX
// RT -> ...). 1 vs 3 full-res passes, LCG-interleaved in one run so neither presentation phase nor
// the 2-slot frame ring can bias a variant. This is the architectural question: is it worth
// collapsing the full-res chain, or is only the FIRST target expensive?
static const char *sc_perf_fullres_ab(void)
{
    const int FW = 1280, FH = 720, FMT = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    Camera3D cam = cam3d();
    RenderTexture2D rt[3];
    for (int i = 0; i < 3; i++) rt[i] = fmtRT(FW, FH, FMT);

    unsigned int seed = 4242u;
    double acc[3] = {0, 0, 0};
    int cnt[3] = {0, 0, 0};
    const int WARM = 60, N = 600;
    for (int f = 0; f < WARM + N; f++)
    {
        seed = seed * 1103515245u + 12345u;
        int variant = (int)((seed >> 16) & 1u);   // 0 = one full-res pass, 1 = three
        int passes = variant ? 3 : 1;
        double t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(rt[0]);
            ClearBackground((Color){8, 8, 16, 255});
            BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, RED); EndMode3D();
        EndTextureMode();
        for (int i = 1; i < passes; i++)
        {
            BeginTextureMode(rt[i]); blit(rt[i-1].texture, FW, FH); EndTextureMode();
        }
        blit(rt[passes-1].texture, W, H);
        EndDrawing();
        if (f >= WARM) { acc[variant] += (GetTime() - t0) * 1000.0; cnt[variant]++; }
    }
    printf("  [perf fullres A/B] 1 pass %.3f ms | 3 passes %.3f ms | cost %.3f ms per extra full-res pass\n",
           acc[0]/cnt[0], acc[1]/cnt[1], (acc[1]/cnt[1] - acc[0]/cnt[0]) / 2.0);
    for (int i = 0; i < 3; i++) UnloadRenderTexture(rt[i]);
    return NULL;
}

// What a shadow-map capture pass costs at 2048² vs 1024², LCG-interleaved in one run. Mirrors
// env_shadow's target shape: an R32F color attachment written by a trivial depth shader, plus the
// depth attachment it depth-tests against. Answers "is dropping the shadow resolution worth it?"
// with a number instead of an opinion (the in-file note claiming 1024 changed nothing was measured
// through the vsync-quantized FPS counter, which could not have shown a 4 ms win).
static const char *sc_perf_shadow_ab(void)
{
    Camera3D cam = cam3d();
    RenderTexture2D big = LoadRenderTexture(2048, 2048);
    RenderTexture2D small = LoadRenderTexture(1024, 1024);

    unsigned int seed = 777u;
    double acc[3] = {0, 0, 0};
    int cnt[3] = {0, 0, 0};
    const int WARM = 60, N = 500;
    for (int f = 0; f < WARM + N; f++)
    {
        seed = seed * 1103515245u + 12345u;
        int variant = (int)((seed >> 16) & 1u);   // 0 = 1024², 1 = 2048²
        RenderTexture2D *sm = variant ? &big : &small;
        double t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(*sm);                     // the capture pass
            ClearBackground(WHITE);
            // Casters must FILL the map, as they do in-game (the light frustum is fit to the
            // arena). A probe that rasterizes one small cube measures no fill and wrongly
            // concludes that the map's resolution is free.
            BeginMode3D(cam);
                DrawCube((Vector3){0,0,-1}, 40.0f, 40.0f, 0.1f, RED);
                DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, GREEN);
            EndMode3D();
        EndTextureMode();
        BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, BLUE); EndMode3D(); // main pass
        EndDrawing();
        if (f >= WARM) { acc[variant] += (GetTime() - t0) * 1000.0; cnt[variant]++; }
    }
    printf("  [perf shadow A/B] 1024² %.3f ms | 2048² %.3f ms | dropping to 1024 saves %.3f ms/frame\n",
           acc[0]/cnt[0], acc[1]/cnt[1], acc[1]/cnt[1] - acc[0]/cnt[0]);
    UnloadRenderTexture(big); UnloadRenderTexture(small);
    return NULL;
}

// What PCF tap count costs. maps/toolkit/shaders/ground_shadow.fs takes 16 Poisson taps out of the
// shadow map for every ground pixel, every frame - a per-pixel cost that is INDEPENDENT of the
// shadow map's resolution (which is why 2048² vs 1024² measures identical while turning shadows
// off entirely is worth many ms). 16 vs 4 taps over a 1280x720 target, LCG-interleaved.
static const char *sc_perf_pcf_ab(void)
{
    const char *FS_TMPL = "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
        "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
        "void main(){ float s = 0.0; vec2 uv = fragTexCoord;\n"
        "  for (int i = 0; i < %d; i++) {\n"
        "    vec2 o = vec2(cos(float(i)*2.4), sin(float(i)*2.4)) * 0.003;\n"
        "    s += step(0.5, texture(texture0, uv + o).r); }\n"
        "  finalColor = vec4(vec3(s / float(%d)), 1.0); }\n";
    char src16[640], src4[640];
    snprintf(src16, sizeof(src16), FS_TMPL, 16, 16);
    snprintf(src4,  sizeof(src4),  FS_TMPL, 4, 4);
    Shader sh[2] = { LoadShaderFromMemory(NULL, src4), LoadShaderFromMemory(NULL, src16) };

    RenderTexture2D dst = fmtRT(1280, 720, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    RenderTexture2D smap = LoadRenderTexture(2048, 2048);
    BeginTextureMode(smap); ClearBackground(WHITE); EndTextureMode();

    unsigned int seed = 31337u;
    double acc[3] = {0, 0, 0};
    int cnt[3] = {0, 0, 0};
    const int WARM = 60, N = 500;
    for (int f = 0; f < WARM + N; f++)
    {
        seed = seed * 1103515245u + 12345u;
        int variant = (int)((seed >> 16) & 1u);   // 0 = 4 taps, 1 = 16 taps
        double t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(dst);
            BeginShaderMode(sh[variant]);
                blit(smap.texture, 1280, 720);
            EndShaderMode();
        EndTextureMode();
        blit(dst.texture, W, H);
        EndDrawing();
        if (f >= WARM) { acc[variant] += (GetTime() - t0) * 1000.0; cnt[variant]++; }
    }
    printf("  [perf pcf A/B @1280x720] 4 taps %.3f ms | 16 taps %.3f ms | the extra 12 taps cost %.3f ms/frame\n",
           acc[0]/cnt[0], acc[1]/cnt[1], acc[1]/cnt[1] - acc[0]/cnt[0]);
    UnloadRenderTexture(dst); UnloadRenderTexture(smap);
    UnloadShader(sh[0]); UnloadShader(sh[1]);
    return NULL;
}

// Pure FBO-scope-switch cost. A 64x64 target renders essentially nothing, so whatever this
// measures is the switch itself: end the swapchain pass, open the FBO pass, close it, resume the
// swapchain pass with LOAD. Compare against perf_base (no switch at all).
static const char *perfSwitches(int count, const char *label)
{
    RenderTexture2D tiny = fmtRT(64, 64, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Camera3D cam = cam3d();
    double t0 = 0; const int WARM = 40, N = 300;
    for (int f = 0; f < WARM + N; f++)
    {
        if (f == WARM) t0 = GetTime();
        BeginDrawing();
        ClearBackground((Color){8, 8, 16, 255});
        BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.5f, 1.5f, 1.5f, BLUE); EndMode3D();
        for (int i = 0; i < count; i++)
        {
            BeginTextureMode(tiny);
                DrawRectangle(0, 0, 8, 8, RED);
            EndTextureMode();
        }
        EndDrawing();
    }
    double ms = (GetTime() - t0) * 1000.0 / N;
    printf("  [perf %s] %.3f ms/frame (%.1f fps)\n", label, ms, 1000.0 / ms);
    UnloadRenderTexture(tiny);
    return NULL;
}

static const char *sc_perf_switch1(void)  { return perfSwitches(1,  "switch x1   64x64 scope switches"); }
static const char *sc_perf_switch4(void)  { return perfSwitches(4,  "switch x4   64x64 scope switches"); }
static const char *sc_perf_switch8(void)  { return perfSwitches(8,  "switch x8   64x64 scope switches"); }

static const char *sc_perf_fullres1(void) { return perfFullResChain(1, "fullres x1  1280x720 RGBA16F"); }
static const char *sc_perf_fullres2(void) { return perfFullResChain(2, "fullres x2  1280x720 RGBA16F"); }
static const char *sc_perf_fullres3(void) { return perfFullResChain(3, "fullres x3  1280x720 RGBA16F"); }

static const char *sc_perf_hdr_main(void)  { return perfPostFX(false, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, "hdr_main   1280x720 RGBA16F, no bloom"); }
static const char *sc_perf_hdr_bloom(void) { return perfPostFX(true,  RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, "hdr_bloom  1280x720 RGBA16F + pyramid"); }
static const char *sc_perf_ldr_bloom(void) { return perfPostFX(true,  RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,     "ldr_bloom  1280x720 RGBA8   + pyramid"); }

// What offscreen 4x MSAA actually costs on the game's own scene target: a 1280x720 RGBA16F
// colour + sampleable depth texture (exactly ScreenDistort's renderTex), reopened four times a
// frame the way the VFX body/emission layers reopen it - the reopen count matters because the
// colour AND depth resolves run at EVERY render-pass end, not once per frame.
// UNCAPPED=1 or this measures the refresh interval (see LANDMINES "measurement traps").
static const char *perfMsaaRT(int samples, int layers, const char *label)
{
    RenderTexture2D rt = fmtRT(1280, 720, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
    if (rt.id == 0) return "could not create the RGBA16F target";
    rlEnableFramebuffer(rt.id);
    rt.depth.id = rlLoadTextureDepth(1280, 720, false);
    rt.depth.width = 1280; rt.depth.height = 720; rt.depth.mipmaps = 1;
    rlFramebufferAttach(rt.id, rt.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
    int got = rlvkSetFramebufferSamples(rt.id, samples);
    Camera3D cam = cam3d();
    double t0 = 0; const int WARM = 20, N = 120;
    for (int f = 0; f < WARM + N; f++)
    {
        if (f == WARM) t0 = GetTime();
        BeginDrawing();
        ClearBackground(BLACK);
        BeginTextureMode(rt);
            ClearBackground((Color){8,8,16,255});
            BeginMode3D(cam);
                for (int i = 0; i < 120; i++)
                {
                    float a = (float)i * 0.31f;
                    DrawCube((Vector3){cosf(a)*2.2f, sinf(a*1.7f)*1.4f, sinf(a)*2.2f}, 0.3f, 0.3f, 0.3f, BLUE);
                }
            EndMode3D();
        EndTextureMode();
        for (int layer = 0; layer < layers; layer++)   // VFX body/emission reopen the scene target
        {
            rlEnableFramebuffer(rt.id);
            BeginMode3D(cam); DrawCube((Vector3){0,0,0}, 1.0f, 1.0f, 1.0f, RED); EndMode3D();
            rlDrawRenderBatchActive();
            rlDisableFramebuffer();
        }
        DrawTextureRec(rt.texture, (Rectangle){0,0,W,-H}, (Vector2){0,0}, WHITE);
        EndDrawing();
    }
    double ms = (GetTime() - t0) * 1000.0 / N;
    printf("  [perf %s] samples=%d reopens=%d  %.3f ms/frame\n", label, got, layers + 1, ms);
    rlUnloadTexture(rt.texture.id); rlUnloadFramebuffer(rt.id);
    return NULL;
}
static const char *sc_perf_msaa_off(void)  { return perfMsaaRT(1, 3, "msaa_off   1280x720 RGBA16F+depth"); }
static const char *sc_perf_msaa_4x(void)   { return perfMsaaRT(4, 3, "msaa_4x    1280x720 RGBA16F+depth"); }
static const char *sc_perf_msaa_off1(void) { return perfMsaaRT(1, 0, "msaa_off1  1280x720 RGBA16F+depth"); }
static const char *sc_perf_msaa_4x1(void)  { return perfMsaaRT(4, 0, "msaa_4x1   1280x720 RGBA16F+depth"); }

static const char *sc_perf_base(void)   { return perfRun(0);    }
static const char *sc_perf_rt256(void)  { return perfRun(256);  }
static const char *sc_perf_rt2048(void) { return perfRun(2048); }

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
    { "sampler_pair",   sc_sampler_pair },
    { "depth",          sc_depth },
    { "depth_rt",       sc_depth_rt },
    { "depth_mask_clear", sc_depth_mask_clear },
    { "msaa_rt",        sc_msaa_rt },
    { "fbo_switch",     sc_fbo_switch },
    { "soft_depth",     sc_soft_depth },
    { "soft_ground",    sc_soft_ground },
    { "winding_rt",     sc_winding_rt },
    { "gas_projection", sc_gas_projection },
    { "instanced",      sc_instanced },
    { "ssbo_vs",        sc_ssbo_vs },
    { "imm_normal",     sc_imm_normal },
    { "readback",       sc_readback },
    { "float_blend_rt", sc_float_blend_rt },
    { "bright_vfx",     sc_bright_vfx },
    { "bright_vfx_ldr", sc_bright_vfx_ldr },
    { "colorblend_flush", sc_colorblend_flush },
    { "tonemap_shoulder", sc_tonemap_shoulder },
    { "ui_after_rt",    sc_ui_after_rt },
    { "stress",         sc_stress },
    { "shadow_proj",    sc_shadow_proj },
    { "shadow_cast",    sc_shadow_cast },
    { "shadow_pipeline",sc_shadow_pipeline },
    { "ubo_arena",      sc_ubo_arena },
    { "perf_base",      sc_perf_base },
    { "perf_dynmesh",   sc_perf_dynmesh },
    { "perf_upload_fbo",sc_perf_upload_fbo },
    { "perf_fullres_ab",sc_perf_fullres_ab },
    { "perf_ssf_filter", sc_perf_ssf_filter },
    { "perf_dispatch_count", sc_perf_dispatch_count },
    { "perf_shadow_ab", sc_perf_shadow_ab },
    { "perf_pcf_ab",    sc_perf_pcf_ab },
    { "perf_switch1",   sc_perf_switch1 },
    { "perf_switch4",   sc_perf_switch4 },
    { "perf_switch8",   sc_perf_switch8 },
    { "perf_fullres1",  sc_perf_fullres1 },
    { "perf_fullres2",  sc_perf_fullres2 },
    { "perf_fullres3",  sc_perf_fullres3 },
    { "perf_hdr_main",  sc_perf_hdr_main },
    { "perf_hdr_bloom", sc_perf_hdr_bloom },
    { "perf_ldr_bloom", sc_perf_ldr_bloom },
    { "perf_rt256",     sc_perf_rt256 },
    { "perf_rt2048",    sc_perf_rt2048 },
    { "perf_msaa_off",  sc_perf_msaa_off },
    { "perf_msaa_4x",   sc_perf_msaa_4x },
    { "perf_msaa_off1", sc_perf_msaa_off1 },
    { "perf_msaa_4x1",  sc_perf_msaa_4x1 },
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
        if (!only && (strncmp(SCENARIOS[i].name, "perf_", 5) == 0)) continue; // measurement probes: by name only
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
