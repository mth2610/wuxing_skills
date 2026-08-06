// core headless test — the SPACE CONTRACT of the volume tube, pinned in source.
//
// WHAT THIS GUARDS. `core/docs/VOLUME_SHADING_HANDOFF.md` records a full
// session lost to a |N.V| that came out inverted on a plain cylinder. The
// cause, found 06/08/2026 and measured numerically by
// `third_party/vulkan/tests/rlvk_visual_test.c` scenario `imm_normal`:
//
//   main.c's MyBeginMode3D calls rlPushMatrix() in RL_MODELVIEW. That arms
//   rlgl's `transformRequired` and parks the VIEW matrix in State.transform.
//   From there an IMMEDIATE-MODE draw (rlBegin/rlVertex3f/rlNormal3f — which
//   is how PMTube_DrawFaded draws this tube) gets its positions AND normals
//   transformed ON THE CPU (rlgl.h:1529 / 1612), so the attributes arriving at
//   the vertex shader are ALREADY IN VIEW SPACE. The batch flush then uploads
//   matModel = State.transform = that same view matrix (rlgl.h:3082, mirrored
//   at third_party/vulkan/rlvk/rlvk_core.inl:595).
//
//   So vs_header.glsl's VS_FinalOutput — `matModel * vec4(vertexNormal, 0)` —
//   applies the view rotation A SECOND TIME on this draw path. Measured:
//       Nworld (0.30, 0.90, -0.32)
//       raw vertexNormal      -> (0.43, 0.85, 0.30) = view*N       (d 0.002)
//       matModel*vertexNormal -> (0.18, 0.56, 0.80) = view*view*N  (d 0.005)
//
// WHY A SOURCE TEST AND NOT A MATHS TEST. There is no arithmetic to check here
// — the defect was never in a formula, it was in WHICH SPACE two vectors were
// in, and the only conclusive instrument for that is the GPU readback above.
// What a headless test CAN do, at zero cost, is stop the fix from being
// silently undone: `VS_FinalOutput(vertexPosition)` is the obvious,
// conventional, every-other-shader-does-it line, and someone WILL restore it
// while tidying. These checks make that a red test instead of another session.
//
// IF YOU ARE HERE BECAUSE THIS TEST FAILED, read trail_volume.vs's header
// before changing anything. The two legal worlds are spelled out there: this
// file pins the current one (MyBeginMode3D pushes MODELVIEW -> view space), and
// names what has to change together if the engine ever moves to the other.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("  FAIL: %s\n", msg);                                             \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

// Whitespace-insensitive substring search, so a reformat of the shader does
// not fail the test but a semantic change does. Same helper shape as
// beam_geometry_test.c's FileHas.
static void CollapseWS(const char *in, char *out, size_t cap) {
  size_t o = 0;
  int sp = 0;
  for (size_t i = 0; in[i] && o + 2 < cap; i++) {
    char c = in[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      sp = 1;
      continue;
    }
    if (sp && o > 0) out[o++] = ' ';
    sp = 0;
    out[o++] = c;
  }
  out[o] = '\0';
}

// Remove // and /* */ comments (GLSL source) so a semantic check matches
// EXECUTABLE code, not the prose that names it. This file's own assertors
// search for things the code must NOT contain — "matModel *", "u_volViewSrc",
// "viewPos - fragPosition" — and the shader headers legitimately name those
// tokens while explaining the bug (the stray-token trap, core/docs/LANDMINES.md).
static void StripComments(const char *in, char *out, size_t cap) {
  size_t o = 0;
  int done = 0;
  for (size_t i = 0; !done && in[i] && o + 1 < cap; i++) {
    if (in[i] == '/' && in[i + 1] == '/') {
      out[o++] = ' ';
      while (in[i] && in[i] != '\n') i++;
      if (in[i] == '\n') out[o++] = in[i];
    } else if (in[i] == '/' && in[i + 1] == '*') {
      out[o++] = ' ';
      i += 2;
      while (in[i] && !(in[i] == '*' && in[i + 1] == '/')) i++;
      if (in[i]) i++;  // skip the '/' of '*/'; the loop increment steps past it
      else done = 1;  // unterminated block comment: nothing after it is code
    } else {
      out[o++] = in[i];
    }
  }
  out[o] = '\0';
}

static int FileHas(const char *path, const char *needle) {
  static char buf[600000], code[600000], flat[600000];
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("  FAIL: cannot open %s\n", path);
    g_fail++;
    return 0;
  }
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n] = '\0';
  fclose(f);
  StripComments(buf, code, sizeof(code));
  CollapseWS(code, flat, sizeof(flat));
  char want[2048];
  CollapseWS(needle, want, sizeof(want));
  return strstr(flat, want) != NULL;
}

static const char *VS = "core/trails/shaders/trail_volume.vs";
static const char *FS = "core/trails/shaders/trail_volume.fs";

// ── 1. The vertex stage must NOT put the attributes through matModel ────────
static void Test_VertexStageDoesNotDoubleTransform(void) {
  printf("Test_VertexStageDoesNotDoubleTransform\n");

  // THE load-bearing line. On this draw path matModel is the view matrix and
  // the attributes are already view space, so VS_FinalOutput would apply the
  // view rotation twice — that is the whole bug.
  CHECK(!FileHas(VS, "VS_FinalOutput("),
        "trail_volume.vs calls VS_FinalOutput again — on an IMMEDIATE-MODE "
        "draw that applies the view transform TWICE (matModel is the view "
        "matrix here and the attributes are already view space). Read this "
        "shader's header before 'restoring' the conventional line.");

  CHECK(FileHas(VS, "fragPosition = vertexPosition;"),
        "fragPosition must be the raw attribute (already view space), not "
        "matModel * position");
  CHECK(FileHas(VS, "fragNormal = normalize(vertexNormal);"),
        "fragNormal must be the raw attribute (already view space), not "
        "matModel * normal");
  // gl_Position is the one thing that DOES still need mvp: on this path
  // modelview is identity (the view went into State.transform), so mvp is the
  // projection alone and the already-view-space position is exactly its input.
  CHECK(FileHas(VS, "gl_Position = mvp * vec4(vertexPosition, 1.0);"),
        "gl_Position must still go through mvp — only fragPosition/fragNormal "
        "skip matModel");

  // A bare `matModel` anywhere in the executable part would mean the fix was
  // half-applied. The header names it many times in prose, so match the code
  // form, not the token (the stray-token trap in core/docs/LANDMINES.md).
  CHECK(!FileHas(VS, "matModel *") && !FileHas(VS, "matModel*"),
        "trail_volume.vs multiplies by matModel somewhere — nothing on this "
        "draw path may");
}

// ── 2. The fragment stage must take the view vector from view space ─────────
static void Test_ViewVectorIsViewSpace(void) {
  printf("Test_ViewVectorIsViewSpace\n");

  CHECK(FileHas(FS, "vec3 V = normalize(-fragPosition);"),
        "the view vector must be normalize(-fragPosition): in view space the "
        "camera is the origin, so no uniform is involved and none can fail to "
        "arrive");

  // `viewPos` is a WORLD coordinate. Subtracting a view-space fragPosition
  // from it is precisely the defect this test exists to prevent, and it is the
  // form every other shader in the engine uses — so it is the form someone
  // will copy back in. It survives ONLY inside debug mode 15, which reads its
  // magnitude on purpose; that use is `length(viewPos)`, not a subtraction.
  CHECK(!FileHas(FS, "viewPos - fragPosition"),
        "trail_volume.fs mixes a WORLD-space viewPos with a VIEW-space "
        "fragPosition — that is the original inverted-|N.V| bug, restored");

  // The switch that used to select between the two is gone deliberately: one
  // of its options is now known-wrong, and a permanent switch over a settled
  // question rots (third_party/vulkan/CLAUDE.md, methodology rule 3).
  CHECK(!FileHas(FS, "u_volViewSrc"),
        "the u_volViewSrc switch is back — the question it existed to answer "
        "is settled; a switch here only invites re-opening it");
}

// ── 3. The two halves must keep agreeing about which space they are in ──────
static void Test_TheTwoStagesAgree(void) {
  printf("Test_TheTwoStagesAgree\n");

  // The fragment stage's cull reads the ATTRIBUTE normal, whose outward sign
  // pm_tube.inl enforces. That is only meaningful if the vertex stage handed
  // the attribute through unrotated — i.e. it is the same contract as test 1,
  // seen from the other end. Pinned together so a change to one shows up as a
  // failure naming the other.
  CHECK(FileHas(FS, "vec3 Nattr = normalize(fragNormal);") &&
            FileHas(FS, "float facing = dot(Nattr, V);"),
        "the cull still reads the attribute normal against V — if either side "
        "changes space, this is where the silhouette breaks");
  CHECK(FileHas(FS, "float d = abs(dot(N, V));"),
        "the shading term is still |N.V| off the same V");

  // The tripwire that produced the numbers in the first place. If this
  // scenario is ever deleted, the claim above becomes unfalsifiable again —
  // which is exactly the state the lost session started from.
  CHECK(FileHas("third_party/vulkan/tests/rlvk_visual_test.c",
                "{ \"imm_normal\", sc_imm_normal },"),
        "the rlvk `imm_normal` scenario is gone — it is the only instrument "
        "that can decide this question; re-add it from "
        "core/docs/VOLUME_SHADING_HANDOFF.md before trusting any space claim");
}

int main(void) {
  printf("=== volume space contract ===\n");
  Test_VertexStageDoesNotDoubleTransform();
  Test_ViewVectorIsViewSpace();
  Test_TheTwoStagesAgree();
  if (g_fail) {
    printf("FAILED (%d)\n", g_fail);
    return 1;
  }
  printf("OK\n");
  return 0;
}
