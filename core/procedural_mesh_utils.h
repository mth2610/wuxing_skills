#ifndef PROCEDURAL_MESH_UTILS_H
#define PROCEDURAL_MESH_UTILS_H

#include "raylib.h"

// Procedural drawing utilities utilizing raw rlgl calls
void DrawCoreSphere(Vector3 center, float radius, int rings, int slices, Color color);
void DrawCoreCylinder(Vector3 bottom, Vector3 top, float radiusBottom, float radiusTop, int slices, Color color);
void DrawCoreCone(Vector3 bottom, float radius, float height, int slices, Color color);
void DrawCorePlaneRect(Vector3 center, Vector2 size, Color color);
void DrawCorePlanePolygon(Vector3 center, float radius, int sides, Color color);
void DrawCoreCube(Vector3 position, float width, float height, float length, Color color);
void DrawCoreTorus(Vector3 center, float innerRadius, float outerRadius, int sides, int rings, Color color);
void DrawCorePrism(Vector3 bottom, Vector3 top, float radius, int sides, Color color);

#endif // PROCEDURAL_MESH_UTILS_H
