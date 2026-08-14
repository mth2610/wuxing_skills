#include "core/ribbon_strip.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

// Ngưỡng coi 1 vector là "gần như 0" (cross product suy biến) - dưới
// ngưỡng này phải fallback sang vector tham chiếu khác để tránh chia 0 /
// NaN khi normalize.
#define RIBBON_DEGENERATE_EPSILON 0.0001f

// ── Foreshortening guard, for the non-camera-facing modes ──────────────────
//
// A camera-facing strip has a constant projected width by construction. A strip
// pinned to a plane (RIBBON_FIXED_NORMAL / RIBBON_WORLD_UP) does not: its width
// vector `side` is perpendicular to the plane normal and ROTATES WITH THE
// TANGENT, so how much of it survives projection changes ALONG the strip. On a
// straight path the tangent is constant and so is the projection — the band is
// uniformly wide or uniformly edge-on, and both look deliberate. Where the path
// CURVES, the tangent sweeps and the projected width sweeps with it.
//
// Measured on the swept trail's bench path (a lemniscate, camera at the sandbox's
// own 8.4 m / 38 deg): the projection factor runs from 1.00 at one end of the
// strip to **0.08 at the other — a 13x change along a single band**, at many
// camera angles. The thin end is sub-pixel, so it renders as dashes while the
// wide end is solid. That is the artefact the swept BLADE trail was chased
// through four rounds for (core/composition/common/vc_ribbon_trail.inl,
// 29/07/2026): dashed only in the plane-pinned style, dashed only where the path
// bent, worse zoomed out, and absent from the camera-facing styles drawn by the
// same code on the same paths — including one with THINNER geometry, which is
// what ruled out every "the strip is sub-pixel" and "the mask is wrong" theory.
//
// THE FIX IS TO ROTATE `side`, NOT TO WIDEN IT. The first attempt held the
// projected width by widening the band in world space and paying for it in
// alpha, conserving brightness x width. That is the right law for a DISTANCE
// floor and the wrong one here: it turns a thin stretch into a DIM stretch, and
// a dim stretch between two bright ones is still read as a break. What actually
// works is to blend `side` toward the camera-facing side vector exactly where
// the plane's own side vector is disappearing — the band keeps its width and its
// brightness, and it gives up its plane orientation only where that orientation
// was projecting to nothing anyway, i.e. where it carries no silhouette
// information. Verified on the same path: the worst projection factor over every
// camera angle and every phase goes from 0.069 to 0.342.
//
// 0.35 = about 70 degrees of foreshortening before the blend starts, and it
// reaches fully camera-facing only at true edge-on. Nothing that currently reads
// correctly is touched: above the threshold this is a no-op.
#define RIBBON_MIN_PROJECTION 0.35f

// Below this the cross product carries no direction, only noise: `side` must be
// carried forward from the previous point rather than recomputed. Generous on
// purpose — the transported vector is as correct as the computed one, so there
// is no cost to switching over early, and a marginal cross product is exactly
// where the jump happens.
#define RIBBON_SIDE_DEGENERATE 0.12f

// Tangent tại điểm i dọc theo path - sai phân trung tâm cho các điểm giữa
// (mượt hơn sai phân 1 phía), sai phân 1 phía ở 2 đầu mút.
static Vector3 ComputeTangent(const RibbonPoint *points, int count, int i) {
  Vector3 tangent;
  if (i == 0) {
    tangent = Vector3Subtract(points[1].position, points[0].position);
  } else if (i == count - 1) {
    tangent =
        Vector3Subtract(points[count - 1].position, points[count - 2].position);
  } else {
    tangent = Vector3Subtract(points[i + 1].position, points[i - 1].position);
  }
  float len = Vector3Length(tangent);
  if (len < RIBBON_DEGENERATE_EPSILON)
    return (Vector3){1.0f, 0.0f, 0.0f};
  return Vector3Scale(tangent, 1.0f / len);
}

// Vector "side" vuông góc với tangent, dùng để offset trái/phải tạo bề rộng
// ribbon. Ưu tiên vuông góc với `primaryNormal` (hướng nhìn camera cho
// RIBBON_CAMERA_FACING, world-up cho RIBBON_WORLD_UP, mặt phẳng người gọi
// cấp cho RIBBON_FIXED_NORMAL). Có 2 lớp fallback khi suy biến:
// 1) tangent gần song song primaryNormal -> dùng `fallbackNormal`
// 2) tangent gần song song luôn cả fallbackNormal (cực hiếm) -> dùng vector
//    vuông góc cố định trên mặt phẳng ngang (cùng kiểu trick đã dùng trong
//    CastFireSkill: { -tangent.z, 0, tangent.x }).
static Vector3 ComputeSideVector(Vector3 tangent, Vector3 primaryNormal,
                                 Vector3 fallbackNormal) {
  Vector3 side = Vector3CrossProduct(tangent, primaryNormal);
  float len = Vector3Length(side);
  if (len > RIBBON_DEGENERATE_EPSILON)
    return Vector3Scale(side, 1.0f / len);

  side = Vector3CrossProduct(tangent, fallbackNormal);
  len = Vector3Length(side);
  if (len > RIBBON_DEGENERATE_EPSILON)
    return Vector3Scale(side, 1.0f / len);

  side = (Vector3){-tangent.z, 0.0f, tangent.x};
  len = Vector3Length(side);
  return (len > RIBBON_DEGENERATE_EPSILON) ? Vector3Scale(side, 1.0f / len)
                                           : (Vector3){1.0f, 0.0f, 0.0f};
}

// Resolves a RibbonMode into the (primaryNormal, fallbackNormal) pair
// ComputeSideVector needs — shared by DrawRibbonStripEx and
// Ribbon_ComputeCrossFrame so the two never drift apart.
static void ResolveFrameNormals(RibbonMode mode, Vector3 fixedNormal, Camera3D camera,
                                Vector3 *outPrimary, Vector3 *outFallback) {
  switch (mode) {
    case RIBBON_WORLD_UP:
      *outPrimary  = (Vector3){0.0f, 1.0f, 0.0f};
      *outFallback = (Vector3){1.0f, 0.0f, 0.0f};
      break;
    case RIBBON_FIXED_NORMAL:
      *outPrimary  = Vector3Normalize(fixedNormal);
      *outFallback = (Vector3){0.0f, 1.0f, 0.0f};
      break;
    case RIBBON_CAMERA_FACING:
    default:
      *outPrimary  = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
      *outFallback = camera.up;
      break;
  }
}

// Same as ComputeTangent but reads a plain Vector3 path instead of
// RibbonPoint — used by Ribbon_ComputeCrossFrame, which has no per-point
// width/tint/v (those are per-LAYER properties for a cross-section, not
// per-point).
static Vector3 ComputeTangentV3(const Vector3 *points, int count, int i) {
  Vector3 tangent;
  if (i == 0) {
    tangent = Vector3Subtract(points[1], points[0]);
  } else if (i == count - 1) {
    tangent = Vector3Subtract(points[count - 1], points[count - 2]);
  } else {
    tangent = Vector3Subtract(points[i + 1], points[i - 1]);
  }
  float len = Vector3Length(tangent);
  if (len < RIBBON_DEGENERATE_EPSILON)
    return (Vector3){1.0f, 0.0f, 0.0f};
  return Vector3Scale(tangent, 1.0f / len);
}

void Ribbon_ComputeCrossFrame(const Vector3 *points, int count,
                              RibbonMode mode, Vector3 fixedNormal, Camera3D camera,
                              Vector3 *outAxisA, Vector3 *outAxisB) {
  if (points == NULL || count < 2 || outAxisA == NULL || outAxisB == NULL)
    return;

  Vector3 primaryNormal, fallbackNormal;
  ResolveFrameNormals(mode, fixedNormal, camera, &primaryNormal, &fallbackNormal);

  Vector3 prevA = {0};
  bool havePrevA = false;
  for (int i = 0; i < count; i++) {
    Vector3 tangent = ComputeTangentV3(points, count, i);
    Vector3 a = ComputeSideVector(tangent, primaryNormal, fallbackNormal);

    // Same continuity fix as DrawRibbonStripEx's side vector — without it,
    // axisA can flip sign between adjacent points and the two crossed
    // planes built from it would bowtie.
    if (havePrevA && Vector3DotProduct(a, prevA) < 0.0f)
      a = Vector3Negate(a);
    prevA = a;
    havePrevA = true;

    outAxisA[i] = a;
    outAxisB[i] = Vector3Normalize(Vector3CrossProduct(tangent, a));
  }
}

// Integer hashing keeps the midpoint generator deterministic on GLES/Mali as
// well as desktop.  This belongs in the path primitive, rather than in a
// lightning composition, because the exact same seeded subdivision is useful
// for roots, cracks, and other irregular ribbons.
static unsigned int RibbonMidpoint_Hash(unsigned int x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static float RibbonMidpoint_Signed(unsigned int x) {
  return (float)(RibbonMidpoint_Hash(x) & 0x00ffffffu) * (2.0f / 16777215.0f) - 1.0f;
}

// A single perpendicular vector is ambiguous in 3D.  This provides a stable
// two-axis plane perpendicular to the segment, allowing a bolt to leave its
// start/end line in any world-space direction without a view-dependent bias.
static void RibbonMidpoint_Frame(Vector3 direction, Vector3 *outA, Vector3 *outB) {
  Vector3 ref = fabsf(direction.y) < 0.92f ? (Vector3){0.0f, 1.0f, 0.0f}
                                            : (Vector3){1.0f, 0.0f, 0.0f};
  *outA = Vector3Normalize(Vector3CrossProduct(direction, ref));
  *outB = Vector3Normalize(Vector3CrossProduct(direction, *outA));
}

RibbonMidpointConfig Ribbon_MidpointDefaultConfig(void) {
  return (RibbonMidpointConfig){
      .levels = 4,
      .initialAmplitude = 0.12f,
      .amplitudeDecay = 0.5f,
      .seed = 0u,
  };
}

int Ribbon_GenerateMidpointDisplacement(Vector3 from, Vector3 to,
                                        const RibbonMidpointConfig *config,
                                        Vector3 *outPoints, int outCapacity) {
  if (outPoints == NULL) return 0;
  RibbonMidpointConfig resolved = config ? *config : Ribbon_MidpointDefaultConfig();
  if (resolved.levels < 0) resolved.levels = 0;
  if (resolved.levels > RIBBON_MIDPOINT_MAX_LEVELS)
    resolved.levels = RIBBON_MIDPOINT_MAX_LEVELS;
  if (resolved.initialAmplitude < 0.0f) resolved.initialAmplitude = 0.0f;
  if (resolved.amplitudeDecay <= 0.0f || resolved.amplitudeDecay >= 1.0f)
    resolved.amplitudeDecay = 0.5f;

  int required = (1 << resolved.levels) + 1;
  if (outCapacity < required) return 0;
  outPoints[0] = from;
  outPoints[1] = to;
  int count = 2;
  float amplitude = resolved.initialAmplitude;

  for (int level = 0; level < resolved.levels; ++level) {
    // Work backward so the fixed caller array expands in-place without a
    // scratch allocation.  Every original segment is still intact when its
    // midpoint is computed.
    for (int segment = count - 2; segment >= 0; --segment) {
      Vector3 a = outPoints[segment];
      Vector3 b = outPoints[segment + 1];
      Vector3 delta = Vector3Subtract(b, a);
      float length = Vector3Length(delta);
      Vector3 midpoint = Vector3Scale(Vector3Add(a, b), 0.5f);
      if (length >= RIBBON_DEGENERATE_EPSILON && amplitude > 0.0f) {
        Vector3 axisA, axisB;
        RibbonMidpoint_Frame(Vector3Scale(delta, 1.0f / length), &axisA, &axisB);
        unsigned int key = resolved.seed ^ (unsigned int)(level * 0x9e3779b9u) ^
                           (unsigned int)(segment * 0x85ebca6bu);
        midpoint = Vector3Add(midpoint, Vector3Scale(axisA, amplitude * RibbonMidpoint_Signed(key)));
        midpoint = Vector3Add(midpoint, Vector3Scale(axisB, amplitude * 0.62f *
                                                      RibbonMidpoint_Signed(key ^ 0xc2b2ae35u)));
      }
      outPoints[segment * 2 + 2] = b;
      outPoints[segment * 2 + 1] = midpoint;
      outPoints[segment * 2] = a;
    }
    count = count * 2 - 1;
    amplitude *= resolved.amplitudeDecay;
  }
  return count;
}

// One geometry loop, two public entry points. `writeNormals` makes the strip
// carry its per-vertex SIDE vector (the across-width unit vector) in the
// normal attribute slot, where the trail deform vertex shader
// (trail_deform.vs) reinterprets it as the wave basis. Classic ribbons never
// write normals, so raylib's lit default shader (which would modulate
// brightness by dot(normal, lightDir)) is untouched for every existing
// consumer — only deform trails route through the Deformed entry point.
static void DrawRibbonStripInternal(const RibbonPoint *points, int count,
                                    Texture2D texture, Camera3D camera,
                                    RibbonMode mode, Vector3 fixedNormal,
                                    VFXContrastProfileId contrastProfile,
                                    VFXContrastLayer contrastLayer,
                                    bool writeNormals) {
  if (points == NULL || count < 2)
    return;

  Vector3 primaryNormal, fallbackNormal;
  ResolveFrameNormals(mode, fixedNormal, camera, &primaryNormal, &fallbackNormal);

  // Dải có thể bị nhìn từ "mặt sau" khi path cong gập - tắt backface
  // culling cho riêng draw call này (tắt/mở thẳng theo style đã có sẵn
  // trong DrawFireSkill, ví dụ rlDisableDepthMask/rlEnableDepthMask).
  // ENGINE_LANDMINES.md §1 — a raster-state change must be flushed on BOTH
  // sides or it does not apply to what is submitted next. Without this flush
  // the disable never took effect for this strip's own quads, and any strip
  // whose quads face AWAY from the camera was silently culled: a
  // RIBBON_CAMERA_FACING strip always faces the viewer so it looked fine, while
  // a RIBBON_FIXED_NORMAL ring lying flat on a plane rendered NOTHING AT ALL
  // (found by E5.2's rune circle, which drew nothing while its geometry, alpha
  // and side vectors all logged as correct).
  rlDrawRenderBatchActive();
  rlDisableBackfaceCulling();
  rlSetTexture(texture.id);
  rlBegin(RL_QUADS);

  Vector3 prevLeft = {0}, prevRight = {0}, prevCenter = {0};
  Color prevTint = WHITE;
  float prevV = 0.0f;
  Vector3 prevSide = {0};
  bool havePrevSide = false;
  Vector3 prevTangent = {0};
  bool havePrevTangent = false;

  for (int i = 0; i < count; i++) {
    // ── THE TANGENT CAN BE FABRICATED, AND THAT IS THE REAL PINCH ───────────
    //
    // ComputeTangent takes a CENTRAL difference, points[i+1] - points[i-1]. When
    // a simulated ribbon bunches — nodes crowding together, which cloth does —
    // those two points can land on top of each other, and the helper then returns
    // a hard-coded (1,0,0). That is not a short vector, it is a CONFIDENT WRONG
    // one: the degenerate-cross-product guard below sees a perfectly healthy
    // cross product and normalises it, so `side` jumps to whatever is
    // perpendicular to a fabricated axis. The band closes to a point and re-opens
    // rotated, always at the same place on a repeating path, which is what makes
    // it look like a deliberate twist.
    //
    // So the tangent is validated FIRST, and carried forward when it has no
    // information — the same parallel-transport argument as the side vector, one
    // level up. Guarding only the cross product was the previous fix and it could
    // not see this, because by then the damage is already an ordinary-looking
    // unit vector.
    Vector3 tangent;
    {
      Vector3 d;
      if (i == 0)              d = Vector3Subtract(points[1].position, points[0].position);
      else if (i == count - 1) d = Vector3Subtract(points[count - 1].position,
                                                   points[count - 2].position);
      else                     d = Vector3Subtract(points[i + 1].position,
                                                   points[i - 1].position);
      if (Vector3Length(d) < RIBBON_DEGENERATE_EPSILON && i > 0)
        d = Vector3Subtract(points[i].position, points[i - 1].position);   // one-sided
      if (Vector3Length(d) < RIBBON_DEGENERATE_EPSILON)
        tangent = havePrevTangent ? prevTangent
                                  : ComputeTangent(points, count, i);      // last resort
      else
        tangent = Vector3Normalize(d);
      prevTangent = tangent;
      havePrevTangent = true;
    }

    // ── THE PINCH, and why a sign flip cannot fix it ────────────────────────
    //
    // `side = cross(tangent, primaryNormal)` COLLAPSES wherever the tangent runs
    // parallel to primaryNormal — for a camera-facing strip, wherever the path
    // points straight at or away from the viewer. `ComputeSideVector` then falls
    // back to an unrelated reference vector, so `side` does not flip, it JUMPS to
    // a completely different direction, and the band closes to a point and
    // re-opens rotated. On a path that curves through the view direction this
    // happens once or twice per loop: the owner's 29/07 capture shows exactly one
    // such pinch with two wedges meeting at a point.
    //
    // The continuity check below only ever negated `side`, which cannot undo a
    // 90-degree jump — that was the previous fix and it was aimed at the wrong
    // failure.
    //
    // PARALLEL TRANSPORT is the standard answer: where the cross product has no
    // information, carry the PREVIOUS side vector forward, re-orthogonalised
    // against the new tangent. The strip then narrows through the degenerate
    // stretch — which is correct, it is being seen end-on — instead of tearing.
    Vector3 raw = Vector3CrossProduct(tangent, primaryNormal);
    Vector3 side;
    if (Vector3Length(raw) > RIBBON_SIDE_DEGENERATE && havePrevSide) {
      side = Vector3Normalize(raw);
    } else if (havePrevSide) {
      Vector3 t = Vector3Subtract(
          prevSide, Vector3Scale(tangent, Vector3DotProduct(prevSide, tangent)));
      side = (Vector3Length(t) > RIBBON_DEGENERATE_EPSILON)
                 ? Vector3Normalize(t)
                 : ComputeSideVector(tangent, primaryNormal, fallbackNormal);
    } else {
      side = ComputeSideVector(tangent, primaryNormal, fallbackNormal);
    }

    // Sign continuity on top of that — a well-conditioned cross product can still
    // come out negated between neighbours.
    if (havePrevSide && Vector3DotProduct(side, prevSide) < 0.0f)
      side = Vector3Negate(side);

    // Foreshortening guard (see RIBBON_MIN_PROJECTION). Camera-facing strips are
    // exempt: their side vector is already built perpendicular to the view, so
    // the factor is 1 by construction and this would be a no-op.
    float halfWidth = points[i].halfWidth;
    Color tint = VFXContrast_ApplyColor(points[i].tint, contrastProfile,
                                        contrastLayer);
    if (mode != RIBBON_CAMERA_FACING) {
      Vector3 toCam = Vector3Subtract(points[i].position, camera.position);
      float camLen = Vector3Length(toCam);
      if (camLen > RIBBON_DEGENERATE_EPSILON) {
        Vector3 viewDir = Vector3Scale(toCam, 1.0f / camLen);
        // `side` is unit, so the fraction of it that survives projection is
        // sin(angle to the view direction).
        float along = Vector3DotProduct(side, viewDir);
        float proj2 = 1.0f - along * along;
        float proj = (proj2 > 0.0f) ? sqrtf(proj2) : 0.0f;
        if (proj < RIBBON_MIN_PROJECTION) {
          Vector3 camSide = Vector3CrossProduct(tangent, viewDir);
          if (Vector3Length(camSide) > RIBBON_DEGENERATE_EPSILON) {
            camSide = Vector3Normalize(camSide);
            // Align it with the side we already have, or the blend would swing
            // the band through zero on its way to the other orientation.
            if (Vector3DotProduct(camSide, side) < 0.0f)
              camSide = Vector3Negate(camSide);
            // 0 at the threshold, 1 at true edge-on: continuous, so a band that
            // sweeps through the view direction never pops.
            float w = 1.0f - proj / RIBBON_MIN_PROJECTION;
            side = Vector3Normalize(Vector3Add(Vector3Scale(side, 1.0f - w),
                                               Vector3Scale(camSide, w)));
          }
        }
      }
    }

    // CONTINUITY IS RECORDED ON THE VECTOR ACTUALLY USED, not on the raw one.
    // It used to be stored before the foreshortening blend above, so the
    // anti-bowtie check compared each point's RAW side against the previous
    // point's RAW side while the geometry was built from the BLENDED ones —
    // which are free to point opposite ways. The strip then pinches to nothing
    // and crosses over itself: the owner's 29/07 capture shows exactly that
    // wedge in the middle of a trail. Anything that modifies `side` must happen
    // before this line.
    if (havePrevSide && Vector3DotProduct(side, prevSide) < 0.0f)
      side = Vector3Negate(side);
    prevSide = side;
    havePrevSide = true;

    Vector3 left =
        Vector3Add(points[i].position, Vector3Scale(side, halfWidth));
    Vector3 right = Vector3Subtract(points[i].position,
                                    Vector3Scale(side, halfWidth));
    Vector3 center = points[i].position;

    if (i > 0) {
      // Left Quad (U: 0.0 -> 0.5)
      if (writeNormals) rlNormal3f(prevSide.x, prevSide.y, prevSide.z);
      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(0.0f, prevV);
      rlVertex3f(prevLeft.x, prevLeft.y, prevLeft.z);

      if (writeNormals) rlNormal3f(prevSide.x, prevSide.y, prevSide.z);
      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(0.5f, prevV);
      rlVertex3f(prevCenter.x, prevCenter.y, prevCenter.z);

      if (writeNormals) rlNormal3f(side.x, side.y, side.z);
      rlColor4ub(tint.r, tint.g, tint.b, tint.a);
      rlTexCoord2f(0.5f, points[i].v);
      rlVertex3f(center.x, center.y, center.z);

      if (writeNormals) rlNormal3f(side.x, side.y, side.z);
      rlColor4ub(tint.r, tint.g, tint.b, tint.a);
      rlTexCoord2f(0.0f, points[i].v);
      rlVertex3f(left.x, left.y, left.z);

      // Right Quad (U: 0.5 -> 1.0)
      if (writeNormals) rlNormal3f(prevSide.x, prevSide.y, prevSide.z);
      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(0.5f, prevV);
      rlVertex3f(prevCenter.x, prevCenter.y, prevCenter.z);

      if (writeNormals) rlNormal3f(prevSide.x, prevSide.y, prevSide.z);
      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(1.0f, prevV);
      rlVertex3f(prevRight.x, prevRight.y, prevRight.z);

      if (writeNormals) rlNormal3f(side.x, side.y, side.z);
      rlColor4ub(tint.r, tint.g, tint.b, tint.a);
      rlTexCoord2f(1.0f, points[i].v);
      rlVertex3f(right.x, right.y, right.z);

      if (writeNormals) rlNormal3f(side.x, side.y, side.z);
      rlColor4ub(tint.r, tint.g, tint.b, tint.a);
      rlTexCoord2f(0.5f, points[i].v);
      rlVertex3f(center.x, center.y, center.z);
    }

    prevLeft = left;
    prevRight = right;
    prevCenter = center;
    prevTint = tint;
    prevV = points[i].v;
  }

  rlEnd();
  rlSetTexture(0); // module binds `texture`, must not leak it into whatever draws next
  rlDrawRenderBatchActive();   // flush before restoring, same reason as above
  rlEnableBackfaceCulling();
  rlDrawRenderBatchActive();
}

void DrawRibbonStripEx(const RibbonPoint *points, int count, Texture2D texture,
                       Camera3D camera, RibbonMode mode, Vector3 fixedNormal) {
  DrawRibbonStripInternal(points, count, texture, camera, mode, fixedNormal,
                          VFX_CONTRAST_NONE, VFX_CONTRAST_BODY, false);
}

void DrawRibbonStripProfiledEx(const RibbonPoint *points, int count,
                               Texture2D texture, Camera3D camera,
                               RibbonMode mode, Vector3 fixedNormal,
                               VFXContrastProfileId contrastProfile,
                               VFXContrastLayer contrastLayer) {
  DrawRibbonStripInternal(points, count, texture, camera, mode, fixedNormal,
                          contrastProfile, contrastLayer, false);
}

// Deform variant: additionally writes the per-vertex SIDE vector into the
// normal attribute slot, where trail_deform.vs reinterprets it as the wave
// basis. Only trails with a deform mode > 0 route through this — classic
// ribbons must keep the normal slot empty so lit default shaders are
// unaffected. See the comment on DrawRibbonStripInternal.
void DrawRibbonStripDeformedEx(const RibbonPoint *points, int count, Texture2D texture,
                               Camera3D camera, RibbonMode mode, Vector3 fixedNormal) {
  DrawRibbonStripInternal(points, count, texture, camera, mode, fixedNormal,
                          VFX_CONTRAST_NONE, VFX_CONTRAST_BODY, true);
}

void DrawRibbonStripDeformedProfiledEx(const RibbonPoint *points, int count,
                                       Texture2D texture, Camera3D camera,
                                       RibbonMode mode, Vector3 fixedNormal,
                                       VFXContrastProfileId contrastProfile,
                                       VFXContrastLayer contrastLayer) {
  DrawRibbonStripInternal(points, count, texture, camera, mode, fixedNormal,
                          contrastProfile, contrastLayer, true);
}

void Ribbon_ConstrainSegment(Vector3 *a, Vector3 *b, float restLen,
                             bool pinnedA, RibbonConstrainMode mode) {
  if (restLen <= 1e-6f) return;
  float dx = b->x - a->x, dy = b->y - a->y, dz = b->z - a->z;
  float dist2 = dx * dx + dy * dy + dz * dz;
  float rest2 = restLen * restLen;
  if (fabsf(dist2 - rest2) < rest2 * 1e-8f || dist2 < 1e-10f) return;
  if (mode == RIBBON_CONSTRAIN_MAX && dist2 < rest2) return;   // already inside
  if (mode == RIBBON_CONSTRAIN_MIN && dist2 > rest2) return;   // already outside

  float dist = sqrtf(dist2);
  float err = (dist - restLen) / dist;
  float cx = dx * err, cy = dy * err, cz = dz * err;
  if (pinnedA) {
    b->x -= cx; b->y -= cy; b->z -= cz;
  } else {
    cx *= 0.5f; cy *= 0.5f; cz *= 0.5f;
    a->x += cx; a->y += cy; a->z += cz;
    b->x -= cx; b->y -= cy; b->z -= cz;
  }
}

void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture,
                     Camera3D camera) {
  DrawRibbonStripEx(points, count, texture, camera, RIBBON_CAMERA_FACING,
                    (Vector3){0.0f, 1.0f, 0.0f});
}

bool DrawRibbonStripAppearanceEx(const RibbonPoint *points, int count,
                                 Texture2D texture, Camera3D camera,
                                 const VFXRibbonDrawConfig *config)
{
  if (points == NULL || count < 2 || config == NULL) return false;
  VFXResolvedAppearance resolved = VFXAppearance_Resolve(
      config->appearance, config->legacyAppearance);
  /* RibbonPoint stores straight RGBA bytes, so it cannot feed the
   * premultiplied blend law directly. Named FIRE still gets its alpha body;
   * its emission pass is converted to additive by BeginAppearance. */
  if (config->pass == VFX_RENDER_PASS_BODY &&
      resolved.surface == VFX_SURFACE_PREMULTIPLIED)
    resolved.surface = VFX_SURFACE_ALPHA;
  VFXRenderScope scope = VFXRender_BeginAppearance(
      config->pass, VFX_APPEARANCE_INHERIT, resolved,
      config->depthWrite, &resolved);
  if (!scope.active) return false;
  DrawRibbonStripProfiledEx(points, count, texture, camera,
                            config->mode, config->fixedNormal,
                            resolved.contrast,
                            VFXRender_ContrastLayer(config->pass));
  VFXRender_EndDraw(&scope);
  return true;
}

void Ribbon_ComputeArcLengthUV(RibbonPoint *points, int count) {
  if (points == NULL || count < 2)
    return;

  // Write cumulative distance straight into .v (the field we're about to
  // overwrite anyway) instead of a separate scratch buffer - avoids a
  // hardcoded max-count cap.
  points[0].v = 0.0f;
  for (int i = 1; i < count; i++)
    points[i].v = points[i - 1].v +
                  Vector3Distance(points[i - 1].position, points[i].position);

  float total = points[count - 1].v;
  if (total < RIBBON_DEGENERATE_EPSILON) {
    // Degenerate (all points coincide) - fall back to uniform index spacing
    // rather than divide by ~0.
    for (int i = 0; i < count; i++)
      points[i].v = (float)i / (float)(count - 1);
    return;
  }

  for (int i = 0; i < count; i++)
    points[i].v /= total;
}

// 1 + amp*sin(time*freq) — inlined rather than depending on
// core/composition/vc_motion.h's identical VC_Breathe: core/ must not
// depend on composition/ (layering would invert). Composition-layer callers
// (e.g. anything already using VC_Breathe directly) are unaffected.
static float RibbonBreathe(float time, float freq, float amp) {
  return 1.0f + amp * sinf(time * freq);
}

void DrawRibbonEnergyField(const Vector3 *points, int count, float width,
                           const float *widthEnvelope,
                           const RibbonEnergyFieldLayer *layers, int layerCount,
                           Texture2D texture, RibbonMode mode, Vector3 fixedNormal,
                           Camera3D camera, float time) {
  if (points == NULL || count < 2 || layers == NULL || layerCount < 1)
    return;
  if (count > RIBBON_ENERGY_FIELD_MAX_PTS)
    count = RIBBON_ENERGY_FIELD_MAX_PTS;
  if (layerCount > RIBBON_ENERGY_FIELD_MAX_LAYERS)
    layerCount = RIBBON_ENERGY_FIELD_MAX_LAYERS;

  static Vector3 s_axisA[RIBBON_ENERGY_FIELD_MAX_PTS];
  static Vector3 s_axisB[RIBBON_ENERGY_FIELD_MAX_PTS];
  static float   s_arcT[RIBBON_ENERGY_FIELD_MAX_PTS]; // normalized 0..1 arc length

  Ribbon_ComputeCrossFrame(points, count, mode, fixedNormal, camera, s_axisA, s_axisB);

  s_arcT[0] = 0.0f;
  for (int i = 1; i < count; i++)
    s_arcT[i] = s_arcT[i - 1] + Vector3Distance(points[i - 1], points[i]);
  float totalLen = s_arcT[count - 1];
  if (totalLen < RIBBON_DEGENERATE_EPSILON) totalLen = RIBBON_DEGENERATE_EPSILON;
  for (int i = 0; i < count; i++)
    s_arcT[i] /= totalLen;

  for (int L = 0; L < layerCount; L++) {
    const RibbonEnergyFieldLayer *layer = &layers[L];
    float breathe = (layer->breatheAmp != 0.0f)
                   ? RibbonBreathe(time, layer->breatheFreq, layer->breatheAmp)
                   : 1.0f;
    float baseHalfW = width * layer->widthRatio * breathe;
    float scroll    = time * layer->scrollSpeed;
    float vTop       = layer->vFlip ? 1.0f : 0.0f;
    float vBot       = layer->vFlip ? 0.0f : 1.0f;

    rlSetTexture(layer->useTexture ? texture.id : 0);
    rlBegin(RL_QUADS);
    Color layerColor = VFXContrast_ApplyColor(layer->color,
                                               layer->contrastProfile,
                                               layer->contrastLayer);
    rlColor4ub(layerColor.r, layerColor.g, layerColor.b, layerColor.a);

    // 2 passes = the "+" cross-section: axisA plane, then axisB plane.
    for (int pass = 0; pass < 2; pass++) {
      for (int i = 1; i < count; i++) {
        Vector3 axis0 = (pass == 0) ? s_axisA[i - 1] : s_axisB[i - 1];
        Vector3 axis1 = (pass == 0) ? s_axisA[i]     : s_axisB[i];

        float w0 = baseHalfW * (widthEnvelope ? widthEnvelope[i - 1] : 1.0f);
        float w1 = baseHalfW * (widthEnvelope ? widthEnvelope[i]     : 1.0f);

        Vector3 l0 = Vector3Add(points[i - 1], Vector3Scale(axis0, w0));
        Vector3 r0 = Vector3Subtract(points[i - 1], Vector3Scale(axis0, w0));
        Vector3 l1 = Vector3Add(points[i], Vector3Scale(axis1, w1));
        Vector3 r1 = Vector3Subtract(points[i], Vector3Scale(axis1, w1));

        float v0 = s_arcT[i - 1] * layer->uvTiling + scroll;
        float v1 = s_arcT[i] * layer->uvTiling + scroll;

        rlTexCoord2f(v0, vTop); rlVertex3f(l0.x, l0.y, l0.z);
        rlTexCoord2f(v1, vTop); rlVertex3f(l1.x, l1.y, l1.z);
        rlTexCoord2f(v1, vBot); rlVertex3f(r1.x, r1.y, r1.z);
        rlTexCoord2f(v0, vBot); rlVertex3f(r0.x, r0.y, r0.z);
      }
    }
    rlEnd();
  }
  rlSetTexture(0);
}
