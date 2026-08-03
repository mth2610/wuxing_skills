// assets/TEXTURE_PACKING.md — the packing rules, and the wiring that enforces
// them.
//
// WHY A C TEST FOR A PYTHON RULE: the real enforcement is
// scripts/validate_vfx_surface_registry.py, run at CMake configure time. But
// that validator already existed once, wired to nothing, failing on five
// profiles for an unknown length of time with nobody looking. A rule nothing
// runs is not a rule. This suite pins the wiring itself — that the validator is
// invoked, that its failure is fatal, and that the grammar it checks is the one
// the spec documents — so the enforcement cannot quietly disappear again.
//
// WHAT IT CANNOT SEE: the pixels. Whether a channel declared STRETCH actually
// fades to zero at its edges, or whether a TILE channel is really seamless, is
// an image question this cannot answer. It checks that the claim is DECLARED
// and that the declaration is machine-checked, not that the art obeys it.
//
// Standalone: links nothing. Paths are repo-root relative.

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_MSG(cond, name, fmt, ...)                                        \
  do {                                                                         \
    g_checks++;                                                                \
    if (cond) printf("PASS: %s\n", name);                                      \
    else {                                                                     \
      printf("FAIL: %s  [" fmt "]\n", name, __VA_ARGS__);                      \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

#define CHECK(cond, name)                                                      \
  do {                                                                         \
    g_checks++;                                                                \
    if (cond) printf("PASS: %s\n", name);                                      \
    else {                                                                     \
      printf("FAIL: %s\n", name);                                              \
      g_failures++;                                                            \
    }                                                                          \
  } while (0)

static char g_buf[600000];

static const char *Slurp(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  size_t n = fread(g_buf, 1, sizeof(g_buf) - 1, f);
  g_buf[n] = '\0';
  fclose(f);
  return g_buf;
}

static void CollapseWS(const char *src, char *out, size_t cap) {
  size_t o = 0;
  int inWS = 0;
  for (size_t i = 0; src[i] && o + 2 < cap; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!inWS && o > 0) out[o++] = ' ';
      inWS = 1;
    } else {
      out[o++] = (char)c;
      inWS = 0;
    }
  }
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = '\0';
}

static int FileHas(const char *path, const char *needle) {
  static char buf[600000], flat[600000];
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n] = '\0';
  fclose(f);
  CollapseWS(buf, flat, sizeof(flat));
  char want[2048];
  CollapseWS(needle, want, sizeof(want));
  return strstr(flat, want) != NULL;
}

// ── 1. The spec exists and states its layouts ───────────────────────────────
static void Test_SpecIsPresentAndComplete(void) {
  const char *spec = "assets/TEXTURE_PACKING.md";
  CHECK(FileHas(spec, "| `STRAND` | `pattern1` | `pattern2` | `distort` | `dissolve` |"),
        "STRAND layout is defined");
  CHECK(FileHas(spec, "| `FLOW` | `body` | `mask` or `dissolve` | `flowx` | `flowy` |"),
        "FLOW layout is defined — the one that folds a body+flow pair into one file");
  CHECK(FileHas(spec, "`STRETCH` and `TILE` cannot be the same channel"),
        "R1: the SHAPE-vs-MATERIAL law is stated at channel granularity");
  CHECK(FileHas(spec, "A sheet MAY mix modes across channels"),
        "R2: mixing modes is legal, which is what smoke_strand actually does");
  CHECK(FileHas(spec, "Slots `distort`, `flowx`, `flowy` are signed"),
        "R3: the signed-channel encoding is pinned");
  CHECK(FileHas(spec, "A is DATA, not coverage"),
        "R4: a packed sheet's alpha is not an opacity mask");
  CHECK(FileHas(spec, "There are no mipmaps"),
        "R5: the one-mip-level consequence is stated");
  CHECK(FileHas(spec, "No channel may be constant"), "R6: no wasted channels");
}

// ── 2. The rule is ENFORCED, not merely written ─────────────────────────────
// This is the part that failed last time.
static void Test_TheRuleIsActuallyWired(void) {
  CHECK(FileHas("CMakeLists.txt",
                "COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_vfx_surface_registry.py"),
        "the validator runs at CMake configure time");
  CHECK(FileHas("CMakeLists.txt", "RESULT_VARIABLE WUXING_VFX_SURFACE_VALID") &&
            FileHas("CMakeLists.txt", "if(NOT WUXING_VFX_SURFACE_VALID EQUAL 0)") &&
            FileHas("CMakeLists.txt", "message(FATAL_ERROR"),
        "...and its exit code is FATAL — a warning would be ignored exactly as "
        "the unwired validator was");

  const char *v = "scripts/validate_vfx_surface_registry.py";
  CHECK(FileHas(v, "CHANNEL_RE = re.compile("),
        "the validator parses the grammar rather than substring-matching prose");
  CHECK(FileHas(v, "r\"^(?P<layout>[A-Z_]+)\\s*\\|\\s*\""),
        "the layout token accepts SPLIT_LEGACY's underscore");
  CHECK(FileHas(v, "\"STRAND\":   {\"R\": {\"pattern1\"}, \"G\": {\"pattern2\"}, \"B\": {\"distort\"}, \"A\": {\"dissolve\"}}"),
        "the validator's STRAND slots match the spec table");
  CHECK(FileHas(v, "SIGNED_SLOTS = {\"distort\", \"flowx\", \"flowy\"}"),
        "and its signed-slot set matches R3");
  CHECK(FileHas(v, "failures += check_channels("),
        "every registered asset's channels string goes through the grammar");
  CHECK(!FileHas(v, "if \"flow\" in assets and \"RG\" not in assets[\"flow\"]"),
        "the old substring check on the word 'RG' is gone — it passed on prose "
        "that merely mentioned RG, which is not a channel declaration");
}

// ── 3. Every registered asset declares a known layout ───────────────────────
static void Test_EveryAssetDeclaresALayout(void) {
  const char *manifest = Slurp("assets/vfx_surface_profiles.json");
  if (!manifest) {
    CHECK(0, "assets/vfx_surface_profiles.json is readable");
    return;
  }

  static const char *kLayouts[] = {"STRAND", "FLOW", "OPAQUE", "FLIPBOOK",
                                   "SPLIT_LEGACY"};
  const char *needle = "\"channels\": \"";
  int total = 0, ok = 0, packed = 0, legacy = 0;
  const char *p = manifest;
  while ((p = strstr(p, needle)) != NULL) {
    p += strlen(needle);
    total++;
    for (size_t i = 0; i < sizeof(kLayouts) / sizeof(kLayouts[0]); i++) {
      size_t n = strlen(kLayouts[i]);
      if (strncmp(p, kLayouts[i], n) == 0 && (p[n] == ' ' || p[n] == '|')) {
        ok++;
        if (strcmp(kLayouts[i], "STRAND") == 0 || strcmp(kLayouts[i], "FLOW") == 0) packed++;
        if (strcmp(kLayouts[i], "SPLIT_LEGACY") == 0) legacy++;
        break;
      }
    }
  }

  CHECK_MSG(total > 0 && ok == total,
            "every registered asset opens with a known layout token",
            "%d of %d conform", ok, total);
  CHECK_MSG(packed >= 2, "at least the two reference STRAND sheets are packed",
            "found %d packed", packed);
  CHECK_MSG(legacy > 0,
            "the SPLIT_LEGACY debt is declared rather than hidden — the "
            "validator prints the count and it is the migration's progress bar",
            "found %d legacy assets", legacy);
}

// ── 4. The two reference sheets say what the spec says they say ─────────────
// smoke_strand is cited by name in TEXTURE_PACKING.md R2 as the example of a
// sheet that MIXES modes. If its declaration ever stops mixing, the spec's
// worked example silently becomes fiction.
static void Test_ReferenceSheetsMatchTheSpecsClaim(void) {
  const char *m = "assets/vfx_surface_profiles.json";
  CHECK(FileHas(m, "STRAND | R:pattern1/STRETCH | G:pattern2/STRETCH | B:distort/TILE | A:dissolve/TILE"),
        "smoke_strand mixes modes: R/G are a SHAPE stretched once, B/A are "
        "panned and therefore seamless");
  CHECK(FileHas(m, "STRAND | R:pattern1/TILE | G:pattern2/TILE | B:distort/TILE | A:dissolve/TILE"),
        "energy_wisp tiles every channel — a repeating filament MATERIAL, the "
        "opposite authoring decision on the same layout");
  CHECK(FileHas("core/trails/shaders/trail_deform.fs", "bool stretch = u_strandFlow.z > 0.5;"),
        "and the consumer carries the switch that tells the two apart");
}

// ── 5. R5's ground truth: nothing generates mipmaps ─────────────────────────
static void Test_NoMipmapsIsStillTrue(void) {
  CHECK(!FileHas("core/vfx_surface_registry.c", "GenTextureMipmaps"),
        "the surface registry generates no mipmaps, so R5's frequency warning "
        "still applies");
  CHECK(FileHas("core/vfx_surface_registry.c", "SetTextureFilter") &&
            FileHas("core/vfx_surface_registry.c", "SetTextureWrap"),
        "...it sets only filter and wrap, which is what R5 cites");
}

int main(void) {
  printf("=== texture packing: spec, enforcement wiring, and declarations ===\n");
  Test_SpecIsPresentAndComplete();
  Test_TheRuleIsActuallyWired();
  Test_EveryAssetDeclaresALayout();
  Test_ReferenceSheetsMatchTheSpecsClaim();
  Test_NoMipmapsIsStillTrue();
  printf("---- %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
