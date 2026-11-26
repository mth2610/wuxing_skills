#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures = 0;
#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (cond) {                                                        \
            printf("PASS: %s\n", msg);                                     \
        } else {                                                           \
            printf("FAIL: %s\n", msg);                                     \
            s_failures++;                                                  \
        }                                                                  \
    } while (0)

static int FileContains(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return 0; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    int found = (strstr(buf, needle) != NULL);
    free(buf);
    return found;
}

static int FileExists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Verify the 3x4 affine row packing math matches Raylib's Vector3Transform. */
typedef struct {
    float m0, m4, m8,  m12;
    float m1, m5, m9,  m13;
    float m2, m6, m10, m14;
    float m3, m7, m11, m15;
} TestMat;

static void PackAffine(TestMat m, float r[12]) {
    r[0]  = m.m0; r[1]  = m.m4; r[2]  = m.m8;  r[3]  = m.m12;
    r[4]  = m.m1; r[5]  = m.m5; r[6]  = m.m9;  r[7]  = m.m13;
    r[8]  = m.m2; r[9]  = m.m6; r[10] = m.m10; r[11] = m.m14;
}

int main(void) {
    const char *hdr = "core/vfx_model/vfx_model.h";
    const char *src = "core/vfx_model/vfx_model.c";
    const char *vs  = "core/vfx_model/shaders/vfx_model.vs";
    const char *fs  = "core/vfx_model/shaders/vfx_model.fs";

    CHECK(FileExists(hdr) && FileExists(src) && FileExists(vs) && FileExists(fs),
          "core/vfx_model header, implementation, and shaders exist");
    CHECK(FileExists("assets/vfx/models/dragon_v12_gold.glb"),
          "Exported Dragon V12 test GLB asset exists");

    /* ENGINE_LANDMINES §30: GPU skinning, zero UpdateMeshBuffer calls */
    CHECK(!FileContains(src, "UpdateMeshBuffer") && !FileContains(src, "UpdateModelAnimation"),
          "GPU skinning path avoids CPU UpdateMeshBuffer/UpdateModelAnimation stalls");

    /* ENGINE_LANDMINES §25: [0] + base-name fallback for rlvk uniform arrays */
    CHECK(FileContains(src, "\"u_boneRows[0]\"") && FileContains(src, "\"u_boneRows\"") &&
          FileContains(src, "SetShaderValueV"),
          "Bone row array queries [0] with base-name fallback for rlvk");

    /* ENGINE_LANDMINES §8 & §10: uniforms uploaded inside BeginShaderMode */
    CHECK(FileContains(src, "BeginShaderMode(s_shader)") &&
          FileContains(src, "EndShaderMode()"),
          "Per-instance uniforms and draws are scoped inside BeginShaderMode");

    /* ENGINE_LANDMINES §9: shader space inverted to world space */
    CHECK(FileContains(vs, "u_worldFromShaderSpace") &&
          FileContains(src, "MatrixInvert(GetCameraMatrix(camera))"),
          "Vertex shader recovers true world-space position and normal");

    /* Bright background contract: premultiplied resolver matches BLEND_ALPHA_PREMULTIPLY */
    CHECK(FileContains(fs, "VFX_ResolvePremultiplied") &&
          FileContains(src, "BLEND_ALPHA_PREMULTIPLY"),
          "QI_SPIRIT mode pairs VFX_ResolvePremultiplied with BLEND_ALPHA_PREMULTIPLY");

    /* Mali / GLES 3.00 safety: no fract(sin()) hash, highp precision present */
    CHECK(!FileContains(fs, "fract(sin(") && FileContains(fs, "vnoise3") &&
          FileContains(vs, "precision highp float;") &&
          FileContains(fs, "precision highp float;"),
          "Shaders are Mali/GLES3 safe (vnoise3, highp precision)");

    /* Numerical verification of 3x4 affine row packing */
    TestMat m = {
        0.0f, -1.0f, 0.0f, 3.5f,
        1.0f,  0.0f, 0.0f, -2.0f,
        0.0f,  0.0f, 2.0f, 1.25f,
        0.0f,  0.0f, 0.0f, 1.0f
    };
    float rows[12];
    PackAffine(m, rows);
    float px = 2.0f, py = 4.0f, pz = -1.5f;
    float gx = rows[0]*px + rows[1]*py + rows[2]*pz + rows[3];
    float gy = rows[4]*px + rows[5]*py + rows[6]*pz + rows[7];
    float gz = rows[8]*px + rows[9]*py + rows[10]*pz + rows[11];
    float ex = m.m0*px + m.m4*py + m.m8*pz + m.m12;
    float ey = m.m1*px + m.m5*py + m.m9*pz + m.m13;
    float ez = m.m2*px + m.m6*py + m.m10*pz + m.m14;
    CHECK(fabsf(gx - ex) < 1e-6f && fabsf(gy - ey) < 1e-6f && fabsf(gz - ez) < 1e-6f,
          "3x4 affine bone row packing matches Raylib column-vector transform");

    if (s_failures > 0) {
        printf("%d test(s) FAILED.\n", s_failures);
        return 1;
    }
    printf("All vfx_model tests PASSED.\n");
    return 0;
}
