#include "core/procedural_mesh_utils.h"
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