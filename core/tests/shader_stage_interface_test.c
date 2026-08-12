// core headless test — every FS input must have a producing VS output.
//
// GL links varyings by NAME and leaves an unmatched fragment input *undefined*
// rather than failing; rlvk demotes it to a Private SPIR-V variable with the same
// silence (third_party/vulkan/rlvk/rlvk_shaderc.inl::rlvkMatchStageInterface). So a
// fragment shader can read a varying nobody writes, forever, with no error anywhere.
// It bites hardest when the undefined value gates a `discard`: the pass then throws
// away every fragment and the effect simply is not there.
//
// That is exactly what `fluid_surface_capture.vs` did — it declared three outputs
// while both fragment stages paired with it open on `if (v_life <= 0.0) discard;`.
//
// The pairs below mirror the ResourceManager_LoadShader call sites; the test asserts
// those call sites still exist, so a moved pairing fails here instead of rotting.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { bad++; printf("FAIL: %s\n", #expr); } } while (0)

static char *ReadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *text = (char *)malloc((size_t)n + 1);
    if (!text) { fclose(f); return NULL; }
    size_t got = fread(text, 1, (size_t)n, f);
    text[got] = '\0'; fclose(f); return text;
}

// Collect identifiers declared at the start of a line as `<qualifier> <type> <name>`.
// Enough for these hand-written shaders: no interface blocks, no arrays, one per line.
static int CollectDeclarations(const char *src, const char *qualifier, char names[][64], int maxNames)
{
    int count = 0;
    size_t qlen = strlen(qualifier);
    const char *line = src;
    while (line && *line && count < maxNames)
    {
        const char *scan = line;
        while (*scan == ' ' || *scan == '\t') scan++;
        if (strncmp(scan, qualifier, qlen) == 0 && (scan[qlen] == ' ' || scan[qlen] == '\t'))
        {
            // Walk declarators after the qualifier: `out vec3 a; out float b;`
            const char *cursor = scan + qlen;
            while (*cursor && *cursor != '\n')
            {
                while (*cursor == ' ' || *cursor == '\t') cursor++;
                const char *typeStart = cursor;                       // the type
                while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\n') cursor++;
                if (cursor == typeStart) break;
                while (*cursor == ' ' || *cursor == '\t') cursor++;
                const char *nameStart = cursor;                       // the name
                while (*cursor && *cursor != ';' && *cursor != ' ' && *cursor != '\t' && *cursor != '\n') cursor++;
                size_t len = (size_t)(cursor - nameStart);
                if (len == 0 || len >= 64) break;
                if (count < maxNames) { memcpy(names[count], nameStart, len); names[count][len] = '\0'; count++; }
                while (*cursor == ' ' || *cursor == '\t') cursor++;
                if (*cursor != ';') break;
                cursor++;
                while (*cursor == ' ' || *cursor == '\t') cursor++;
                if (strncmp(cursor, qualifier, qlen) == 0 && (cursor[qlen] == ' ' || cursor[qlen] == '\t'))
                    cursor += qlen;                                    // another declarator on this line
                else break;
            }
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return count;
}

static int CheckPair(const char *vsPath, const char *fsPath)
{
    int bad = 0;
    char *vs = ReadFile(vsPath), *fs = ReadFile(fsPath);
    if (!vs || !fs) { printf("FAIL: cannot read %s / %s\n", vsPath, fsPath); free(vs); free(fs); return 1; }

    static char outs[32][64], ins[32][64];
    int outCount = CollectDeclarations(vs, "out", outs, 32);
    int inCount  = CollectDeclarations(fs, "in", ins, 32);

    for (int i = 0; i < inCount; ++i)
    {
        if (strncmp(ins[i], "gl_", 3) == 0) continue;
        int matched = 0;
        for (int o = 0; o < outCount; ++o) if (strcmp(ins[i], outs[o]) == 0) { matched = 1; break; }
        if (!matched)
        {
            printf("FAIL: %s reads `%s`, which %s never writes\n", fsPath, ins[i], vsPath);
            bad++;
        }
    }
    free(vs); free(fs);
    return bad;
}

int main(void)
{
    int bad = 0;

    // The GPU particle backend's surface-input pair (particle_gpu_backend.c).
    bad += CheckPair("core/particles/shaders/gpu/fluid_surface_capture.vs",
                     "core/fluid/shaders/fluid_capture_particle.fs");
    bad += CheckPair("core/particles/shaders/gpu/fluid_surface_capture.vs",
                     "core/fluid/shaders/fluid_capture_particle_back.fs");
    // The GPU PBD pool's pair (fluid_pbd_gpu.c) — same fragment stages, other vertex stage.
    bad += CheckPair("core/fluid/shaders/fluid_pbd_surface.vs",
                     "core/fluid/shaders/fluid_capture_particle.fs");
    bad += CheckPair("core/fluid/shaders/fluid_pbd_surface.vs",
                     "core/fluid/shaders/fluid_capture_particle_back.fs");

    // The pairings themselves: if a call site moves, this test must be updated with it
    // rather than silently checking shaders nobody pairs any more.
    char *backend = ReadFile("core/particles/gpu/particle_gpu_backend.c");
    if (!backend) { printf("FAIL: cannot read particle_gpu_backend.c\n"); bad++; }
    else
    {
        CHECK(strstr(backend, "\"core/particles/shaders/gpu/fluid_surface_capture.vs\", \"core/fluid/shaders/fluid_capture_particle.fs\"") != NULL);
        CHECK(strstr(backend, "\"core/particles/shaders/gpu/fluid_surface_capture.vs\", \"core/fluid/shaders/fluid_capture_particle_back.fs\"") != NULL);
        free(backend);
    }

    // The parser must actually be able to fail, or the whole file is decoration.
    {
        const char *vsLike = "out vec3 v_centerView;\nout vec2 v_corner;\n";
        const char *fsLike = "in vec3 v_centerView;\nin float v_missing;\n";
        static char o[32][64], n[32][64];
        int oc = CollectDeclarations(vsLike, "out", o, 32);
        int ic = CollectDeclarations(fsLike, "in", n, 32);
        CHECK(oc == 2 && ic == 2);
        CHECK(strcmp(o[0], "v_centerView") == 0 && strcmp(n[1], "v_missing") == 0);
        int found = 0;
        for (int j = 0; j < oc; ++j) if (strcmp(o[j], "v_missing") == 0) found = 1;
        CHECK(found == 0);   // the parser sees the mismatch it is meant to catch
    }

    printf("%s: shader_stage_interface_test (%d failures)\n", bad ? "FAIL" : "PASS", bad);
    return bad ? 1 : 0;
}
