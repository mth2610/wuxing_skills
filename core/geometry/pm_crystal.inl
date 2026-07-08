// Simple deterministic hash to get pseudo-random float [0..1]
static float HashDeterministic(int seed, int subIndex)
{
    unsigned int n = (unsigned int)(seed * 73856093 ^ subIndex * 19349663);
    n = (n ^ 6179) * 31337;            // [cite: 2]
    return (float)(n % 1000) * 0.001f; // TỐI ƯU: Nhân 0.001f nhanh hơn chia 1000.0f
}

void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color)
{
    if (desc == NULL || desc->sides < 3 || desc->segments < 2)
        return; // [cite: 3]

    int sides = desc->sides > 16 ? 16 : desc->sides;          // [cite: 4]
    int segments = desc->segments > 16 ? 16 : desc->segments; // [cite: 5]

    // TỐI ƯU: Pre-calculate các phép chia thành phép nhân
    float invSides = 1.0f / (float)sides;
    float invSegsMinus1 = 1.0f / (float)(segments - 1);

    int crystalSeed = (int)(desc->height * 1000.0f) ^ (int)(desc->radius * 1000.0f); // [cite: 6]

    float baseCos[16], baseSin[16], baseRadius[16]; // [cite: 7]
    float angleStep = PI * invSides;

    for (int j = 0; j < sides; j++)
    {
        float angleOffset = (HashDeterministic(crystalSeed, j) - 0.5f) * (angleStep * 0.35f); // [cite: 8]
        float angle = (float)j * 2.0f * angleStep + angleOffset;                              // [cite: 9]

        float r_noise = 0.8f + 0.4f * HashDeterministic(crystalSeed + 1337, j); // [cite: 10]
        baseRadius[j] = desc->radius * r_noise;                                 //
        baseCos[j] = cosf(angle);
        baseSin[j] = sinf(angle);
    }

    // TỐI ƯU: Trải phẳng mảng 2D thành 1D (Cache-friendly)
    // Kích thước tối đa: 16 segments * 16 sides = 256
    Vector3 verts[256];

    for (int i = 0; i < segments; i++)
    {
        float t = (float)i * invSegsMinus1;           //
        float currentH = desc->height * t * progress; // [cite: 13]

        float taperFactor = 1.0f - (t * desc->taper);
        if (taperFactor < 0.0f)
            taperFactor = 0.0f; // [cite: 14]

        float twistAngle = t * desc->twist; // [cite: 15]
        float cosTwist = cosf(twistAngle);  // [cite: 16]
        float sinTwist = sinf(twistAngle);

        float ridgeNoise = 1.0f + (HashDeterministic(crystalSeed, i * 19) - 0.5f) * desc->noise * 0.15f;
        float segmentScale = taperFactor * ridgeNoise; // [cite: 17]

        int rowOffset = i * sides;
        for (int j = 0; j < sides; j++)
        {
            float r = baseRadius[j] * segmentScale;
            float xRot = baseCos[j] * cosTwist - baseSin[j] * sinTwist; //
            float zRot = baseSin[j] * cosTwist + baseCos[j] * sinTwist; // [cite: 19]

            verts[rowOffset + j] = (Vector3){
                pos.x + xRot * r,
                pos.y + currentH,
                pos.z + zRot * r}; // [cite: 20]
        }
    }

    float apexOffsetX = (HashDeterministic(crystalSeed, 991) - 0.5f) * desc->radius * 0.4f; // [cite: 21]
    float apexOffsetZ = (HashDeterministic(crystalSeed, 992) - 0.5f) * desc->radius * 0.4f; // [cite: 22]
    Vector3 apex = {
        pos.x + apexOffsetX * (1.0f - desc->taper),
        pos.y + desc->height * progress,
        pos.z + apexOffsetZ * (1.0f - desc->taper)}; // [cite: 23]

    rlDrawRenderBatchActive();                      // [cite: 24]
    rlBegin(RL_TRIANGLES);                          //
    rlColor4ub(color.r, color.g, color.b, color.a); // [cite: 26]

    // Vẽ thân
    for (int i = 0; i < segments - 1; i++)
    {
        float t = (float)i * invSegsMinus1;
        float t_next = (float)(i + 1) * invSegsMinus1; // [cite: 27]
        int rowCur = i * sides;
        int rowNext = (i + 1) * sides;

        for (int j = 0; j < sides; j++) // [cite: 28]
        {
            int next_j = (j + 1) % sides;
            Vector3 v1 = verts[rowCur + j];       // [cite: 29]
            Vector3 v2 = verts[rowCur + next_j];  // [cite: 30]
            Vector3 v3 = verts[rowNext + next_j]; // [cite: 31]
            Vector3 v4 = verts[rowNext + j];      //

            float u1 = (float)j * invSides;
            float u2 = (float)(j + 1) * invSides; // [cite: 33]

            // TỐI ƯU TOÁN HỌC: Tái sử dụng vector đường chéo chung (v3 - v1)
            // Thay vì tính 4 vectors[cite: 33, 34, 37, 38], ta chỉ tính 3.
            Vector3 edge_v2_v1 = {v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
            Vector3 edge_v3_v1 = {v3.x - v1.x, v3.y - v1.y, v3.z - v1.z}; // Dùng chung cho cả 2 tam giác
            Vector3 edge_v4_v1 = {v4.x - v1.x, v4.y - v1.y, v4.z - v1.z};

            // Tam giác 1 (v1 -> v2 -> v3)
            Vector3 cross1 = Vector3CrossProduct(edge_v2_v1, edge_v3_v1);
            Vector3 normal1 = Vector3Normalize(cross1);

            rlCheckRenderBatchLimit(6); // Gộp batch check (tối ưu overhead) [cite: 35]
            rlNormal3f(normal1.x, normal1.y, normal1.z);
            rlTexCoord2f(u1, t);
            rlVertex3f(v1.x, v1.y, v1.z); // [cite: 36]
            rlTexCoord2f(u2, t);
            rlVertex3f(v2.x, v2.y, v2.z);
            rlTexCoord2f(u2, t_next);
            rlVertex3f(v3.x, v3.y, v3.z);

            // Tam giác 2 (v1 -> v3 -> v4)
            Vector3 cross2 = Vector3CrossProduct(edge_v3_v1, edge_v4_v1);
            Vector3 normal2 = Vector3Normalize(cross2);

            rlNormal3f(normal2.x, normal2.y, normal2.z);
            rlTexCoord2f(u1, t);
            rlVertex3f(v1.x, v1.y, v1.z);
            rlTexCoord2f(u2, t_next);
            rlVertex3f(v3.x, v3.y, v3.z); // [cite: 39]
            rlTexCoord2f(u1, t_next);
            rlVertex3f(v4.x, v4.y, v4.z);
        }
    }

    // Vẽ chóp đỉnh
    int topRow = (segments - 1) * sides; // [cite: 40]
    for (int j = 0; j < sides; j++)      // [cite: 41]
    {
        int next_j = (j + 1) % sides;
        Vector3 v1 = verts[topRow + j]; // [cite: 42]
        Vector3 v2 = verts[topRow + next_j];

        Vector3 edge1 = {v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
        Vector3 edge2 = {apex.x - v1.x, apex.y - v1.y, apex.z - v1.z}; // [cite: 43]
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));

        rlCheckRenderBatchLimit(3);
        rlNormal3f(normal.x, normal.y, normal.z);
        rlTexCoord2f((float)j * invSides, 1.0f);
        rlVertex3f(v1.x, v1.y, v1.z); // [cite: 44]
        rlTexCoord2f((float)(j + 1) * invSides, 1.0f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.5f, 1.05f);
        rlVertex3f(apex.x, apex.y, apex.z); // [cite: 45]
    }

    // Vẽ đáy
    rlNormal3f(0.0f, -1.0f, 0.0f);      // Normal cho đáy luôn hướng xuống, đặt ra ngoài vòng lặp
    for (int j = 1; j < sides - 1; j++) // [cite: 45]
    {
        Vector3 v1 = verts[0];
        Vector3 v2 = verts[j + 1]; // [cite: 46]
        Vector3 v3 = verts[j];

        rlCheckRenderBatchLimit(3);
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(v1.x, v1.y, v1.z); // [cite: 47]
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(v3.x, v3.y, v3.z);
    }
    rlEnd(); //
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