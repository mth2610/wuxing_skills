// core headless test — the INSTANCED shader permutation (M3, 20/08/2026).
//
// Two .vs files used to be hand-copied so an instanced draw could have its own
// `instanceTransform` attribute: effect_material_instanced.vs and
// crystal_instanced.vs. Copies drift, and both had. They are now one source per
// material plus a compile-time permutation, selected by
// ResourceManager_LoadShaderVariant("#define INSTANCED 1\n").
//
// Everything asserted here is TEXT, which is why it belongs in this tier rather
// than on a GPU: the seam is a preprocessor construct, and every way it broke
// while being built broke textually.
//
// What this tier CANNOT tell you: whether the INSTANCED program links on a real
// device. No VFX-test fixture reaches it — sandbox's harness takes exactly one
// primary API per .inl, and water/ice_crystal.inl's is VFX_ComposeIceCrystal,
// not the instanced VFX_DrawIceCrystalBurst next to it. That path is reached
// only by casting Glacial Cannon in the game.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

static char *ReadAll(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static int FileHas(const char *path, const char *needle)
{
    char *src = ReadAll(path);
    if (!src) return 0;
    int found = (strstr(src, needle) != NULL);
    free(src);
    return found;
}

static int FileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

// ── 1. The seam ─────────────────────────────────────────────────────────────
// One source, two programs. The ifdef must name BOTH headers: if the #else arm
// is missing, every non-instanced draw silently loses its varyings.

static void Test_BothMaterialsUseTheSeam(void)
{
    printf("\n-- the ifdef seam --\n");
    const char *vs[] = { "core/shaders/effect_material.vs", "core/shaders/crystal.vs" };

    for (int i = 0; i < 2; i++)
    {
        CHECK(FileHas(vs[i], "#ifdef INSTANCED"), vs[i]);
        CHECK(FileHas(vs[i], "common/vs_instanced_header.glsl"),
              "  ... selects the instanced header when INSTANCED is defined");
        CHECK(FileHas(vs[i], "common/vs_header.glsl"),
              "  ... and falls back to the shared header when it is not");
    }

    CHECK(!FileExists("core/shaders/effect_material_instanced.vs"),
          "the hand-copied effect_material twin is gone");
    CHECK(!FileExists("core/shaders/crystal_instanced.vs"),
          "the hand-copied crystal twin is gone");
}

// ── 2. The attribute must exist in exactly ONE variant ──────────────────────
// This is the whole reason the permutation is compile-time. Reading an unbound
// `in mat4 instanceTransform` on a draw that is not DrawMeshInstanced is
// undefined behaviour across drivers, so the declaration itself must not exist
// in the non-instanced program. A runtime `if` would not have removed it.

static void Test_InstanceAttributeIsExclusiveToTheInstancedHeader(void)
{
    printf("\n-- instanceTransform lives in one header only --\n");
    CHECK(FileHas("core/shaders/common/vs_instanced_header.glsl", "in mat4 instanceTransform"),
          "the instanced header declares instanceTransform");
    CHECK(!FileHas("core/shaders/common/vs_header.glsl", "instanceTransform"),
          "the shared header does NOT mention it, at all");
}

// ── 3. Interface parity ─────────────────────────────────────────────────────
// The seam only works because a .vs's main() is identical either way: it calls
// VS_FinalOutput and never transforms anything itself. That holds only while
// both headers expose the same names.

static void Test_TheTwoHeadersExposeTheSameInterface(void)
{
    printf("\n-- the two headers are interface-identical --\n");
    const char *shared    = "core/shaders/common/vs_header.glsl";
    const char *instanced = "core/shaders/common/vs_instanced_header.glsl";

    const char *required[] = {
        "void VS_FinalOutput(vec3 displacedPos)",
        "in vec3 vertexPosition",
        "in vec2 vertexTexCoord",
        "in vec3 vertexNormal",
        "out vec3 fragPosition",
        "out vec2 fragTexCoord",
        "out vec3 fragNormal",
        "uniform mat4",
        "uniform float u_time",
    };
    int n = (int)(sizeof(required) / sizeof(required[0]));

    for (int i = 0; i < n; i++)
    {
        CHECK(FileHas(shared, required[i]) && FileHas(instanced, required[i]), required[i]);
    }
}

// ── 4. No include directive may sit inside a comment ────────────────────────
// shader_preprocessor.c is a purely textual expander with no notion of
// comments. An include written inside a // comment is STILL expanded — and
// when the file doing it is the file being included, that is infinite
// recursion, stopped only by the depth limit after eight nested copies.
// This test exists because vs_instanced_header.glsl did exactly that on the
// day it was written, in its own usage example.
//
// Scope is core/ — the module this test belongs to. maps/toolkit/shaders has
// two pre-existing instances (prop_lit.vs, grass_material.vs) that say
// "does NOT #include ..." in prose; grass_material.vs is the live one, because
// the parser takes the next double-quote ANYWHERE after the directive as the
// path and there is one further down that file. Those belong to the Map Agent.

static int LineIsCommentedInclude(const char *line)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (!(p[0] == '/' && p[1] == '/') && p[0] != '*') return 0;
    return strstr(p, "#include") != NULL;
}

static int ScanForCommentedIncludes(const char *path)
{
    char *src = ReadAll(path);
    if (!src) return 0;

    int hits = 0;
    char *line = src;
    while (line && *line)
    {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (LineIsCommentedInclude(line))
        {
            printf("      %s: %s\n", path, line);
            hits++;
        }
        if (!nl) break;
        *nl = '\n';
        line = nl + 1;
    }
    free(src);
    return hits;
}

static void Test_NoIncludeDirectiveHidesInAComment(void)
{
    printf("\n-- no include directive inside a comment (core/) --\n");

    // Listed explicitly rather than walked: this tier links nothing, and the
    // shared headers plus the two permutation consumers are what the seam
    // actually touches.
    const char *files[] = {
        "core/shaders/common/vs_header.glsl",
        "core/shaders/common/vs_instanced_header.glsl",
        "core/shaders/common/fs_header.glsl",
        "core/shaders/common/fx.glsl",
        "core/shaders/common/noise.glsl",
        "core/shaders/common/lighting.glsl",
        "core/shaders/common/displacement.glsl",
        "core/shaders/common/soft_particle.glsl",
        "core/shaders/common/triplanar.glsl",
        "core/shaders/common/vfx_composite.glsl",
        "core/shaders/common/vfx_contrast.glsl",
        "core/shaders/common/vfx_lights.glsl",
        "core/shaders/effect_material.vs",
        "core/shaders/crystal.vs",
    };
    int n = (int)(sizeof(files) / sizeof(files[0]));

    int total = 0;
    for (int i = 0; i < n; i++) total += ScanForCommentedIncludes(files[i]);

    CHECK(total == 0, "no shared shader source hides an include in a comment");
}

int main(void)
{
    printf("=== INSTANCED shader permutation ===\n");
    Test_BothMaterialsUseTheSeam();
    Test_InstanceAttributeIsExclusiveToTheInstancedHeader();
    Test_TheTwoHeadersExposeTheSameInterface();
    Test_NoIncludeDirectiveHidesInAComment();
    printf("---- %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
