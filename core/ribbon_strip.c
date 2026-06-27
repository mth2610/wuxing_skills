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
// ribbon. Ưu tiên vuông góc với hướng nhìn camera (cho silhouette luôn đúng
// hướng camera, đúng kiểu Trail Renderer). Có 2 lớp fallback khi suy biến:
// 1) tangent gần song song hướng nhìn camera -> dùng camera.up
// 2) tangent gần song song luôn cả camera.up (cực hiếm) -> dùng vector
//    vuông góc cố định trên mặt phẳng ngang (cùng kiểu trick đã dùng trong
//    CastFireSkill: { -tangent.z, 0, tangent.x }).
static Vector3 ComputeSideVector(Vector3 tangent, Vector3 cameraForward,
                                 Vector3 cameraUp) {
  Vector3 side = Vector3CrossProduct(tangent, cameraForward);
  float len = Vector3Length(side);
  if (len > RIBBON_DEGENERATE_EPSILON)
    return Vector3Scale(side, 1.0f / len);

  side = Vector3CrossProduct(tangent, cameraUp);
  len = Vector3Length(side);
  if (len > RIBBON_DEGENERATE_EPSILON)
    return Vector3Scale(side, 1.0f / len);

  side = (Vector3){-tangent.z, 0.0f, tangent.x};
  len = Vector3Length(side);
  return (len > RIBBON_DEGENERATE_EPSILON) ? Vector3Scale(side, 1.0f / len)
                                           : (Vector3){1.0f, 0.0f, 0.0f};
}

void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture,
                     Camera3D camera) {
  if (points == NULL || count < 2)
    return;

  Vector3 cameraForward =
      Vector3Normalize(Vector3Subtract(camera.target, camera.position));

  // Dải có thể bị nhìn từ "mặt sau" khi path cong gập - tắt backface
  // culling cho riêng draw call này (tắt/mở thẳng theo style đã có sẵn
  // trong DrawFireSkill, ví dụ rlDisableDepthMask/rlEnableDepthMask).
  rlDisableBackfaceCulling();
  rlSetTexture(texture.id);
  rlBegin(RL_QUADS);

  Vector3 prevLeft = {0}, prevRight = {0};
  Color prevTint = WHITE;
  float prevV = 0.0f;
  Vector3 prevSide = {0};
  bool havePrevSide = false;

  for (int i = 0; i < count; i++) {
    Vector3 tangent = ComputeTangent(points, count, i);
    Vector3 side = ComputeSideVector(tangent, cameraForward, camera.up);

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

    if (i > 0) {
      // Quad nối mặt cắt trước (prevLeft/prevRight) với mặt cắt hiện tại
      // (left/right) - thứ tự near-left -> near-right -> far-right ->
      // far-left tạo 1 vòng quad hợp lệ, không tự cắt nhau.
      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(0.0f, prevV);
      rlVertex3f(prevLeft.x, prevLeft.y, prevLeft.z);

      rlColor4ub(prevTint.r, prevTint.g, prevTint.b, prevTint.a);
      rlTexCoord2f(1.0f, prevV);
      rlVertex3f(prevRight.x, prevRight.y, prevRight.z);

      rlColor4ub(points[i].tint.r, points[i].tint.g, points[i].tint.b,
                 points[i].tint.a);
      rlTexCoord2f(1.0f, points[i].v);
      rlVertex3f(right.x, right.y, right.z);

      rlColor4ub(points[i].tint.r, points[i].tint.g, points[i].tint.b,
                 points[i].tint.a);
      rlTexCoord2f(0.0f, points[i].v);
      rlVertex3f(left.x, left.y, left.z);
    }

    prevLeft = left;
    prevRight = right;
    prevTint = points[i].tint;
    prevV = points[i].v;
  }

  rlEnd();
  rlEnableBackfaceCulling();
}