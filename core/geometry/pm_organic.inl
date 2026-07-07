/* ===========================================================================
 * ORGANIC ENVIRONMENT DETAILS (OPTIMIZED)
 * =========================================================================*/

void ProceduralMesh_DrawOrganicStonePillar(Vector3 pillarPos, float currentHeight, float baseRad, float topRad)
{
#define HEIGHT_SEGS 8
#define RADIAL_SEGS 8

    Vector3 rings[HEIGHT_SEGS + 1][RADIAL_SEGS];
    Vector3 normals[HEIGHT_SEGS + 1][RADIAL_SEGS];

    float seedVal = pillarPos.x * 13.37f + pillarPos.z * 73.19f;
    float tiltAngle = (4.0f + 6.0f * sinf(seedVal)) * DEG2RAD;
    float dirAngle = seedVal * 2.0f;
    float dirInX = cosf(dirAngle);
    float dirInZ = sinf(dirAngle);

    float cosA[RADIAL_SEGS];
    float sinA[RADIAL_SEGS];
    for (int r = 0; r < RADIAL_SEGS; r++)
    {
        float angle = (float)r / RADIAL_SEGS * 2.0f * PI;
        cosA[r] = cosf(angle);
        sinA[r] = sinf(angle);
    }

    for (int h = 0; h <= HEIGHT_SEGS; h++)
    {
        float hRatio = (float)h / HEIGHT_SEGS;
        float easeRatio = hRatio * hRatio;
        float rad = baseRad + (topRad - baseRad) * easeRatio;

        float shiftDist = easeRatio * currentHeight * sinf(tiltAngle);
        Vector3 centerOffset = {dirInX * shiftDist, 0.0f, dirInZ * shiftDist};

        for (int r = 0; r < RADIAL_SEGS; r++)
        {
            float wave1 = sinf(hRatio * 6.0f + r * 1.5f + seedVal);
            float wave2 = cosf(hRatio * 15.0f - r * 3.0f + seedVal * 0.5f);

            float noiseWave = 1.0f + 0.12f * wave1 + 0.08f * wave2;
            float perturbedRad = rad * noiseWave;

            Vector3 localPos = {perturbedRad * cosA[r], hRatio * currentHeight, perturbedRad * sinA[r]};
            Vector3 localNormal = Vector3Normalize((Vector3){cosA[r], 0.1f, sinA[r]});

            rings[h][r] = Vector3Add(Vector3Add(pillarPos, localPos), centerOffset);
            normals[h][r] = localNormal;
        }
    }

    rlPushMatrix();
    rlColor4ub(255, 255, 255, 255);
    rlCheckRenderBatchLimit(HEIGHT_SEGS * RADIAL_SEGS * 4);

    rlBegin(RL_QUADS);
    for (int h = 0; h < HEIGHT_SEGS; h++)
    {
        float v1 = (float)h / HEIGHT_SEGS, v2 = (float)(h + 1) / HEIGHT_SEGS;
        for (int r = 0; r < RADIAL_SEGS; r++)
        {
            int nextR = (r + 1) % RADIAL_SEGS;
            float u1 = (float)r / RADIAL_SEGS, u2 = (float)(r + 1) / RADIAL_SEGS;

            rlNormal3f(normals[h][nextR].x, normals[h][nextR].y, normals[h][nextR].z);
            rlTexCoord2f(u2, v1);
            rlVertex3f(rings[h][nextR].x, rings[h][nextR].y, rings[h][nextR].z);
            rlNormal3f(normals[h][r].x, normals[h][r].y, normals[h][r].z);
            rlTexCoord2f(u1, v1);
            rlVertex3f(rings[h][r].x, rings[h][r].y, rings[h][r].z);
            rlNormal3f(normals[h + 1][r].x, normals[h + 1][r].y, normals[h + 1][r].z);
            rlTexCoord2f(u1, v2);
            rlVertex3f(rings[h + 1][r].x, rings[h + 1][r].y, rings[h + 1][r].z);
            rlNormal3f(normals[h + 1][nextR].x, normals[h + 1][nextR].y, normals[h + 1][nextR].z);
            rlTexCoord2f(u2, v2);
            rlVertex3f(rings[h + 1][nextR].x, rings[h + 1][nextR].y, rings[h + 1][nextR].z);
        }
    }
    rlEnd();

    rlCheckRenderBatchLimit(RADIAL_SEGS * 3);
    rlBegin(RL_TRIANGLES);
    float finalShift = currentHeight * sinf(tiltAngle);
    Vector3 peak = {pillarPos.x + dirInX * finalShift, pillarPos.y + currentHeight, pillarPos.z + dirInZ * finalShift};

    for (int r = 0; r < RADIAL_SEGS; r++)
    {
        int nextR = (r + 1) % RADIAL_SEGS;
        rlNormal3f(0.0f, 1.0f, 0.0f);
        rlTexCoord2f((float)r / RADIAL_SEGS, 1.0f);
        rlVertex3f(rings[HEIGHT_SEGS][r].x, rings[HEIGHT_SEGS][r].y, rings[HEIGHT_SEGS][r].z);
        rlTexCoord2f((float)nextR / RADIAL_SEGS, 1.0f);
        rlVertex3f(rings[HEIGHT_SEGS][nextR].x, rings[HEIGHT_SEGS][nextR].y, rings[HEIGHT_SEGS][nextR].z);
        rlTexCoord2f(0.5f, 1.0f);
        rlVertex3f(peak.x, peak.y, peak.z);
    }
    rlEnd();
    rlPopMatrix();
#undef HEIGHT_SEGS
#undef RADIAL_SEGS
}

void ProceduralMesh_DrawOrganicPuddle(Vector3 pos, float radius)
{
    int sides = 32;
    float time = GetTime();

    float cosA[33];
    float sinA[33];
    float n[33];
    for (int i = 0; i <= sides; i++)
    {
        float angle = (i / (float)sides) * 2.0f * PI;
        cosA[i] = cosf(angle);
        sinA[i] = sinf(angle);
        n[i] = 1.0f + 0.15f * sinf(angle * 3.0f + time) + 0.1f * cosf(angle * 5.0f - time * 0.5f);
    }

    rlCheckRenderBatchLimit(sides * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(0, 150, 255, 220);

    for (int i = 0; i < sides; i++)
    {
        Vector3 v1 = {pos.x + radius * n[i] * cosA[i], pos.y, pos.z + radius * n[i] * sinA[i]};
        Vector3 v2 = {pos.x + radius * n[i + 1] * cosA[i + 1], pos.y, pos.z + radius * n[i + 1] * sinA[i + 1]};

        rlTexCoord2f(0.5f, 0.5f);
        rlVertex3f(pos.x, pos.y, pos.z);
        rlTexCoord2f(0.5f + 0.5f * n[i + 1] * cosA[i + 1], 0.5f + 0.5f * n[i + 1] * sinA[i + 1]);
        rlVertex3f(v2.x, v2.y, v2.z);
        rlTexCoord2f(0.5f + 0.5f * n[i] * cosA[i], 0.5f + 0.5f * n[i] * sinA[i]);
        rlVertex3f(v1.x, v1.y, v1.z);
    }
    rlEnd();
}