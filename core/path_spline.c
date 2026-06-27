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