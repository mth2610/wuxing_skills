/* Every contract test in this directory greps SOURCE FILES for literal strings.
 * That is a good technique with one sharp edge: the helper each of them writes
 * is some variant of
 *
 *     FILE *f = fopen(path, "rb"); if (!f) return 0;
 *
 * so when the greped file is renamed or deleted, a positive assertion goes red
 * for a reason that has nothing to do with what it was protecting, and a
 * NEGATIVE assertion (!FileHas(dead_path, x)) goes green FOREVER while
 * protecting nothing at all. The second failure mode is silent and permanent.
 *
 * This happened at scale: pm_sweep_legacy.inl and vc_ribbon_trail.inl were
 * removed on 10/08/2026, and three suites stayed red for 107 commits with ~25
 * assertions between them that had stopped meaning anything.
 *
 * So: one test that reads every other test and checks that each repo-relative
 * path it names still exists. It cannot tell whether a NEEDLE still means
 * something — only a person can — but it catches the whole class of "the file
 * moved" in one place, and it costs no change to the 72 tests that each own a
 * private copy of that fopen. */
#include <stdio.h>
#include <string.h>
#include <dirent.h>

static int g_failures = 0, g_checks = 0;

static int LooksLikePath(const char *s, size_t n)
{
    if (n < 5 || n > 200) return 0;
    if (memchr(s, '/', n) == NULL) return 0;          /* bare CMakeLists.txt etc. */
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' || c == '-';
        if (!ok) return 0;
    }
    static const char *ext[] = {".c", ".h", ".inl", ".fs", ".vs", ".glsl",
                                ".png", ".json", ".md", ".txt", ".sh", NULL};
    for (int e = 0; ext[e]; e++) {
        size_t l = strlen(ext[e]);
        if (n > l && memcmp(s + n - l, ext[e], l) == 0) return 1;
    }
    return 0;
}

static int Exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

/* Scan one test's source for quoted literals that name a repo file. */
static void CheckOneTest(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "core/tests/%s", name);
    static char buf[600000];
    FILE *f = fopen(path, "rb");
    if (f == NULL) { printf("FAIL: cannot read %s\n", path); g_failures++; return; }
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    for (size_t i = 0; i < n; i++) {
        if (buf[i] != '"') continue;
        size_t start = ++i;
        while (i < n && buf[i] != '"' && buf[i] != '\n') {
            if (buf[i] == '\\') i++;                   /* skip escapes wholesale */
            i++;
        }
        if (i >= n || buf[i] != '"') continue;
        size_t len = i - start;
        if (!LooksLikePath(buf + start, len)) continue;
        /* Only literals that are the FIRST argument of a call are haystacks.
         * A path in any later argument is a NEEDLE being searched for inside
         * some other file — `RequireNot(cmake, "core/x.c", ...)` asserts that
         * CMakeLists does not mention core/x.c, and that path is SUPPOSED to
         * be gone. */
        size_t b = start - 1;                          /* the opening quote */
        while (b > 0 && (buf[b - 1] == ' ' || buf[b - 1] == '\n' ||
                         buf[b - 1] == '\t')) b--;
        if (b == 0 || (buf[b - 1] != '(' && buf[b - 1] != '=')) continue;
        if (buf[b - 1] == '=') { g_checks++;           /* const char *p = "..."; */
            char c2[256]; memcpy(c2, buf + start, len); c2[len] = '\0';
            if (!Exists(c2)) {
                printf("FAIL: %s binds a path that no longer exists: %s\n", name, c2);
                g_failures++;
            }
            continue;
        }
        /* ...and a NEGATED first argument is an assertion that the file is
         * gone — `!FileExists("old/path.inl")` is correct precisely when the
         * path does not resolve. */
        size_t k = b - 1;
        while (k > 0 && (buf[k - 1] == ' ' || buf[k - 1] == '\n')) k--;
        while (k > 0 && ((buf[k - 1] >= 'A' && buf[k - 1] <= 'Z') ||
                         (buf[k - 1] >= 'a' && buf[k - 1] <= 'z') ||
                         (buf[k - 1] >= '0' && buf[k - 1] <= '9') ||
                         buf[k - 1] == '_')) k--;
        while (k > 0 && (buf[k - 1] == ' ' || buf[k - 1] == '\n')) k--;
        if (k > 0 && buf[k - 1] == '!') continue;
        char cand[256];
        if (len >= sizeof(cand)) continue;
        memcpy(cand, buf + start, len);
        cand[len] = '\0';
        g_checks++;
        if (!Exists(cand)) {
            printf("FAIL: %s greps a path that no longer exists: %s\n", name, cand);
            g_failures++;
        }
    }
}

int main(void)
{
    printf("=== contract tests: every greped path still exists ===\n");
    DIR *d = opendir("core/tests");
    if (d == NULL) {
        printf("FAIL: cannot open core/tests (run from the repo root)\n");
        return 1;
    }
    struct dirent *e;
    int suites = 0;
    while ((e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (l < 8 || strcmp(e->d_name + l - 7, "_test.c") != 0) continue;
        if (strcmp(e->d_name, "contract_path_test.c") == 0) continue;  /* self */
        CheckOneTest(e->d_name);
        suites++;
    }
    closedir(d);
    printf("---- %d suites scanned, %d paths checked, %d failures\n",
           suites, g_checks, g_failures);
    return g_failures ? 1 : 0;
}
