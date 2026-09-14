#include "core/path_spline.h"
#include "raymath.h"

Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                       float t) {
  float u = 1.0f - t;
  float tt = t * t;
  float uu = u * u;
  float uuu = uu * u;
  float ttt = tt * t;
  Vector3 p = Vector3Scale(p0, uuu);
  p = Vector3Add(p, Vector3Scale(p1, 3.0f * uu * t));
  p = Vector3Add(p, Vector3Scale(p2, 3.0f * u * tt));
  p = Vector3Add(p, Vector3Scale(p3, ttt));
  return p;
}

Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target,
                         float t) {
  float u = 1.0f - t;
  Vector3 tangent = Vector3Add(
      Vector3Add(Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u),
                 Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t)),
      Vector3Scale(Vector3Subtract(target, p2), 3.0f * t * t));

  if (tangent.x == 0 && tangent.y == 0 && tangent.z == 0)
    return (Vector3){1.0f, 0.0f, 0.0f};
  return Vector3Normalize(tangent);
}

static inline float KnotInterval(Vector3 a, Vector3 b) {
  float distSq = Vector3DistanceSqr(a, b);
  return (distSq > 1e-8f) ? sqrtf(sqrtf(distSq)) : 1e-4f;
}

static inline Vector3 V3LerpParam(Vector3 v1, Vector3 v2, float t1, float t2, float t) {
  float denom = t2 - t1;
  if (fabsf(denom) < 1e-5f) return v1;
  float factor = (t - t1) / denom;
  return (Vector3){
      v1.x + factor * (v2.x - v1.x),
      v1.y + factor * (v2.y - v1.y),
      v1.z + factor * (v2.z - v1.z)};
}

Vector3 CatmullRom_Centripetal(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
  if (t <= 0.0f) return p1;
  if (t >= 1.0f) return p2;

  float t0 = 0.0f;
  float t1 = t0 + KnotInterval(p0, p1);
  float t2 = t1 + KnotInterval(p1, p2);
  float t3 = t2 + KnotInterval(p2, p3);

  float evalT = t1 + t * (t2 - t1);

  Vector3 a1 = V3LerpParam(p0, p1, t0, t1, evalT);
  Vector3 a2 = V3LerpParam(p1, p2, t1, t2, evalT);
  Vector3 a3 = V3LerpParam(p2, p3, t2, t3, evalT);

  Vector3 b1 = V3LerpParam(a1, a2, t0, t2, evalT);
  Vector3 b2 = V3LerpParam(a2, a3, t1, t3, evalT);

  return V3LerpParam(b1, b2, t1, t2, evalT);
}

int SamplePath(const Vector3 *path, int pathCount, float spacing,
               Vector3 *outSegments, int maxSegments) {
  if (pathCount == 0 || maxSegments <= 0)
    return 0;

  outSegments[0] = path[0];
  int segmentIndex = 1;
  float targetDist = spacing;
  float accumulatedDist = 0.0f;

  for (int i = 0; i < pathCount - 1; i++) {
    float d = Vector3Distance(path[i], path[i + 1]);
    while (accumulatedDist + d >= targetDist) {
      float t = (d > 0.0f) ? ((targetDist - accumulatedDist) / d) : 0.0f;
      outSegments[segmentIndex] = Vector3Lerp(path[i], path[i + 1], t);
      segmentIndex++;
      if (segmentIndex >= maxSegments)
        return segmentIndex;
      targetDist += spacing;
    }
    accumulatedDist += d;
  }
  return segmentIndex;
}