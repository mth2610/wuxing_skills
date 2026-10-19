#ifndef CORE_MOTION_PATH_H
#define CORE_MOTION_PATH_H
#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#define MOTION_PATH_MAX_POINTS 64

typedef struct MotionPath
{
  Vector3 points[MOTION_PATH_MAX_POINTS];
  Vector3 tangents[MOTION_PATH_MAX_POINTS];
  Vector3 normals[MOTION_PATH_MAX_POINTS];
  float distance[MOTION_PATH_MAX_POINTS];
  int count;
  float length;
} MotionPath;

typedef struct MotionPathSample
{
  Vector3 position, tangent, normal, binormal;
  float distance;
  int segment;
} MotionPathSample;

static inline Vector3 MotionVec_Add(Vector3 a, Vector3 b)
{
  return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}
static inline Vector3 MotionVec_Sub(Vector3 a, Vector3 b)
{
  return (Vector3){a.x - b.x, a.y - b.y, a.z - b.z};
}
static inline Vector3 MotionVec_Scale(Vector3 a, float s)
{
  return (Vector3){a.x * s, a.y * s, a.z * s};
}
static inline float MotionVec_Dot(Vector3 a, Vector3 b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline Vector3 MotionVec_Cross(Vector3 a, Vector3 b)
{
  return (Vector3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x};
}
static inline float MotionVec_Length(Vector3 a)
{
  return sqrtf(MotionVec_Dot(a, a));
}
static inline Vector3 MotionVec_Normalize(Vector3 a)
{
  float lSq = MotionVec_Dot(a, a);
  return lSq > 1e-12f ? MotionVec_Scale(a, 1.0f / sqrtf(lSq)) : (Vector3){0};
}
static inline Vector3 MotionVec_Limit(Vector3 a, float maximum)
{
  if (maximum <= 0.0f)
    return a;
  float lSq = MotionVec_Dot(a, a);
  return lSq > maximum * maximum ? MotionVec_Scale(a, maximum / sqrtf(lSq)) : a;
}
static inline float Motion_Clamp(float x, float a, float b)
{
  return fminf(fmaxf(x, a), b);
}

static inline Vector3 MotionPath_TransportNormal(Vector3 normal, Vector3 from,
                                                 Vector3 to)
{
  Vector3 axis = MotionVec_Cross(from, to);
  float c = MotionVec_Dot(from, to);
  if (c > -0.9999f)
  {
    Vector3 v = MotionVec_Cross(axis, normal);
    normal = MotionVec_Add(
        normal, MotionVec_Add(
                    v, MotionVec_Scale(MotionVec_Cross(axis, v), 1.0f / (1.0f + c))));
  }
  normal =
      MotionVec_Sub(normal, MotionVec_Scale(to, MotionVec_Dot(normal, to)));
  if (MotionVec_Dot(normal, normal) < 1e-10f)
  {
    Vector3 ref = fabsf(to.y) < 0.9f ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
    normal = MotionVec_Sub(ref, MotionVec_Scale(to, MotionVec_Dot(ref, to)));
  }
  return MotionVec_Normalize(normal);
}

static inline bool MotionPath_Build(MotionPath *path, const Vector3 *points,
                                    int count)
{
  if (!path)
    return false;
  *path = (MotionPath){0};
  if (!points || count < 2 || count > MOTION_PATH_MAX_POINTS)
    return false;
  for (int i = 0; i < count; i++)
  {
    if (!isfinite(points[i].x) || !isfinite(points[i].y) ||
        !isfinite(points[i].z))
    {
      path->count = 0;
      return false;
    }
    if (path->count)
    {
      Vector3 diff = MotionVec_Sub(points[i], path->points[path->count - 1]);
      if (MotionVec_Dot(diff, diff) < 1e-10f)
        continue;
    }
    path->points[path->count++] = points[i];
  }
  if (path->count < 2)
  {
    path->count = 0;
    return false;
  }
  for (int i = 0; i < path->count - 1; i++)
  {
    Vector3 delta = MotionVec_Sub(path->points[i + 1], path->points[i]);
    float len = MotionVec_Length(delta);
    path->tangents[i] = len > 1e-6f ? MotionVec_Scale(delta, 1.0f / len) : (Vector3){0, 0, 1};
    path->distance[i + 1] = path->distance[i] + len;
    path->normals[i] = MotionPath_TransportNormal(
        i ? path->normals[i - 1] : (Vector3){0, 1, 0},
        i ? path->tangents[i - 1] : path->tangents[i], path->tangents[i]);
  }
  path->length = path->distance[path->count - 1];
  path->tangents[path->count - 1] = path->tangents[path->count - 2];
  path->normals[path->count - 1] = path->normals[path->count - 2];
  return true;
}

static inline MotionPathSample MotionPath_Sample(const MotionPath *path,
                                                 float distance)
{
  MotionPathSample out = {0};
  if (!path || path->count < 2)
    return out;
  out.distance = Motion_Clamp(distance, 0.0f, path->length);
  /* Binary search thay cho linear scan O(N) */
  int lo = 0, hi = path->count - 2;
  while (lo < hi)
  {
    int mid = (lo + hi + 1) >> 1;
    if (path->distance[mid] <= out.distance)
      lo = mid;
    else
      hi = mid - 1;
  }
  int i = lo;
  float segLen = path->distance[i + 1] - path->distance[i];
  float along = out.distance - path->distance[i];
  out.position = MotionVec_Add(path->points[i],
                               MotionVec_Scale(path->tangents[i], along));
  out.tangent = path->tangents[i];
  out.normal = path->normals[i];
  out.binormal = MotionVec_Cross(out.tangent, out.normal);
  out.segment = i;
  (void)segLen;
  return out;
}

static inline MotionPathSample
MotionPath_Project(const MotionPath *path, Vector3 p, int first, int last)
{
  MotionPathSample best = {0};
  if (!path || path->count < 2)
    return best;
  float bestSq = 1e30f;
  float bestDistance = 0.0f;
  Vector3 bestPos = path->points[0];
  int maxSeg = path->count - 2;
  if (first < 0)
    first = 0;
  if (first > maxSeg)
    first = maxSeg;
  if (last < first)
    last = first;
  if (last > maxSeg)
    last = maxSeg;
  int bestSegment = first;

  for (int i = first; i <= last; i++)
  {
    /* Dùng trực tiếp tangent đơn vị và segLen đã tính sẵn, không cần phép chia */
    Vector3 toP = MotionVec_Sub(p, path->points[i]);
    float segLen = path->distance[i + 1] - path->distance[i];
    float along = Motion_Clamp(MotionVec_Dot(toP, path->tangents[i]), 0.0f, segLen);
    Vector3 q = MotionVec_Add(path->points[i], MotionVec_Scale(path->tangents[i], along));
    Vector3 diff = MotionVec_Sub(p, q);
    float sq = MotionVec_Dot(diff, diff);
    if (sq < bestSq)
    {
      bestSq = sq;
      bestDistance = path->distance[i] + along;
      bestPos = q;
      bestSegment = i;
    }
  }
  best.position = bestPos;
  best.tangent = path->tangents[bestSegment];
  best.normal = path->normals[bestSegment];
  best.binormal = MotionVec_Cross(best.tangent, best.normal);
  best.distance = bestDistance;
  best.segment = bestSegment;
  return best;
}

static inline Vector3 MotionPath_WorldOffset(MotionPathSample f,
                                             Vector3 local)
{
  return MotionVec_Add(MotionVec_Scale(f.tangent, local.x),
                       MotionVec_Add(MotionVec_Scale(f.normal, local.y),
                                     MotionVec_Scale(f.binormal, local.z)));
}
static inline Vector3 MotionPath_LocalOffset(MotionPathSample f,
                                             Vector3 world)
{
  return (Vector3){MotionVec_Dot(world, f.tangent),
                   MotionVec_Dot(world, f.normal),
                   MotionVec_Dot(world, f.binormal)};
}
static inline float Motion_SegmentDistance(Vector3 a, Vector3 b, Vector3 p)
{
  Vector3 d = MotionVec_Sub(b, a);
  float sq = MotionVec_Dot(d, d);
  float t = sq > 1e-10f
                ? Motion_Clamp(MotionVec_Dot(MotionVec_Sub(p, a), d) / sq, 0.0f, 1.0f)
                : 0.0f;
  return MotionVec_Length(
      MotionVec_Sub(p, MotionVec_Add(a, MotionVec_Scale(d, t))));
}
#endif