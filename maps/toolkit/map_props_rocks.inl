// Rock props (one mesh/texture set, many placements) — plain-textured or
// prop_lit (maps/toolkit/prop_lit.h) depending on whether normal/roughness
// paths are given. #include'd once from map_props.c — not a standalone
// translation unit.

// --- Frustum culling ----------------------------------------------------
// Rocks are one DrawModelEx each, so a scattered map pays `count` draw calls
// (song_quao: 64 of 157 draws in the frame) even when every rock is behind
// the camera. MapProp_DrawRockSystem therefore tests each placement's bounding
// sphere against the camera frustum (helpers in map_props_cull.inl) and skips
// only the ones fully outside it, so nothing on screen changes.

static inline float Rock_Smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static inline float Rock_Mix(float a, float b, float t)
{
    return a * (1.0f - t) + b * t;
}

// --- Geodesic Icosahedron Mesh Generator (Zero Polar Singularities) ---
typedef struct
{
    unsigned int key;
    int mid;
} RockEdgeMidpoint;

static inline Vector3 Rock_VNorm(Vector3 v)
{
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 1e-6f)
    {
        float inv = 1.0f / l;
        return (Vector3){v.x * inv, v.y * inv, v.z * inv};
    }
    return (Vector3){0.0f, 1.0f, 0.0f};
}

static int Rock_GetMidpoint(int v1, int v2, Vector3 *verts, int *vCount,
                            RockEdgeMidpoint *hashTable, int hashMask)
{
    unsigned int a = (unsigned int)(v1 < v2 ? v1 : v2);
    unsigned int b = (unsigned int)(v1 < v2 ? v2 : v1);
    unsigned int key = (a << 16) | b;
    unsigned int h = (key * 2654435761u) & (unsigned int)hashMask;

    while (hashTable[h].key != 0)
    {
        if (hashTable[h].key == key)
            return hashTable[h].mid;
        h = (h + 1) & (unsigned int)hashMask;
    }

    int midIdx = (*vCount)++;
    Vector3 p1 = verts[a];
    Vector3 p2 = verts[b];
    Vector3 m = {(p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f, (p1.z + p2.z) * 0.5f};
    verts[midIdx] = Rock_VNorm(m);

    hashTable[h].key = key;
    hashTable[h].mid = midIdx;
    return midIdx;
}

static Mesh Nature_GenMeshIcoSphere(float radius, int subdivisions)
{
    Mesh mesh = {0};
    float t = (1.0f + sqrtf(5.0f)) * 0.5f;
    Vector3 baseV[12] = {
        {-1.0f, t, 0.0f}, {1.0f, t, 0.0f}, {-1.0f, -t, 0.0f}, {1.0f, -t, 0.0f}, {0.0f, -1.0f, t}, {0.0f, 1.0f, t}, {0.0f, -1.0f, -t}, {0.0f, 1.0f, -t}, {t, 0.0f, -1.0f}, {t, 0.0f, 1.0f}, {-t, 0.0f, -1.0f}, {-t, 0.0f, 1.0f}};

    int maxV = 12;
    int maxT = 20;
    for (int s = 0; s < subdivisions; s++)
    {
        maxV += (maxT * 3) / 2;
        maxT *= 4;
    }

    Vector3 *verts = (Vector3 *)RL_MALLOC((size_t)maxV * sizeof(Vector3));
    if (!verts)
        return mesh;
    for (int i = 0; i < 12; i++)
        verts[i] = Rock_VNorm(baseV[i]);
    int vCount = 12;

    int (*tris)[3] = (int (*)[3])RL_MALLOC((size_t)maxT * sizeof(int[3]));
    int (*newTris)[3] = (int (*)[3])RL_MALLOC((size_t)maxT * sizeof(int[3]));
    if (!tris || !newTris)
    {
        RL_FREE(verts);
        if (tris)
            RL_FREE(tris);
        if (newTris)
            RL_FREE(newTris);
        return mesh;
    }

    static const int baseTri[20][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    memcpy(tris, baseTri, 20 * sizeof(int[3]));
    int tCount = 20;

    int hashSize = 4096;
    while (hashSize < maxT * 4)
        hashSize *= 2;
    RockEdgeMidpoint *edgeHash = (RockEdgeMidpoint *)RL_CALLOC((size_t)hashSize, sizeof(RockEdgeMidpoint));
    if (!edgeHash)
    {
        RL_FREE(verts);
        RL_FREE(tris);
        RL_FREE(newTris);
        return mesh;
    }

    for (int s = 0; s < subdivisions; s++)
    {
        memset(edgeHash, 0, (size_t)hashSize * sizeof(RockEdgeMidpoint));
        int newTCount = 0;
        for (int i = 0; i < tCount; i++)
        {
            int i1 = tris[i][0];
            int i2 = tris[i][1];
            int i3 = tris[i][2];
            int m12 = Rock_GetMidpoint(i1, i2, verts, &vCount, edgeHash, hashSize - 1);
            int m23 = Rock_GetMidpoint(i2, i3, verts, &vCount, edgeHash, hashSize - 1);
            int m31 = Rock_GetMidpoint(i3, i1, verts, &vCount, edgeHash, hashSize - 1);

            newTris[newTCount][0] = i1;
            newTris[newTCount][1] = m12;
            newTris[newTCount][2] = m31;
            newTCount++;
            newTris[newTCount][0] = i2;
            newTris[newTCount][1] = m23;
            newTris[newTCount][2] = m12;
            newTCount++;
            newTris[newTCount][0] = i3;
            newTris[newTCount][1] = m31;
            newTris[newTCount][2] = m23;
            newTCount++;
            newTris[newTCount][0] = m12;
            newTris[newTCount][1] = m23;
            newTris[newTCount][2] = m31;
            newTCount++;
        }
        memcpy(tris, newTris, (size_t)newTCount * sizeof(int[3]));
        tCount = newTCount;
    }

    int finalVCount = tCount * 3;
    mesh.vertexCount = finalVCount;
    mesh.triangleCount = tCount;
    mesh.vertices = (float *)RL_MALLOC((size_t)finalVCount * 3u * sizeof(float));
    mesh.texcoords = (float *)RL_MALLOC((size_t)finalVCount * 2u * sizeof(float));
    mesh.normals = (float *)RL_MALLOC((size_t)finalVCount * 3u * sizeof(float));

    if (mesh.vertices && mesh.texcoords && mesh.normals)
    {
        for (int t = 0; t < tCount; t++)
        {
            for (int c = 0; c < 3; c++)
            {
                int vi = tris[t][c];
                Vector3 p = verts[vi];
                int outIdx = t * 3 + c;
                mesh.vertices[outIdx * 3 + 0] = p.x * radius;
                mesh.vertices[outIdx * 3 + 1] = p.y * radius;
                mesh.vertices[outIdx * 3 + 2] = p.z * radius;

                mesh.normals[outIdx * 3 + 0] = p.x;
                mesh.normals[outIdx * 3 + 1] = p.y;
                mesh.normals[outIdx * 3 + 2] = p.z;

                float u = atan2f(p.z, p.x) * (0.5f / PI) + 0.5f;
                float v = asinf(fmaxf(-1.0f, fminf(1.0f, p.y))) * (1.0f / PI) + 0.5f;
                mesh.texcoords[outIdx * 2 + 0] = u;
                mesh.texcoords[outIdx * 2 + 1] = v;
            }
        }
    }

    RL_FREE(edgeHash);
    RL_FREE(verts);
    RL_FREE(tris);
    RL_FREE(newTris);
    return mesh;
}

static Mesh Nature_BuildProceduralRockMeshVariant(int rockType, int variantSeed)
{
    int subdivisions = 3; // 1280 triangles, 3840 vertices
    if (rockType == MAP_ROCK_RIVER_STONE || rockType == MAP_ROCK_SCREE)
    {
        subdivisions = 2; // 320 triangles, 960 vertices
    }
    Mesh mesh = Nature_GenMeshIcoSphere(1.0f, subdivisions);

    float *origTexcoords = (float *)RL_MALLOC((size_t)mesh.vertexCount * 2u * sizeof(float));
    if (origTexcoords)
        memcpy(origTexcoords, mesh.texcoords, (size_t)mesh.vertexCount * 2u * sizeof(float));

    const float vPhase = (float)(variantSeed & 3) * 1.5707963f; // 90 deg step per variant
    const float vAsymX = (variantSeed == 1) ? 1.16f : ((variantSeed == 2) ? 0.90f : ((variantSeed == 3) ? 1.08f : 1.0f));
    const float vAsymZ = (variantSeed == 1) ? 0.88f : ((variantSeed == 2) ? 1.15f : ((variantSeed == 3) ? 0.94f : 1.0f));

    // Deep polyhedral cleavage planes for sharp, chiseled granite / karst facets
    static const float kBoulderPlanes[9][4] = {
        {0.78f, 0.48f, 0.39f, 0.52f},
        {-0.72f, 0.52f, 0.46f, 0.50f},
        {0.18f, 0.44f, -0.88f, 0.51f},
        {-0.32f, 0.65f, -0.69f, 0.48f},
        {0.55f, 0.74f, -0.38f, 0.45f},
        {-0.64f, 0.28f, -0.71f, 0.54f},
        {0.12f, 0.92f, 0.37f, 0.44f},
        {0.84f, 0.18f, -0.51f, 0.53f},
        {-0.22f, 0.58f, 0.78f, 0.49f},
    };

    for (int i = 0; i < mesh.vertexCount; i++)
    {
        float x = mesh.vertices[i * 3 + 0];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];

        float horizR = sqrtf(x * x + z * z);
        float angle = atan2f(z, x);
        float cosA = (horizR > 1e-6f) ? (x / horizR) : cosf(angle);
        float sinA = (horizR > 1e-6f) ? (z / horizR) : sinf(angle);
        float heightFactor = (y + 1.0f) * 0.5f;

        float vx = x, vy = y, vz = z;

        if (rockType == MAP_ROCK_CRAG || rockType == MAP_ROCK_LIMESTONE)
        {
            // Karst pinnacle / dissolved limestone needle spire with vertical fluting (rillenkarren)
            float c1 = cosf(3.0f * angle + 0.45f);
            float c2 = sinf(5.0f * angle - 0.85f);
            float c3 = cosf(7.0f * angle + 1.10f);
            float ridge = 1.0f + 0.28f * (c1 >= 0.0f ? sqrtf(c1) : -sqrtf(-c1)) +
                          0.15f * c2 - 0.10f * fabsf(c3);

            // Vertical fluting rain rills (rillenkarren)
            float fluting = 0.14f * cosf(10.0f * angle) - 0.08f * fabsf(sinf(16.0f * angle));

            // Sharp acute apex taper: radius scales with distance from summit
            float distFromTop = (1.05f - y) * 0.5f;
            if (distFromTop < 0.0f)
                distFromTop = 0.0f;
            float taper = (0.22f + 0.78f * powf(distFromTop, 0.68f)) * 1.18f;

            // Chisel knife-edge summit: squash cross-section into an acute blade
            float summitPinch = Rock_Smoothstep(0.35f, 0.98f, y);
            float ridgeStrike = cosf(angle - 0.4f);
            float ridgeChisel = 1.0f - summitPinch * 0.65f * (1.0f - fabsf(ridgeStrike));

            float newR = (0.35f + 0.65f * horizR) * (ridge + fluting) * taper * ridgeChisel;
            vx = cosA * newR * 1.25f;
            vz = sinA * newR * 0.85f;

            // Clip against cleavage planes
            for (int p = 0; p < 9; p++)
            {
                float nx = kBoulderPlanes[p][0], ny = kBoulderPlanes[p][1], nz = kBoulderPlanes[p][2];
                float d = kBoulderPlanes[p][3] * 1.12f;
                float dist = vx * nx + y * ny + vz * nz - d;
                if (dist > 0.0f)
                {
                    vx -= nx * dist * 0.88f;
                    vz -= nz * dist * 0.88f;
                }
            }

            // Towering vertical proportions with stepped karst dissolution terraces
            float strataPhase = y * 16.0f + angle * 1.5f;
            float terrace = 0.07f * (sinf(strataPhase) + 0.35f * sinf(strataPhase * 2.0f));
            float baseWiden = (y < -0.30f) ? (1.0f + (-0.30f - y) * 0.85f) : 1.0f;
            float hf2 = heightFactor * heightFactor;
            float leanX = 0.22f * hf2;
            float leanZ = -0.14f * hf2;

            vx = vx * baseWiden + leanX;
            vy = (y + 0.15f) * 2.65f + terrace;
            vz = vz * baseWiden + leanZ;
        }
        else if (rockType == MAP_ROCK_BASALT)
        {
            // Hexagonal Columnar Basalt jointing (Giant's Causeway / Gành Đá Đĩa style)
            float hexAngle = angle + 0.52359877f; // PI/6
            float modHex = fmodf(hexAngle + 20.0f * PI, PI / 3.0f) - (PI / 6.0f);
            float hexPrism = 1.0f / cosf(modHex);
            if (hexPrism > 1.155f)
                hexPrism = 1.155f;

            float colHeight = 1.15f;
            float colShiftX = 0.0f, colShiftZ = 0.0f;
            if (angle > -0.6f && angle < 1.5f)
            {
                colHeight = 1.25f;
                colShiftX = 0.15f;
                colShiftZ = 0.10f;
            }
            else if (angle >= 1.5f || angle < -2.3f)
            {
                colHeight = 0.95f;
                colShiftX = -0.18f;
                colShiftZ = 0.05f;
            }
            else
            {
                colHeight = 1.05f;
                colShiftX = 0.05f;
                colShiftZ = -0.15f;
            }

            if (y >= 0.65f)
            {
                float rCap = (1.0f - y) * (1.0f / 0.35f);
                vx = cosA * hexPrism * rCap * 1.05f + colShiftX;
                vz = sinA * hexPrism * rCap * 0.95f + colShiftZ;
                vy = colHeight + 0.03f * cosf(6.0f * angle);
            }
            else
            {
                float wallY = (y - 0.65f) * (1.0f / 1.50f) * (colHeight + 0.90f) + colHeight;
                float rWall = hexPrism;
                if (y < -0.85f)
                {
                    float flare = (-0.85f - y) * (1.0f / 0.15f);
                    rWall *= (1.0f + flare * 0.35f);
                }
                float coolingJoint = 0.035f * sinf(wallY * 16.0f);
                vx = cosA * (rWall + coolingJoint) * 1.05f + colShiftX;
                vz = sinA * (rWall + coolingJoint) * 0.95f + colShiftZ;
                vy = wallY;
            }
        }
        else if (rockType == MAP_ROCK_SANDSTONE)
        {
            // Sedimentary Sandstone: horizontal bedding strata terraces + tabular rectangular block profile
            float sqX = copysignf(powf(fabsf(cosA), 0.62f), cosA) * 1.30f;
            float sqZ = copysignf(powf(fabsf(sinA), 0.62f), sinA) * 0.95f;

            if (y >= 0.50f)
            {
                float rCap = (1.0f - y) * 2.0f;
                vx = sqX * (0.35f + 0.65f * rCap);
                vz = sqZ * (0.35f + 0.65f * rCap);
                vy = 0.55f + 0.04f * sinf(sqX * 3.5f);
            }
            else
            {
                float tY = (y + 0.85f) * (3.0f / 1.35f);
                float tIdx = floorf(tY);
                if (tIdx < 0.0f)
                    tIdx = 0.0f;
                if (tIdx > 2.0f)
                    tIdx = 2.0f;
                float tFrac = tY - tIdx;
                float stepY = (tIdx + Rock_Smoothstep(0.12f, 0.88f, tFrac) * 0.88f) * (1.35f / 3.0f) - 0.85f;

                float undercut = 1.0f - 0.12f * (1.0f - fabsf(tFrac - 0.5f) * 2.0f);
                float rWall = undercut;
                if (y < -0.85f)
                {
                    float flare = (-0.85f - y) * (1.0f / 0.15f);
                    rWall *= (1.0f + flare * 0.45f);
                }
                vx = sqX * rWall;
                vz = sqZ * rWall;
                vy = stepY * 0.72f;
            }
        }
        else if (rockType == MAP_ROCK_OBSIDIAN)
        {
            // Obsidian: Vitreous volcanic glass with razor-sharp conchoidal concave shell scoops
            float c1 = cosf(2.0f * angle - 0.4f);
            float c2 = sinf(4.0f * angle + 0.9f);
            float shard = 1.0f + 0.42f * c1 - 0.26f * fabsf(c2);

            float distFromTop = (1.05f - y) * 0.5f;
            if (distFromTop < 0.0f)
                distFromTop = 0.0f;
            float spireTaper = powf(distFromTop, 0.65f) * 1.28f;

            vx = cosA * shard * spireTaper * 1.25f;
            vy = y * 1.45f;
            vz = sinA * shard * spireTaper * 0.85f;

            static const float kScallops[5][4] = {
                {0.45f, 0.40f, 0.35f, 0.62f},
                {-0.50f, 0.30f, -0.30f, 0.58f},
                {0.15f, 0.85f, -0.40f, 0.55f},
                {-0.35f, -0.15f, 0.45f, 0.52f},
                {0.55f, -0.20f, -0.25f, 0.50f},
            };
            for (int s = 0; s < 5; s++)
            {
                float dx = vx - kScallops[s][0];
                float dy = vy - kScallops[s][1];
                float dz = vz - kScallops[s][2];
                float distSq = dx * dx + dy * dy + dz * dz;
                float radius = kScallops[s][3];
                if (distSq > 1e-10f && distSq < radius * radius)
                {
                    float dist = sqrtf(distSq);
                    float invDist = 1.0f / dist;
                    float scoop = (radius - dist) * 0.82f;
                    vx -= (dx * invDist) * scoop;
                    vy -= (dy * invDist) * scoop;
                    vz -= (dz * invDist) * scoop;
                }
            }
        }
        else if (rockType == MAP_ROCK_SLATE)
        {
            // Metamorphic Slate / Shale: thin foliated cleavage plates tilted at 24 degree dip
            const float cosD = 0.9135f, sinD = 0.4067f;
            float rotY = y * cosD - z * sinD;
            float rotZ = y * sinD + z * cosD;

            float sheetY = rotY * 4.0f;
            float sheetIdx = floorf(sheetY + 0.5f);
            float sheetFrac = sheetY + 0.5f - sheetIdx;
            float steppedY = (sheetIdx + Rock_Smoothstep(0.15f, 0.85f, sheetFrac) * 0.40f - 0.5f) * (0.35f / 4.0f);

            vy = steppedY * cosD + rotZ * sinD;
            vz = -steppedY * sinD + rotZ * cosD;
            vx = x * 1.35f + 0.15f * sinf(angle * 5.0f);
        }
        else if (rockType == MAP_ROCK_RIVER_STONE)
        {
            // Asymmetric water-worn hydrodynamic river stone (flattened cobble profile)
            float c1 = cosf(2.0f * angle + 0.35f);
            float c2 = sinf(3.0f * angle - 0.45f);
            float c3 = cosf(4.0f * angle + 1.15f);
            float pebble = 1.0f + 0.18f * c1 + 0.12f * c2 + 0.06f * c3;

            // Elongate along X (river flow axis), slightly compress along Z
            vx = cosA * horizR * pebble * 1.28f;
            vz = sinA * horizR * pebble * 0.92f;

            // Distinctly flattened vertical profile (hydro-flattened river cobble)
            float topArch = 0.70f + 0.30f * (1.0f - fminf(1.0f, horizR * horizR));
            float rawY = y * 0.42f * topArch;
            float strataTerrace = sinf(rawY * 12.0f + vx * 2.5f) * 0.020f;
            vy = rawY + strataTerrace;

            // 5 hydro-smoothed planar bevel facets
            for (int p = 0; p < 5; p++)
            {
                float dist = vx * kBoulderPlanes[p][0] + vy * kBoulderPlanes[p][1] + vz * kBoulderPlanes[p][2] - kBoulderPlanes[p][3] * 0.88f;
                if (dist > 0.0f)
                {
                    float bevel = Rock_Smoothstep(0.0f, 0.22f, dist) * dist * 0.75f;
                    vx -= kBoulderPlanes[p][0] * bevel;
                    vy -= kBoulderPlanes[p][1] * bevel;
                    vz -= kBoulderPlanes[p][2] * bevel;
                }
            }

            // Flatten bottom so stone rests stably on the riverbed
            if (y < -0.45f)
            {
                float baseFlatten = (-0.45f - y) * (1.0f / 0.55f);
                vy = vy * (1.0f - baseFlatten * 0.45f) - 0.05f * baseFlatten;
            }
        }
        else if (rockType == MAP_ROCK_SCREE)
        {
            // Scree / Talus gravel: irregular shattered angular chip
            float chipNoise = 0.38f * cosf(3.0f * angle + vPhase) + 0.28f * sinf(5.0f * angle - vPhase * 0.7f) - 0.22f * fabsf(cosf(7.0f * angle));
            vx = cosA * horizR * (1.0f + chipNoise) * 1.15f * vAsymX;
            vy = y * 0.45f + 0.12f * sinf(x * 8.0f + vPhase);
            vz = sinA * horizR * (1.0f + chipNoise) * 0.82f * vAsymZ;

            for (int p = 0; p < 5; p++)
            {
                float dist = vx * kBoulderPlanes[p][0] + vy * kBoulderPlanes[p][1] + vz * kBoulderPlanes[p][2] - kBoulderPlanes[p][3] * 0.78f;
                if (dist > 0.0f)
                {
                    vx -= kBoulderPlanes[p][0] * dist * 0.94f;
                    vy -= kBoulderPlanes[p][1] * dist * 0.94f;
                    vz -= kBoulderPlanes[p][2] * dist * 0.94f;
                }
            }
        }
        else
        {
            // Ninh Thuan Exfoliated Granite Corestone:
            // Smooth spheroidal superellipsoid + 1-2 gentle sheet joints + shallow lower-flank tafoni
            float warpAngle = angle + 0.22f * sinf(y * 2.4f + vPhase) + vPhase;
            float c1 = cosf(2.0f * warpAngle + 0.35f);
            float c2 = sinf(3.0f * warpAngle - 0.65f);
            float lobe = 0.14f * c1 + 0.09f * c2;

            // Superellipsoid-like spheroidal rounding (fuller shoulders, natural corestone mass)
            float rSph = sqrtf(x * x + y * y + z * z + 1e-6f);
            float sxN = x / rSph, syN = y / rSph, szN = z / rSph;
            float superMix = 0.22f * (powf(fabsf(sxN), 2.4f) + powf(fabsf(syN), 2.4f) + powf(fabsf(szN), 2.4f));
            float rCore = 1.0f + lobe + superMix * 0.35f;

            vx = cosA * horizR * rCore * 1.18f * vAsymX;
            vy = y * 0.86f * (1.0f + 0.08f * c1);
            vz = sinA * horizR * rCore * 0.92f * vAsymZ;

            // Variant 2: Pedestal / neck-pinch wind-scoured corestone (Nui Chua balanced boulder)
            if (variantSeed == 2)
            {
                float neckBand = expf(-((y + 0.28f) * (y + 0.28f)) * 7.5f);
                float pinch = 1.0f - 0.22f * neckBand;
                vx *= pinch;
                vz *= pinch;
            }
            // Variant 3: Split-joint granite block with single tectonic fracture groove
            else if (variantSeed == 3)
            {
                float jointDist = fabsf(vx * 0.86f + vz * 0.50f - 0.05f * sinf(vy * 2.6f));
                if (jointDist < 0.20f && vy > -0.25f)
                {
                    float groove = (1.0f - jointDist / 0.20f) * (vy + 0.25f) * 0.32f;
                    vy -= groove;
                }
            }

            // 3 structural tectonic joint planes + 2 exfoliation sheet-joint planes
            // (replaces pure potato-sphere rounding with authentic Ninh Thuan granite facets)
            static const float kGraniteJoints[5][4] = {
                { 0.24f,  0.92f,  0.30f, 0.70f},
                {-0.62f,  0.42f, -0.66f, 0.72f},
                { 0.78f,  0.22f, -0.58f, 0.76f},
                {-0.45f, -0.18f,  0.87f, 0.75f},
                { 0.14f,  0.55f, -0.82f, 0.74f},
            };
            for (int p = 0; p < 5; p++)
            {
                float nx = kGraniteJoints[p][0], ny = kGraniteJoints[p][1], nz = kGraniteJoints[p][2];
                float d = kGraniteJoints[p][3];
                float dist = vx * nx + vy * ny + vz * nz - d;
                if (dist > -0.06f)
                {
                    float u = dist + 0.06f;
                    float softDist = 0.5f * (u + sqrtf(u * u + (p < 2 ? 0.018f : 0.008f)));
                    float strength = (p < 2) ? 0.56f : 0.44f;
                    vx -= nx * softDist * strength;
                    vy -= ny * softDist * strength;
                    vz -= nz * softDist * strength;
                }
            }

            // Subtle spheroidal exfoliation onion-skin shell step
            float shellCoord = (vy + 0.85f) * 3.4f + 0.35f * sinf(vx * 2.4f + vz * 2.1f + vPhase);
            float shellIdx = floorf(shellCoord);
            float shellFrac = shellCoord - shellIdx;
            float exfoliationStep = 0.028f * (Rock_Smoothstep(0.10f, 0.32f, shellFrac) - 0.5f);
            float rLen = sqrtf(vx * vx + vz * vz + 1e-6f);
            vx += (vx / rLen) * exfoliationStep;
            vz += (vz / rLen) * exfoliationStep;

            // Wind-eroded alveolar tafoni pockets on lower windward flank
            float tafoniWave = sinf(vx * 4.2f + vPhase) * cosf(vz * 4.0f - vPhase) * sinf(vy * 4.8f);
            if (tafoniWave > 0.36f && vy < 0.25f && vy > -0.45f)
            {
                float scoop = (tafoniWave - 0.36f) * 0.24f;
                vx *= (1.0f - scoop);
                vz *= (1.0f - scoop);
            }

            if (vy < -0.22f)
            {
                float tuck = (-0.22f - vy);
                vx *= (1.0f - tuck * 0.12f);
                vz *= (1.0f - tuck * 0.12f);
            }
        }

        mesh.vertices[i * 3 + 0] = vx;
        mesh.vertices[i * 3 + 1] = vy;
        mesh.vertices[i * 3 + 2] = vz;

        float uOrig = origTexcoords ? origTexcoords[i * 2 + 0] : mesh.texcoords[i * 2 + 0];
        float vOrig = origTexcoords ? origTexcoords[i * 2 + 1] : mesh.texcoords[i * 2 + 1];

        if (rockType == MAP_ROCK_SANDSTONE)
        {
            if (y >= 0.50f)
            {
                mesh.texcoords[i * 2 + 0] = vx * 0.85f + 0.5f;
                mesh.texcoords[i * 2 + 1] = vz * 0.85f + 0.5f;
            }
            else
            {
                mesh.texcoords[i * 2 + 0] = uOrig * 3.2f;
                mesh.texcoords[i * 2 + 1] = (vy + 0.85f) * 1.8f;
            }
        }
        else if (rockType == MAP_ROCK_RIVER_STONE)
        {
            mesh.texcoords[i * 2 + 0] = uOrig * 2.2f;
            mesh.texcoords[i * 2 + 1] = vOrig * 1.8f;
        }
        else if (rockType == MAP_ROCK_BASALT)
        {
            if (y >= 0.65f)
            {
                mesh.texcoords[i * 2 + 0] = vx * 0.85f + 0.5f;
                mesh.texcoords[i * 2 + 1] = vz * 0.85f + 0.5f;
            }
            else
            {
                mesh.texcoords[i * 2 + 0] = uOrig * 6.0f;
                mesh.texcoords[i * 2 + 1] = (vy + 0.85f) * 1.5f;
            }
        }
        else if (rockType == MAP_ROCK_SCREE)
        {
            mesh.texcoords[i * 2 + 0] = uOrig * 1.5f;
            mesh.texcoords[i * 2 + 1] = vOrig * 1.5f;
        }
        else
        {
            mesh.texcoords[i * 2 + 0] = uOrig * 2.8f;
            mesh.texcoords[i * 2 + 1] = vOrig * 2.2f;
        }
    }
    if (origTexcoords)
        RL_FREE(origTexcoords);

    // Recompute normals from actual deformed triangle geometry (unindexed & indexed)
    memset(mesh.normals, 0, (size_t)mesh.vertexCount * 3u * sizeof(float));
    bool flatFaceted = (rockType == MAP_ROCK_BASALT || rockType == MAP_ROCK_SANDSTONE ||
                        rockType == MAP_ROCK_OBSIDIAN || rockType == MAP_ROCK_SLATE);

    float cosCreaseThreshold = 0.819f; // ~35 deg for chiseled boulders
    if (rockType == MAP_ROCK_GRANITE)
    {
        cosCreaseThreshold = 0.743f; // ~42 deg: keeps structural joint facets crisp while rounding shoulders
    }
    else if (rockType == MAP_ROCK_RIVER_STONE)
    {
        cosCreaseThreshold = 0.500f; // ~60 deg for water-smoothed river stones
    }
    else if (rockType == MAP_ROCK_CRAG || rockType == MAP_ROCK_LIMESTONE)
    {
        cosCreaseThreshold = 0.707f; // ~45 deg for fluted karst crags
    }

    Vector3 *faceNormals = (Vector3 *)RL_MALLOC((size_t)mesh.triangleCount * sizeof(Vector3));
    if (faceNormals)
    {
        for (int t = 0; t < mesh.triangleCount; t++)
        {
            int i0 = mesh.indices ? (int)mesh.indices[t * 3 + 0] : (t * 3 + 0);
            int i1 = mesh.indices ? (int)mesh.indices[t * 3 + 1] : (t * 3 + 1);
            int i2 = mesh.indices ? (int)mesh.indices[t * 3 + 2] : (t * 3 + 2);

            Vector3 v0 = {mesh.vertices[i0 * 3 + 0], mesh.vertices[i0 * 3 + 1], mesh.vertices[i0 * 3 + 2]};
            Vector3 v1 = {mesh.vertices[i1 * 3 + 0], mesh.vertices[i1 * 3 + 1], mesh.vertices[i1 * 3 + 2]};
            Vector3 v2 = {mesh.vertices[i2 * 3 + 0], mesh.vertices[i2 * 3 + 1], mesh.vertices[i2 * 3 + 2]};

            Vector3 e1 = Vector3Subtract(v1, v0);
            Vector3 e2 = Vector3Subtract(v2, v0);
            Vector3 n = Vector3CrossProduct(e1, e2);
            float nLenSq = n.x * n.x + n.y * n.y + n.z * n.z;
            if (nLenSq > 1e-10f)
            {
                float invLen = 1.0f / sqrtf(nLenSq);
                faceNormals[t] = (Vector3){n.x * invLen, n.y * invLen, n.z * invLen};
            }
            else
            {
                faceNormals[t] = (Vector3){0.0f, 1.0f, 0.0f};
            }
        }
    }

    for (int t = 0; t < mesh.triangleCount; t++)
    {
        int i0 = mesh.indices ? (int)mesh.indices[t * 3 + 0] : (t * 3 + 0);
        int i1 = mesh.indices ? (int)mesh.indices[t * 3 + 1] : (t * 3 + 1);
        int i2 = mesh.indices ? (int)mesh.indices[t * 3 + 2] : (t * 3 + 2);

        Vector3 fn = faceNormals ? faceNormals[t] : (Vector3){0.0f, 1.0f, 0.0f};

        if (flatFaceted)
        {
            mesh.normals[i0 * 3 + 0] = fn.x;
            mesh.normals[i0 * 3 + 1] = fn.y;
            mesh.normals[i0 * 3 + 2] = fn.z;
            mesh.normals[i1 * 3 + 0] = fn.x;
            mesh.normals[i1 * 3 + 1] = fn.y;
            mesh.normals[i1 * 3 + 2] = fn.z;
            mesh.normals[i2 * 3 + 0] = fn.x;
            mesh.normals[i2 * 3 + 1] = fn.y;
            mesh.normals[i2 * 3 + 2] = fn.z;
        }
        else
        {
            mesh.normals[i0 * 3 + 0] += fn.x;
            mesh.normals[i0 * 3 + 1] += fn.y;
            mesh.normals[i0 * 3 + 2] += fn.z;
            mesh.normals[i1 * 3 + 0] += fn.x;
            mesh.normals[i1 * 3 + 1] += fn.y;
            mesh.normals[i1 * 3 + 2] += fn.z;
            mesh.normals[i2 * 3 + 0] += fn.x;
            mesh.normals[i2 * 3 + 1] += fn.y;
            mesh.normals[i2 * 3 + 2] += fn.z;
        }
    }

    if (!flatFaceted)
    {
        // Crease-aware spatial normal welding preserving sharp geological cleavage planes
        float *welded = (float *)RL_MALLOC((size_t)mesh.vertexCount * 3u * sizeof(float));
        int *nextVert = (int *)RL_MALLOC((size_t)mesh.vertexCount * sizeof(int));
        int *vertBucket = (int *)RL_MALLOC((size_t)mesh.vertexCount * sizeof(int));
        if (welded && nextVert && vertBucket)
        {
            int bucketHeads[2048];
            for (int b = 0; b < 2048; b++)
                bucketHeads[b] = -1;
            for (int i = 0; i < mesh.vertexCount; i++)
            {
                int qx = (int)roundf(mesh.vertices[i * 3 + 0] * 128.0f);
                int qy = (int)roundf(mesh.vertices[i * 3 + 1] * 128.0f);
                int qz = (int)roundf(mesh.vertices[i * 3 + 2] * 128.0f);
                unsigned int h = ((unsigned int)qx * 73856093u) ^
                                 ((unsigned int)qy * 19349663u) ^
                                 ((unsigned int)qz * 83492791u);
                int b = (int)(h & 2047u);
                vertBucket[i] = b;
                nextVert[i] = bucketHeads[b];
                bucketHeads[b] = i;
            }
            for (int i = 0; i < mesh.vertexCount; i++)
            {
                float vx = mesh.vertices[i * 3 + 0];
                float vy = mesh.vertices[i * 3 + 1];
                float vz = mesh.vertices[i * 3 + 2];
                int b = vertBucket[i];
                int ti = i / 3;
                Vector3 ni = (faceNormals && ti < mesh.triangleCount) ? faceNormals[ti] : (Vector3){0.0f, 1.0f, 0.0f};

                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                for (int j = bucketHeads[b]; j != -1; j = nextVert[j])
                {
                    float dx = mesh.vertices[j * 3 + 0] - vx;
                    float dy = mesh.vertices[j * 3 + 1] - vy;
                    float dz = mesh.vertices[j * 3 + 2] - vz;
                    if (dx * dx + dy * dy + dz * dz < 0.0004f)
                    {
                        int tj = j / 3;
                        Vector3 nj = (faceNormals && tj < mesh.triangleCount) ? faceNormals[tj] : (Vector3){0.0f, 1.0f, 0.0f};
                        float dotN = ni.x * nj.x + ni.y * nj.y + ni.z * nj.z;
                        if (dotN >= cosCreaseThreshold)
                        {
                            sx += nj.x;
                            sy += nj.y;
                            sz += nj.z;
                        }
                    }
                }
                if (sx * sx + sy * sy + sz * sz < 1e-6f)
                {
                    sx = ni.x;
                    sy = ni.y;
                    sz = ni.z;
                }
                welded[i * 3 + 0] = sx;
                welded[i * 3 + 1] = sy;
                welded[i * 3 + 2] = sz;
            }
            memcpy(mesh.normals, welded, (size_t)mesh.vertexCount * 3u * sizeof(float));
        }
        if (vertBucket)
            RL_FREE(vertBucket);
        if (nextVert)
            RL_FREE(nextVert);
        if (welded)
            RL_FREE(welded);

        for (int i = 0; i < mesh.vertexCount; i++)
        {
            float nx = mesh.normals[i * 3 + 0];
            float ny = mesh.normals[i * 3 + 1];
            float nz = mesh.normals[i * 3 + 2];
            float lenSq = nx * nx + ny * ny + nz * nz;
            if (lenSq > 1e-8f)
            {
                float invLen = 1.0f / sqrtf(lenSq);
                mesh.normals[i * 3 + 0] = nx * invLen;
                mesh.normals[i * 3 + 1] = ny * invLen;
                mesh.normals[i * 3 + 2] = nz * invLen;
            }
            else
            {
                mesh.normals[i * 3 + 0] = 0.0f;
                mesh.normals[i * 3 + 1] = 1.0f;
                mesh.normals[i * 3 + 2] = 0.0f;
            }
        }
    }
    if (faceNormals)
        RL_FREE(faceNormals);

    // Compute robust tangents so normal mapping functions seamlessly on unindexed meshes
    if (mesh.tangents == NULL)
    {
        mesh.tangents = (float *)RL_MALLOC((size_t)mesh.vertexCount * 4u * sizeof(float));
    }
    if (mesh.tangents != NULL)
    {
        memset(mesh.tangents, 0, (size_t)mesh.vertexCount * 4u * sizeof(float));

        for (int t = 0; t < mesh.triangleCount; t++)
        {
            int i0 = mesh.indices ? (int)mesh.indices[t * 3 + 0] : (t * 3 + 0);
            int i1 = mesh.indices ? (int)mesh.indices[t * 3 + 1] : (t * 3 + 1);
            int i2 = mesh.indices ? (int)mesh.indices[t * 3 + 2] : (t * 3 + 2);

            Vector3 v0 = {mesh.vertices[i0 * 3 + 0], mesh.vertices[i0 * 3 + 1], mesh.vertices[i0 * 3 + 2]};
            Vector3 v1 = {mesh.vertices[i1 * 3 + 0], mesh.vertices[i1 * 3 + 1], mesh.vertices[i1 * 3 + 2]};
            Vector3 v2 = {mesh.vertices[i2 * 3 + 0], mesh.vertices[i2 * 3 + 1], mesh.vertices[i2 * 3 + 2]};
            Vector2 uv0 = {mesh.texcoords[i0 * 2 + 0], mesh.texcoords[i0 * 2 + 1]};
            Vector2 uv1 = {mesh.texcoords[i1 * 2 + 0], mesh.texcoords[i1 * 2 + 1]};
            Vector2 uv2 = {mesh.texcoords[i2 * 2 + 0], mesh.texcoords[i2 * 2 + 1]};

            Vector3 e1 = Vector3Subtract(v1, v0);
            Vector3 e2 = Vector3Subtract(v2, v0);
            float du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
            float du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;
            float det = du1 * dv2 - du2 * dv1;
            float invDet = (fabsf(det) > 1e-6f) ? (1.0f / det) : 0.0f;
            Vector3 tangent = {
                (dv2 * e1.x - dv1 * e2.x) * invDet,
                (dv2 * e1.y - dv1 * e2.y) * invDet,
                (dv2 * e1.z - dv1 * e2.z) * invDet};
            int triVerts[3] = {i0, i1, i2};
            for (int k = 0; k < 3; k++)
            {
                int vi = triVerts[k] * 4;
                mesh.tangents[vi + 0] += tangent.x;
                mesh.tangents[vi + 1] += tangent.y;
                mesh.tangents[vi + 2] += tangent.z;
                mesh.tangents[vi + 3] = 1.0f;
            }
        }

        for (int i = 0; i < mesh.vertexCount; i++)
        {
            Vector3 n = {mesh.normals[i * 3 + 0], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]};
            Vector3 t = {mesh.tangents[i * 4 + 0], mesh.tangents[i * 4 + 1], mesh.tangents[i * 4 + 2]};
            float dotNT = Vector3DotProduct(n, t);
            t.x -= n.x * dotNT;
            t.y -= n.y * dotNT;
            t.z -= n.z * dotNT;
            float tLenSq = t.x * t.x + t.y * t.y + t.z * t.z;
            if (tLenSq > 1e-10f)
            {
                float invTLen = 1.0f / sqrtf(tLenSq);
                mesh.tangents[i * 4 + 0] = t.x * invTLen;
                mesh.tangents[i * 4 + 1] = t.y * invTLen;
                mesh.tangents[i * 4 + 2] = t.z * invTLen;
            }
            else
            {
                Vector3 refAxis = (fabsf(n.y) < 0.99f) ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
                Vector3 fallbackT = Vector3CrossProduct(refAxis, n);
                float fLen = Vector3Length(fallbackT);
                float invFLen = (fLen > 1e-6f) ? (1.0f / fLen) : 1.0f;
                mesh.tangents[i * 4 + 0] = fallbackT.x * invFLen;
                mesh.tangents[i * 4 + 1] = fallbackT.y * invFLen;
                mesh.tangents[i * 4 + 2] = fallbackT.z * invFLen;
            }
            mesh.tangents[i * 4 + 3] = 1.0f;
        }
    }

    // Re-upload deformed procedural mesh (with its new vertices, UVs, normals, and tangents) to GPU VRAM
    if (mesh.vaoId > 0)
    {
        rlUnloadVertexArray(mesh.vaoId);
        mesh.vaoId = 0;
    }
    if (mesh.vboId != NULL)
    {
        for (int i = 0; i < 7; i++)
        {
            if (mesh.vboId[i] > 0)
                rlUnloadVertexBuffer(mesh.vboId[i]);
        }
        RL_FREE(mesh.vboId);
        mesh.vboId = NULL;
    }
    UploadMesh(&mesh, false);

    return mesh;
}

static MapRockSet Rock_CreateTypedSetVariant(int rockType, int variantSeed, const char *diffusePath,
                                             const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMeshVariant(rockType, variantSeed);
    MapProp_MeshBounds(&mesh, &rocks.boundsCenter, &rocks.boundsRadius);
    float maxRXZ = 0.0f;
    float maxY = 0.0f;
    if (mesh.vertices && mesh.vertexCount > 0)
    {
        for (int v = 0; v < mesh.vertexCount; v++)
        {
            float vx = mesh.vertices[v * 3 + 0];
            float vy = mesh.vertices[v * 3 + 1];
            float vz = mesh.vertices[v * 3 + 2];
            float rXZ = sqrtf(vx * vx + vz * vz);
            if (rXZ > maxRXZ)
                maxRXZ = rXZ;
            if (vy > maxY)
                maxY = vy;
        }
    }
    rocks.localRadiusXZ = (maxRXZ > 0.05f) ? maxRXZ : 0.85f;
    rocks.localMaxY = (maxY > 0.05f) ? maxY : 0.90f;
    // Fold boundsCenter offset length into boundsRadius once at init so per-frame culling needs no sqrtf
    float centerLen = Vector3Length(rocks.boundsCenter);
    rocks.boundsRadius += centerLen;
    rocks.boundsCenter = (Vector3){0.0f, 0.0f, 0.0f};

    rocks.model = LoadModelFromMesh(mesh);
    if (normalPath && roughnessPath)
    {
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);
        Texture2D normal = MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT);
        Texture2D roughness = MapLoadMippedTexture(roughnessPath, TEXTURE_WRAP_REPEAT);
        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);
        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

static MapRockSet Rock_CreateTypedSet(int rockType, const char *diffusePath,
                                      const char *normalPath, const char *roughnessPath)
{
    return Rock_CreateTypedSetVariant(rockType, 0, diffusePath, normalPath, roughnessPath);
}

MapRockSet MapProp_CreateRocks(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    return Rock_CreateTypedSet(0, diffusePath, normalPath, roughnessPath);
}

MapRockSet MapProp_CreateMountainCrags(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    return Rock_CreateTypedSet(1, diffusePath, normalPath, roughnessPath);
}

MapRockSet MapProp_CreateRiverStones(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    return Rock_CreateTypedSet(2, diffusePath, normalPath, roughnessPath);
}

// World-space bounding sphere of one placement. Encloses the mesh
// DrawModelEx will draw at `pos` with `scale`; `pad` widens it for anything
// drawn around the rock (fake blob shadows) that must not disappear with it.
static inline void Rock_PlacementBounds(const MapRockSet *set, Vector3 pos,
                                        Vector3 scale, float pad,
                                        Vector3 *outCenter, float *outRadius)
{
    float maxScale = fmaxf(scale.x, fmaxf(scale.y, scale.z));
    float centerOffset = (set->boundsCenter.x == 0.0f &&
                          set->boundsCenter.y == 0.0f &&
                          set->boundsCenter.z == 0.0f)
                             ? 0.0f
                             : Vector3Length(set->boundsCenter);
    *outCenter = pos;
    *outRadius = maxScale * (set->boundsRadius + centerOffset) + pad;
}

void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count, bool drawShadow)
{
    if (!rocks || !rocks->ready || !placements || count <= 0)
        return;

    MapPropViewFrustum frustum;
    bool cull = MapProp_BuildViewFrustum(&frustum);
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < count; i++)
    {
        const MapRockPlacement *p = &placements[i];

        Vector3 pos = p->position;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};

        if (cull)
        {
            Vector3 bc;
            float br;
            Rock_PlacementBounds(rocks, pos, scale,
                                 drawShadow ? 2.0f * p->radiusScale : 0.0f, &bc, &br);
            if (MapProp_FrustumCullsSphere(&frustum, bc, br))
                continue;
        }

        if (drawShadow)
        {
            float waterSurfaceY = 0.0f, waterDepth = 0.0f;
            bool inWater = MapManager_GetWaterInfoAt(p->position.x, p->position.z, &waterSurfaceY, &waterDepth);
            if (!inWater)
            {
                Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                            p->radiusScale * 2.0f, p->heightScale * 2.0f);
            }
        }

        DrawModelEx(rocks->model, pos, rotAxis, p->rotationDeg, scale, WHITE);
    }
}

void MapProp_DrawRockShadowCasters(MapRockSet *rocks,
                                   const MapRockPlacement *placements, int count,
                                   Shader depthShader)
{
    if (!rocks || !rocks->ready || !placements || count <= 0 ||
        rocks->model.materialCount < 1)
        return;
    Shader previous = rocks->model.materials[0].shader;
    rocks->model.materials[0].shader = depthShader;
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < count; i++)
    {
        const MapRockPlacement *p = &placements[i];
        Vector3 pos = p->position;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
        DrawModelEx(rocks->model, pos, rotAxis, p->rotationDeg, scale, WHITE);
    }
    rlDrawRenderBatchActive();
    rocks->model.materials[0].shader = previous;
}

void MapProp_UnloadRocks(MapRockSet *rocks)
{
    if (!rocks || !rocks->ready)
        return;
    // The environment owns the cloud field; this material only borrows it.
    for (int i = 0; i < rocks->model.materialCount; i++)
        rocks->model.materials[i].maps[MATERIAL_MAP_METALNESS].texture = (Texture2D){0};
    UnloadModel(rocks->model);
    rocks->ready = false;
}

// --- Mountain ring (border rocks for the floating-island motif) ---------

int MapProp_GenerateMountainRing(MapRockPlacement *outPlacements, int maxCount,
                                 float mapWidth, float mapDepth,
                                 float minRadiusScale, float maxRadiusScale,
                                 float minHeightScale, float maxHeightScale,
                                 unsigned int seed)
{
    if (!outPlacements || maxCount <= 0)
        return 0;

    SetRandomSeed(seed);

    float perimeter = 2.0f * (mapWidth + mapDepth);
    float centerX = mapWidth * 0.5f;
    float centerZ = mapDepth * 0.5f;

    for (int i = 0; i < maxCount; i++)
    {
        float t = ((float)i / (float)maxCount) * perimeter;
        float x, z;
        if (t < mapWidth)
        {
            x = t;
            z = 0.0f;
        }
        else if (t < mapWidth + mapDepth)
        {
            x = mapWidth;
            z = t - mapWidth;
        }
        else if (t < 2.0f * mapWidth + mapDepth)
        {
            x = mapWidth - (t - mapWidth - mapDepth);
            z = mapDepth;
        }
        else
        {
            x = 0.0f;
            z = mapDepth - (t - 2.0f * mapWidth - mapDepth);
        }

        float outward = (float)GetRandomValue(-100, 200) * 0.01f;
        float tangent = (float)GetRandomValue(-150, 150) * 0.01f;
        float dx = x - centerX, dz = z - centerZ;
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 0.001f)
        {
            float invLen = 1.0f / len;
            dx *= invLen;
            dz *= invLen;
        }
        x += dx * outward + (-dz) * tangent;
        z += dz * outward + (dx)*tangent;

        outPlacements[i].position = (Vector3){x, 0.0f, z};
        outPlacements[i].radiusScale = (float)GetRandomValue((int)(minRadiusScale * 100.0f), (int)(maxRadiusScale * 100.0f)) * 0.01f;
        outPlacements[i].heightScale = (float)GetRandomValue((int)(minHeightScale * 100.0f), (int)(maxHeightScale * 100.0f)) * 0.01f;
        outPlacements[i].rotationDeg = (float)GetRandomValue(0, 359);
    }

    return maxCount;
}

// --- Unified Rock & Crag System -----------------------------------------

MapRockSystem MapProp_CreateRockSystem(const char *diffusePath, const char *normalPath, const char *roughnessPath, int capacity)
{
    MapRockSystem sys = {0};

    static const char *kArchDiffuse[MAP_ROCK_ARCHETYPE_COUNT] = {
        "assets/textures/rocks/granite_diffuse.png",
        "assets/textures/rocks/limestone_diffuse.png",
        "assets/textures/rocks/river_stone_diffuse.png",
        "assets/textures/rocks/granite_diffuse.png",
        "assets/textures/rocks/basalt_diffuse.png",
        "assets/textures/rocks/sandstone_diffuse.png",
        "assets/textures/rocks/limestone_diffuse.png",
        "assets/textures/rocks/obsidian_diffuse.png",
        "assets/textures/rocks/slate_diffuse.png",
        "assets/textures/rocks/scree_diffuse.png",
    };
    static const char *kArchNormal[MAP_ROCK_ARCHETYPE_COUNT] = {
        "assets/textures/rocks/granite_normal.png",
        "assets/textures/rocks/limestone_normal.png",
        "assets/textures/rocks/river_stone_normal.png",
        "assets/textures/rocks/granite_normal.png",
        "assets/textures/rocks/basalt_normal.png",
        "assets/textures/rocks/sandstone_normal.png",
        "assets/textures/rocks/limestone_normal.png",
        "assets/textures/rocks/obsidian_normal.png",
        "assets/textures/rocks/slate_normal.png",
        "assets/textures/rocks/scree_normal.png",
    };
    static const char *kArchRoughness[MAP_ROCK_ARCHETYPE_COUNT] = {
        "assets/textures/rocks/granite_roughness.png",
        "assets/textures/rocks/limestone_roughness.png",
        "assets/textures/rocks/river_stone_roughness.png",
        "assets/textures/rocks/granite_roughness.png",
        "assets/textures/rocks/basalt_roughness.png",
        "assets/textures/rocks/sandstone_roughness.png",
        "assets/textures/rocks/limestone_roughness.png",
        "assets/textures/rocks/obsidian_roughness.png",
        "assets/textures/rocks/slate_roughness.png",
        "assets/textures/rocks/scree_roughness.png",
    };

    for (int a = 0; a < MAP_ROCK_ARCHETYPE_COUNT; a++)
    {
        const char *dPath = (FileExists(kArchDiffuse[a])) ? kArchDiffuse[a] : diffusePath;
        const char *nPath = (normalPath && FileExists(kArchNormal[a])) ? kArchNormal[a] : normalPath;
        const char *rPath = (roughnessPath && FileExists(kArchRoughness[a])) ? kArchRoughness[a] : roughnessPath;

        sys.models[a] = Rock_CreateTypedSetVariant(a, 0, dPath, nPath, rPath);
        for (int v = 0; v < MAP_ROCK_VARIANTS_PER_ARCHETYPE - 1; v++)
        {
            sys.variants[a][v] = Rock_CreateTypedSetVariant(a, v + 1, dPath, nPath, rPath);
        }
    }

    // Aliases for backwards compatibility
    sys.boulders = sys.models[MAP_ROCK_BOULDER];
    sys.crags = sys.models[MAP_ROCK_CRAG];
    sys.riverStones = sys.models[MAP_ROCK_RIVER_STONE];

    if (capacity > 0)
    {
        sys.items = (MapRockItem *)RL_MALLOC((size_t)capacity * sizeof(MapRockItem));
        sys.capacity = sys.items ? capacity : 0;
    }
    sys.count = 0;
    sys.ready = true;
    return sys;
}

bool MapProp_AddRockItem(MapRockSystem *sys, Vector3 pos, float rScale, float hScale, float rotDeg, MapRockArchetype archetype)
{
    if (!sys || !sys->ready || !sys->items || sys->count >= sys->capacity)
        return false;
    sys->items[sys->count++] = (MapRockItem){
        .position = pos,
        .radiusScale = rScale,
        .heightScale = hScale,
        .rotationDeg = rotDeg,
        .archetype = archetype};
    return true;
}

int MapProp_AddRockCluster(MapRockSystem *sys, Vector3 center, MapRockArchetype archetype,
                           int count, float clusterRadius, float baseScale, unsigned int seed)
{
    if (!sys || count <= 0)
        return 0;
    unsigned int rng = seed ? seed : 0x7a3b19u;
    int added = 0;

    // Primary anchor stone
    float anchorRot = Nature_Random01(&rng) * 360.0f;
    float anchorH = (archetype == MAP_ROCK_CRAG || archetype == MAP_ROCK_LIMESTONE)
                        ? (baseScale * (1.6f + 0.6f * Nature_Random01(&rng)))
                        : (baseScale * (0.65f + 0.25f * Nature_Random01(&rng)));
    if (MapProp_AddRockItem(sys, center, baseScale, anchorH, anchorRot, archetype))
        added++;

    // Companion stones orbiting anchor
    for (int i = 1; i < count; i++)
    {
        float angle = Nature_Random01(&rng) * 2.0f * PI;
        float dist = clusterRadius * (0.35f + 0.65f * Nature_Random01(&rng));
        float sinA, cosA;
        Nature_FastSinCos(angle, &sinA, &cosA);
        Vector3 compPos = {center.x + cosA * dist, center.y, center.z + sinA * dist};
        float companionScale = baseScale * (0.35f + 0.45f * Nature_Random01(&rng));
        float compRot = anchorRot + (Nature_Random01(&rng) - 0.5f) * 60.0f;
        float compH = (archetype == MAP_ROCK_CRAG || archetype == MAP_ROCK_LIMESTONE)
                          ? (companionScale * (1.4f + 0.5f * Nature_Random01(&rng)))
                          : (companionScale * (0.55f + 0.30f * Nature_Random01(&rng)));
        if (MapProp_AddRockItem(sys, compPos, companionScale, compH, compRot, archetype))
            added++;
    }
    return added;
}

static inline int Rock_HashVariant(int idx, float x, float z)
{
    unsigned int h = (unsigned int)idx * 2654435761u ^
                     (unsigned int)((int)(x * 13.0f) * 73856093) ^
                     (unsigned int)((int)(z * 17.0f) * 19349663);
    return (int)(h & 3u);
}

static inline const MapRockSet *Rock_ResolveArchetypeSet(const MapRockSystem *sys, MapRockArchetype archetype, int variant)
{
    if ((int)archetype >= 0 && archetype < MAP_ROCK_ARCHETYPE_COUNT)
    {
        if (variant > 0 && variant < MAP_ROCK_VARIANTS_PER_ARCHETYPE && sys->variants[archetype][variant - 1].ready)
            return &sys->variants[archetype][variant - 1];
        if (sys->models[archetype].ready)
            return &sys->models[archetype];
    }
    if (archetype == MAP_ROCK_CRAG)
        return &sys->crags;
    if (archetype == MAP_ROCK_RIVER_STONE)
        return &sys->riverStones;
    return &sys->boulders;
}

void MapProp_DrawRockSystem(const MapRockSystem *sys, bool drawShadows)
{
    if (!sys || !sys->ready || !sys->items || sys->count <= 0)
        return;

    PropLit_UpdateLighting();
    MapPropViewFrustum frustum;
    bool cull = MapProp_BuildViewFrustum(&frustum);
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < sys->count; i++)
    {
        const MapRockItem *p = &sys->items[i];
        int variant = Rock_HashVariant(i, p->position.x, p->position.z);
        const MapRockSet *set = Rock_ResolveArchetypeSet(sys, p->archetype, variant);
        if (!set->ready)
            continue;

        Vector3 pos = p->position;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
        bool canCastFakeShadow = drawShadows &&
                                 p->archetype != MAP_ROCK_CRAG &&
                                 p->archetype != MAP_ROCK_LIMESTONE;

        if (cull)
        {
            float shadowPad = canCastFakeShadow ? (2.0f * p->radiusScale) : 0.0f;
            Vector3 bc;
            float br;
            Rock_PlacementBounds(set, pos, scale, shadowPad, &bc, &br);
            if (MapProp_FrustumCullsSphere(&frustum, bc, br))
                continue;
        }

        if (canCastFakeShadow)
        {
            float waterSurfaceY = 0.0f, waterDepth = 0.0f;
            bool inWater = MapManager_GetWaterInfoAt(p->position.x, p->position.z, &waterSurfaceY, &waterDepth);
            if (!inWater)
            {
                Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                            p->radiusScale * 2.0f, p->heightScale * 2.0f);
            }
        }

        // [VISUAL UPGRADE]: Sắc độ đá ấm bắt nắng chiều vàng (không bị sậm đen trên đồi cỏ)
        Color rockTint = (Color){184, 176, 166, 255};
        if (p->archetype == MAP_ROCK_SANDSTONE)
        {
            rockTint = (Color){198, 180, 148, 255};
        }
        else if (p->archetype == MAP_ROCK_RIVER_STONE)
        {
            rockTint = (Color){192, 185, 175, 250};
        }
        else if (p->archetype == MAP_ROCK_SCREE)
        {
            rockTint = (Color){168, 160, 150, 255};
        }
        else if (p->archetype == MAP_ROCK_LIMESTONE || p->archetype == MAP_ROCK_CRAG)
        {
            rockTint = (Color){196, 192, 184, 255};
        }

        if (set->model.materialCount > 0)
            MapShadow_AttachMaterial(&set->model.materials[0]);
        DrawModelEx(set->model, pos, rotAxis, p->rotationDeg, scale, rockTint);
    }
}

void MapProp_DrawRockSystemShadowCasters(MapRockSystem *sys, Shader depthShader)
{
    if (!sys || !sys->ready || !sys->items || sys->count <= 0)
        return;

    Shader prevShaders[MAP_ROCK_ARCHETYPE_COUNT] = {0};
    Shader prevVariantShaders[MAP_ROCK_ARCHETYPE_COUNT][MAP_ROCK_VARIANTS_PER_ARCHETYPE - 1] = {{0}};
    for (int a = 0; a < MAP_ROCK_ARCHETYPE_COUNT; a++)
    {
        if (sys->models[a].ready && sys->models[a].model.materialCount > 0)
        {
            prevShaders[a] = sys->models[a].model.materials[0].shader;
            sys->models[a].model.materials[0].shader = depthShader;
        }
        for (int v = 0; v < MAP_ROCK_VARIANTS_PER_ARCHETYPE - 1; v++)
        {
            if (sys->variants[a][v].ready && sys->variants[a][v].model.materialCount > 0)
            {
                prevVariantShaders[a][v] = sys->variants[a][v].model.materials[0].shader;
                sys->variants[a][v].model.materials[0].shader = depthShader;
            }
        }
    }

    Vector3 focus = {0};
    float halfExtent = 0.0f;
    EnvShadow_GetActiveCaptureBounds(&focus, &halfExtent);
    float cullBase = (halfExtent > 1.0f) ? halfExtent : 28.0f;
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < sys->count; i++)
    {
        const MapRockItem *p = &sys->items[i];
        if (cullBase > 35.0f && p->archetype == MAP_ROCK_SCREE && p->radiusScale < 0.25f)
            continue;
        float dx = p->position.x - focus.x;
        float dz = p->position.z - focus.z;
        float pad = p->radiusScale * 2.0f;
        float limit = cullBase + pad;
        if (dx * dx + dz * dz > limit * limit)
            continue;
        int variant = Rock_HashVariant(i, p->position.x, p->position.z);
        const MapRockSet *set = Rock_ResolveArchetypeSet(sys, p->archetype, variant);
        if (!set->ready || set->model.materialCount < 1)
            continue;

        Vector3 pos = p->position;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
        DrawModelEx(set->model, pos, rotAxis, p->rotationDeg, scale, WHITE);
    }

    rlDrawRenderBatchActive();

    for (int a = 0; a < MAP_ROCK_ARCHETYPE_COUNT; a++)
    {
        if (sys->models[a].ready && sys->models[a].model.materialCount > 0)
        {
            sys->models[a].model.materials[0].shader = prevShaders[a];
        }
        for (int v = 0; v < MAP_ROCK_VARIANTS_PER_ARCHETYPE - 1; v++)
        {
            if (sys->variants[a][v].ready && sys->variants[a][v].model.materialCount > 0)
            {
                sys->variants[a][v].model.materials[0].shader = prevVariantShaders[a][v];
            }
        }
    }
}

void MapProp_UnloadRockSystem(MapRockSystem *sys)
{
    if (!sys || !sys->ready)
        return;
    for (int a = 0; a < MAP_ROCK_ARCHETYPE_COUNT; a++)
    {
        if (sys->models[a].ready)
        {
            MapProp_UnloadRocks(&sys->models[a]);
        }
        for (int v = 0; v < MAP_ROCK_VARIANTS_PER_ARCHETYPE - 1; v++)
        {
            if (sys->variants[a][v].ready)
            {
                MapProp_UnloadRocks(&sys->variants[a][v]);
            }
        }
    }
    if (sys->items)
    {
        RL_FREE(sys->items);
        sys->items = NULL;
    }
    *sys = (MapRockSystem){0};
}

void MapProp_PopulateRockItems(MapRockSystem *sys, const MapGroundSurface *ground,
                               Vector3 groundCenter, const MapRockItem *items, int itemCount)
{
    if (!sys || !items || itemCount <= 0)
        return;
    for (int i = 0; i < itemCount; i++)
    {
        const MapRockItem *item = &items[i];
        Vector3 pos = item->position;
        Vector3 normal = {0.0f, 1.0f, 0.0f};
        if (ground && ground->ready)
        {
            if (!MapProp_SampleGroundSurface(ground, groundCenter, item->position.x, item->position.z, &pos, &normal))
            {
                pos = item->position;
            }
        }
        float sinkOffset = item->heightScale * 0.20f;
        switch (item->archetype)
        {
        case MAP_ROCK_BASALT:
        case MAP_ROCK_CRAG:
        case MAP_ROCK_LIMESTONE:
            sinkOffset = item->heightScale * 0.10f;
            break;
        case MAP_ROCK_OBSIDIAN:
            sinkOffset = item->heightScale * 0.12f;
            break;
        case MAP_ROCK_SLATE:
            sinkOffset = item->heightScale * 0.15f;
            break;
        case MAP_ROCK_SANDSTONE:
            sinkOffset = item->heightScale * 0.20f;
            break;
        case MAP_ROCK_GRANITE:
        case MAP_ROCK_BOULDER:
            sinkOffset = item->heightScale * 0.25f;
            break;
        case MAP_ROCK_RIVER_STONE:
            sinkOffset = item->heightScale * 0.26f;
            break;
        case MAP_ROCK_SCREE:
            sinkOffset = item->heightScale * 0.16f;
            break;
        default:
            sinkOffset = item->heightScale * 0.20f;
            break;
        }
        float heightScale = item->heightScale;
        float radiusScale = item->radiusScale;
        float slopeCos = fmaxf(normal.y, 0.25f);
        float tanSlope = sqrtf(fmaxf(0.0f, 1.0f - slopeCos * slopeCos)) / slopeCos;
        float slopeSink = tanSlope * radiusScale * 0.55f;
        sinkOffset = fminf(sinkOffset + slopeSink, fmaxf(sinkOffset, heightScale * 0.65f));

        // Let river stones sit naturally on the riverbed rather than stretching vertically
        // into unnatural dome halos above deep water.
        pos.y -= sinkOffset;

        MapProp_AddRockItem(sys, pos, radiusScale, heightScale, item->rotationDeg, item->archetype);
    }
}

void MapProp_AddCragPlacements(MapRockSystem *sys, const MapGroundSurface *ground,
                               Vector3 groundCenter, const Vector2 *points, int count,
                               float baseScale)
{
    if (!sys || !points || count <= 0)
        return;
    float scaleBase = baseScale > 0.0f ? baseScale : 1.45f;
    for (int i = 0; i < count; i++)
    {
        Vector3 pos = {points[i].x, 0.0f, points[i].y};
        Vector3 normal;
        if (ground && ground->ready &&
            MapProp_SampleGroundSurface(ground, groundCenter, points[i].x, points[i].y, &pos, &normal))
        {
            float rot = (float)(i * 67 % 360);
            float scale = scaleBase + 0.35f * (float)(i % 3);
            MapProp_AddRockItem(sys, pos, scale, scale * 0.85f, rot, MAP_ROCK_GRANITE);
        }
    }
}

float MapProp_EvaluateRockSuppression(float x, float z, const MapRockItem *items, int itemCount)
{
    if (!items || itemCount <= 0)
        return 1.0f;
    float suppression = 1.0f;
    for (int i = 0; i < itemCount; i++)
    {
        float r = items[i].radiusScale;
        if (r <= 1e-4f)
            continue;
        float dx = x - items[i].position.x;
        float dz = z - items[i].position.z;
        // Suppress grass only inside the tight rock ground-contact core (coreR = 0.42 * r)
        // and taper over a narrow skirt (maxR = 0.72 * r) so meadow tufts hug boulders with zero bald holes.
        float maxR = r * 0.72f;
        if (fabsf(dx) > maxR || fabsf(dz) > maxR)
            continue;
        float distSq = dx * dx + dz * dz;
        if (distSq > maxR * maxR)
            continue;
        float coreR = r * 0.42f;
        if (distSq <= coreR * coreR)
            return 0.0f;
        float dist = sqrtf(distSq);
        float t = (dist - coreR) / (r * 0.30f);
        float fade = t * t * (3.0f - 2.0f * t);
        if (fade < suppression)
            suppression = fade;
    }
    return suppression;
}

static Vector2 Rock_ClosestPointOnTriangleXZ(Vector2 p, Vector2 a, Vector2 b, Vector2 c)
{
    Vector2 ab = {b.x - a.x, b.y - a.y};
    Vector2 ac = {c.x - a.x, c.y - a.y};
    Vector2 ap = {p.x - a.x, p.y - a.y};
    float d1 = ab.x * ap.x + ab.y * ap.y;
    float d2 = ac.x * ap.x + ac.y * ap.y;
    if (d1 <= 0.0f && d2 <= 0.0f)
        return a;

    Vector2 bp = {p.x - b.x, p.y - b.y};
    float d3 = ab.x * bp.x + ab.y * bp.y;
    float d4 = ac.x * bp.x + ac.y * bp.y;
    if (d3 >= 0.0f && d4 <= d3)
        return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        float v = d1 / (d1 - d3);
        return (Vector2){a.x + v * ab.x, a.y + v * ab.y};
    }

    Vector2 cp = {p.x - c.x, p.y - c.y};
    float d5 = ab.x * cp.x + ab.y * cp.y;
    float d6 = ac.x * cp.x + ac.y * cp.y;
    if (d6 >= 0.0f && d5 <= d6)
        return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        float w = d2 / (d2 - d6);
        return (Vector2){a.x + w * ac.x, a.y + w * ac.y};
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return (Vector2){b.x + w * (c.x - b.x), b.y + w * (c.y - b.y)};
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return (Vector2){a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w};
}

// Lightweight cached spatial hash for O(1) rock collision queries (rebuilt only when sys/count changes)
#define ROCK_HASH_BUCKETS 512
#define ROCK_HASH_CELL    12.0f
static const MapRockSystem *s_rockHashSys = NULL;
static const MapRockItem *s_rockHashItems = NULL;
static int s_rockHashCount = -1;
static int s_rockHead[ROCK_HASH_BUCKETS];
static int *s_rockNext = NULL;
static int s_rockNextCap = 0;

static inline unsigned int RockHashCell(int cx, int cz) {
    return (((unsigned int)cx * 73856093u) ^ ((unsigned int)cz * 19349663u)) & (ROCK_HASH_BUCKETS - 1u);
}

static void EnsureRockCollisionHash(const MapRockSystem *sys) {
    if (s_rockHashSys == sys && s_rockHashItems == sys->items && s_rockHashCount == sys->count) return;
    if (sys->count > s_rockNextCap) {
        RL_FREE(s_rockNext);
        s_rockNextCap = sys->count + 64;
        s_rockNext = (int *)RL_MALLOC((size_t)s_rockNextCap * sizeof(int));
    }
    for (int b = 0; b < ROCK_HASH_BUCKETS; b++) s_rockHead[b] = -1;
    if (!s_rockNext) return;
    const float invCell = 1.0f / ROCK_HASH_CELL;
    for (int i = 0; i < sys->count; i++) {
        int cx = (int)floorf(sys->items[i].position.x * invCell);
        int cz = (int)floorf(sys->items[i].position.z * invCell);
        unsigned int b = RockHashCell(cx, cz);
        s_rockNext[i] = s_rockHead[b];
        s_rockHead[b] = i;
    }
    s_rockHashSys = sys;
    s_rockHashItems = sys->items;
    s_rockHashCount = sys->count;
}

void MapProp_ResolveRockSystemCollision(const MapRockSystem *sys, Vector3 *pos,
                                        float agentRadius, float stepHeight,
                                        float *inOutGroundY)
{
    if (!sys || !sys->ready || !sys->items || sys->count <= 0 || !pos)
        return;

    EnsureRockCollisionHash(sys);
    const float invCell = 1.0f / ROCK_HASH_CELL;
    int px = (int)floorf(pos->x * invCell);
    int pz = (int)floorf(pos->z * invCell);

    float supportY = inOutGroundY ? *inOutGroundY : pos->y;
    for (int dzc = -1; dzc <= 1; dzc++) {
        for (int dxc = -1; dxc <= 1; dxc++) {
            unsigned int b = RockHashCell(px + dxc, pz + dzc);
            for (int i = s_rockHead[b]; i >= 0; i = s_rockNext[i])
            {
                const MapRockItem *p = &sys->items[i];
        if (p->archetype == MAP_ROCK_SCREE && p->radiusScale < 0.35f)
            continue;
        if (p->radiusScale <= 1e-4f || p->heightScale <= 1e-4f)
            continue;

        int variant = Rock_HashVariant(i, p->position.x, p->position.z);
        const MapRockSet *set = Rock_ResolveArchetypeSet(sys, p->archetype, variant);
        if (!set || !set->ready || set->model.meshCount < 1)
            continue;

        float maxScale = fmaxf(p->radiusScale, p->heightScale);
        float broadR = set->boundsRadius * maxScale + agentRadius + 0.5f;
        float dx = pos->x - p->position.x;
        float dz = pos->z - p->position.z;
        if (fabsf(dx) > broadR || fabsf(dz) > broadR)
            continue;
        if (dx * dx + dz * dz > broadR * broadR)
            continue;

        const Mesh *m = &set->model.meshes[0];
        if (!m->vertices || m->triangleCount <= 0)
            continue;

        float theta = p->rotationDeg * (PI / 180.0f);
        float cosT = cosf(theta);
        float sinT = sinf(theta);
        float invR = 1.0f / p->radiusScale;

        float lx = (dx * cosT - dz * sinT) * invR;
        float lz = (dx * sinT + dz * cosT) * invR;

        // Pass 1: Exact barycentric walkable surface height on top-facing triangles
        for (int t = 0; t < m->triangleCount; t++)
        {
            unsigned int i0 = m->indices ? m->indices[t * 3 + 0] : (unsigned int)(t * 3 + 0);
            unsigned int i1 = m->indices ? m->indices[t * 3 + 1] : (unsigned int)(t * 3 + 1);
            unsigned int i2 = m->indices ? m->indices[t * 3 + 2] : (unsigned int)(t * 3 + 2);

            Vector3 v0 = {m->vertices[i0 * 3 + 0], m->vertices[i0 * 3 + 1], m->vertices[i0 * 3 + 2]};
            Vector3 v1 = {m->vertices[i1 * 3 + 0], m->vertices[i1 * 3 + 1], m->vertices[i1 * 3 + 2]};
            Vector3 v2 = {m->vertices[i2 * 3 + 0], m->vertices[i2 * 3 + 1], m->vertices[i2 * 3 + 2]};

            Vector3 e1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
            Vector3 e2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
            float ny = e1.z * e2.x - e1.x * e2.z;
            if (ny <= 1e-6f)
                continue;

            float nx = e1.y * e2.z - e1.z * e2.y;
            float nz = e1.x * e2.y - e1.y * e2.x;
            float nLen = sqrtf(nx * nx + ny * ny + nz * nz);
            if (nLen <= 1e-6f || (ny / nLen) < 0.25f)
                continue;

            float d0x = v1.x - v0.x, d0z = v1.z - v0.z;
            float d1x = v2.x - v0.x, d1z = v2.z - v0.z;
            float d2x = lx - v0.x, d2z = lz - v0.z;
            float denom = d0x * d1z - d1x * d0z;
            if (fabsf(denom) < 1e-7f)
                continue;
            float invDen = 1.0f / denom;
            float u = (d2x * d1z - d1x * d2z) * invDen;
            float v = (d0x * d2z - d2x * d0z) * invDen;
            float w = 1.0f - u - v;
            if (u >= -0.04f && v >= -0.04f && w >= -0.04f)
            {
                float localY = w * v0.y + u * v1.y + v * v2.y;
                float surfY = p->position.y + localY * p->heightScale;
                if (pos->y >= surfY - stepHeight && surfY > supportY)
                    supportY = surfY;
            }
        }

        // Pass 2: Exact mesh-triangle horizontal push-out for body overlap above stepHeight
        float effectiveGround = (supportY > pos->y) ? supportY : pos->y;
        float bodyMinY = effectiveGround + stepHeight;
        float bodyMaxY = effectiveGround + 1.65f;
        for (int t = 0; t < m->triangleCount; t++)
        {
            unsigned int i0 = m->indices ? m->indices[t * 3 + 0] : (unsigned int)(t * 3 + 0);
            unsigned int i1 = m->indices ? m->indices[t * 3 + 1] : (unsigned int)(t * 3 + 1);
            unsigned int i2 = m->indices ? m->indices[t * 3 + 2] : (unsigned int)(t * 3 + 2);

            Vector3 v0 = {m->vertices[i0 * 3 + 0], m->vertices[i0 * 3 + 1], m->vertices[i0 * 3 + 2]};
            Vector3 v1 = {m->vertices[i1 * 3 + 0], m->vertices[i1 * 3 + 1], m->vertices[i1 * 3 + 2]};
            Vector3 v2 = {m->vertices[i2 * 3 + 0], m->vertices[i2 * 3 + 1], m->vertices[i2 * 3 + 2]};

            float triMinY = p->position.y + fminf(v0.y, fminf(v1.y, v2.y)) * p->heightScale;
            float triMaxY = p->position.y + fmaxf(v0.y, fmaxf(v1.y, v2.y)) * p->heightScale;
            if (triMaxY <= bodyMinY || triMinY >= bodyMaxY)
                continue;

            Vector2 cp = Rock_ClosestPointOnTriangleXZ((Vector2){lx, lz},
                                                       (Vector2){v0.x, v0.z},
                                                       (Vector2){v1.x, v1.z},
                                                       (Vector2){v2.x, v2.z});
            float lPushX = (lx - cp.x) * p->radiusScale;
            float lPushZ = (lz - cp.y) * p->radiusScale;
            float distSq = lPushX * lPushX + lPushZ * lPushZ;
            if (distSq >= agentRadius * agentRadius)
                continue;

            float wPushX, wPushZ;
            if (distSq > 1e-6f)
            {
                float dist = sqrtf(distSq);
                float mag = agentRadius - dist;
                float nLx = lPushX / dist;
                float nLz = lPushZ / dist;
                wPushX = (nLx * cosT + nLz * sinT) * mag;
                wPushZ = (-nLx * sinT + nLz * cosT) * mag;
            }
            else
            {
                Vector3 e1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
                Vector3 e2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
                float tnx = e1.y * e2.z - e1.z * e2.y;
                float tnz = e1.x * e2.y - e1.y * e2.x;
                float tLen = sqrtf(tnx * tnx + tnz * tnz);
                if (tLen < 1e-5f)
                    continue;
                float nLx = tnx / tLen;
                float nLz = tnz / tLen;
                wPushX = (nLx * cosT + nLz * sinT) * agentRadius;
                wPushZ = (-nLx * sinT + nLz * cosT) * agentRadius;
            }

            pos->x += wPushX;
            pos->z += wPushZ;
            dx = pos->x - p->position.x;
            dz = pos->z - p->position.z;
            lx = (dx * cosT - dz * sinT) * invR;
            lz = (dx * sinT + dz * cosT) * invR;
        }
        }
        }
    }

    if (inOutGroundY)
        *inOutGroundY = supportY;
}
