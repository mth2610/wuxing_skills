// core headless test — the composition translation unit.
//
// Every .inl under core/composition is pasted into ONE translation unit by
// visual_composer.c. That makes file-scope statics effectively GLOBAL across the
// whole module, so two files that each declare an innocuous `static ForceField
// s_flameFld` collide — which is a redefinition error whose message names two
// files that have nothing to do with each other, and which only appears after a
// full rebuild of the largest .c in the project.
//
// This catches it in milliseconds, before the compiler is involved.
//
// Function-LOCAL statics (indented, inside a body) are deliberately ignored:
// they are scoped per function and colliding names there are legal and common.
// Only column-0 declarations are file scope.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__); g_failures++; } \
} while (0)

#define MAX_SYMS 4096
#define NAME_LEN 96
#define PATH_LEN 512

static char g_name[MAX_SYMS][NAME_LEN];
static char g_file[MAX_SYMS][PATH_LEN];
static int  g_count = 0;

// A file-scope `static <type> name` — declaration must start at column 0.
static void ScanFile(const char *path, const char *rel)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "static", 6) != 0) continue;   // column 0 only
        const char *p = line + 6;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!strncmp(p, "const ", 6)) p += 6;
        if (!strncmp(p, "inline ", 7)) p += 7;
        while (*p && isspace((unsigned char)*p)) p++;
        // type
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
        while (*p && (isspace((unsigned char)*p) || *p == '*')) p++;
        // name
        char name[NAME_LEN]; int n = 0;
        while (*p && (isalnum((unsigned char)*p) || *p == '_') && n < NAME_LEN - 1)
            name[n++] = *p++;
        name[n] = '\0';
        if (n == 0) continue;
        // must be followed by = [ ; or ( — otherwise we mis-parsed
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '=' && *p != '[' && *p != ';' && *p != '(') continue;
        if (g_count < MAX_SYMS)
        {
            snprintf(g_name[g_count], NAME_LEN, "%s", name);
            snprintf(g_file[g_count], PATH_LEN, "%s", rel);
            g_count++;
        }
    }
    fclose(f);
}

static void Walk(const char *base, const char *rel)
{
    char dirpath[PATH_LEN];
    snprintf(dirpath, sizeof(dirpath), "%s%s%s", base, rel[0] ? "/" : "", rel);
    DIR *d = opendir(dirpath);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.') continue;
        char childRel[PATH_LEN], full[PATH_LEN];
        snprintf(childRel, sizeof(childRel), "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof(full), "%s/%s", base, childRel);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) Walk(base, childRel);
        else
        {
            size_t L = strlen(e->d_name);
            if (L > 4 && !strcmp(e->d_name + L - 4, ".inl")) ScanFile(full, childRel);
        }
    }
    closedir(d);
}

int main(void)
{
    printf("=== core headless test: composition translation unit ===\n");

    Walk("core/composition", "");
    if (g_count == 0)
    {
        printf("FAIL: no .inl files scanned (run from the repo root)\n");
        return 1;
    }

    int clashes = 0;
    char detail[1024] = {0};
    for (int i = 0; i < g_count; i++)
        for (int j = i + 1; j < g_count; j++)
        {
            if (strcmp(g_name[i], g_name[j]) != 0) continue;
            if (strcmp(g_file[i], g_file[j]) == 0) continue;   // same file, fine
            clashes++;
            if (strlen(detail) < sizeof(detail) - 200)
            {
                char one[200];
                snprintf(one, sizeof(one), "%s (%s vs %s) ",
                         g_name[i], g_file[i], g_file[j]);
                strcat(detail, one);
            }
        }

    CHECK_MSG(clashes == 0, "no file-scope static collides across the composition TU",
              "%d clash(es): %s", clashes, detail);
    printf("  (%d file-scope statics across the module)\n", g_count);

    printf("---\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
