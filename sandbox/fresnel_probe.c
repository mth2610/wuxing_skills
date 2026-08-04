// FRESNEL PROBE — has this project ever computed |N.V| correctly?
//
// The question is not about the smoke column. Every fresnel in the engine
// writes `normalize(viewPos - fragPosition)`, and two facts put that in doubt at
// once: `viewPos` is bound only by SkillManager_BeginShader(), and
// `fragPosition` comes through matModel, which inside a 3D pass is model x view
// (ENGINE_LANDMINES §9). If both hold, that line subtracts a view-space point
// from a world-space one everywhere it appears.
//
// GROUND TRUTH, and it needs no taste: on a cylinder seen from the side, |N.V|
// is 1 along the centre line and falls to 0 at both silhouettes, symmetrically.
// So draw a plain cylinder — raylib's own, not this project's tube code, so
// nothing here can be blamed on the mesh — shade it with each reading in turn,
// and read the scanline back as numbers.
//
// ON-AXIS, NOT OFF TO THE SIDE. The first version placed two cylinders either
// side of cam.target so both readings could be read from one screenshot, and
// that made the scoring untrustworthy: a cylinder that is NOT on the camera's
// view axis has its own natural perspective skew — the near point of the
// cylinder shifts toward screen centre purely from geometry, nothing to do with
// which fresnel convention is used — and "peak within 10% of the cylinder's own
// footprint centre" cannot tell that skew apart from a wrong convention. Both
// off-axis columns read as "peak off-centre" and the run was unscoreable.
//
// The fix: put the cylinder exactly ON the ray from cam.position through
// cam.target. raylib's LookAt projection has no lens shift, so that ray always
// lands on the exact screen centre — cam.target itself qualifies, and needs no
// extra math to prove it. One position, tested across consecutive frames (mode
// 0, then mode 1, then the |fragNormal| sanity check), so each reading gets the
// undistorted case instead of three shapes fighting for one screenshot.
//
// FOUR MORE BUGS FOUND WHILE BUILDING THIS PROBE, all worth remembering
// because they are exactly the kind of thing this shader was built to catch
// in OTHER code, caught here in the test instead:
//
//   1. This probe calls raw BeginShaderMode(), which — per the shader's own
//      warning above — never binds viewPos. First on-axis run reported
//      byte-identical scanlines for mode 0 and mode 1: proof viewPos was
//      (0,0,0), so `viewPos - fragPosition` had silently collapsed into
//      `-fragPosition` and the probe was comparing a reading against itself.
//      Fixed by binding viewPos = cam.position before each draw, same as
//      skill_manager.c:1260.
//   2. The scan's left/right edge points were offset along raw world X
//      (centre.x ± PROBE_R). The cylinder is rotationally symmetric about
//      world Y, so its true silhouette sits along the CAMERA's right vector,
//      not world X — for an isometric camera (forward has both X and Z) those
//      are different lines. Fixed by offsetting along
//      normalize(cross(forward, up)) instead. Turned out to be a no-op for
//      the DEFAULT sandbox camera specifically — sandbox_core.c's rig has
//      camera.position.x == camera.target.x, so forward.x = 0 and the right
//      vector collapses to pure world X anyway — but it is not a no-op in
//      general and stays fixed correctly.
//   3. The remaining weirdness (asymmetric profile, edge values bouncing back
//      up instead of reaching 0) survived fixes 1 and 2 unchanged. The saved
//      PNGs (autotest_output/fresnel_probe_mode*.png) showed why: the probe
//      cylinder was NOT a clean isolated shape — its body showed cream
//      (0xFFD39B) and steel-blue (0x3B5998), DrawCharacter3D's own
//      skin/clothing colours (main.c:1069), not this shader's grayscale.
//      main.c:775 sets camera.target = player.position, and the probe sat
//      exactly at cam.target — directly enclosing the player character model.
//      Not a shader or post-fx bug at all; the probe was standing where the
//      player stands. Fixed by moving partway back along the same view ray
//      toward cam.position instead of sitting at cam.target — still on-axis
//      (the WHOLE ray projects to screen centre, not just the target point),
//      but floating above the player's head and clear of the ground.
//   4. THE BIG ONE. A mode-3 decisive test (length(fragPosition) vs two
//      literal reference markers, see below) read fragPosition as WORLD
//      space, contradicting ENGINE_LANDMINES §9. Re-reading §9 before
//      trusting that reversal: its mechanism is `matModel = modelTransform *
//      rlGetMatrixTransform()`, which is specifically what raylib's DrawMesh
//      does — and this probe drew its cylinder with DrawCoreCylinder
//      (core/geometry/pm_core_shapes.inl), rlBegin(RL_QUADS)/rlVertex3f
//      IMMEDIATE MODE, a different rlgl code path for populating matModel.
//      Checking what the REAL fresnel shaders draw with:
//      core/material/material_system.h:162-163 — CrystalMaterial builds a
//      Mesh (ProceduralMesh_BuildCrystalClusterMesh) and draws it via
//      ProceduralMesh_DrawBakedCrystalCluster(Mesh, Material, Matrix), a
//      DrawMesh wrapper. The production shaders go through DrawMesh; this
//      probe did not, so its world-space reading did not transfer to them.
//      Fixed by switching the probe to DrawMesh (GenMeshCylinder + a real
//      Material) — see EnsureMesh/ProbeCylinderTransform.
//
// HOW TO READ IT
//   peak near the centre, ~0 at both ends, symmetric   -> that reading is right
//   peak off-centre, or asymmetric                     -> that reading is wrong
//   flat                                                -> N or V is constant
//
// Whichever mode passes settles the convention for the whole project, not just
// for one effect.

#include "sandbox/fresnel_probe.h"

#include "core/resource_manager.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PROBE_IDLE = 0,
    PROBE_MODE0,   // normalize(viewPos - fragPosition)
    PROBE_MODE1,   // normalize(-fragPosition)
    PROBE_MODE2,   // length(fragNormal) sanity check
    PROBE_MODE3,   // length(fragPosition) — decisive world-vs-view space test
} ProbeStage;

static ProbeStage s_stage = PROBE_IDLE;
static bool s_pending = false;
static Camera3D s_cam = {0};
static Shader s_shader = {0};
static bool s_tried = false;

// ON THE CAMERA'S OWN AXIS, not in arena coordinates — see file header.
//
// SMALL RELATIVE TO ITS DISTANCE FROM THE CAMERA — this matters now in a way
// it did not before. ReportScan's edge points (centre ± PROBE_R along the
// camera's right vector) are an ORTHOGRAPHIC approximation of the true
// perspective tangent point; the two coincide only as PROBE_R/distance -> 0.
// At radius 0.9 and the probe sitting ~3.4 m from camera (new default zoom +
// the 45%-back placement from the player fix), that ratio is ~0.26 — a
// mode1 (-fragPosition) run at that ratio came back a lopsided ramp instead
// of a symmetric dome: real dot-product values, measured against a silhouette
// assumption that was measurably wrong at this range. Shrunk 3x so the
// approximation error shrinks with it; raise it back only alongside a real
// perspective-correct tangent formula, not by feel.
static const float PROBE_R = 0.3f;
static const float PROBE_H = 1.0f;
static Vector3 s_probePos = {0};

// Mode 3's two reference markers (world-hypothesis / view-hypothesis), and
// where they land on screen so Readback can sample them without recomputing
// GetWorldToScreen from scratch.
static Vector3 s_refWorldPos = {0}, s_refCamPos = {0};
static float s_distWorld = 0.0f, s_distCam = 0.0f;

typedef struct {
    bool valid;
    float peakOff;
    int lv, cv, rv;
    bool verdict; // true = DUNG (reads like a real fresnel)
} ScanResult;
static ScanResult s_result[3];

void FresnelProbe_Arm(void) { s_stage = PROBE_MODE0; }

static Mesh s_mesh = {0};       // unit cylinder: radius 1, height 1, base at local y=0
static Material s_material = {0};
static bool s_meshReady = false;

static void EnsureShader(void)
{
    if (s_tried) return;
    s_tried = true;
    s_shader = ResourceManager_LoadShader("core/shaders/probe_fresnel.vs",
                                          "core/shaders/probe_fresnel.fs");
    if (s_shader.id == 0)
        TraceLog(LOG_WARNING, "[FRESNEL] probe_fresnel.vs/.fs failed to load");
}

// DRAWN VIA DrawMesh, NOT immediate-mode rlBegin/rlVertex3f — this was itself
// a bug, found by checking what the REAL fresnel shaders actually draw with.
// core/material/material_system.h:162-163: CrystalMaterial builds a Mesh via
// ProceduralMesh_BuildCrystalClusterMesh, then draws it with
// ProceduralMesh_DrawBakedCrystalCluster(Mesh, Material, Matrix) — the
// signature of a DrawMesh wrapper. That is EXACTLY the mechanism
// ENGINE_LANDMINES §9 describes (matModel = modelTransform *
// rlGetMatrixTransform() inside DrawMesh). This probe's first version used
// DrawCoreCylinder (core/geometry/pm_core_shapes.inl), which is
// rlBegin(RL_QUADS)/rlVertex3f immediate-mode — a DIFFERENT rlgl code path
// for populating matModel than DrawMesh uses. Its mode-3 result (fragPosition
// reads as WORLD space) does not transfer to the DrawMesh-based production
// shaders; only a DrawMesh-based probe does.
static void EnsureMesh(void)
{
    if (s_meshReady) return;
    s_meshReady = true;
    s_mesh = GenMeshCylinder(1.0f, 1.0f, 48);
    s_material = LoadMaterialDefault();
}

// transform for a cylinder of the given radius/height, CENTRE at `pos`
// (matching the old DrawCoreCylinder(bottom, top, ...) call sites, which
// took bottom = pos - h/2, top = pos + h/2).
static Matrix ProbeCylinderTransform(Vector3 pos, float radius, float height)
{
    Matrix scale = MatrixScale(radius, height, radius);
    Matrix translate = MatrixTranslate(pos.x, pos.y - height * 0.5f, pos.z);
    return MatrixMultiply(scale, translate);
}

void FresnelProbe_Draw3D(Camera3D cam)
{
    if (s_stage == PROBE_IDLE) return;
    s_cam = cam;
    s_pending = true;

    EnsureShader();
    if (s_shader.id == 0) { s_stage = PROBE_IDLE; s_pending = false; return; }
    EnsureMesh();
    s_material.shader = s_shader;

    // NOT cam.target itself — main.c:775 sets camera.target = player.position
    // (+0.2 up), so a cylinder placed there enclosed the player character.
    // The saved screenshots showed cream (0xFFD39B) and steel-blue (0x3B5998)
    // — DrawCharacter3D's own skin/clothing colours (main.c:1069) — bleeding
    // through the supposedly-plain-grey probe. Not a shader or post-fx bug:
    // the probe was standing where the player stands.
    //
    // The view ray stays the fix — ANY point on it projects to screen centre,
    // not only cam.target, because it IS the camera's local view-space Z axis.
    // Move partway back toward cam.position instead: cam is above and behind
    // the player (camera.position.y = 5 vs target.y = 0 in the default
    // sandbox rig), so this lands the cylinder floating well above the
    // player's head and clear of the ground, still dead-centre on screen.
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    float camDist = Vector3Distance(cam.position, cam.target);
    s_probePos = Vector3Add(cam.position, Vector3Scale(fwd, camDist * 0.45f));

    int modeLoc = GetShaderLocation(s_shader, "u_probeMode");
    if (modeLoc < 0)
        TraceLog(LOG_WARNING, "[FRESNEL] u_probeMode has no location — the "
                              "shader will not switch reading");

    // viewPos is bound ONLY by SkillManager_BeginShader() — this probe calls
    // raw BeginShaderMode(), so without this line viewPos reads (0,0,0) and
    // mode 0 (viewPos - fragPosition) silently COLLAPSES into mode 1
    // (-fragPosition). That happened: the first run of this probe (and the
    // off-axis run before it) reported byte-identical scanlines for both
    // modes, which is only possible if viewPos was zero — the probe had been
    // comparing -fragPosition against itself the whole time. Bind it the same
    // way skill_manager.c:1260 does.
    int viewPosLoc = GetShaderLocation(s_shader, "viewPos");
    if (viewPosLoc < 0)
        TraceLog(LOG_WARNING, "[FRESNEL] viewPos has no location — mode 0 "
                              "cannot be distinguished from mode 1");

    float mode = (s_stage == PROBE_MODE0) ? 0.0f
               : (s_stage == PROBE_MODE1) ? 1.0f
               : (s_stage == PROBE_MODE2) ? 2.0f
                                           : 3.0f;

    // Flush whatever the rest of the 3D pass has queued via the immediate-mode
    // batch before we touch shader state — same trap as ENGINE_LANDMINES §1.
    // DrawMesh below draws its OWN VAO (not through that batch), but setting a
    // uniform requires enabling our shader first, and doing that while an
    // unrelated batch is still pending would flush it under the wrong shader.
    rlDrawRenderBatchActive();
    if (modeLoc >= 0) SetShaderValue(s_shader, modeLoc, &mode, SHADER_UNIFORM_FLOAT);
    if (viewPosLoc >= 0) SetShaderValue(s_shader, viewPosLoc, &cam.position, SHADER_UNIFORM_VEC3);
    DrawMesh(s_mesh, s_material, ProbeCylinderTransform(s_probePos, PROBE_R, PROBE_H));

    if (s_stage == PROBE_MODE3) {
        // Two LITERAL reference markers (mode 4, u_refValue), beside the main
        // cylinder, in the SAME frame — so they pass through the identical
        // tonemap/colour-grade curve the cylinder's own readback did. Judging
        // one readback against a hand-guessed tonemap inverse is a guess;
        // judging it against two markers that went through the same unknown
        // curve is not.
        s_distWorld = Vector3Length(s_probePos);
        s_distCam = Vector3Distance(s_probePos, cam.position);
        Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
        s_refWorldPos = Vector3Add(s_probePos, Vector3Scale(right, 1.2f));
        s_refCamPos = Vector3Subtract(s_probePos, Vector3Scale(right, 1.2f));

        int refLoc = GetShaderLocation(s_shader, "u_refValue");
        float m4 = 4.0f;
        float refW = s_distWorld / 20.0f;
        float refC = s_distCam / 20.0f;

        if (modeLoc >= 0) SetShaderValue(s_shader, modeLoc, &m4, SHADER_UNIFORM_FLOAT);
        if (refLoc >= 0) SetShaderValue(s_shader, refLoc, &refW, SHADER_UNIFORM_FLOAT);
        DrawMesh(s_mesh, s_material,
                ProbeCylinderTransform(s_refWorldPos, PROBE_R * 0.6f, PROBE_H * 0.5f));

        if (refLoc >= 0) SetShaderValue(s_shader, refLoc, &refC, SHADER_UNIFORM_FLOAT);
        DrawMesh(s_mesh, s_material,
                ProbeCylinderTransform(s_refCamPos, PROBE_R * 0.6f, PROBE_H * 0.5f));
    }
}

// Read one horizontal scanline across the on-axis cylinder and report the shape
// of it. The verdict is computed here, not left to the reader: where the peak
// sits and how symmetric the two halves are is exactly what separates a correct
// convention from a plausible-looking wrong one. Meaningful now specifically
// because the cylinder is on-axis — off-axis, "peak at centre" is not the right
// question (see file header).
static ScanResult ReportScan(Image img, const char *label)
{
    ScanResult r = {0};

    /* The edge points must be offset along the CAMERA's right vector, not raw
     * world X. The cylinder is rotationally symmetric about world Y, so its
     * true silhouette sits wherever is tangent to the view direction — for an
     * isometric camera (forward has both X and Z components) that is nowhere
     * near centre.x ± PROBE_R. Sampling the wrong span puts background/other
     * geometry inside the assumed body and the body's real edge somewhere in
     * the middle of the row — which is exactly the "0 at index 11, high at
     * both true edges" shape the first on-axis run produced. */
    Vector3 fwd = Vector3Normalize(Vector3Subtract(s_cam.target, s_cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, s_cam.up));
    Vector3 pL = Vector3Subtract(s_probePos, Vector3Scale(right, PROBE_R));
    Vector3 pR = Vector3Add(s_probePos, Vector3Scale(right, PROBE_R));

    /* GetWorldToScreen speaks WINDOW coordinates, LoadImageFromScreen returns
     * FRAMEBUFFER pixels — on Retina they differ by exactly 2x. Take the ratio
     * from real data, not an assumption. */
    float dpi = (GetScreenWidth() > 0) ? ((float)img.width / (float)GetScreenWidth()) : 1.0f;
    Vector2 sL = GetWorldToScreen(pL, s_cam);
    Vector2 sR = GetWorldToScreen(pR, s_cam);
    Vector2 sC = GetWorldToScreen(s_probePos, s_cam);
    int x0 = (int)(((sL.x < sR.x) ? sL.x : sR.x) * dpi);
    int x1 = (int)(((sL.x < sR.x) ? sR.x : sL.x) * dpi);
    int row = (int)(sC.y * dpi);

    if (row < 0 || row >= img.height || x1 <= x0 + 8 ||
        x1 < 0 || x0 >= img.width) {
        TraceLog(LOG_WARNING,
                 "[FRESNEL] %s: chieu ra %d..%d px hang %d — ngoai man hinh hoac "
                 "qua nho de doc", label, x0, x1, row);
        return r;
    }
    if (x0 < 0) x0 = 0;
    if (x1 >= img.width) x1 = img.width - 1;

    int width = x1 - x0;
    int mid = (x0 + x1) / 2;
    int peak = -1, peakX = x0;
    for (int x = x0; x <= x1; x++) {
        int v = GetImageColor(img, x, row).r;
        if (v > peak) { peak = v; peakX = x; }
    }
    float peakOff = (float)(peakX - mid) / (float)(width * 0.5f); // 0 = centred
    int lv = GetImageColor(img, x0 + width / 12, row).r;
    int rv = GetImageColor(img, x1 - width / 12, row).r;
    int cv = GetImageColor(img, mid, row).r;

    char prof[160];
    int n = 0;
    for (int k = 0; k <= 16 && n < (int)sizeof(prof) - 4; k++) {
        int x = x0 + (width * k) / 16;
        n += snprintf(prof + n, sizeof(prof) - n, "%4d", GetImageColor(img, x, row).r);
    }

    // On-axis, this is finally a fair test: peak within a tenth of the centre,
    // both rims dark, the two rims close to each other, centre clearly brighter.
    bool centred = (peakOff > -0.10f && peakOff < 0.10f);
    bool rimsDark = (lv < 90 && rv < 90);
    bool symmetric = (abs(lv - rv) < 30);
    bool domed = (cv > lv + 40 && cv > rv + 40);
    r.valid = true;
    r.peakOff = peakOff;
    r.lv = lv; r.cv = cv; r.rv = rv;
    r.verdict = centred && rimsDark && symmetric && domed;

    TraceLog(LOG_INFO,
             "[FRESNEL] %-14s x %4d..%4d hang %4d  rong %3d px  dinh lech %+.2f  "
             "trai %3d giua %3d phai %3d  -> %s",
             label, x0, x1, row, width, (double)peakOff, lv, cv, rv,
             r.verdict ? "DUNG" : "SAI");
    TraceLog(LOG_INFO, "[FRESNEL]  %s", prof);
    return r;
}

// Split out of FresnelProbe_Readback so mode 3's early-exit path can call it
// directly instead of a goto (a label in C cannot be followed by a
// declaration — `print_verdict: ScanResult m0 = ...;` does not compile,
// declarations are not statements in C the way they are in C++).
static void PrintVerdict(void)
{
    ScanResult m0 = s_result[0], m1 = s_result[1], m2 = s_result[2];
    if (!m0.valid || !m1.valid) {
        TraceLog(LOG_WARNING, "[FRESNEL] mode0/mode1 khong doc duoc — xem canh bao ben tren");
        return;
    }
    if (m2.valid && !(m2.lv > 200 && m2.cv > 200 && m2.rv > 200))
        TraceLog(LOG_WARNING, "[FRESNEL] |fragNormal| khong phang gan 255 khap mat "
                              "(trai %d giua %d phai %d) — N co the la rac, hai ket qua tren khong dang tin",
                 m2.lv, m2.cv, m2.rv);

    if (m0.verdict && !m1.verdict)
        TraceLog(LOG_INFO, "[FRESNEL] VERDICT: normalize(viewPos - fragPosition) DUNG — quy uoc hien tai cua du an la dung");
    else if (m1.verdict && !m0.verdict)
        TraceLog(LOG_INFO, "[FRESNEL] VERDICT: normalize(-fragPosition) DUNG, normalize(viewPos - fragPosition) SAI — "
                           "TAT CA fresnel dung 'viewPos - fragPosition' (plasma_shell, crystal, aura_shell, "
                           "effect_material, water_splash) dang sai, can ghi vao ENGINE_LANDMINES.md");
    else if (m0.verdict && m1.verdict)
        TraceLog(LOG_WARNING, "[FRESNEL] VERDICT: CA HAI deu doc DUNG — khong the ca hai dung cung luc, kiem tra lai tieu chi cham diem");
    else
        TraceLog(LOG_WARNING, "[FRESNEL] VERDICT tu dome-shape: KHONG cach doc nao dat — nhung xem dong "
                              "length(fragPosition) ben tren, do la phep do dut diem, tin no hon hinh dang vom");
}

void FresnelProbe_Readback(void)
{
    if (!s_pending) return;
    s_pending = false;

    Image img = LoadImageFromScreen();

    if (s_stage == PROBE_MODE3) {
        // The decisive test fs_header.glsl itself describes: length(fragPosition)
        // must equal distance-to-ORIGIN if fragPosition is world space, or
        // distance-to-CAMERA if view space. distWorld/distCam/s_ref*Pos were
        // computed in Draw3D (same frame), alongside two LITERAL reference
        // markers carrying exactly those two f values — judged against them
        // instead of a hand-guessed tonemap inverse.
        float dpi = (GetScreenWidth() > 0) ? ((float)img.width / (float)GetScreenWidth()) : 1.0f;
        Vector2 sC = GetWorldToScreen(s_probePos, s_cam);
        Vector2 sW = GetWorldToScreen(s_refWorldPos, s_cam);
        Vector2 sV = GetWorldToScreen(s_refCamPos, s_cam);
        int cx = (int)(sC.x * dpi), cy = (int)(sC.y * dpi);
        int wx = (int)(sW.x * dpi), wy = (int)(sW.y * dpi);
        int vx = (int)(sV.x * dpi), vy = (int)(sV.y * dpi);
        bool cOk = cx >= 0 && cx < img.width && cy >= 0 && cy < img.height;
        bool wOk = wx >= 0 && wx < img.width && wy >= 0 && wy < img.height;
        bool vOk = vx >= 0 && vx < img.width && vy >= 0 && vy < img.height;
        if (cOk && wOk && vOk) {
            int centerV = GetImageColor(img, cx, cy).r;
            int worldRefV = GetImageColor(img, wx, wy).r;
            int camRefV = GetImageColor(img, vx, vy).r;
            int dWorld = abs(centerV - worldRefV), dCam = abs(centerV - camRefV);
            const char *closer = (dWorld < dCam) ? "WORLD" : (dCam < dWorld) ? "VIEW" : "HOA (khong phan biet duoc)";
            TraceLog(LOG_INFO,
                     "[FRESNEL] length(fragPosition) tai tam: %3d  |  moc WORLD (f=%.2f): %3d (lech %d)  |  "
                     "moc VIEW (f=%.2f): %3d (lech %d)  ->  GAN %s HON",
                     centerV, (double)(s_distWorld / 20.0f), worldRefV, dWorld,
                     (double)(s_distCam / 20.0f), camRefV, dCam, closer);
        } else {
            TraceLog(LOG_WARNING, "[FRESNEL] length(fragPosition): tam hoac moc tham chieu chieu ra ngoai man hinh "
                                  "(tam %s, moc world %s, moc view %s)",
                     cOk ? "OK" : "NGOAI", wOk ? "OK" : "NGOAI", vOk ? "OK" : "NGOAI");
        }
        if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
        ExportImage(img, "autotest_output/fresnel_probe_mode3.png");
        UnloadImage(img);
        s_stage = PROBE_IDLE;
        PrintVerdict();
        return;
    }

    const char *label;
    int idx;
    const char *file;
    switch (s_stage) {
    case PROBE_MODE0: label = "viewPos-frag"; idx = 0; file = "autotest_output/fresnel_probe_mode0.png"; break;
    case PROBE_MODE1: label = "-frag (view)"; idx = 1; file = "autotest_output/fresnel_probe_mode1.png"; break;
    default:          label = "|fragNormal|"; idx = 2; file = "autotest_output/fresnel_probe_mode2.png"; break;
    }

    if (idx == 0)
        TraceLog(LOG_INFO, "[FRESNEL] ---- on-axis |N.V| tren hinh tru: 1 o giua, 0 o hai bien ----");
    s_result[idx] = ReportScan(img, label);

    if (!DirectoryExists("autotest_output")) MakeDirectory("autotest_output");
    ExportImage(img, file);
    UnloadImage(img);

    if (s_stage == PROBE_MODE0) {
        s_stage = PROBE_MODE1; // next frame's Draw3D picks this up automatically
        return;
    }
    if (s_stage == PROBE_MODE1) {
        s_stage = PROBE_MODE2;
        return;
    }
    if (s_stage == PROBE_MODE2) {
        s_stage = PROBE_MODE3; // next frame's Draw3D picks this up; PrintVerdict() runs after it, above
        return;
    }
}
