#include "crystal_mesh_generator.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.14159265358979323846f
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
    if (desc == NULL || desc->sides < 3 || desc->segments < 2)
        return;

    int sides = desc->sides;
    if (sides > 16)
        sides = 16;
    int segments = desc->segments;
    if (segments > 16)
        segments = 16;

    // Tạo một seed duy nhất dựa trên đặc tính của crystal hiện tại để làm lệch tâm và vát góc
    int crystalSeed = (int)(desc->height * 1000.0f) ^ (int)(desc->radius * 1000.0f);

    // BƯỚC TỐI ƯU 1: Tính trước hình dáng đáy (Base Shape) - Mang lại vẻ bất đối xứng tự nhiên
    float baseCos[16], baseSin[16], baseRadius[16];
    for (int j = 0; j < sides; j++)
    {
        // Làm lệch góc các mặt phẳng một chút để ra hình dáng pha lê tự nhiên (ví dụ: lục giác không đều)
        float angleOffset = (HashDeterministic(crystalSeed, j) - 0.5f) * (PI / sides * 0.35f);
        float angle = (float)j / (float)sides * 2.0f * PI + angleOffset;

        // Làm mặt to mặt nhỏ ngẫu nhiên
        float r_noise = 0.8f + 0.4f * HashDeterministic(crystalSeed + 1337, j);
        baseRadius[j] = desc->radius * r_noise;

        baseCos[j] = cosf(angle);
        baseSin[j] = sinf(angle);
    }

    Vector3 verts[17][16];

    // Tạo các đỉnh của thân tinh thể
    for (int i = 0; i < segments; i++)
    {
        float t = (float)i / (float)(segments - 1);
        float currentH = desc->height * t * progress;

        // Vát cạnh tuyến tính sắc nét hơn
        float taperFactor = 1.0f - (t * desc->taper);
        if (taperFactor < 0.0f)
            taperFactor = 0.0f;

        // BƯỚC TỐI ƯU 2: Tính lượng giác Xoắn (Twist) ĐÚNG 1 LẦN cho mỗi phân đoạn
        float twistAngle = t * desc->twist;
        float cosTwist = cosf(twistAngle);
        float sinTwist = sinf(twistAngle);

        // Tạo các ngấn ngang nhẹ (Striations) đặc trưng của các cụm pha lê thạch anh
        float ridgeNoise = 1.0f + (HashDeterministic(crystalSeed, i * 19) - 0.5f) * desc->noise * 0.15f;
        float segmentScale = taperFactor * ridgeNoise;

        for (int j = 0; j < sides; j++)
        {
            float r = baseRadius[j] * segmentScale;

            // Dùng định lý cộng lượng giác: Tốc độ tăng phi mã do không gọi sin/cos ở vòng lặp trong
            // x = r * cos(angle + twist) = r * (cos(A)cos(B) - sin(A)sin(B))
            float xRot = baseCos[j] * cosTwist - baseSin[j] * sinTwist;
            float zRot = baseSin[j] * cosTwist + baseCos[j] * sinTwist;

            verts[i][j] = (Vector3){
                pos.x + xRot * r,
                pos.y + currentH,
                pos.z + zRot * r};
        }
    }

    // Đỉnh (Apex) hơi lệch tâm một cách có chủ đích (Offset Apex)
    float apexOffsetX = (HashDeterministic(crystalSeed, 991) - 0.5f) * desc->radius * 0.4f;
    float apexOffsetZ = (HashDeterministic(crystalSeed, 992) - 0.5f) * desc->radius * 0.4f;
    Vector3 apex = {
        pos.x + apexOffsetX * (1.0f - desc->taper),
        pos.y + desc->height * progress,
        pos.z + apexOffsetZ * (1.0f - desc->taper)};

    rlDrawRenderBatchActive();

    // BƯỚC THẨM MỸ: Dùng RL_TRIANGLES thay vì RL_QUADS cho thân.
    // Điều này đảm bảo Flat Shading sắc lẹm tuyệt đối, tránh hiện tượng bóng lỗi khi bị Twist.
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);

    // Vẽ thân tinh thể
    for (int i = 0; i < segments - 1; i++)
    {
        float t = (float)i / (float)(segments - 1);
        float t_next = (float)(i + 1) / (float)(segments - 1);

        for (int j = 0; j < sides; j++)
        {
            int next_j = (j + 1) % sides;
            Vector3 v1 = verts[i][j];          // Đáy-Trái
            Vector3 v2 = verts[i][next_j];     // Đáy-Phải
            Vector3 v3 = verts[i + 1][next_j]; // Đỉnh-Phải
            Vector3 v4 = verts[i + 1][j];      // Đỉnh-Trái

            float u1 = (float)j / sides;
            float u2 = (float)(j + 1) / sides;

            // Tam giác 1 (v1 -> v2 -> v3)
            Vector3 edge1 = Vector3Subtract(v2, v1);
            Vector3 edge2 = Vector3Subtract(v3, v1);
            Vector3 normal1 = Vector3Normalize(Vector3CrossProduct(edge1, edge2));

            rlCheckRenderBatchLimit(3); // An toàn chống Crash khi render cluster
            rlNormal3f(normal1.x, normal1.y, normal1.z);
            rlTexCoord2f(u1, t);
            rlVertex3f(v1.x, v1.y, v1.z);
            rlTexCoord2f(u2, t);
            rlVertex3f(v2.x, v2.y, v2.z);
            rlTexCoord2f(u2, t_next);
            rlVertex3f(v3.x, v3.y, v3.z);

            // Tam giác 2 (v1 -> v3 -> v4)
            Vector3 edge3 = Vector3Subtract(v3, v1);
            Vector3 edge4 = Vector3Subtract(v4, v1);
            Vector3 normal2 = Vector3Normalize(Vector3CrossProduct(edge3, edge4));

            rlCheckRenderBatchLimit(3);
            rlNormal3f(normal2.x, normal2.y, normal2.z);
            rlTexCoord2f(u1, t);
            rlVertex3f(v1.x, v1.y, v1.z);
            rlTexCoord2f(u2, t_next);
            rlVertex3f(v3.x, v3.y, v3.z);
            rlTexCoord2f(u1, t_next);
            rlVertex3f(v4.x, v4.y, v4.z);
        }
    }

    // Vẽ chóp đỉnh tinh thể (Pyramidal tip)
    int topSlice = segments - 1;
    for (int j = 0; j < sides; j++)
    {
        int next_j = (j + 1) % sides;
        Vector3 v1 = verts[topSlice][j];
        Vector3 v2 = verts[topSlice][next_j];
        Vector3 v3 = apex;

        Vector3 edge1 = Vector3Subtract(v2, v1);
        Vector3 edge2 = Vector3Subtract(v3, v1);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));

        rlCheckRenderBatchLimit(3);
        rlNormal3f(normal.x, normal.y, normal.z);
        rlTexCoord2f((float)j / sides, 1.0f);
        rlVertex3f(v1.x, v1.y, v1.z);
        rlTexCoord2f((float)(j + 1) / sides, 1.0f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.5f, 1.05f);
        rlVertex3f(v3.x, v3.y, v3.z);
    }

    // Vẽ đáy (Bottom cap)
    for (int j = 1; j < sides - 1; j++)
    {
        Vector3 v1 = verts[0][0];
        Vector3 v2 = verts[0][j + 1];
        Vector3 v3 = verts[0][j];

        rlCheckRenderBatchLimit(3);
        rlNormal3f(0.0f, -1.0f, 0.0f);
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(v1.x, v1.y, v1.z);
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(v3.x, v3.y, v3.z);
    }
    rlEnd();
}

void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color)
{
    if (desc == NULL || count <= 0)
        return;

    for (int i = 0; i < count; i++)
    {
        float r_val = HashDeterministic(seed, i * 3);
        float a_val = HashDeterministic(seed, i * 3 + 1);
        float h_val = HashDeterministic(seed, i * 3 + 2);

        float dist = r_val * desc->radius * 0.9f;
        float angle = a_val * 2.0f * PI;

        // Thêm độ lún (âm) để cụm pha lê mọc ra tự nhiên, không bị bằng phẳng giả tạo ở dưới đáy
        float yOffset = -desc->height * 0.15f * HashDeterministic(seed, i * 3 + 4);

        Vector3 offset = {
            cosf(angle) * dist,
            yOffset,
            sinf(angle) * dist};

        Vector3 crystalPos = Vector3Add(center, offset);

        CrystalDesc childDesc = *desc;
        childDesc.height = desc->height * (0.4f + h_val * 0.8f); // Độ chênh lệch chiều cao đa dạng hơn
        childDesc.radius = desc->radius * (0.4f + r_val * 0.6f);
        childDesc.twist = desc->twist + (a_val - 0.5f) * 1.5f;

        rlPushMatrix();
        rlTranslatef(crystalPos.x, crystalPos.y, crystalPos.z);

        // Tăng góc nghiêng lên 45 độ để cluster trông kịch tính và tỏa ra đẹp hơn
        float tiltDeg = r_val * 45.0f;
        rlRotatef(angle * RAD2DEG, 0, 1, 0);
        rlRotatef(tiltDeg, 0, 0, 1);
        rlRotatef(-angle * RAD2DEG, 0, 1, 0);

        ProceduralMesh_DrawCrystal((Vector3){0, 0, 0}, &childDesc, progress, color);

        rlPopMatrix();
    }
}