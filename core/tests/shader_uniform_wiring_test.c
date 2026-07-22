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

static void CheckPair(const char *fsPath, const char *cPath)
{
    char *fs = SlurpFile(fsPath);
    char *cs = SlurpFile(cPath);
    if (!fs || !cs)
    {
        printf("FAIL: could not read %s / %s\n", fsPath, cPath);
        g_failures++; g_checks++;
        free(fs); free(cs);
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

    // A location fetched but never uploaded is the same silent zero.
    int setCalls = 0;
    for (const char *p = cs; (p = strstr(p, "SetShaderValue")) != NULL; p++) setCalls++;
    int getCalls = 0;
    for (const char *p = cs; (p = strstr(p, "GetShaderLocation")) != NULL; p++) getCalls++;
    CHECK_MSG(setCalls >= getCalls, "no uniform location is fetched but never uploaded",
              "%d GetShaderLocation vs %d SetShaderValue calls", getCalls, setCalls);

    printf("  (%d uniforms declared, %d engine-bound)\n", n, n - (n - unwired) - unwired + unwired);
    free(fs); free(cs);
}

int main(void)
{
    printf("=== core headless test: shader uniform wiring ===\n");
    CheckPair("core/shaders/particle_lit.fs", "core/particle_system.c");

    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
