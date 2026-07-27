#include "core/ribbon_strip.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdbool.h>
#include <stddef.h>

// Ngưỡng coi 1 vector là "gần như 0" (cross product suy biến) - dưới
// ngưỡng này phải fallback sang vector tham chiếu khác để tránh chia 0 /
// NaN khi normalize.
#define RIBBON_DEGENERATE_EPSILON 0.0001f

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

void DrawRibbonStripEx(const RibbonPoint *points, int count, Texture2D texture,
                       Camera3D camera, RibbonMode mode, Vector3 fixedNormal) {
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

  for (int i = 0; i < count; i++) {
    Vector3 tangent = ComputeTangent(points, count, i);
    Vector3 side = ComputeSideVector(tangent, primaryNormal, fallbackNormal);

    // Giữ liên tục hướng "side" giữa các điểm liền kề trong CÙNG 1 dải,
    // tránh hiện tượng "bowtie" (dải bị xoắn) do cross product đổi dấu khi
    // tangent đi qua vùng gần song song với hướng nhìn camera.
    if (havePrevSide && Vector3DotProduct(side, prevSide) < 0.0f)
      side = Vector3Negate(side);
    prevSide = side;
    havePrevSide = true;

    Vector3 left =
        Vector3Add(points[i].position, Vector3Scale(side, points[i].halfWidth));
    Vector3 right = Vector3Subtract(points[i].position,
                                    Vector3Scale(side, points[i].halfWidth));
    Vector3 center = points[i].position;

    if (i > 0) {
      // Left Quad (U: 0.0 -> 0.5)
      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(0.0f, prevV);
      rlVertex3f(prevLeft.x, prevLeft.y, prevLeft.z);

      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(0.5f, prevV);
      rlVertex3f(prevCenter.x, prevCenter.y, prevCenter.z);

      rlColor4ub(points[i].tint.r, points[i].tint.g, points[i].tint.b,
                 points[i].tint.a);
      rlTexCoord2f(0.5f, points[i].v);
      rlVertex3f(center.x, center.y, center.z);

      rlColor4ub(points[i].tint.r, points[i].tint.g, points[i].tint.b,
                 points[i].tint.a);
      rlTexCoord2f(0.0f, points[i].v);
      rlVertex3f(left.x, left.y, left.z);

      // Right Quad (U: 0.5 -> 1.0)
      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(0.5f, prevV);
      rlVertex3f(prevCenter.x, prevCenter.y, prevCenter.z);

      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(1.0f, prevV);
      rlVertex3f(prevRight.x, prevRight.y, prevRight.z);

      rlColor4ub(points[i].tint.r, points[i].tint.g, points[i].tint.b,
                 points[i].tint.a);
      rlTexCoord2f(1.0f, points[i].v);
      rlVertex3f(right.x, right.y, right.z);

      rlColor4ub(points[i].tint.r, points[i].tint.g, points[i].tint.b,
                 points[i].tint.a);
      rlTexCoord2f(0.5f, points[i].v);
      rlVertex3f(center.x, center.y, center.z);
    }

    prevLeft = left;
    prevRight = right;
    prevCenter = center;
    prevTint = points[i].tint;
    prevV = points[i].v;
  }

  rlEnd();
  rlSetTexture(0); // module binds `texture`, must not leak it into whatever draws next
  rlDrawRenderBatchActive();   // flush before restoring, same reason as above
  rlEnableBackfaceCulling();
  rlDrawRenderBatchActive();
}

void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture,
                     Camera3D camera) {
  DrawRibbonStripEx(points, count, texture, camera, RIBBON_CAMERA_FACING,
                    (Vector3){0.0f, 1.0f, 0.0f});
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
    rlColor4ub(layer->color.r, layer->color.g, layer->color.b, layer->color.a);

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