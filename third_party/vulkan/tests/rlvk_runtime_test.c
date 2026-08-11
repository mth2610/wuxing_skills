// rlvk headless runtime test — exercises the Vulkan 1.1 retarget paths that need no
// window/swapchain: device init, texture staging upload+readback, SSBO roundtrip,
// compute dispatch (one-shot path), clean shutdown.
// Run with MoltenVK ICD + validation layers via env (see run script).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "raylib.h"

// rlvk logs through raylib's TraceLog; we are not linking raylib, so provide it
void TraceLog(int logLevel, const char *text, ...)
{
    static const char *names[] = { "ALL", "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL" };
    va_list args;
    va_start(args, text);
    printf("[%s] ", (logLevel >= 0 && logLevel <= 6) ? names[logLevel] : "?");
    vprintf(text, args);
    printf("\n");
    va_end(args);
}

#define TRACELOG(level, ...) TraceLog(level, __VA_ARGS__)
#define RLVK_IMPLEMENTATION
#include "rlvk.h"

static int g_failures = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

int main(void)
{
    printf("=== rlvk headless runtime test ===\n");

    // 1. Device bring-up (instance, physical device pick, logical device, caps, frame ring)
    rlglInit(640, 480);
    CHECK(rlGetVersion() >= 0, "rlglInit survived");

    // 2. Texture staging roundtrip: upload a deterministic RGBA8 pattern, read it back
    {
        enum { W = 64, H = 64 };
        static unsigned char src[W*H*4], *back = NULL;
        for (int i = 0; i < W*H; i++)
        {
            src[i*4+0] = (unsigned char)(i & 0xFF);
            src[i*4+1] = (unsigned char)((i >> 8) & 0xFF);
            src[i*4+2] = (unsigned char)(i*7 & 0xFF);
            src[i*4+3] = 255;
        }
        unsigned int tex = rlLoadTexture(src, W, H, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        CHECK(tex != 0 && tex != 0xFFFFFFFFu, "rlLoadTexture");
        back = (unsigned char *)rlReadTexturePixels(tex, W, H, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        CHECK(back != NULL, "rlReadTexturePixels returned data");
        if (back) CHECK(memcmp(src, back, sizeof(src)) == 0, "texture roundtrip bytes match");
        if (back) free(back);

        // Partial update: overwrite an 8x8 block at (16,16), re-read, verify both regions
        static unsigned char patch[8*8*4];
        memset(patch, 0xAB, sizeof(patch));
        rlUpdateTexture(tex, 16, 16, 8, 8, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, patch);
        back = (unsigned char *)rlReadTexturePixels(tex, W, H, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        int patched_ok = back && (back[((17*W)+17)*4] == 0xAB);
        int outside_ok = back && (memcmp(back, src, 16*4) == 0);   // first row untouched
        CHECK(patched_ok, "rlUpdateTexture patched region");
        CHECK(outside_ok, "rlUpdateTexture left outside intact");
        if (back) free(back);
        rlUnloadTexture(tex);
    }

    // 3. RGB8 expansion path (3-channel -> RGBA staging conversion + repack on read)
    {
        enum { W = 16, H = 16 };
        static unsigned char rgb[W*H*3];
        for (int i = 0; i < W*H*3; i++) rgb[i] = (unsigned char)(i & 0xFF);
        unsigned int tex = rlLoadTexture(rgb, W, H, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8, 1);
        unsigned char *back = (unsigned char *)rlReadTexturePixels(tex, W, H, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        CHECK(back && memcmp(rgb, back, sizeof(rgb)) == 0, "RGB8 expand/repack roundtrip");
        if (back) free(back);
        rlUnloadTexture(tex);
    }

    // 4. SSBO roundtrip + partial update + GPU-side copy
    {
        enum { N = 256 };
        static float data[N], readback[N];
        for (int i = 0; i < N; i++) data[i] = (float)i*0.5f;
        unsigned int ssbo = rlLoadShaderBuffer(sizeof(data), data, RL_DYNAMIC_COPY);
        CHECK(ssbo != 0 && ssbo != 0xFFFFFFFFu, "rlLoadShaderBuffer");
        CHECK(rlGetShaderBufferSize(ssbo) == sizeof(data), "rlGetShaderBufferSize");
        memset(readback, 0, sizeof(readback));
        rlReadShaderBuffer(ssbo, readback, sizeof(readback), 0);
        CHECK(memcmp(data, readback, sizeof(data)) == 0, "SSBO upload/readback match");

        float patch[4] = { 111.0f, 222.0f, 333.0f, 444.0f };
        rlUpdateShaderBuffer(ssbo, patch, sizeof(patch), 64*sizeof(float));
        rlReadShaderBuffer(ssbo, readback, sizeof(readback), 0);
        CHECK(readback[64] == 111.0f && readback[67] == 444.0f && readback[63] == data[63],
              "SSBO partial update");

        unsigned int ssbo2 = rlLoadShaderBuffer(sizeof(data), NULL, RL_DYNAMIC_COPY);
        rlCopyShaderBuffer(ssbo2, ssbo, 0, 0, sizeof(data));
        memset(readback, 0, sizeof(readback));
        rlReadShaderBuffer(ssbo2, readback, sizeof(readback), 0);
        CHECK(readback[0] == 0.0f && readback[64] == 111.0f, "rlCopyShaderBuffer GPU copy");
        rlUnloadShaderBuffer(ssbo2);

        // 5a. Bisect: compute WITHOUT uniforms (pure SSBO add) - separates SSBO-write
        //     correctness from loose-uniform delivery
        {
            const char *cs0 =
                "#version 430\n"
                "layout(local_size_x = 64) in;\n"
                "layout(std430, binding = 0) buffer Data { float v[]; };\n"
                "void main() { v[gl_GlobalInvocationID.x] += 10.0; }\n";
            unsigned int cs0Id = rlLoadShader(cs0, RL_COMPUTE_SHADER);
            unsigned int prog0 = rlLoadShaderProgramCompute(cs0Id);
            CHECK(prog0 != 0 && prog0 != 0xFFFFFFFFu, "no-uniform compute compiled");
            rlEnableShader(prog0);
            rlBindShaderBuffer(ssbo, 0);
            rlComputeShaderDispatch(N/64, 1, 1);
            rlDisableShader();
            memset(readback, 0, sizeof(readback));
            rlReadShaderBuffer(ssbo, readback, sizeof(readback), 0);
            CHECK(readback[1] == data[1] + 10.0f && readback[100] == data[100] + 10.0f,
                  "no-uniform compute result (+10)");
            // undo so the next test's expectations stay simple
            for (int i = 0; i < N; i++) readback[i] -= 10.0f;
            (void)readback;
            float minus[N]; for (int i = 0; i < N; i++) minus[i] = -10.0f;
            (void)minus;
            rlUnloadShader(prog0);
            // restore original contents (plus the earlier patch at 64..67)
            rlUpdateShaderBuffer(ssbo, data, sizeof(data), 0);
            float patch2[4] = { 111.0f, 222.0f, 333.0f, 444.0f };
            rlUpdateShaderBuffer(ssbo, patch2, sizeof(patch2), 64*sizeof(float));
        }

        // NOTE: explicit `layout(binding=N) uniform` blocks are NOT supported in compute:
        // shaderc's auto_bind_uniforms rebases even explicitly-bound UBOs (N + base). Loose
        // uniforms (the GL-style path) are the supported pattern.

        // 5. Compute dispatch: multiply every element by a loose uniform, verify on CPU
        const char *cs =
            "#version 430\n"
            "layout(local_size_x = 64) in;\n"
            "layout(std430, binding = 0) buffer Data { float v[]; };\n"
            "uniform float mulFactor;\n"
            "void main() { v[gl_GlobalInvocationID.x] *= mulFactor; }\n";
        unsigned int csId = rlLoadShader(cs, RL_COMPUTE_SHADER);
        CHECK(csId != 0 && csId != 0xFFFFFFFFu, "rlLoadShader(compute) stashed");
        unsigned int prog = rlLoadShaderProgramCompute(csId);
        CHECK(prog != 0 && prog != 0xFFFFFFFFu, "rlLoadShaderProgramCompute compiled");
        if (prog != 0 && prog != 0xFFFFFFFFu)
        {
            int loc = rlGetLocationUniform(prog, "mulFactor");
            CHECK(loc >= 0, "compute loose uniform reflected");
            rlEnableShader(prog);
            float factor = 3.0f;
            rlSetUniform(loc, &factor, RL_SHADER_UNIFORM_FLOAT, 1);
            rlBindShaderBuffer(ssbo, 0);
            rlComputeShaderDispatch(N/64, 1, 1);
            rlDisableShader();

            memset(readback, 0, sizeof(readback));
            rlReadShaderBuffer(ssbo, readback, sizeof(readback), 0);
            int ok = 1;
            for (int i = 0; i < N; i++)
            {
                float expect = (i >= 64 && i < 68) ?
                    ((float[]){111.0f,222.0f,333.0f,444.0f})[i-64]*3.0f : (float)i*0.5f*3.0f;
                if (readback[i] != expect) { ok = 0; printf("  mismatch [%d]: got %f want %f\n", i, readback[i], expect); break; }
            }
            CHECK(ok, "compute dispatch result correct");
            rlUnloadShader(prog);
        }
        rlUnloadShaderBuffer(ssbo);
    }

    // 6. Graphics shader compile (shaderc relaxed rules + clip-z epilogue injection)
    {
        const char *vs =
            "#version 330\n"
            "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor;\n"
            "out vec2 fragTexCoord; out vec4 fragColor;\n"
            "uniform mat4 mvp; uniform float wobble;\n"
            "void main(){ fragTexCoord=vertexTexCoord; fragColor=vertexColor;\n"
            "  gl_Position = mvp*vec4(vertexPosition + vec3(0,wobble,0), 1.0); }\n";
        const char *fs =
            "#version 330\n"
            "in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
            "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
            "void main(){ finalColor = texture(texture0, fragTexCoord)*colDiffuse*fragColor; }\n";
        unsigned int prog = rlLoadShaderProgram(vs, fs);
        CHECK(prog != 0 && prog != 0xFFFFFFFFu && prog != rlGetShaderIdDefault(), "rlLoadShaderProgram (GLSL 330 via shaderc)");
        if (prog && prog != rlGetShaderIdDefault())
        {
            CHECK(rlGetLocationUniform(prog, "wobble") >= 0, "graphics loose uniform reflected");
            rlUnloadShader(prog);
        }
    }

    // 8. Format capability query. The spec's Mandatory Format Support tables are the
    //    oracle here: R16_SFLOAT must support linear filtering and blending, R32_SFLOAT
    //    need not. So the R16 answers are conformance assertions (a false there means the
    //    query is wired wrong, not that the device is exotic), while the R32 answers are
    //    only required to be self-consistent with what init cached and to be honest about
    //    an attachment format the engine's screen-space passes already use.
    {
        CHECK(rlvkFormatSupportsColorAttachment(RL_PIXELFORMAT_UNCOMPRESSED_R32),
              "R32F is a colour attachment (spec-mandatory)");
        CHECK(rlvkFormatSupportsColorAttachment(RL_PIXELFORMAT_UNCOMPRESSED_R16),
              "R16F is a colour attachment (spec-mandatory)");
        CHECK(rlvkFormatSupportsBlend(RL_PIXELFORMAT_UNCOMPRESSED_R16),
              "R16F supports blending (spec-mandatory)");
        CHECK(rlvkFormatSupportsLinearFilter(RL_PIXELFORMAT_UNCOMPRESSED_R16),
              "R16F supports LINEAR filtering (spec-mandatory)");
        CHECK(rlvkFormatSupportsBlend(RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8),
              "RGBA8 supports blending (spec-mandatory)");
        CHECK(rlvkFormatSupportsBlend(RL_PIXELFORMAT_UNCOMPRESSED_R32) == RLVK.Caps.floatBlendR32 &&
              rlvkFormatSupportsLinearFilter(RL_PIXELFORMAT_UNCOMPRESSED_R32) == RLVK.Caps.floatFilterR32,
              "R32F caps cached at init match a live query");
        // Report the optional answers rather than asserting them: this line is the point
        // of the whole section when the suite is later run on a mobile driver.
        printf("      R32F on this device: blend=%d linearFilter=%d\n",
               (int)RLVK.Caps.floatBlendR32, (int)RLVK.Caps.floatFilterR32);
    }

    rlglClose();
    printf("=== done: %d failure(s) ===\n", g_failures);
    return g_failures ? 1 : 0;
}
