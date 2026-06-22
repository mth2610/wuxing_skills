#ifndef PATH_SPLINE_H
#define PATH_SPLINE_H

#include "raylib.h"

// Tính điểm trên đường cong Bezier bậc 3
Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);

// Tính vector hướng (Tangent) tại 1 điểm trên đường cong
Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target,
                         float t);

// Rải điểm đều đặn dọc theo một chuỗi các Vector3 (Dùng để dựng Mesh/Ribbon)
// Trả về số lượng điểm thực tế đã rải được
int SamplePath(const Vector3 *path, int pathCount, float spacing,
               Vector3 *outSegments, int maxSegments);

#endif // PATH_SPLINE_H