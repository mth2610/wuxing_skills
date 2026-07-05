#include "crystal_mesh_generator.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.1415926535f
#endif

// Simple deterministic hash to get pseudo-random float [0..1]
static float HashDeterministic(int seed, int subIndex)
{
    unsigned int n = (unsigned int)(seed * 73856093 ^ subIndex * 19349663);
    n = (n ^ 6179) * 31337;
    return (float)(n % 1000) / 1000.0f;
}

void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color)
{
    if (desc == NULL || desc->sides < 3 || desc->segments < 2) return;

    int sides = desc->sides;
    if (sides > 16) sides = 16;
    int segments = desc->segments;
    if (segments > 16) segments = 16;

    // Local vertex buffer
    Vector3 verts[17][16];

    // Generate body vertices
    for (int i = 0; i < segments; i++)
    {
        float t = (float)i / (float)(segments - 1);
        float currentH = desc->height * t * progress;
        
        // Taper reduces radius as height increases
        float r_curr = desc->radius * (1.0f - t * desc->taper * 0.85f);

        float twistAngle = t * desc->twist;

        for (int j = 0; j < sides; j++)
        {
            float angle = (float)j / (float)sides * 2.0f * PI + twistAngle;
            
            // Apply noise to radius
            float vertexNoise = 1.0f + sinf(angle * 4.0f + t * 6.0f) * desc->noise * 0.12f;
            float r = r_curr * vertexNoise;

            verts[i][j] = (Vector3){
                pos.x + cosf(angle) * r,
                pos.y + currentH,
                pos.z + sinf(angle) * r
            };
        }
    }

    // Apex point for the tip
    Vector3 apex = { pos.x, pos.y + desc->height * progress, pos.z };

    rlDrawRenderBatchActive();

    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);

    // Draw body segments
    for (int i = 0; i < segments - 1; i++)
    {
        float t = (float)i / (float)(segments - 1);
        float t_next = (float)(i + 1) / (float)(segments - 1);

        for (int j = 0; j < sides; j++)
        {
            int next_j = (j + 1) % sides;
            Vector3 v1 = verts[i][j];
            Vector3 v2 = verts[i][next_j];
            Vector3 v3 = verts[i+1][next_j];
            Vector3 v4 = verts[i+1][j];

            // Flat normal calculation
            Vector3 edge1 = Vector3Subtract(v2, v1);
            Vector3 edge2 = Vector3Subtract(v4, v1);
            Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
            rlNormal3f(normal.x, normal.y, normal.z);

            // Pass height mapping in UV coord v/y
            rlTexCoord2f((float)j / sides, t);
            rlVertex3f(v1.x, v1.y, v1.z);
            rlTexCoord2f((float)(j+1) / sides, t);
            rlVertex3f(v2.x, v2.y, v2.z);
            rlTexCoord2f((float)(j+1) / sides, t_next);
            rlVertex3f(v3.x, v3.y, v3.z);
            rlTexCoord2f((float)j / sides, t_next);
            rlVertex3f(v4.x, v4.y, v4.z);
        }
    }
    rlEnd();

    // Draw pyramidal tip (triangles connecting last slice to apex)
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    int topSlice = segments - 1;
    for (int j = 0; j < sides; j++)
    {
        int next_j = (j + 1) % sides;
        Vector3 v1 = verts[topSlice][j];
        Vector3 v2 = verts[topSlice][next_j];
        Vector3 v3 = apex;

        // Flat normal
        Vector3 edge1 = Vector3Subtract(v2, v1);
        Vector3 edge2 = Vector3Subtract(v3, v1);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        rlNormal3f(normal.x, normal.y, normal.z);

        rlTexCoord2f((float)j / sides, 1.0f);
        rlVertex3f(v1.x, v1.y, v1.z);
        rlTexCoord2f((float)(j+1) / sides, 1.0f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.5f, 1.05f);
        rlVertex3f(v3.x, v3.y, v3.z);
    }

    // Draw bottom cap polygon (triangle fan facing down)
    for (int j = 1; j < sides - 1; j++)
    {
        Vector3 v1 = verts[0][0];
        Vector3 v2 = verts[0][j+1];
        Vector3 v3 = verts[0][j];
        rlNormal3f(0.0f, -1.0f, 0.0f);
        
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(v1.x, v1.y, v1.z);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(v3.x, v3.y, v3.z);
    }
    rlEnd();
}

void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color)
{
    if (desc == NULL || count <= 0) return;

    for (int i = 0; i < count; i++)
    {
        // Deterministic positioning based on seed & index
        float r_val = HashDeterministic(seed, i * 3);
        float a_val = HashDeterministic(seed, i * 3 + 1);
        float h_val = HashDeterministic(seed, i * 3 + 2);

        float dist = r_val * desc->radius * 0.8f;
        float angle = a_val * 2.0f * PI;

        Vector3 offset = {
            cosf(angle) * dist,
            0.0f,
            sinf(angle) * dist
        };

        Vector3 crystalPos = Vector3Add(center, offset);

        // Customize desc for this instance deterministically
        CrystalDesc childDesc = *desc;
        childDesc.height = desc->height * (0.6f + h_val * 0.5f);
        childDesc.radius = desc->radius * (0.5f + r_val * 0.5f);
        childDesc.twist = desc->twist + (a_val - 0.5f) * 0.8f;

        // Push matrix to tilt the crystal away from center
        rlPushMatrix();
        rlTranslatef(crystalPos.x, crystalPos.y, crystalPos.z);
        
        // Tilt angle away from center
        float tiltDeg = r_val * 22.0f; // tilt up to 22 degrees
        rlRotatef(angle * RAD2DEG, 0, 1, 0);
        rlRotatef(tiltDeg, 0, 0, 1);
        rlRotatef(-angle * RAD2DEG, 0, 1, 0);

        ProceduralMesh_DrawCrystal((Vector3){0, 0, 0}, &childDesc, progress, color);

        rlPopMatrix();
    }
}
