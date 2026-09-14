#ifndef PATH_SPLINE_H
#define PATH_SPLINE_H

#include "raylib.h"

// Tính điểm trên đường cong Bezier bậc 3
Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);

// Tính vector hướng (Tangent) tại 1 điểm trên đường cong
Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target,
                         float t);

// Tính điểm trên đường cong Centripetal Catmull-Rom Spline (alpha = 0.5)
// p1 đến p2 là đoạn cần nội suy; p0 và p3 là 2 điểm lân cận định hướng tiếp tuyến.
// t thuộc [0.0, 1.0]. Khử triệt để lỗi tự giao cắt (loops) và văng quỹ đạo (overshoot).
Vector3 CatmullRom_Centripetal(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);

// Rải điểm đều đặn dọc theo một chuỗi các Vector3 (Dùng để dựng Mesh/Ribbon)
// Trả về số lượng điểm thực tế đã rải được
int SamplePath(const Vector3 *path, int pathCount, float spacing,
               Vector3 *outSegments, int maxSegments);

#endif // PATH_SPLINE_H