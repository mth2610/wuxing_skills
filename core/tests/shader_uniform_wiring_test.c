// core headless test — shader uniform wiring.
//
// Catches the silent failure that a GLSL uniform is DECLARED and USED in a
// shader, but the C side never looks up its location or never uploads a value.
// Nothing errors: GetShaderLocation is simply not called, SetShaderValue is
// simply not called, and an unwritten GLSL uniform reads as ZERO. The feature
// then behaves as if its parameter were pinned to 0 — which, for a debug
// override or a strength knob, looks exactly like "the feature does nothing"
// and sends you debugging the maths instead of the plumbing.
//
// This is a SOURCE-level check: it parses the .fs for `uniform <type> <name>`
// and the owning .c for `GetShaderLocation(..., "<name>")` plus a matching
// SetShaderValue*. It cannot prove the value is correct, only that the wire
// exists at all — which is precisely the gap that pure-maths tests leave open.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

static char *SlurpFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

// Strip // line comments so a commented-out uniform is not treated as real.
static void StripLineComments(char *s)
{
    for (char *p = s; *p; p++)
    {
        if (p[0] == '/' && p[1] == '/')
            while (*p && *p != '\n') *p++ = ' ';
    }
}

#define MAX_UNIFORMS 64
#define NAME_LEN 64

static int CollectUniforms(const char *src, char out[][NAME_LEN])
{
    int count = 0;
    const char *p = src;
    while ((p = strstr(p, "uniform")) != NULL)
    {
        // must be a standalone token
        if (p != src && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) { p += 7; continue; }
        const char *q = p + 7;
        while (*q && isspace((unsigned char)*q)) q++;
        // skip the type
        while (*q && (isalnum((unsigned char)*q) || *q == '_')) q++;
        while (*q && isspace((unsigned char)*q)) q++;
        // read the name
        char name[NAME_LEN]; int n = 0;
        while (*q && (isalnum((unsigned char)*q) || *q == '_') && n < NAME_LEN - 1)
            name[n++] = *q++;
        name[n] = '\0';
        if (n > 0 && count < MAX_UNIFORMS)
        {
            int dup = 0;
            for (int i = 0; i < count; i++) if (!strcmp(out[i], name)) dup = 1;
            if (!dup) strcpy(out[count++], name);
        }
        p += 7;
    }
    return count;
}

// Uniforms raylib/rlgl bind for us — not the C file's job to wire.
static int IsEngineBound(const char *name)
{
    static const char *engine[] = {
        "texture0", "texture1", "texture2", "colDiffuse", "mvp",
        "matModel", "matView", "matProjection", "matNormal", NULL
    };
    for (int i = 0; engine[i]; i++) if (!strcmp(engine[i], name)) return 1;
    return 0;
}

static int MentionsQuoted(const char *src, const char *name)
{
    char pat[NAME_LEN + 4];
    snprintf(pat, sizeof(pat), "\"%s\"", name);
    return strstr(src, pat) != NULL;
}

// Resolve #include "..." the way core/shader_preprocessor.c does, appending the
// included text. WITHOUT this the test silently stops covering any uniform that
// moves into a shared block — it counts fewer uniforms and passes for the wrong
// reason, which is precisely the drift it exists to catch. (Observed: moving the
// VFX light uniforms into common/vfx_lights.glsl dropped surface_lit.fs from 35
// to 31 checked uniforms and the suite stayed green.)
static char *SlurpShaderWithIncludes(const char *path)
{
    char *base = SlurpFile(path);
    if (!base) return NULL;
    static char merged[1 << 19];
    merged[0] = '\0';
    strncat(merged, base, sizeof(merged) - 1);
    const char *p = base;
    while ((p = strstr(p, "#include")) != NULL)
    {
        const char *q = strchr(p, '"');
        if (!q) break;
        const char *e = strchr(q + 1, '"');
        if (!e) break;
        char inc[512];
        size_t n = (size_t)(e - q - 1);
        if (n >= sizeof(inc)) break;
        memcpy(inc, q + 1, n);
        inc[n] = '\0';
        char *sub = SlurpFile(inc);          // repo-relative, as the engine does
        if (sub)
        {
            strncat(merged, "\n", sizeof(merged) - strlen(merged) - 1);
            strncat(merged, sub, sizeof(merged) - strlen(merged) - 1);
            free(sub);
        }
        else
            printf("  note: could not open included %s\n", inc);
        p = e + 1;
    }
    free(base);
    char *out = (char *)malloc(strlen(merged) + 1);
    strcpy(out, merged);
    return out;
}

// `cPaths` is NUL-terminated: a uniform may be uploaded from a helper rather
// than from the shader's own owner (VFXLight_BindToShader does exactly that),
// and demanding it appear in one specific file would punish the deduplication.
static void CheckPairMulti(const char *fsPath, const char *const *cPaths)
{
    char *fs = SlurpShaderWithIncludes(fsPath);
    static char csAll[1 << 19];
    csAll[0] = '\0';
    for (int i = 0; cPaths[i]; i++)
    {
        char *one = SlurpFile(cPaths[i]);
        if (one) { strncat(csAll, one, sizeof(csAll) - strlen(csAll) - 1); free(one); }
    }
    char *cs = csAll;
    const char *cPath = cPaths[0];
    if (!fs || !cs[0])
    {
        printf("FAIL: could not read %s / %s\n", fsPath, cPath);
        g_failures++; g_checks++;
        free(fs);
        return;
    }
    StripLineComments(fs);

    static char names[MAX_UNIFORMS][NAME_LEN];
    int n = CollectUniforms(fs, names);

    int unwired = 0;
    char missing[512] = {0};
    for (int i = 0; i < n; i++)
    {
        if (IsEngineBound(names[i])) continue;
        if (!MentionsQuoted(cs, names[i]))
        {
            unwired++;
            if (strlen(missing) < sizeof(missing) - NAME_LEN - 2)
            {
                strcat(missing, names[i]);
                strcat(missing, " ");
            }
        }
    }

    char label[256];
    snprintf(label, sizeof(label), "every uniform in %s is wired from %s",
             strrchr(fsPath, '/') ? strrchr(fsPath, '/') + 1 : fsPath,
             strrchr(cPath, '/') ? strrchr(cPath, '/') + 1 : cPath);
    CHECK_MSG(unwired == 0, label, "%d never referenced in C: %s", unwired, missing);

    // A location fetched but never uploaded is the same silent zero. Checked by
    // NAME, not by comparing call counts: counts never had to match (a location
    // is fetched once and may be set conditionally, or set from a loop), so that
    // version produced a false failure the moment two source files were merged.
    int orphan = 0;
    char orphans[512] = {0};
    for (const char *p2 = cs; (p2 = strstr(p2, "GetShaderLocation")) != NULL; p2 += 17)
    {
        // walk back to the assigned variable: `<name> = GetShaderLocation(`
        const char *eq = p2;
        while (eq > cs && *eq != '=' && *eq != '\n') eq--;
        if (*eq != '=') continue;                 // not an assignment
        const char *end = eq - 1;
        while (end > cs && isspace((unsigned char)*end)) end--;
        const char *start = end;
        while (start > cs && (isalnum((unsigned char)*start) || *start == '_')) start--;
        if (!isalnum((unsigned char)*start) && *start != '_') start++;
        char var[NAME_LEN]; size_t vn = (size_t)(end - start + 1);
        if (vn == 0 || vn >= NAME_LEN) continue;
        memcpy(var, start, vn); var[vn] = '\0';
        if (strstr(var, "locs[")) continue;       // raylib's own slots
        // is it ever passed to a SetShaderValue*?
        int used = 0;
        for (const char *q = cs; (q = strstr(q, "SetShaderValue")) != NULL; q += 14)
        {
            const char *lineEnd = strchr(q, ';');
            if (!lineEnd) break;
            size_t len = (size_t)(lineEnd - q);
            char buf[512];
            if (len >= sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, q, len); buf[len] = '\0';
            if (strstr(buf, var)) { used = 1; break; }
        }
        if (!used && strlen(orphans) < sizeof(orphans) - NAME_LEN - 2)
        {
            orphan++;
            strcat(orphans, var); strcat(orphans, " ");
        }
    }
    CHECK_MSG(orphan == 0, "no uniform location is fetched but never uploaded",
              "%d orphaned: %s", orphan, orphans);

    printf("  (%d uniforms declared, includes followed)\n", n);
    free(fs);
}

int main(void)
{
    printf("=== core headless test: shader uniform wiring ===\n");
    static const char *particleSrc[] = { "core/particle_system.c", "core/vfx_light.c", NULL };
    static const char *surfaceSrc[]  = { "core/surface_material.c", "core/vfx_light.c", NULL };
    CheckPairMulti("core/shaders/particle_lit.fs", particleSrc);
    CheckPairMulti("core/shaders/surface_lit.fs", surfaceSrc);
    static const char *grassSrc[] = { "maps/toolkit/grass_material.c", "core/vfx_light.c", NULL };
    CheckPairMulti("maps/toolkit/shaders/grass_material.fs", grassSrc);
    static const char *groundSrc[] = { "maps/toolkit/map_props_ground.inl", "core/vfx_light.c", NULL };
    CheckPairMulti("maps/toolkit/shaders/ground_splat.fs", groundSrc);
    static const char *pathSrc[] = { "maps/toolkit/map_props_strip.inl", "core/vfx_light.c", NULL };
    CheckPairMulti("maps/toolkit/shaders/path_blend.fs", pathSrc);

    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
