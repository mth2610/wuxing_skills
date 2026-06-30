#include "core/procedural_mesh_utils.h"
#include "core/path_spline.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// rlgl immediate-mode batches have a finite vertex capacity. On mobile GPU
// drivers (Adreno/Mali) overflowing mid-primitive can corrupt the batch or
// silently drop geometry. Always check before starting a new primitive run.
// vertsNeeded = total verts you are about to push before the next rlEnd().

void DrawCoreSphere(Vector3 center, float radius, int rings, int slices,
                    Color color) {
  if (rings < 3)
    rings = 3;
  if (slices < 3)
    slices = 3;

  rlCheckRenderBatchLimit(rings * slices * 4);

  // Precompute per-slice trig once; reused for every ring instead of
  // recomputing sinf/cosf(phi) `rings` times each.
  float cosPhi[slices + 1];
  float sinPhi[slices + 1];
  for (int j = 0; j <= slices; j++) {
    float phi = (float)j * (2.0f * PI) / slices;
    cosPhi[j] = cosf(phi);
    sinPhi[j] = sinf(phi);
  }

  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);

  float prevTheta = 0.0f;
  float prevSt = sinf(prevTheta), prevCt = cosf(prevTheta);

  for (int i = 0; i < rings; i++) {
    float theta2 = (float)(i + 1) * PI / rings;
    float st1 = prevSt, ct1 = prevCt;
    float st2 = sinf(theta2), ct2 = cosf(theta2);
    prevSt = st2;
    prevCt = ct2;

    float vt1 = (float)i / rings;
    float vt2 = (float)(i + 1) / rings;

    for (int j = 0; j < slices; j++) {
      float cp1 = cosPhi[j], sp1 = sinPhi[j];
      float cp2 = cosPhi[j + 1], sp2 = sinPhi[j + 1];

      float u1 = (float)j / slices;
      float u2 = (float)(j + 1) / slices;

      Vector3 n1 = {st1 * cp1, ct1, st1 * sp1};
      Vector3 n2 = {st1 * cp2, ct1, st1 * sp2};
      Vector3 n3 = {st2 * cp2, ct2, st2 * sp2};
      Vector3 n4 = {st2 * cp1, ct2, st2 * sp1};

      rlNormal3f(n1.x, n1.y, n1.z);
      rlTexCoord2f(u1, vt1);
      rlVertex3f(center.x + n1.x * radius, center.y + n1.y * radius,
                 center.z + n1.z * radius);

      rlNormal3f(n2.x, n2.y, n2.z);
      rlTexCoord2f(u2, vt1);
      rlVertex3f(center.x + n2.x * radius, center.y + n2.y * radius,
                 center.z + n2.z * radius);

      rlNormal3f(n3.x, n3.y, n3.z);
      rlTexCoord2f(u2, vt2);
      rlVertex3f(center.x + n3.x * radius, center.y + n3.y * radius,
                 center.z + n3.z * radius);

      rlNormal3f(n4.x, n4.y, n4.z);
      rlTexCoord2f(u1, vt2);
      rlVertex3f(center.x + n4.x * radius, center.y + n4.y * radius,
                 center.z + n4.z * radius);
    }
  }
  rlEnd();
}

void DrawCoreCylinder(Vector3 bottom, Vector3 top, float radiusBottom,
                      float radiusTop, int slices, Color color) {
  if (slices < 3)
    slices = 3;

  Vector3 axis = Vector3Subtract(top, bottom);
  float height = Vector3Length(axis);
  if (height < 0.0001f)
    return;

  Vector3 dir = Vector3Normalize(axis);

  // Find orthonormal basis perpendicular to dir
  Vector3 u;
  if (fabsf(dir.x) > 0.9f) {
    u = (Vector3){0.0f, 1.0f, 0.0f};
  } else {
    u = (Vector3){1.0f, 0.0f, 0.0f};
  }
  Vector3 v = Vector3Normalize(Vector3CrossProduct(dir, u));
  u = Vector3Normalize(Vector3CrossProduct(v, dir));

  // For a true cone/frustum (radiusBottom != radiusTop) the side wall is
  // slanted, so the geometric normal is NOT the pure radial direction
  // (ringDir) -- that was the bug. The correct normal is the radial
  // direction tilted by the slope of the slant, exactly like DrawCoreCone
  // already does with its (nxz, ny) construction. We precompute that
  // tilt once here (it's the same for every slice since the slope is
  // constant along the wall).
  float slopeLen =
      sqrtf((radiusBottom - radiusTop) * (radiusBottom - radiusTop) +
            height * height);
  float nRadial = (slopeLen > 0.0001f) ? (height / slopeLen) : 1.0f;
  float nAxial =
      (slopeLen > 0.0001f) ? ((radiusBottom - radiusTop) / slopeLen) : 0.0f;

  // Precompute per-slice trig once instead of recomputing each loop pass
  // (body + bottom cap + top cap each independently called cosf/sinf
  // on the same phi1/phi2 before).
  float cosPhi[slices + 1];
  float sinPhi[slices + 1];
  for (int j = 0; j <= slices; j++) {
    float phi = (float)j * (2.0f * PI) / slices;
    cosPhi[j] = cosf(phi);
    sinPhi[j] = sinf(phi);
  }

  // Draw cylinder/cone body
  rlCheckRenderBatchLimit(slices * 4);
  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);
  for (int j = 0; j < slices; j++) {
    float c1 = cosPhi[j], s1 = sinPhi[j];
    float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];

    Vector3 ringDir1 = Vector3Add(Vector3Scale(u, c1), Vector3Scale(v, s1));
    Vector3 ringDir2 = Vector3Add(Vector3Scale(u, c2), Vector3Scale(v, s2));

    Vector3 p1 = Vector3Add(bottom, Vector3Scale(ringDir1, radiusBottom));
    Vector3 p2 = Vector3Add(bottom, Vector3Scale(ringDir2, radiusBottom));
    Vector3 p3 = Vector3Add(top, Vector3Scale(ringDir2, radiusTop));
    Vector3 p4 = Vector3Add(top, Vector3Scale(ringDir1, radiusTop));

    // Correct slanted normal: radial component tilted toward/away
    // from the axis by the wall's slope. Falls back to pure ringDir
    // when radiusBottom == radiusTop (nRadial=1, nAxial=0), matching
    // the original (correct) cylinder behavior exactly.
    Vector3 n1 =
        Vector3Add(Vector3Scale(ringDir1, nRadial), Vector3Scale(dir, nAxial));
    Vector3 n2 =
        Vector3Add(Vector3Scale(ringDir2, nRadial), Vector3Scale(dir, nAxial));

    rlNormal3f(n1.x, n1.y, n1.z);
    rlTexCoord2f((float)j / slices, 0.0f);
    rlVertex3f(p1.x, p1.y, p1.z);

    rlNormal3f(n2.x, n2.y, n2.z);
    rlTexCoord2f((float)(j + 1) / slices, 0.0f);
    rlVertex3f(p2.x, p2.y, p2.z);

    rlNormal3f(n2.x, n2.y, n2.z);
    rlTexCoord2f((float)(j + 1) / slices, 1.0f);
    rlVertex3f(p3.x, p3.y, p3.z);

    rlNormal3f(n1.x, n1.y, n1.z);
    rlTexCoord2f((float)j / slices, 1.0f);
    rlVertex3f(p4.x, p4.y, p4.z);
  }
  rlEnd();

  // Bottom cap
  if (radiusBottom > 0.0f) {
    rlCheckRenderBatchLimit(slices * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(-dir.x, -dir.y, -dir.z);
    for (int j = 0; j < slices; j++) {
      float c1 = cosPhi[j], s1 = sinPhi[j];
      float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];

      Vector3 ringDir1 = Vector3Add(Vector3Scale(u, c1), Vector3Scale(v, s1));
      Vector3 ringDir2 = Vector3Add(Vector3Scale(u, c2), Vector3Scale(v, s2));

      Vector3 p1 = Vector3Add(bottom, Vector3Scale(ringDir1, radiusBottom));
      Vector3 p2 = Vector3Add(bottom, Vector3Scale(ringDir2, radiusBottom));

      rlVertex3f(bottom.x, bottom.y, bottom.z);
      rlVertex3f(p2.x, p2.y, p2.z);
      rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
  }

  // Top cap
  if (radiusTop > 0.0f) {
    rlCheckRenderBatchLimit(slices * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(dir.x, dir.y, dir.z);
    for (int j = 0; j < slices; j++) {
      float c1 = cosPhi[j], s1 = sinPhi[j];
      float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];

      Vector3 ringDir1 = Vector3Add(Vector3Scale(u, c1), Vector3Scale(v, s1));
      Vector3 ringDir2 = Vector3Add(Vector3Scale(u, c2), Vector3Scale(v, s2));

      Vector3 p1 = Vector3Add(top, Vector3Scale(ringDir1, radiusTop));
      Vector3 p2 = Vector3Add(top, Vector3Scale(ringDir2, radiusTop));

      rlVertex3f(top.x, top.y, top.z);
      rlVertex3f(p1.x, p1.y, p1.z);
      rlVertex3f(p2.x, p2.y, p2.z);
    }
    rlEnd();
  }
}

void DrawCoreCone(Vector3 bottom, float radius, float height, int slices,
                  Color color) {
  if (slices < 3)
    slices = 3;
  Vector3 top = {bottom.x, bottom.y + height, bottom.z};

  float len = sqrtf(radius * radius + height * height);
  float ny = (len > 0.0001f) ? (radius / len) : 0.0f;
  float nxz = (len > 0.0001f) ? (height / len) : 1.0f;

  // Precompute per-slice trig once (was computed separately by the cap
  // loop and the sides loop before).
  float cosPhi[slices + 1];
  float sinPhi[slices + 1];
  for (int j = 0; j <= slices; j++) {
    float phi = (float)j * (2.0f * PI) / slices;
    cosPhi[j] = cosf(phi);
    sinPhi[j] = sinf(phi);
  }

  // Base cap
  if (radius > 0.0f) {
    rlCheckRenderBatchLimit(slices * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(0.0f, -1.0f, 0.0f);
    for (int j = 0; j < slices; j++) {
      float c1 = cosPhi[j], s1 = sinPhi[j];
      float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];

      Vector3 p1 = {bottom.x + c1 * radius, bottom.y, bottom.z + s1 * radius};
      Vector3 p2 = {bottom.x + c2 * radius, bottom.y, bottom.z + s2 * radius};

      rlVertex3f(bottom.x, bottom.y, bottom.z);
      rlVertex3f(p2.x, p2.y, p2.z);
      rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
  }

  // Cone sides
  rlCheckRenderBatchLimit(slices * 3);
  rlBegin(RL_TRIANGLES);
  rlColor4ub(color.r, color.g, color.b, color.a);
  for (int j = 0; j < slices; j++) {
    float phi1 = (float)j * (2.0f * PI) / slices;
    float phi2 =
        (j + 1 == slices) ? 0.0f : (float)(j + 1) * (2.0f * PI) / slices;
    float c1 = cosPhi[j], s1 = sinPhi[j];
    float c2 = cosPhi[j + 1], s2 = sinPhi[j + 1];

    Vector3 p1 = {bottom.x + c1 * radius, bottom.y, bottom.z + s1 * radius};
    Vector3 p2 = {bottom.x + c2 * radius, bottom.y, bottom.z + s2 * radius};

    // Average normal for lighting (kept from original; note this is an
    // approximation since a flat triangle has one true face normal,
    // but per-vertex averaged normals give smoother shading across
    // the cone's circumference, which is the original intent).
    float avgCos = cosf((phi1 + phi2) * 0.5f);
    float avgSin = sinf((phi1 + phi2) * 0.5f);
    Vector3 n = {avgCos * nxz, ny, avgSin * nxz};

    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(top.x, top.y, top.z);
    rlVertex3f(p1.x, p1.y, p1.z);
    rlVertex3f(p2.x, p2.y, p2.z);
  }
  rlEnd();
}

void DrawCorePlaneRect(Vector3 center, Vector2 size, Color color) {
  float hw = size.x * 0.5f;
  float hl = size.y * 0.5f;

  rlCheckRenderBatchLimit(4);
  rlBegin(RL_QUADS);
  rlNormal3f(0.0f, 1.0f, 0.0f);
  rlColor4ub(color.r, color.g, color.b, color.a);
  rlVertex3f(center.x - hw, center.y, center.z - hl);
  rlVertex3f(center.x - hw, center.y, center.z + hl);
  rlVertex3f(center.x + hw, center.y, center.z + hl);
  rlVertex3f(center.x + hw, center.y, center.z - hl);
  rlEnd();
}

void DrawCorePlanePolygon(Vector3 center, float radius, int sides,
                          Color color) {
  if (sides < 3)
    sides = 3;

  rlCheckRenderBatchLimit(sides * 3);
  rlBegin(RL_TRIANGLES);
  rlColor4ub(color.r, color.g, color.b, color.a);
  rlNormal3f(0.0f, 1.0f, 0.0f);

  float prevCos = 1.0f, prevSin = 0.0f; // phi = 0
  for (int i = 0; i < sides; i++) {
    float phi2 = (i + 1 == sides) ? 0.0f : (float)(i + 1) * (2.0f * PI) / sides;
    float c1 = prevCos, s1 = prevSin;
    float c2 = cosf(phi2), s2 = sinf(phi2);
    prevCos = c2;
    prevSin = s2;

    Vector3 p1 = {center.x + c1 * radius, center.y, center.z + s1 * radius};
    Vector3 p2 = {center.x + c2 * radius, center.y, center.z + s2 * radius};

    rlVertex3f(center.x, center.y, center.z);
    rlVertex3f(p2.x, p2.y, p2.z);
    rlVertex3f(p1.x, p1.y, p1.z);
  }
  rlEnd();
}

void DrawCoreCube(Vector3 position, float width, float height, float length,
                  Color color) {
  float x = position.x;
  float y = position.y;
  float z = position.z;
  float hw = width * 0.5f, hh = height * 0.5f, hl = length * 0.5f;

  rlCheckRenderBatchLimit(24);
  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);

  // Front Face
  rlNormal3f(0.0f, 0.0f, 1.0f);
  rlVertex3f(x - hw, y - hh, z + hl);
  rlVertex3f(x + hw, y - hh, z + hl);
  rlVertex3f(x + hw, y + hh, z + hl);
  rlVertex3f(x - hw, y + hh, z + hl);

  // Back Face
  rlNormal3f(0.0f, 0.0f, -1.0f);
  rlVertex3f(x - hw, y - hh, z - hl);
  rlVertex3f(x - hw, y + hh, z - hl);
  rlVertex3f(x + hw, y + hh, z - hl);
  rlVertex3f(x + hw, y - hh, z - hl);

  // Top Face
  rlNormal3f(0.0f, 1.0f, 0.0f);
  rlVertex3f(x - hw, y + hh, z - hl);
  rlVertex3f(x - hw, y + hh, z + hl);
  rlVertex3f(x + hw, y + hh, z + hl);
  rlVertex3f(x + hw, y + hh, z - hl);

  // Bottom Face
  rlNormal3f(0.0f, -1.0f, 0.0f);
  rlVertex3f(x - hw, y - hh, z - hl);
  rlVertex3f(x + hw, y - hh, z - hl);
  rlVertex3f(x + hw, y - hh, z + hl);
  rlVertex3f(x - hw, y - hh, z + hl);

  // Right face
  rlNormal3f(1.0f, 0.0f, 0.0f);
  rlVertex3f(x + hw, y - hh, z - hl);
  rlVertex3f(x + hw, y + hh, z - hl);
  rlVertex3f(x + hw, y + hh, z + hl);
  rlVertex3f(x + hw, y - hh, z + hl);

  // Left Face
  rlNormal3f(-1.0f, 0.0f, 0.0f);
  rlVertex3f(x - hw, y - hh, z - hl);
  rlVertex3f(x - hw, y - hh, z + hl);
  rlVertex3f(x - hw, y + hh, z + hl);
  rlVertex3f(x - hw, y + hh, z - hl);
  rlEnd();
}

void DrawCoreTorus(Vector3 center, float innerRadius, float outerRadius,
                   int sides, int rings, Color color) {
  if (sides < 3)
    sides = 3;
  if (rings < 3)
    rings = 3;

  // r and R are invariant across the entire mesh -- hoisted out of the
  // nested loop where they were previously recomputed rings*sides times.
  float r = (outerRadius - innerRadius) * 0.5f;
  float R = innerRadius + r;

  rlCheckRenderBatchLimit(rings * sides * 4);

  // Precompute per-ring (theta) and per-side (phi) trig once each,
  // instead of recomputing theta1/theta2 and phi1/phi2 from scratch on
  // every (i, j) pair.
  float cosTheta[rings + 1];
  float sinTheta[rings + 1];
  for (int i = 0; i <= rings; i++) {
    float theta = (float)i * (2.0f * PI) / rings;
    cosTheta[i] = cosf(theta);
    sinTheta[i] = sinf(theta);
  }

  float cosPhi[sides + 1];
  float sinPhi[sides + 1];
  for (int j = 0; j <= sides; j++) {
    float phi = (float)j * (2.0f * PI) / sides;
    cosPhi[j] = cosf(phi);
    sinPhi[j] = sinf(phi);
  }

  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);
  for (int i = 0; i < rings; i++) {
    float cosTheta1 = cosTheta[i], sinTheta1 = sinTheta[i];
    float cosTheta2 = cosTheta[i + 1], sinTheta2 = sinTheta[i + 1];

    for (int j = 0; j < sides; j++) {
      float cosPhi1 = cosPhi[j], sinPhi1 = sinPhi[j];
      float cosPhi2 = cosPhi[j + 1], sinPhi2 = sinPhi[j + 1];

      Vector3 p1 = {center.x + (R + r * cosPhi1) * cosTheta1,
                    center.y + r * sinPhi1,
                    center.z + (R + r * cosPhi1) * sinTheta1};
      Vector3 p2 = {center.x + (R + r * cosPhi2) * cosTheta1,
                    center.y + r * sinPhi2,
                    center.z + (R + r * cosPhi2) * sinTheta1};
      Vector3 p3 = {center.x + (R + r * cosPhi2) * cosTheta2,
                    center.y + r * sinPhi2,
                    center.z + (R + r * cosPhi2) * sinTheta2};
      Vector3 p4 = {center.x + (R + r * cosPhi1) * cosTheta2,
                    center.y + r * sinPhi1,
                    center.z + (R + r * cosPhi1) * sinTheta2};

      Vector3 n1 = {cosPhi1 * cosTheta1, sinPhi1, cosPhi1 * sinTheta1};
      Vector3 n2 = {cosPhi2 * cosTheta1, sinPhi2, cosPhi2 * sinTheta1};
      Vector3 n3 = {cosPhi2 * cosTheta2, sinPhi2, cosPhi2 * sinTheta2};
      Vector3 n4 = {cosPhi1 * cosTheta2, sinPhi1, cosPhi1 * sinTheta2};

      rlNormal3f(n1.x, n1.y, n1.z);
      rlVertex3f(p1.x, p1.y, p1.z);
      rlNormal3f(n4.x, n4.y, n4.z);
      rlVertex3f(p4.x, p4.y, p4.z);
      rlNormal3f(n3.x, n3.y, n3.z);
      rlVertex3f(p3.x, p3.y, p3.z);
      rlNormal3f(n2.x, n2.y, n2.z);
      rlVertex3f(p2.x, p2.y, p2.z);
    }
  }
  rlEnd();
}

void DrawCorePrism(Vector3 bottom, Vector3 top, float radius, int sides,
                   Color color) {
  DrawCoreCylinder(bottom, top, radius, radius, sides, color);
}

Vector3 ProceduralMesh_BezierPoint(Vector3 p0, Vector3 p1, Vector3 p2,
                                   Vector3 p3, float t) {
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

Vector3 ProceduralMesh_BezierTangent(Vector3 p0, Vector3 p1, Vector3 p2,
                                     Vector3 p3, float t) {
  float u = 1.0f - t;
  Vector3 d = {0};
  d.x = 3.0f * u * u * (p1.x - p0.x) + 6.0f * u * t * (p2.x - p1.x) +
        3.0f * t * t * (p3.x - p2.x);
  d.y = 3.0f * u * u * (p1.y - p0.y) + 6.0f * u * t * (p2.y - p1.y) +
        3.0f * t * t * (p3.y - p2.y);
  d.z = 3.0f * u * u * (p1.z - p0.z) + 6.0f * u * t * (p2.z - p1.z) +
        3.0f * t * t * (p3.z - p2.z);
  return d;
}

/* ---------------------------------------------------------------------------
 * Lớp 2: Config mặc định — khớp 100% hành vi gốc của Water Stream
 * -------------------------------------------------------------------------*/

TubeMeshConfig ProceduralMesh_DefaultTubeConfig(void) {
  TubeMeshConfig cfg = {0};

  cfg.capsuleTailExp = 1.0f;
  cfg.tailTaperMin = 0.15f;
  cfg.tailTaperMax = 1.00f;
  cfg.headGrowth = 0.20f;

  cfg.wobbleAmplitude = 0.1f; /* WATER_BODY_WOBBLE_AMPLITUDE gốc */
  cfg.wobbleFrequency = 4.0f; /* WATER_BODY_WOBBLE_FREQUENCY gốc */
  cfg.wobbleSpeed = 8.0f;     /* WATER_BODY_WOBBLE_SPEED gốc */

  cfg.deform1Amp = 0.12f;
  cfg.deform1FreqT = 18.0f;
  cfg.deform1FreqPhi = 3.0f;
  cfg.deform1Speed = 10.0f;
  cfg.deform2Amp = 0.08f;
  cfg.deform2FreqT = 9.0f;
  cfg.deform2FreqPhi = 5.0f;
  cfg.deform2Speed = 6.0f;

  cfg.tailApexFactor = 0.25f;
  cfg.headApexFactor = 0.80f;

  return cfg;
}

/* ---------------------------------------------------------------------------
 * Lớp 3: Build — thay thế toàn bộ loop "for i <= segments" trong bản gốc
 * -------------------------------------------------------------------------*/

void ProceduralMesh_BuildTube(TubeMeshData *out, Vector3 p0, Vector3 p1,
                              Vector3 p2, Vector3 p3, float baseRadius,
                              float flowProgress, float time, int segments,
                              int radialSegs, const TubeMeshConfig *cfg) {
  if (out == NULL)
    return;

  TubeMeshConfig defaultCfg;
  if (cfg == NULL) {
    defaultCfg = ProceduralMesh_DefaultTubeConfig();
    cfg = &defaultCfg;
  }

  if (segments > TUBE_MESH_MAX_SEGMENTS)
    segments = TUBE_MESH_MAX_SEGMENTS;
  if (radialSegs > TUBE_MESH_MAX_RADIAL)
    radialSegs = TUBE_MESH_MAX_RADIAL;
  out->segments = segments;
  out->radialSegs = radialSegs;
  out->tailApexFactor = cfg->tailApexFactor;
  out->headApexFactor = cfg->headApexFactor;

  /* Pre-compute sin/cos quanh vòng tròn 1 lần (giống tube_skill.c gốc) */
  float sinPhi[TUBE_MESH_MAX_RADIAL];
  float cosPhi[TUBE_MESH_MAX_RADIAL];
  for (int j = 0; j < radialSegs; j++) {
    float phi = (float)j * (2.0f * PI) / (float)radialSegs;
    sinPhi[j] = sinf(phi);
    cosPhi[j] = cosf(phi);
  }

  for (int i = 0; i <= segments; i++) {
    float t = (float)i / (float)segments;
    float currentT = t * flowProgress;

    Vector3 pos = ProceduralMesh_BezierPoint(p0, p1, p2, p3, currentT);
    Vector3 tangent = Vector3Normalize(
        ProceduralMesh_BezierTangent(p0, p1, p2, p3, currentT));

    /* Frenet frame (Step 2 trong API doc mục 10) */
    Vector3 up = (Vector3){0.0f, 1.0f, 0.0f};
    if (fabsf(tangent.y) > 0.99f)
      up = (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(up, tangent));
    up = Vector3CrossProduct(tangent, right);

    /* Profile bán kính: capsule bo 2 đầu + taper đuôi + phình đầu */
    float baseCapsule =
        0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI))) * cfg->capsuleTailExp;
    float tailTaper =
        cfg->tailTaperMin + (cfg->tailTaperMax - cfg->tailTaperMin) * t;
    float capsuleCurve = baseCapsule * tailTaper;
    float headWeight = 1.0f + cfg->headGrowth * t;

    if (i == 0) {
      out->tailTangent = tangent;
      out->tailCenter = pos;
    }
    if (i == segments) {
      out->headTangent = tangent;
      out->headCenter = pos;
    }

    /* Wobble: xoay frame quanh trục tangent (Step 3 trong API doc mục 10) */
    float wobble = cfg->wobbleAmplitude * sinf(t * PI * cfg->wobbleFrequency +
                                               time * cfg->wobbleSpeed);
    Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)),
                                   Vector3Scale(right, sinf(wobble)));
    Vector3 twistedRight =
        Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));

    for (int j = 0; j < radialSegs; j++) {
      Vector3 normal = Vector3Add(Vector3Scale(twistedRight, cosPhi[j]),
                                  Vector3Scale(twistedUp, sinPhi[j]));
      out->normals[i][j] = normal;

      float phi = (float)j * (2.0f * PI) / (float)radialSegs;

      /* Deform: 2 lớp sóng sin chồng (Step 4 trong API doc mục 10) */
      float deform1 = sinf(t * cfg->deform1FreqT + phi * cfg->deform1FreqPhi +
                           time * cfg->deform1Speed);
      float deform2 = sinf(t * cfg->deform2FreqT - phi * cfg->deform2FreqPhi -
                           time * cfg->deform2Speed);
      float deform =
          1.0f + deform1 * cfg->deform1Amp + deform2 * cfg->deform2Amp;

      float finalRadius = baseRadius * capsuleCurve * headWeight * deform;
      out->rings[i][j] = Vector3Add(pos, Vector3Scale(normal, finalRadius));
    }

    if (i == 0)
      out->tailRadius = baseRadius * capsuleCurve * headWeight;
    if (i == segments)
      out->headRadius = baseRadius * capsuleCurve * headWeight;
  }
}

/* ---------------------------------------------------------------------------
 * Lớp 3: Draw — quad strip thân + 2 end-cap (Step 5 trong API doc mục 10)
 * -------------------------------------------------------------------------*/

void ProceduralMesh_DrawTube(const TubeMeshData *data, float uvLengthScale) {
  if (data == NULL)
    return;

  const int segments = data->segments;
  const int radialSegs = data->radialSegs;

  rlPushMatrix();
  rlCheckRenderBatchLimit(segments * radialSegs * 4);
  rlBegin(RL_QUADS);
  for (int i = 0; i < segments; i++) {
    float v1 = (float)i / (float)segments * uvLengthScale;
    float v2 = (float)(i + 1) / (float)segments * uvLengthScale;

    for (int j = 0; j < radialSegs; j++) {
      int nextJ = (j + 1) % radialSegs;
      float u1 = (float)j / (float)radialSegs;
      float u2 = (float)nextJ / (float)radialSegs;

      rlNormal3f(data->normals[i][j].x, data->normals[i][j].y,
                 data->normals[i][j].z);
      rlTexCoord2f(u1, v1);
      rlVertex3f(data->rings[i][j].x, data->rings[i][j].y, data->rings[i][j].z);

      rlNormal3f(data->normals[i][nextJ].x, data->normals[i][nextJ].y,
                 data->normals[i][nextJ].z);
      rlTexCoord2f(u2, v1);
      rlVertex3f(data->rings[i][nextJ].x, data->rings[i][nextJ].y,
                 data->rings[i][nextJ].z);

      rlNormal3f(data->normals[i + 1][nextJ].x, data->normals[i + 1][nextJ].y,
                 data->normals[i + 1][nextJ].z);
      rlTexCoord2f(u2, v2);
      rlVertex3f(data->rings[i + 1][nextJ].x, data->rings[i + 1][nextJ].y,
                 data->rings[i + 1][nextJ].z);

      rlNormal3f(data->normals[i + 1][j].x, data->normals[i + 1][j].y,
                 data->normals[i + 1][j].z);
      rlTexCoord2f(u1, v2);
      rlVertex3f(data->rings[i + 1][j].x, data->rings[i + 1][j].y,
                 data->rings[i + 1][j].z);
    }
  }
  rlEnd();

  /* --- End-caps: dùng tailApexFactor/headApexFactor đã lưu từ TubeMeshConfig
   * lúc Build, để mỗi skill có hình dáng đuôi/đầu riêng (nhọn/tù/bo tròn). */
  rlCheckRenderBatchLimit(radialSegs * 6);
  rlBegin(RL_TRIANGLES);

  Vector3 tailApex = Vector3Subtract(
      data->tailCenter,
      Vector3Scale(data->tailTangent, data->tailRadius * data->tailApexFactor));
  float tailV_apex = -0.1f;
  float tailV_ring = 0.0f;
  for (int j = 0; j < radialSegs; j++) {
    int nextJ = (j + 1) % radialSegs;
    float u1 = (float)j / (float)radialSegs;
    float u2 = (float)nextJ / (float)radialSegs;
    float uCenter = (u1 + u2) * 0.5f;

    rlNormal3f(-data->tailTangent.x, -data->tailTangent.y,
               -data->tailTangent.z);
    rlTexCoord2f(uCenter, tailV_apex);
    rlVertex3f(tailApex.x, tailApex.y, tailApex.z);
    rlNormal3f(data->normals[0][j].x, data->normals[0][j].y,
               data->normals[0][j].z);
    rlTexCoord2f(u1, tailV_ring);
    rlVertex3f(data->rings[0][j].x, data->rings[0][j].y, data->rings[0][j].z);
    rlNormal3f(data->normals[0][nextJ].x, data->normals[0][nextJ].y,
               data->normals[0][nextJ].z);
    rlTexCoord2f(u2, tailV_ring);
    rlVertex3f(data->rings[0][nextJ].x, data->rings[0][nextJ].y,
               data->rings[0][nextJ].z);
  }

  Vector3 headApex = Vector3Add(
      data->headCenter,
      Vector3Scale(data->headTangent, data->headRadius * data->headApexFactor));
  float headV_ring = uvLengthScale;
  float headV_apex = uvLengthScale + 0.1f;
  for (int j = 0; j < radialSegs; j++) {
    int nextJ = (j + 1) % radialSegs;
    float u1 = (float)j / (float)radialSegs;
    float u2 = (float)nextJ / (float)radialSegs;
    float uCenter = (u1 + u2) * 0.5f;

    Vector3 avgNormal1 = Vector3Normalize(
        Vector3Add(data->normals[segments][j], data->headTangent));
    Vector3 avgNormal2 = Vector3Normalize(
        Vector3Add(data->normals[segments][nextJ], data->headTangent));

    rlNormal3f(data->headTangent.x, data->headTangent.y, data->headTangent.z);
    rlTexCoord2f(uCenter, headV_apex);
    rlVertex3f(headApex.x, headApex.y, headApex.z);
    rlNormal3f(avgNormal1.x, avgNormal1.y, avgNormal1.z);
    rlTexCoord2f(u1, headV_ring);
    rlVertex3f(data->rings[segments][j].x, data->rings[segments][j].y,
               data->rings[segments][j].z);
    rlNormal3f(avgNormal2.x, avgNormal2.y, avgNormal2.z);
    rlTexCoord2f(u2, headV_ring);
    rlVertex3f(data->rings[segments][nextJ].x, data->rings[segments][nextJ].y,
               data->rings[segments][nextJ].z);
  }

  rlEnd();
  rlPopMatrix();
}

/* ---------------------------------------------------------------------------
 * Deterministic small hash-based PRNG dùng chung cho WavePlane (nhiễu) và
 * Rock (jitter theo seed). Không dùng rand()/srand() toàn cục để tránh
 * tác động tới state ngẫu nhiên khác trong game.
 * -------------------------------------------------------------------------*/
static unsigned int ProceduralMesh__Hash(unsigned int x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

/* Trả về float xác định trong [-1,1] từ 2 chỉ số nguyên + seed. */
static float ProceduralMesh__Noise2(int ix, int iz, int seed) {
  unsigned int h = ProceduralMesh__Hash((unsigned int)(ix * 73856093) ^
                                        (unsigned int)(iz * 19349663) ^
                                        (unsigned int)(seed * 83492791));
  return ((float)(h & 0xFFFFu) / 65535.0f) * 2.0f - 1.0f;
}

/* ---------------------------------------------------------------------------
 * WAVE PLANE — Build/Draw
 * -------------------------------------------------------------------------*/

WavePlaneConfig ProceduralMesh_DefaultWavePlaneConfig(void) {
  WavePlaneConfig cfg = {0};
  cfg.wavelength = 220.0f;
  cfg.amplitude = 18.0f;
  cfg.direction = (Vector3){1.0f, 0.0f, 0.2f};
  cfg.crestSharpness = 1.5f;
  return cfg;
}

void ProceduralMesh_BuildWavePlane(WavePlaneMeshData *out, Vector3 center,
                                   float width, float length, int segmentsX,
                                   int segmentsZ, float time,
                                   const WavePlaneConfig *cfg) {
  if (out == NULL)
    return;

  WavePlaneConfig defaultCfg;
  if (cfg == NULL) {
    defaultCfg = ProceduralMesh_DefaultWavePlaneConfig();
    cfg = &defaultCfg;
  }

  if (segmentsX > WAVE_PLANE_MAX_SEGMENTS_X)
    segmentsX = WAVE_PLANE_MAX_SEGMENTS_X;
  if (segmentsZ > WAVE_PLANE_MAX_SEGMENTS_Z)
    segmentsZ = WAVE_PLANE_MAX_SEGMENTS_Z;
  if (segmentsX < 1)
    segmentsX = 1;
  if (segmentsZ < 1)
    segmentsZ = 1;
  out->segmentsX = segmentsX;
  out->segmentsZ = segmentsZ;

  Vector3 dir = cfg->direction;
  float dlen = sqrtf(dir.x * dir.x + dir.z * dir.z);
  if (dlen < 0.0001f) {
    dir = (Vector3){1.0f, 0.0f, 0.0f};
  } else {
    dir.x /= dlen;
    dir.z /= dlen;
  }
  dir.y = 0.0f;
  /* Perpendicular hướng trong mặt XZ, dùng cho lớp sóng phụ lệch hướng. */
  Vector3 perp = {-dir.z, 0.0f, dir.x};

  float k = (2.0f * PI) / fmaxf(cfg->wavelength, 1.0f);
  float hw = width * 0.5f;
  float hl = length * 0.5f;

  for (int i = 0; i <= segmentsX; i++) {
    float fx = (float)i / (float)segmentsX;
    float px = -hw + fx * width;
    for (int j = 0; j <= segmentsZ; j++) {
      float fz = (float)j / (float)segmentsZ;
      float pz = -hl + fz * length;

      float proj = px * dir.x + pz * dir.z;       /* dọc hướng lan truyền chính */
      float projPerp = px * perp.x + pz * perp.z; /* dọc hướng phụ */

      /* Lớp 1: sóng chính theo hướng truyền, có thể "nhọn đỉnh" qua mũ. */
      float phase1 = proj * k + time * 1.6f;
      float s1 = sinf(phase1);
      if (cfg->crestSharpness > 0.0f) {
        float sign = (s1 >= 0.0f) ? 1.0f : -1.0f;
        s1 = sign * powf(fabsf(s1), 1.0f / (1.0f + cfg->crestSharpness));
      }

      /* Lớp 2: tần số gấp ~2.3x, lệch hướng nhẹ + lệch pha — phá vỡ tính
       * lặp lại đều đặn của lớp 1. */
      float phase2 = (proj * 0.6f + projPerp * 0.5f) * k * 2.3f + time * 2.3f + 1.7f;
      float s2 = sinf(phase2);

      /* Lớp 3: tần số thấp hơn, hướng phụ, biên độ nhỏ — gợn lớn chậm. */
      float phase3 = projPerp * k * 0.4f - time * 0.9f;
      float s3 = sinf(phase3);

      /* Nhiễu xác định theo lưới ô (không animate) để bề mặt không hoàn
       * toàn "toán học đều đặn" kể cả khi đứng yên. */
      float n = ProceduralMesh__Noise2(i, j, 1337) * 0.15f;

      float h = cfg->amplitude * (0.55f * s1 + 0.30f * s2 + 0.15f * s3 + n);

      out->verts[i][j] = (Vector3){center.x + px, center.y + h, center.z + pz};
    }
  }

  /* Normal xấp xỉ qua finite-difference giữa đỉnh lân cận (đã đẩy Y). */
  for (int i = 0; i <= segmentsX; i++) {
    for (int j = 0; j <= segmentsZ; j++) {
      Vector3 c = out->verts[i][j];
      Vector3 xNeg = out->verts[(i > 0) ? i - 1 : i][j];
      Vector3 xPos = out->verts[(i < segmentsX) ? i + 1 : i][j];
      Vector3 zNeg = out->verts[i][(j > 0) ? j - 1 : j];
      Vector3 zPos = out->verts[i][(j < segmentsZ) ? j + 1 : j];
      (void)c;

      Vector3 tangentX = Vector3Subtract(xPos, xNeg);
      Vector3 tangentZ = Vector3Subtract(zPos, zNeg);
      Vector3 normal = Vector3Normalize(Vector3CrossProduct(tangentZ, tangentX));
      if (normal.y < 0.0f)
        normal = Vector3Negate(normal);
      out->normals[i][j] = normal;
    }
  }
}

void ProceduralMesh_DrawWavePlane(const WavePlaneMeshData *data, Color color) {
  if (data == NULL)
    return;

  const int segX = data->segmentsX;
  const int segZ = data->segmentsZ;

  rlCheckRenderBatchLimit(segX * segZ * 4);
  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);

  for (int i = 0; i < segX; i++) {
    float u1 = (float)i / (float)segX;
    float u2 = (float)(i + 1) / (float)segX;
    for (int j = 0; j < segZ; j++) {
      float v1 = (float)j / (float)segZ;
      float v2 = (float)(j + 1) / (float)segZ;

      Vector3 p1 = data->verts[i][j];
      Vector3 p2 = data->verts[i + 1][j];
      Vector3 p3 = data->verts[i + 1][j + 1];
      Vector3 p4 = data->verts[i][j + 1];

      Vector3 n1 = data->normals[i][j];
      Vector3 n2 = data->normals[i + 1][j];
      Vector3 n3 = data->normals[i + 1][j + 1];
      Vector3 n4 = data->normals[i][j + 1];

      rlNormal3f(n1.x, n1.y, n1.z);
      rlTexCoord2f(u1, v1);
      rlVertex3f(p1.x, p1.y, p1.z);

      rlNormal3f(n2.x, n2.y, n2.z);
      rlTexCoord2f(u2, v1);
      rlVertex3f(p2.x, p2.y, p2.z);

      rlNormal3f(n3.x, n3.y, n3.z);
      rlTexCoord2f(u2, v2);
      rlVertex3f(p3.x, p3.y, p3.z);

      rlNormal3f(n4.x, n4.y, n4.z);
      rlTexCoord2f(u1, v2);
      rlVertex3f(p4.x, p4.y, p4.z);
    }
  }
  rlEnd();
}

/* ---------------------------------------------------------------------------
 * CURLING WAVE — Build/Draw
 * Sweep tiết diện "C" hở dọc widthDirection. Tiết diện được tham số hóa
 * theo t trong [0,1]: t=0 đáy, t=1 mép cuộn ngoài cùng. Hình dạng "C" dựng
 * bằng cách cho góc quét profileAngle đi từ -90° (đáy, hướng lên) tới
 * (90° + curl) độ (mép cuộn đổ ra ngoài/xuống khi curlAmount > 0), bán kính
 * cong cố định theo height — giống cách BuildTube quét quanh vòng tròn
 * nhưng ở đây chỉ quét một cung hở thay vì full 360°.
 * -------------------------------------------------------------------------*/

CurlingWaveConfig ProceduralMesh_DefaultCurlingWaveConfig(void) {
  CurlingWaveConfig cfg = {0};
  cfg.curlAmount = 0.6f;
  cfg.height = 160.0f;
  cfg.archWidth = 400.0f;
  return cfg;
}

void ProceduralMesh_BuildCurlingWave(CurlingWaveMeshData *out,
                                     Vector3 baseCenter,
                                     Vector3 widthDirection,
                                     const CurlingWaveConfig *cfg,
                                     int profileSegs, int widthSegs) {
  if (out == NULL)
    return;

  CurlingWaveConfig defaultCfg;
  if (cfg == NULL) {
    defaultCfg = ProceduralMesh_DefaultCurlingWaveConfig();
    cfg = &defaultCfg;
  }

  if (profileSegs > CURLING_WAVE_MAX_PROFILE_SEGS)
    profileSegs = CURLING_WAVE_MAX_PROFILE_SEGS;
  if (widthSegs > CURLING_WAVE_MAX_WIDTH_SEGS)
    widthSegs = CURLING_WAVE_MAX_WIDTH_SEGS;
  if (profileSegs < 2)
    profileSegs = 2;
  if (widthSegs < 1)
    widthSegs = 1;
  out->profileSegs = profileSegs;
  out->widthSegs = widthSegs;

  Vector3 width = Vector3Normalize(widthDirection);
  /* Trục "sâu" (depth, nằm ngang vuông góc width trong mặt XZ) và "lên"
   * (up) dùng để dựng cung "C" trong mặt phẳng (depth, up) tại mỗi lát
   * dọc width — giống Frenet frame (right/up) trong BuildTube. */
  Vector3 up = {0.0f, 1.0f, 0.0f};
  Vector3 depth = Vector3Normalize(Vector3CrossProduct(up, width));

  /* Bán kính cong của cung "C": height xấp xỉ bán kính để đáy->đỉnh chiếm
   * góc 90°, phần curl thêm (90°*curlAmount, tối đa ~140° tổng) đổ mép
   * trên ra ngoài/xuống tạo overhang. */
  float archRadius = cfg->height;
  float startAngle = -PI * 0.5f;                 /* đáy: hướng vào trong tường */
  float sweepAngle = PI * 0.5f + (PI * 0.5f * cfg->curlAmount); /* tới mép cuộn */

  for (int p = 0; p <= profileSegs; p++) {
    float tp = (float)p / (float)profileSegs;
    /* Nhẹ ease-out để phần lip cuộn nhanh hơn phần thân, giống ảnh thật
     * của sóng cuộn (thân dốc đều, lip bẻ gấp ở cuối). */
    float shaped = tp * tp * (3.0f - 2.0f * tp); /* smoothstep, tránh góc gãy */
    float angle = startAngle + sweepAngle * shaped;

    float cy = sinf(angle) * archRadius + archRadius; /* y: 0 ở đáy */
    float cd = cosf(angle) * archRadius;               /* d: âm = đổ ra ngoài (overhang) */

    for (int w = 0; w <= widthSegs; w++) {
      float tw = (float)w / (float)widthSegs;
      float pw = (-0.5f + tw) * cfg->archWidth;

      /* Nhiễu nhỏ dọc width + profile để mép cuộn không phẳng lì tuyệt
       * đối — tránh trông "đúc khuôn" hoàn toàn đều. */
      float jitter = ProceduralMesh__Noise2(p, w, 4242) *
                     (cfg->height * 0.015f) * (0.3f + 0.7f * tp);

      Vector3 pos = baseCenter;
      pos = Vector3Add(pos, Vector3Scale(width, pw));
      pos = Vector3Add(pos, Vector3Scale(up, cy + jitter));
      pos = Vector3Add(pos, Vector3Scale(depth, cd + jitter));

      out->verts[w][p] = pos;
    }
  }

  /* Normal xấp xỉ finite-difference theo profile (p) và width (w). */
  for (int w = 0; w <= widthSegs; w++) {
    for (int p = 0; p <= profileSegs; p++) {
      Vector3 pNeg = out->verts[w][(p > 0) ? p - 1 : p];
      Vector3 pPos = out->verts[w][(p < profileSegs) ? p + 1 : p];
      Vector3 wNeg = out->verts[(w > 0) ? w - 1 : w][p];
      Vector3 wPos = out->verts[(w < widthSegs) ? w + 1 : w][p];

      Vector3 tangentP = Vector3Subtract(pPos, pNeg);
      Vector3 tangentW = Vector3Subtract(wPos, wNeg);
      Vector3 normal = Vector3Normalize(Vector3CrossProduct(tangentP, tangentW));
      /* Mặt cong hướng ra ngoài tường (theo -depth khi không curl) — đảm
       * bảo normal.y >= 0 thiên về hướng đẩy lên/ra để mặt sáng hợp lý. */
      out->normals[w][p] = normal;
    }
  }
}

void ProceduralMesh_DrawCurlingWave(const CurlingWaveMeshData *data,
                                    Color color) {
  if (data == NULL)
    return;

  const int widthSegs = data->widthSegs;
  const int profileSegs = data->profileSegs;

  rlCheckRenderBatchLimit(widthSegs * profileSegs * 4);
  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);

  for (int w = 0; w < widthSegs; w++) {
    float u1 = (float)w / (float)widthSegs;
    float u2 = (float)(w + 1) / (float)widthSegs;
    for (int p = 0; p < profileSegs; p++) {
      float v1 = (float)p / (float)profileSegs;
      float v2 = (float)(p + 1) / (float)profileSegs;

      Vector3 p1 = data->verts[w][p];
      Vector3 p2 = data->verts[w + 1][p];
      Vector3 p3 = data->verts[w + 1][p + 1];
      Vector3 p4 = data->verts[w][p + 1];

      Vector3 n1 = data->normals[w][p];
      Vector3 n2 = data->normals[w + 1][p];
      Vector3 n3 = data->normals[w + 1][p + 1];
      Vector3 n4 = data->normals[w][p + 1];

      rlNormal3f(n1.x, n1.y, n1.z);
      rlTexCoord2f(u1, v1);
      rlVertex3f(p1.x, p1.y, p1.z);

      rlNormal3f(n2.x, n2.y, n2.z);
      rlTexCoord2f(u2, v1);
      rlVertex3f(p2.x, p2.y, p2.z);

      rlNormal3f(n3.x, n3.y, n3.z);
      rlTexCoord2f(u2, v2);
      rlVertex3f(p3.x, p3.y, p3.z);

      rlNormal3f(n4.x, n4.y, n4.z);
      rlTexCoord2f(u1, v2);
      rlVertex3f(p4.x, p4.y, p4.z);
    }
  }
  rlEnd();
}

/* ---------------------------------------------------------------------------
 * LOW-POLY ROCK — Build/Draw
 * Icosahedron cơ bản (12 đỉnh, 20 mặt) + subdivide tam giác (mỗi cạnh chia
 * đôi, đẩy điểm giữa ra mặt cầu) tới `subdivisions` lần, rồi jitter bán
 * kính từng đỉnh theo seed. Flat-shaded (1 normal/mặt, không trung bình
 * normal đỉnh) để giữ cảm giác facet góc cạnh thay vì mặt cầu mượt.
 * -------------------------------------------------------------------------*/

#define ROCK_ICOSA_VERTS 12
#define ROCK_ICOSA_FACES 20

static void ProceduralMesh__BuildIcosahedron(Vector3 verts[ROCK_MESH_MAX_VERTS],
                                             int faces[ROCK_MESH_MAX_FACES][3],
                                             int *outVertCount,
                                             int *outFaceCount) {
  const float t = (1.0f + sqrtf(5.0f)) * 0.5f; /* golden ratio */

  Vector3 base[ROCK_ICOSA_VERTS] = {
      {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
      {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
      {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
  };
  for (int i = 0; i < ROCK_ICOSA_VERTS; i++)
    verts[i] = Vector3Normalize(base[i]);

  int baseFaces[ROCK_ICOSA_FACES][3] = {
      {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
      {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
      {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
      {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
  };
  for (int i = 0; i < ROCK_ICOSA_FACES; i++) {
    faces[i][0] = baseFaces[i][0];
    faces[i][1] = baseFaces[i][1];
    faces[i][2] = baseFaces[i][2];
  }

  *outVertCount = ROCK_ICOSA_VERTS;
  *outFaceCount = ROCK_ICOSA_FACES;
}

/* Subdivide mỗi tam giác thành 4, điểm giữa cạnh đẩy ra mặt cầu đơn vị.
 * Không dùng bảng tra cạnh dùng-chung (edge cache) để giữ code đơn giản —
 * chấp nhận trùng lặp đỉnh giữa cạnh (mesh facet vẫn đúng hình học, chỉ
 * không hàn liền hoàn toàn mượt, mà ở đây CHÍNH LÀ điều ta muốn: facet
 * rời rạc, vì sau đó còn jitter bán kính độc lập theo seed nên các đỉnh
 * trùng vị trí hình học ban đầu sẽ lệch nhau sau jitter -> tạo khe facet
 * rõ, đúng kiểu low-poly rock chứ không phải lỗi). */
static void ProceduralMesh__SubdivideIcosphere(
    Vector3 verts[ROCK_MESH_MAX_VERTS], int faces[ROCK_MESH_MAX_FACES][3],
    int *vertCount, int *faceCount) {
  int newFaces[ROCK_MESH_MAX_FACES][3];
  int newFaceCount = 0;
  int v = *vertCount;

  for (int f = 0; f < *faceCount; f++) {
    if (newFaceCount + 4 > ROCK_MESH_MAX_FACES || v + 3 > ROCK_MESH_MAX_VERTS)
      break; /* an toàn static array, dừng subdivide sớm nếu chạm giới hạn */

    int ia = faces[f][0], ib = faces[f][1], ic = faces[f][2];
    Vector3 a = verts[ia], b = verts[ib], c = verts[ic];

    Vector3 ab = Vector3Normalize(Vector3Lerp(a, b, 0.5f));
    Vector3 bc = Vector3Normalize(Vector3Lerp(b, c, 0.5f));
    Vector3 ca = Vector3Normalize(Vector3Lerp(c, a, 0.5f));

    int iab = v++;
    int ibc = v++;
    int ica = v++;
    verts[iab] = ab;
    verts[ibc] = bc;
    verts[ica] = ca;

    newFaces[newFaceCount][0] = ia;  newFaces[newFaceCount][1] = iab; newFaces[newFaceCount][2] = ica; newFaceCount++;
    newFaces[newFaceCount][0] = iab; newFaces[newFaceCount][1] = ib;  newFaces[newFaceCount][2] = ibc; newFaceCount++;
    newFaces[newFaceCount][0] = ica; newFaces[newFaceCount][1] = ibc; newFaces[newFaceCount][2] = ic;  newFaceCount++;
    newFaces[newFaceCount][0] = iab; newFaces[newFaceCount][1] = ibc; newFaces[newFaceCount][2] = ica; newFaceCount++;
  }

  for (int f = 0; f < newFaceCount; f++) {
    faces[f][0] = newFaces[f][0];
    faces[f][1] = newFaces[f][1];
    faces[f][2] = newFaces[f][2];
  }
  *faceCount = newFaceCount;
  *vertCount = v;
}

void ProceduralMesh_BuildRock(RockMeshData *out, Vector3 center, float radius,
                              float jitterAmount, int seed, int subdivisions) {
  if (out == NULL)
    return;

  if (subdivisions < 0)
    subdivisions = 0;
  if (subdivisions > 2) /* level 2 (~162 verts) đã sát ROCK_MESH_MAX_VERTS */
    subdivisions = 2;

  Vector3 unitVerts[ROCK_MESH_MAX_VERTS];
  int faces[ROCK_MESH_MAX_FACES][3];
  int vertCount, faceCount;
  ProceduralMesh__BuildIcosahedron(unitVerts, faces, &vertCount, &faceCount);

  for (int s = 0; s < subdivisions; s++)
    ProceduralMesh__SubdivideIcosphere(unitVerts, faces, &vertCount, &faceCount);

  /* Jitter bán kính từng đỉnh, xác định theo seed + chỉ số đỉnh. */
  for (int i = 0; i < vertCount; i++) {
    float n = ProceduralMesh__Noise2(i, 0, seed); /* [-1,1] */
    float r = radius * (1.0f + n * jitterAmount);
    out->verts[i] = Vector3Add(center, Vector3Scale(unitVerts[i], r));
  }
  out->vertCount = vertCount;
  out->faceCount = faceCount;

  for (int f = 0; f < faceCount; f++) {
    out->faceVertIdx[f][0] = faces[f][0];
    out->faceVertIdx[f][1] = faces[f][1];
    out->faceVertIdx[f][2] = faces[f][2];

    Vector3 a = out->verts[faces[f][0]];
    Vector3 b = out->verts[faces[f][1]];
    Vector3 c = out->verts[faces[f][2]];
    Vector3 normal =
        Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a),
                                             Vector3Subtract(c, a)));
    /* Đảm bảo normal hướng ra ngoài tâm (facet lồi quanh center). */
    Vector3 toFace = Vector3Subtract(a, center);
    if (Vector3DotProduct(normal, toFace) < 0.0f)
      normal = Vector3Negate(normal);
    out->faceNormals[f] = normal;
  }
}

void ProceduralMesh_DrawRock(const RockMeshData *data, Color color) {
  if (data == NULL)
    return;

  rlCheckRenderBatchLimit(data->faceCount * 3);
  rlBegin(RL_TRIANGLES);
  rlColor4ub(color.r, color.g, color.b, color.a);

  for (int f = 0; f < data->faceCount; f++) {
    Vector3 n = data->faceNormals[f];
    Vector3 a = data->verts[data->faceVertIdx[f][0]];
    Vector3 b = data->verts[data->faceVertIdx[f][1]];
    Vector3 c = data->verts[data->faceVertIdx[f][2]];

    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
  }
  rlEnd();
}

/* ---------------------------------------------------------------------------
 * SHARD CLUSTER — Build/Draw
 * Mỗi shard: tapered prism toả ra từ origin, hướng lệch ngẫu nhiên trong 1
 * cone quanh mainDirection, dùng ProceduralMesh__Noise2 (cùng PRNG xác định
 * với Rock/WavePlane) để random length/thickness/hướng theo seed.
 * -------------------------------------------------------------------------*/

ShardClusterConfig ProceduralMesh_DefaultShardClusterConfig(void) {
  ShardClusterConfig cfg = {0};
  cfg.spreadAngle = 35.0f * (PI / 180.0f);
  cfg.thicknessMin = 0.06f;
  cfg.thicknessMax = 0.14f;
  cfg.tipSharpness = 0.85f;
  cfg.sides = 5;
  return cfg;
}

void ProceduralMesh_BuildShardCluster(ShardClusterMeshData *out, Vector3 origin,
                                      Vector3 mainDirection, int shardCount,
                                      float minLength, float maxLength,
                                      int seed, const ShardClusterConfig *cfg) {
  if (out == NULL)
    return;

  ShardClusterConfig defaultCfg;
  if (cfg == NULL) {
    defaultCfg = ProceduralMesh_DefaultShardClusterConfig();
    cfg = &defaultCfg;
  }

  if (shardCount > SHARD_CLUSTER_MAX_SHARDS)
    shardCount = SHARD_CLUSTER_MAX_SHARDS;
  if (shardCount < 1)
    shardCount = 1;
  int sides = cfg->sides;
  if (sides > SHARD_MAX_SIDES)
    sides = SHARD_MAX_SIDES;
  if (sides < 3)
    sides = 3;
  out->shardCount = shardCount;
  out->sides = sides;

  Vector3 dir = Vector3Normalize(mainDirection);
  Vector3 helper = (fabsf(dir.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f}
                                          : (Vector3){0.0f, 1.0f, 0.0f};
  Vector3 right = Vector3Normalize(Vector3CrossProduct(helper, dir));
  Vector3 up = Vector3CrossProduct(dir, right);

  for (int s = 0; s < shardCount; s++) {
    /* Random xác định theo seed + chỉ số shard: hướng lệch (yaw/pitch
     * trong cone), độ dài, độ dày, góc xoay quanh trục riêng. */
    float rYaw = ProceduralMesh__Noise2(s, 0, seed) * PI; /* full 360 quanh dir */
    float rSpread = (ProceduralMesh__Noise2(s, 1, seed) * 0.5f + 0.5f) *
                    cfg->spreadAngle; /* 0..spreadAngle */
    float rLenT = ProceduralMesh__Noise2(s, 2, seed) * 0.5f + 0.5f; /* 0..1 */
    float rThickT = ProceduralMesh__Noise2(s, 3, seed) * 0.5f + 0.5f;
    float rTwist = ProceduralMesh__Noise2(s, 4, seed) * PI;

    float length = minLength + (maxLength - minLength) * rLenT;
    float thicknessRatio =
        cfg->thicknessMin + (cfg->thicknessMax - cfg->thicknessMin) * rThickT;
    float baseRadius = length * thicknessRatio;
    float tipRadius = baseRadius * (1.0f - cfg->tipSharpness);

    /* Hướng shard: lệch khỏi dir bằng góc rSpread quanh 1 trục ngẫu nhiên
     * (rYaw) trong mặt phẳng (right, up). */
    Vector3 tiltAxis = Vector3Add(Vector3Scale(right, cosf(rYaw)),
                                  Vector3Scale(up, sinf(rYaw)));
    Vector3 shardDir =
        Vector3Add(Vector3Scale(dir, cosf(rSpread)),
                   Vector3Scale(tiltAxis, sinf(rSpread)));
    shardDir = Vector3Normalize(shardDir);

    /* Frame ngang quanh shardDir (cho tiết diện đa giác), xoay thêm rTwist
     * để các shard không có tiết diện cùng pha -> tránh nhìn "rập khuôn". */
    Vector3 shardHelper = (fabsf(shardDir.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f}
                                                       : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 sRight = Vector3Normalize(Vector3CrossProduct(shardHelper, shardDir));
    Vector3 sUp = Vector3CrossProduct(shardDir, sRight);
    Vector3 rotRight = Vector3Add(Vector3Scale(sRight, cosf(rTwist)),
                                  Vector3Scale(sUp, sinf(rTwist)));
    Vector3 rotUp = Vector3Add(Vector3Scale(sUp, cosf(rTwist)),
                               Vector3Scale(sRight, -sinf(rTwist)));

    Vector3 base = origin;
    Vector3 tip = Vector3Add(origin, Vector3Scale(shardDir, length));
    out->baseCenter[s] = base;
    out->tipCenter[s] = tip;

    for (int j = 0; j < sides; j++) {
      float phi = (float)j * (2.0f * PI) / (float)sides;
      Vector3 radial = Vector3Add(Vector3Scale(rotRight, cosf(phi)),
                                  Vector3Scale(rotUp, sinf(phi)));
      out->baseRing[s][j] = Vector3Add(base, Vector3Scale(radial, baseRadius));
      out->tipRing[s][j] = Vector3Add(tip, Vector3Scale(radial, tipRadius));
      out->baseNormal[s][j] = radial; /* xấp xỉ -- phối hợp với slant ở Draw */
    }
  }
}

void ProceduralMesh_DrawShardCluster(const ShardClusterMeshData *data,
                                     Color color) {
  if (data == NULL)
    return;

  const int sides = data->sides;

  rlCheckRenderBatchLimit(data->shardCount * sides * 7);
  for (int s = 0; s < data->shardCount; s++) {
    Vector3 axis = Vector3Normalize(
        Vector3Subtract(data->tipCenter[s], data->baseCenter[s]));

    /* Thân: quad strip base ring -> tip ring */
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int j = 0; j < sides; j++) {
      int nextJ = (j + 1) % sides;
      Vector3 n1 = data->baseNormal[s][j];
      Vector3 n2 = data->baseNormal[s][nextJ];

      rlNormal3f(n1.x, n1.y, n1.z);
      rlVertex3f(data->baseRing[s][j].x, data->baseRing[s][j].y,
                 data->baseRing[s][j].z);
      rlNormal3f(n2.x, n2.y, n2.z);
      rlVertex3f(data->baseRing[s][nextJ].x, data->baseRing[s][nextJ].y,
                 data->baseRing[s][nextJ].z);
      rlNormal3f(n2.x, n2.y, n2.z);
      rlVertex3f(data->tipRing[s][nextJ].x, data->tipRing[s][nextJ].y,
                 data->tipRing[s][nextJ].z);
      rlNormal3f(n1.x, n1.y, n1.z);
      rlVertex3f(data->tipRing[s][j].x, data->tipRing[s][j].y,
                 data->tipRing[s][j].z);
    }
    rlEnd();

    /* Đáy + đỉnh: triangle fan (đỉnh có thể gần như 1 điểm nếu tipRadius
     * nhỏ -> đọc như mũi nhọn). */
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(-axis.x, -axis.y, -axis.z);
    for (int j = 0; j < sides; j++) {
      int nextJ = (j + 1) % sides;
      rlVertex3f(data->baseCenter[s].x, data->baseCenter[s].y,
                 data->baseCenter[s].z);
      rlVertex3f(data->baseRing[s][nextJ].x, data->baseRing[s][nextJ].y,
                 data->baseRing[s][nextJ].z);
      rlVertex3f(data->baseRing[s][j].x, data->baseRing[s][j].y,
                 data->baseRing[s][j].z);
    }
    rlNormal3f(axis.x, axis.y, axis.z);
    for (int j = 0; j < sides; j++) {
      int nextJ = (j + 1) % sides;
      rlVertex3f(data->tipCenter[s].x, data->tipCenter[s].y,
                 data->tipCenter[s].z);
      rlVertex3f(data->tipRing[s][j].x, data->tipRing[s][j].y,
                 data->tipRing[s][j].z);
      rlVertex3f(data->tipRing[s][nextJ].x, data->tipRing[s][nextJ].y,
                 data->tipRing[s][nextJ].z);
    }
    rlEnd();
  }
}

/* ---------------------------------------------------------------------------
 * VORTEX FUNNEL — Build/Draw
 * Sweep tiết diện tròn co dần (topRadius<->bottomRadius) + xoay dần
 * (twistAmount) dọc trục +Y thẳng đứng, cộng sóng cos(phi*ridgeCount) đẩy
 * bán kính ra để tạo gờ xoắn ốc nổi. Cùng layout 2-lớp (height rồi radial)
 * như BuildTube's (segments rồi radialSegs), nhưng path ở đây cố định
 * thẳng đứng nên không cần Bezier/Frenet -- right/up cố định, chỉ phi xoay
 * theo twist.
 * -------------------------------------------------------------------------*/

VortexFunnelConfig ProceduralMesh_DefaultVortexFunnelConfig(void) {
  VortexFunnelConfig cfg = {0};
  cfg.topRadius = 140.0f;
  cfg.bottomRadius = 25.0f;
  cfg.height = 260.0f;
  cfg.twistAmount = 320.0f;
  cfg.ridgeCount = 5;
  cfg.ridgeAmount = 0.12f;
  return cfg;
}

void ProceduralMesh_BuildVortexFunnel(VortexFunnelMeshData *out, Vector3 center,
                                      const VortexFunnelConfig *cfg,
                                      int heightSegs, int radialSegs,
                                      float time) {
  if (out == NULL)
    return;

  VortexFunnelConfig defaultCfg;
  if (cfg == NULL) {
    defaultCfg = ProceduralMesh_DefaultVortexFunnelConfig();
    cfg = &defaultCfg;
  }

  if (heightSegs > VORTEX_FUNNEL_MAX_HEIGHT_SEGS)
    heightSegs = VORTEX_FUNNEL_MAX_HEIGHT_SEGS;
  if (radialSegs > VORTEX_FUNNEL_MAX_RADIAL_SEGS)
    radialSegs = VORTEX_FUNNEL_MAX_RADIAL_SEGS;
  if (heightSegs < 1)
    heightSegs = 1;
  if (radialSegs < 3)
    radialSegs = 3;
  out->heightSegs = heightSegs;
  out->radialSegs = radialSegs;

  float twistRad = cfg->twistAmount * (PI / 180.0f);

  /* Precompute trig quanh chu vi 1 lần (giống BuildTube). */
  float sinPhi[VORTEX_FUNNEL_MAX_RADIAL_SEGS];
  float cosPhi[VORTEX_FUNNEL_MAX_RADIAL_SEGS];
  for (int j = 0; j < radialSegs; j++) {
    float phi = (float)j * (2.0f * PI) / (float)radialSegs;
    sinPhi[j] = sinf(phi);
    cosPhi[j] = cosf(phi);
  }

  for (int i = 0; i <= heightSegs; i++) {
    float t = (float)i / (float)heightSegs; /* 0 = đáy, 1 = đỉnh */
    float y = t * cfg->height;
    float ringRadius =
        cfg->bottomRadius + (cfg->topRadius - cfg->bottomRadius) * t;
    /* Xoay tiết diện theo chiều cao (twist) + animate theo time (giống
     * wobble của BuildTube nhưng đây là xoay đều quanh trục, không dao
     * động qua lại -- xoáy thật quay 1 chiều liên tục). */
    float twistAngle = twistRad * t + time * 2.0f;
    float tc = cosf(twistAngle), ts = sinf(twistAngle);

    for (int j = 0; j < radialSegs; j++) {
      float phi = (float)j * (2.0f * PI) / (float)radialSegs;

      /* Gờ xoắn ốc: sóng cos nổi theo phi, lệch pha dần theo twist để gờ
       * trông như xoắn dọc thân thay vì đứng yên theo chiều cao. */
      float ridge =
          1.0f + cfg->ridgeAmount *
                     cosf((float)cfg->ridgeCount * (phi + twistAngle));
      float r = ringRadius * ridge;

      /* Toạ độ tiết diện trước khi xoay (twist), trong mặt phẳng XZ cục
       * bộ tại chiều cao t. */
      float lx = cosPhi[j] * r;
      float lz = sinPhi[j] * r;
      /* Áp dụng xoay twist quanh trục Y. */
      float wx = lx * tc - lz * ts;
      float wz = lx * ts + lz * tc;

      out->rings[i][j] =
          (Vector3){center.x + wx, center.y + y, center.z + wz};

      /* Normal xấp xỉ: hướng radial ra ngoài (bỏ qua slope co dần + gờ
       * nhỏ, đủ tốt cho shading low-poly hữu cơ giống Rock/CurlingWave). */
      float nx = cosPhi[j] * tc - sinPhi[j] * ts;
      float nz = cosPhi[j] * ts + sinPhi[j] * tc;
      out->normals[i][j] = (Vector3){nx, 0.0f, nz};
    }
  }
}

void ProceduralMesh_DrawVortexFunnel(const VortexFunnelMeshData *data,
                                     Color color) {
  if (data == NULL)
    return;

  const int heightSegs = data->heightSegs;
  const int radialSegs = data->radialSegs;

  rlCheckRenderBatchLimit(heightSegs * radialSegs * 4);
  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);
  for (int i = 0; i < heightSegs; i++) {
    for (int j = 0; j < radialSegs; j++) {
      int nextJ = (j + 1) % radialSegs;

      rlNormal3f(data->normals[i][j].x, data->normals[i][j].y,
                 data->normals[i][j].z);
      rlVertex3f(data->rings[i][j].x, data->rings[i][j].y, data->rings[i][j].z);

      rlNormal3f(data->normals[i][nextJ].x, data->normals[i][nextJ].y,
                 data->normals[i][nextJ].z);
      rlVertex3f(data->rings[i][nextJ].x, data->rings[i][nextJ].y,
                 data->rings[i][nextJ].z);

      rlNormal3f(data->normals[i + 1][nextJ].x, data->normals[i + 1][nextJ].y,
                 data->normals[i + 1][nextJ].z);
      rlVertex3f(data->rings[i + 1][nextJ].x, data->rings[i + 1][nextJ].y,
                 data->rings[i + 1][nextJ].z);

      rlNormal3f(data->normals[i + 1][j].x, data->normals[i + 1][j].y,
                 data->normals[i + 1][j].z);
      rlVertex3f(data->rings[i + 1][j].x, data->rings[i + 1][j].y,
                 data->rings[i + 1][j].z);
    }
  }
  rlEnd();
}

/* ---------------------------------------------------------------------------
 * FISSURE — Build/Draw
 * Rải centerline bằng SamplePath (path_spline.h), rồi dựng tiết diện ngang
 * 5 đỉnh (mép trái, vai trái, đáy, vai phải, mép phải) tạo hình V lởm chởm
 * quanh mỗi điểm centerline, jitter xác định theo seed (cùng PRNG với
 * Rock/ShardCluster) để tránh vết nứt thẳng đều robotic.
 * -------------------------------------------------------------------------*/

void ProceduralMesh_BuildFissure(FissureMeshData *out, const Vector3 *pathPoints,
                                 int pathPointCount, float width, float depth,
                                 float jaggedness, int seed) {
  if (out == NULL || pathPoints == NULL || pathPointCount < 2)
    return;

  Vector3 centerline[FISSURE_MAX_SEGMENTS + 1];
  float spacing = fmaxf(width * 0.5f, 1.0f);
  int sampleCount = SamplePath(pathPoints, pathPointCount, spacing, centerline,
                               FISSURE_MAX_SEGMENTS + 1);
  if (sampleCount < 2) {
    out->segments = 0;
    return;
  }
  int segments = sampleCount - 1;
  out->segments = segments;

  float hw = width * 0.5f;

  for (int i = 0; i <= segments; i++) {
    Vector3 p = centerline[i];

    /* Tangent dọc centerline: finite-difference giữa điểm lân cận. */
    Vector3 pPrev = centerline[(i > 0) ? i - 1 : i];
    Vector3 pNext = centerline[(i < segments) ? i + 1 : i];
    Vector3 tangent = Vector3Normalize(Vector3Subtract(pNext, pPrev));
    if (Vector3LengthSqr(tangent) < 0.0001f)
      tangent = (Vector3){1.0f, 0.0f, 0.0f};

    /* Hướng ngang (across) trong mặt phẳng XZ, vuông góc tangent. */
    Vector3 up = {0.0f, 1.0f, 0.0f};
    Vector3 across = Vector3Normalize(Vector3CrossProduct(up, tangent));

    /* Jitter xác định theo seed + chỉ số lát: lệch ngang centerline (để
     * vết nứt không thẳng tăm tắp), lệch độ rộng mép trái/phải, lệch độ
     * sâu đáy. */
    float jLateral = ProceduralMesh__Noise2(i, 0, seed) * hw * 0.3f * jaggedness;
    float jLeftEdge = ProceduralMesh__Noise2(i, 1, seed) * hw * 0.25f * jaggedness;
    float jRightEdge = ProceduralMesh__Noise2(i, 2, seed) * hw * 0.25f * jaggedness;
    float jDepth = ProceduralMesh__Noise2(i, 3, seed) * depth * 0.3f * jaggedness;
    float jShoulderL = ProceduralMesh__Noise2(i, 4, seed) * depth * 0.2f * jaggedness;
    float jShoulderR = ProceduralMesh__Noise2(i, 5, seed) * depth * 0.2f * jaggedness;

    Vector3 centerJ = Vector3Add(p, Vector3Scale(across, jLateral));

    /* 5 đỉnh tiết diện: mép trái (y=0) -> vai trái (y nhẹ âm) -> đáy
     * (y=-depth, lệch ngang nhẹ) -> vai phải -> mép phải (y=0). */
    Vector3 edgeL = Vector3Add(centerJ, Vector3Scale(across, -hw - jLeftEdge));
    Vector3 shoulderL = Vector3Add(centerJ, Vector3Scale(across, -hw * 0.45f));
    shoulderL.y += -depth * 0.35f + jShoulderL;
    Vector3 bottom = centerJ;
    bottom.y += -depth + jDepth;
    Vector3 shoulderR = Vector3Add(centerJ, Vector3Scale(across, hw * 0.45f));
    shoulderR.y += -depth * 0.35f + jShoulderR;
    Vector3 edgeR = Vector3Add(centerJ, Vector3Scale(across, hw + jRightEdge));

    out->verts[i][0] = edgeL;
    out->verts[i][1] = shoulderL;
    out->verts[i][2] = bottom;
    out->verts[i][3] = shoulderR;
    out->verts[i][4] = edgeR;
  }

  /* Normal xấp xỉ finite-difference theo cả 2 hướng (dọc segment, ngang
   * cross-section), giống cách WavePlane/CurlingWave tính normal. */
  for (int i = 0; i <= segments; i++) {
    for (int c = 0; c < FISSURE_CROSS_VERTS; c++) {
      Vector3 cNeg = out->verts[i][(c > 0) ? c - 1 : c];
      Vector3 cPos = out->verts[i][(c < FISSURE_CROSS_VERTS - 1) ? c + 1 : c];
      Vector3 iNeg = out->verts[(i > 0) ? i - 1 : i][c];
      Vector3 iPos = out->verts[(i < segments) ? i + 1 : i][c];

      Vector3 tangentC = Vector3Subtract(cPos, cNeg);
      Vector3 tangentI = Vector3Subtract(iPos, iNeg);
      Vector3 normal =
          Vector3Normalize(Vector3CrossProduct(tangentI, tangentC));
      if (normal.y < 0.0f)
        normal = Vector3Negate(normal);
      out->normals[i][c] = normal;
    }
  }
}

void ProceduralMesh_DrawFissure(const FissureMeshData *data, Color color) {
  if (data == NULL || data->segments < 1)
    return;

  const int segments = data->segments;

  rlCheckRenderBatchLimit(segments * (FISSURE_CROSS_VERTS - 1) * 4);
  rlBegin(RL_QUADS);
  rlColor4ub(color.r, color.g, color.b, color.a);
  for (int i = 0; i < segments; i++) {
    for (int c = 0; c < FISSURE_CROSS_VERTS - 1; c++) {
      Vector3 p1 = data->verts[i][c];
      Vector3 p2 = data->verts[i][c + 1];
      Vector3 p3 = data->verts[i + 1][c + 1];
      Vector3 p4 = data->verts[i + 1][c];

      Vector3 n1 = data->normals[i][c];
      Vector3 n2 = data->normals[i][c + 1];
      Vector3 n3 = data->normals[i + 1][c + 1];
      Vector3 n4 = data->normals[i + 1][c];

      rlNormal3f(n1.x, n1.y, n1.z);
      rlVertex3f(p1.x, p1.y, p1.z);
      rlNormal3f(n2.x, n2.y, n2.z);
      rlVertex3f(p2.x, p2.y, p2.z);
      rlNormal3f(n3.x, n3.y, n3.z);
      rlVertex3f(p3.x, p3.y, p3.z);
      rlNormal3f(n4.x, n4.y, n4.z);
      rlVertex3f(p4.x, p4.y, p4.z);
    }
  }
  rlEnd();
}

/* ============================================================================
 * GPU VERTEX DISPLACEMENT MESH SYSTEM — implementation
 * vertices/texcoords/normals/indices cấp phát qua raylib's MemAlloc (cùng cơ
 * chế raylib tự cấp phát nội bộ cho Texture2D/Shader) — đây là bake 1 LẦN
 * lúc cast, không phải pool động trong vòng lặp update, nên không vi phạm
 * quy tắc "no malloc trong update loop" của core.
 * ==========================================================================*/

Mesh ProceduralMesh_CreateBaseGrid(float width, float length, int segmentsX,
                                   int segmentsZ) {
  if (segmentsX < 1) segmentsX = 1;
  if (segmentsZ < 1) segmentsZ = 1;

  int vertCountX = segmentsX + 1;
  int vertCountZ = segmentsZ + 1;
  int vertCount = vertCountX * vertCountZ;
  int triCount = segmentsX * segmentsZ * 2;

  Mesh mesh = { 0 };
  mesh.vertexCount = vertCount;
  mesh.triangleCount = triCount;
  mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
  mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));
  mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
  mesh.indices = (unsigned short *)MemAlloc(triCount * 3 * sizeof(unsigned short));

  float halfW = width * 0.5f;
  float halfL = length * 0.5f;

  for (int j = 0; j < vertCountZ; j++) {
    float v = (float)j / (float)segmentsZ;
    float z = -halfL + v * length;
    for (int i = 0; i < vertCountX; i++) {
      float u = (float)i / (float)segmentsX;
      float x = -halfW + u * width;
      int idx = j * vertCountX + i;

      mesh.vertices[idx * 3 + 0] = x;
      mesh.vertices[idx * 3 + 1] = 0.0f;
      mesh.vertices[idx * 3 + 2] = z;

      mesh.texcoords[idx * 2 + 0] = u;
      mesh.texcoords[idx * 2 + 1] = v;

      mesh.normals[idx * 3 + 0] = 0.0f;
      mesh.normals[idx * 3 + 1] = 1.0f;
      mesh.normals[idx * 3 + 2] = 0.0f;
    }
  }

  int t = 0;
  for (int j = 0; j < segmentsZ; j++) {
    for (int i = 0; i < segmentsX; i++) {
      unsigned short a = (unsigned short)(j * vertCountX + i);
      unsigned short b = (unsigned short)(a + 1);
      unsigned short c = (unsigned short)(a + vertCountX);
      unsigned short d = (unsigned short)(c + 1);

      mesh.indices[t++] = a; mesh.indices[t++] = c; mesh.indices[t++] = b;
      mesh.indices[t++] = b; mesh.indices[t++] = c; mesh.indices[t++] = d;
    }
  }

  UploadMesh(&mesh, false);
  return mesh;
}

Mesh ProceduralMesh_CreateBaseCylinder(int radialSegs, int heightSegs) {
  if (radialSegs < 3) radialSegs = 3;
  if (heightSegs < 1) heightSegs = 1;

  int vertCountRadial = radialSegs + 1; // seam vertex duplicated for UV wrap
  int vertCountHeight = heightSegs + 1;
  int vertCount = vertCountRadial * vertCountHeight;
  int triCount = radialSegs * heightSegs * 2;

  Mesh mesh = { 0 };
  mesh.vertexCount = vertCount;
  mesh.triangleCount = triCount;
  mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
  mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));
  mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
  mesh.indices = (unsigned short *)MemAlloc(triCount * 3 * sizeof(unsigned short));

  for (int j = 0; j < vertCountHeight; j++) {
    float v = (float)j / (float)heightSegs;
    for (int i = 0; i < vertCountRadial; i++) {
      float u = (float)i / (float)radialSegs;
      float phi = u * 2.0f * PI;
      float cx = cosf(phi);
      float cz = sinf(phi);
      int idx = j * vertCountRadial + i;

      mesh.vertices[idx * 3 + 0] = cx;
      mesh.vertices[idx * 3 + 1] = v;
      mesh.vertices[idx * 3 + 2] = cz;

      mesh.texcoords[idx * 2 + 0] = u;
      mesh.texcoords[idx * 2 + 1] = v;

      mesh.normals[idx * 3 + 0] = cx;
      mesh.normals[idx * 3 + 1] = 0.0f;
      mesh.normals[idx * 3 + 2] = cz;
    }
  }

  int t = 0;
  for (int j = 0; j < heightSegs; j++) {
    for (int i = 0; i < radialSegs; i++) {
      unsigned short a = (unsigned short)(j * vertCountRadial + i);
      unsigned short b = (unsigned short)(a + 1);
      unsigned short c = (unsigned short)(a + vertCountRadial);
      unsigned short d = (unsigned short)(c + 1);

      mesh.indices[t++] = a; mesh.indices[t++] = c; mesh.indices[t++] = b;
      mesh.indices[t++] = b; mesh.indices[t++] = c; mesh.indices[t++] = d;
    }
  }

  UploadMesh(&mesh, false);
  return mesh;
}

Mesh ProceduralMesh_CreateBaseSphere(float radius, int rings, int slices) {
  if (rings < 3) rings = 3;
  if (slices < 3) slices = 3;

  int vertCountLat = rings + 1;  // pole to pole
  int vertCountLon = slices + 1; // seam vertex duplicated for UV wrap
  int vertCount = vertCountLat * vertCountLon;
  int triCount = rings * slices * 2;

  Mesh mesh = { 0 };
  mesh.vertexCount = vertCount;
  mesh.triangleCount = triCount;
  mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
  mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));
  mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
  mesh.indices = (unsigned short *)MemAlloc(triCount * 3 * sizeof(unsigned short));

  for (int i = 0; i < vertCountLat; i++) {
    float v = (float)i / (float)rings;
    float theta = v * PI;
    float sinTheta = sinf(theta);
    float cosTheta = cosf(theta);

    for (int j = 0; j < vertCountLon; j++) {
      float u = (float)j / (float)slices;
      float phi = u * 2.0f * PI;
      float nx = sinTheta * cosf(phi);
      float ny = cosTheta;
      float nz = sinTheta * sinf(phi);
      int idx = i * vertCountLon + j;

      mesh.vertices[idx * 3 + 0] = nx * radius;
      mesh.vertices[idx * 3 + 1] = ny * radius;
      mesh.vertices[idx * 3 + 2] = nz * radius;

      mesh.normals[idx * 3 + 0] = nx;
      mesh.normals[idx * 3 + 1] = ny;
      mesh.normals[idx * 3 + 2] = nz;

      mesh.texcoords[idx * 2 + 0] = u;
      mesh.texcoords[idx * 2 + 1] = v;
    }
  }

  int t = 0;
  for (int i = 0; i < rings; i++) {
    for (int j = 0; j < slices; j++) {
      unsigned short a = (unsigned short)(i * vertCountLon + j);
      unsigned short b = (unsigned short)(a + 1);
      unsigned short c = (unsigned short)(a + vertCountLon);
      unsigned short d = (unsigned short)(c + 1);

      mesh.indices[t++] = a; mesh.indices[t++] = c; mesh.indices[t++] = b;
      mesh.indices[t++] = b; mesh.indices[t++] = c; mesh.indices[t++] = d;
    }
  }

  UploadMesh(&mesh, false);
  return mesh;
}

MeshDisplacementParams ProceduralMesh_DefaultDisplacementParams(void) {
  MeshDisplacementParams p = { 0 };
  p.amplitude = 10.0f;
  p.frequency = 0.05f;
  p.speed = 1.0f;
  p.twistAmount = 0.0f;
  p.taperStart = 1.0f;
  p.taperEnd = 1.0f;
  return p;
}

void ProceduralMesh_SetDisplacementUniforms(Shader shader,
                                            const MeshDisplacementParams *params) {
  if (shader.id == 0 || shader.locs == NULL || params == NULL) return;

  int loc;
  if ((loc = GetShaderLocation(shader, "u_dispAmplitude")) >= 0)
    SetShaderValue(shader, loc, &params->amplitude, SHADER_UNIFORM_FLOAT);
  if ((loc = GetShaderLocation(shader, "u_dispFrequency")) >= 0)
    SetShaderValue(shader, loc, &params->frequency, SHADER_UNIFORM_FLOAT);
  if ((loc = GetShaderLocation(shader, "u_dispSpeed")) >= 0)
    SetShaderValue(shader, loc, &params->speed, SHADER_UNIFORM_FLOAT);
  if ((loc = GetShaderLocation(shader, "u_dispTwist")) >= 0)
    SetShaderValue(shader, loc, &params->twistAmount, SHADER_UNIFORM_FLOAT);
  if ((loc = GetShaderLocation(shader, "u_dispTaperStart")) >= 0)
    SetShaderValue(shader, loc, &params->taperStart, SHADER_UNIFORM_FLOAT);
  if ((loc = GetShaderLocation(shader, "u_dispTaperEnd")) >= 0)
    SetShaderValue(shader, loc, &params->taperEnd, SHADER_UNIFORM_FLOAT);
  if ((loc = GetShaderLocation(shader, "u_dispPathP0")) >= 0)
    SetShaderValue(shader, loc, &params->pathP0, SHADER_UNIFORM_VEC3);
  if ((loc = GetShaderLocation(shader, "u_dispPathP1")) >= 0)
    SetShaderValue(shader, loc, &params->pathP1, SHADER_UNIFORM_VEC3);
  if ((loc = GetShaderLocation(shader, "u_dispPathP2")) >= 0)
    SetShaderValue(shader, loc, &params->pathP2, SHADER_UNIFORM_VEC3);
  if ((loc = GetShaderLocation(shader, "u_dispPathP3")) >= 0)
    SetShaderValue(shader, loc, &params->pathP3, SHADER_UNIFORM_VEC3);
}

void ProceduralMesh_UnloadBase(Mesh *mesh) {
  if (mesh == NULL) return;
  UnloadMesh(*mesh);
  *mesh = (Mesh){ 0 };
}