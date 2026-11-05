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

static Mesh Nature_BuildProceduralRockMesh(int rockType)
{
    int rings = 22;
    int slices = 30;
    if (rockType == MAP_ROCK_CRAG || rockType == MAP_ROCK_LIMESTONE)
    {
        rings = 24;
        slices = 32;
    }
    else if (rockType == MAP_ROCK_BASALT || rockType == MAP_ROCK_SANDSTONE)
    {
        rings = 22;
        slices = 30;
    }
    else if (rockType == MAP_ROCK_SCREE)
    {
        rings = 12;
        slices = 16;
    }
    else if (rockType == MAP_ROCK_RIVER_STONE)
    {
        rings = 18;
        slices = 26;
    }
    Mesh mesh = GenMeshSphere(1.0f, rings, slices);

    float *origTexcoords = (float *)RL_MALLOC((size_t)mesh.vertexCount * 2u * sizeof(float));
    if (origTexcoords)
        memcpy(origTexcoords, mesh.texcoords, (size_t)mesh.vertexCount * 2u * sizeof(float));

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
            // Authentic water-tumbled river cobble / boulder
            float c1 = cosf(2.0f * angle + 0.35f);
            float c2 = sinf(3.0f * angle - 0.45f);
            float pebble = 1.0f + 0.14f * c1 + 0.08f * c2;

            vx = cosA * horizR * pebble * 1.12f;
            vz = sinA * horizR * pebble * 0.88f;

            float topArch = 0.85f + 0.15f * (1.0f - fminf(1.0f, horizR * horizR));
            vy = y * 0.62f * topArch;
            if (vy < -0.10f)
            {
                vy = -0.10f + (vy + 0.10f) * 0.60f;
            }
        }
        else if (rockType == MAP_ROCK_SCREE)
        {
            // Scree / Talus gravel: irregular shattered angular chip
            float chipNoise = 0.38f * cosf(3.0f * angle) + 0.28f * sinf(5.0f * angle) - 0.22f * fabsf(cosf(7.0f * angle));
            vx = cosA * horizR * (1.0f + chipNoise) * 1.15f;
            vy = y * 0.45f + 0.12f * sinf(x * 8.0f);
            vz = sinA * horizR * (1.0f + chipNoise) * 0.82f;

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
            // Granite boulder / Tor: massive polyhedral corestone with onion-skin spheroidal weathering
            float c1 = cosf(3.0f * angle + 0.35f);
            float c2 = sinf(4.0f * angle - 0.65f);
            float c3 = cosf(6.0f * angle + 1.2f);
            float cleavage = 0.26f * c1 + 0.16f * c2 - 0.12f * fabsf(c3);

            vx = cosA * horizR * (1.0f + cleavage) * 1.24f;
            vy = y * 0.82f;
            vz = sinA * horizR * (1.0f + cleavage) * 0.88f;

            for (int p = 0; p < 9; p++)
            {
                float nx = kBoulderPlanes[p][0];
                float ny = kBoulderPlanes[p][1];
                float nz = kBoulderPlanes[p][2];
                float d = kBoulderPlanes[p][3];
                float dist = vx * nx + vy * ny + vz * nz - d;
                if (dist > -0.08f)
                {
                    float u = dist + 0.08f;
                    float softDist = 0.5f * (u + sqrtf(u * u + 0.012f));
                    const float clipStrength = 0.74f;
                    vx -= nx * softDist * clipStrength;
                    vy -= ny * softDist * clipStrength;
                    vz -= nz * softDist * clipStrength;
                }
            }

            float strata = vy * 8.0f + angle * 1.4f;
            float terrace = 0.035f * (sinf(strata) + 0.30f * sinf(strata * 2.0f));
            vy += terrace;

            if (vy < -0.20f)
            {
                float tuck = (-0.20f - vy);
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

        if (flatFaceted)
        {
            float nLenSq = n.x * n.x + n.y * n.y + n.z * n.z;
            if (nLenSq > 1e-10f)
            {
                float invLen = 1.0f / sqrtf(nLenSq);
                n.x *= invLen;
                n.y *= invLen;
                n.z *= invLen;
            }
            mesh.normals[i0 * 3 + 0] = n.x;
            mesh.normals[i0 * 3 + 1] = n.y;
            mesh.normals[i0 * 3 + 2] = n.z;
            mesh.normals[i1 * 3 + 0] = n.x;
            mesh.normals[i1 * 3 + 1] = n.y;
            mesh.normals[i1 * 3 + 2] = n.z;
            mesh.normals[i2 * 3 + 0] = n.x;
            mesh.normals[i2 * 3 + 1] = n.y;
            mesh.normals[i2 * 3 + 2] = n.z;
        }
        else
        {
            mesh.normals[i0 * 3 + 0] += n.x;
            mesh.normals[i0 * 3 + 1] += n.y;
            mesh.normals[i0 * 3 + 2] += n.z;
            mesh.normals[i1 * 3 + 0] += n.x;
            mesh.normals[i1 * 3 + 1] += n.y;
            mesh.normals[i1 * 3 + 2] += n.z;
            mesh.normals[i2 * 3 + 0] += n.x;
            mesh.normals[i2 * 3 + 1] += n.y;
            mesh.normals[i2 * 3 + 2] += n.z;
        }
    }

    if (!flatFaceted)
    {
        // Spatial normal welding across unindexed GenMeshSphere triangles with cached bucket indices
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
                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                for (int j = bucketHeads[b]; j != -1; j = nextVert[j])
                {
                    float dx = mesh.vertices[j * 3 + 0] - vx;
                    float dy = mesh.vertices[j * 3 + 1] - vy;
                    float dz = mesh.vertices[j * 3 + 2] - vz;
                    if (dx * dx + dy * dy + dz * dz < 0.0004f)
                    {
                        sx += mesh.normals[j * 3 + 0];
                        sy += mesh.normals[j * 3 + 1];
                        sz += mesh.normals[j * 3 + 2];
                    }
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

        if (!flatFaceted)
        {
            for (int r = 0; r <= rings; r++)
            {
                int idx0 = r * (slices + 1);
                int idx1 = idx0 + slices;
                if (idx1 < mesh.vertexCount)
                {
                    float avgTx = mesh.tangents[idx0 * 4 + 0] + mesh.tangents[idx1 * 4 + 0];
                    float avgTy = mesh.tangents[idx0 * 4 + 1] + mesh.tangents[idx1 * 4 + 1];
                    float avgTz = mesh.tangents[idx0 * 4 + 2] + mesh.tangents[idx1 * 4 + 2];
                    mesh.tangents[idx0 * 4 + 0] = mesh.tangents[idx1 * 4 + 0] = avgTx;
                    mesh.tangents[idx0 * 4 + 1] = mesh.tangents[idx1 * 4 + 1] = avgTy;
                    mesh.tangents[idx0 * 4 + 2] = mesh.tangents[idx1 * 4 + 2] = avgTz;
                }
            }
            float topTx = 0.0f, topTy = 0.0f, topTz = 0.0f;
            float botTx = 0.0f, botTy = 0.0f, botTz = 0.0f;
            for (int s = 0; s <= slices; s++)
            {
                int topIdx = s;
                int botIdx = rings * (slices + 1) + s;
                if (topIdx < mesh.vertexCount)
                {
                    topTx += mesh.tangents[topIdx * 4 + 0];
                    topTy += mesh.tangents[topIdx * 4 + 1];
                    topTz += mesh.tangents[topIdx * 4 + 2];
                }
                if (botIdx < mesh.vertexCount)
                {
                    botTx += mesh.tangents[botIdx * 4 + 0];
                    botTy += mesh.tangents[botIdx * 4 + 1];
                    botTz += mesh.tangents[botIdx * 4 + 2];
                }
            }
            for (int s = 0; s <= slices; s++)
            {
                int topIdx = s;
                int botIdx = rings * (slices + 1) + s;
                if (topIdx < mesh.vertexCount)
                {
                    mesh.tangents[topIdx * 4 + 0] = topTx;
                    mesh.tangents[topIdx * 4 + 1] = topTy;
                    mesh.tangents[topIdx * 4 + 2] = topTz;
                }
                if (botIdx < mesh.vertexCount)
                {
                    mesh.tangents[botIdx * 4 + 0] = botTx;
                    mesh.tangents[botIdx * 4 + 1] = botTy;
                    mesh.tangents[botIdx * 4 + 2] = botTz;
                }
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

static MapRockSet Rock_CreateTypedSet(int rockType, const char *diffusePath,
                                      const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMesh(rockType);
    MapProp_MeshBounds(&mesh, &rocks.boundsCenter, &rocks.boundsRadius);
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

        sys.models[a] = Rock_CreateTypedSet(a, dPath, nPath, rPath);
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

static inline const MapRockSet *Rock_ResolveArchetypeSet(const MapRockSystem *sys, MapRockArchetype archetype)
{
    if ((int)archetype >= 0 && archetype < MAP_ROCK_ARCHETYPE_COUNT && sys->models[archetype].ready)
        return &sys->models[archetype];
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
        const MapRockSet *set = Rock_ResolveArchetypeSet(sys, p->archetype);
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

        Color rockTint = (Color){142, 136, 128, 255};
        if (p->archetype == MAP_ROCK_SANDSTONE)
        {
            rockTint = (Color){168, 152, 125, 255};
        }
        else if (p->archetype == MAP_ROCK_RIVER_STONE)
        {
            rockTint = (Color){172, 165, 156, 250};
        }
        else if (p->archetype == MAP_ROCK_SCREE)
        {
            rockTint = (Color){132, 126, 118, 255};
        }
        else if (p->archetype == MAP_ROCK_LIMESTONE || p->archetype == MAP_ROCK_CRAG)
        {
            rockTint = (Color){165, 162, 155, 255};
        }

        DrawModelEx(set->model, pos, rotAxis, p->rotationDeg, scale, rockTint);
    }
}

void MapProp_DrawRockSystemShadowCasters(MapRockSystem *sys, Shader depthShader)
{
    if (!sys || !sys->ready || !sys->items || sys->count <= 0)
        return;

    Shader prevShaders[MAP_ROCK_ARCHETYPE_COUNT] = {0};
    for (int a = 0; a < MAP_ROCK_ARCHETYPE_COUNT; a++)
    {
        if (sys->models[a].ready && sys->models[a].model.materialCount > 0)
        {
            prevShaders[a] = sys->models[a].model.materials[0].shader;
            sys->models[a].model.materials[0].shader = depthShader;
        }
    }

    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < sys->count; i++)
    {
        const MapRockItem *p = &sys->items[i];
        const MapRockSet *set = Rock_ResolveArchetypeSet(sys, p->archetype);
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
        Vector3 normal;
        if (ground && ground->ready)
        {
            if (!MapProp_SampleGroundSurface(ground, groundCenter, item->position.x, item->position.z, &pos, &normal))
            {
                pos = item->position;
            }
        }
        float sinkOffset = item->heightScale * 0.18f;
        switch (item->archetype)
        {
        case MAP_ROCK_BASALT:
        case MAP_ROCK_CRAG:
        case MAP_ROCK_LIMESTONE:
            sinkOffset = item->heightScale * 0.08f;
            break;
        case MAP_ROCK_OBSIDIAN:
            sinkOffset = item->heightScale * 0.10f;
            break;
        case MAP_ROCK_SLATE:
            sinkOffset = item->heightScale * 0.14f;
            break;
        case MAP_ROCK_SANDSTONE:
            sinkOffset = item->heightScale * 0.16f;
            break;
        case MAP_ROCK_GRANITE:
        case MAP_ROCK_BOULDER:
            sinkOffset = item->heightScale * 0.20f;
            break;
        case MAP_ROCK_RIVER_STONE:
            sinkOffset = item->heightScale * 0.12f;
            break;
        case MAP_ROCK_SCREE:
            sinkOffset = item->heightScale * 0.05f;
            break;
        default:
            sinkOffset = item->heightScale * 0.18f;
            break;
        }
        float heightScale = item->heightScale;
        float radiusScale = item->radiusScale;

        float waterSurfaceY = 0.0f, waterDepth = 0.0f;
        if (item->archetype == MAP_ROCK_RIVER_STONE &&
            MapManager_GetWaterInfoAt(item->position.x, item->position.z, &waterSurfaceY, &waterDepth) &&
            waterDepth > 0.04f)
        {
            float emergeOffset = 0.10f + fminf(0.06f, item->radiusScale * 0.08f);
            float targetTopY = waterSurfaceY + emergeOffset;
            float bedY = pos.y;
            float neededSpan = targetTopY - bedY + 0.12f;
            float minHeightScale = neededSpan * (1.0f / 1.03f);
            if (heightScale < minHeightScale)
            {
                heightScale = minHeightScale;
            }
            if (heightScale > item->heightScale * 1.15f)
            {
                radiusScale = fmaxf(radiusScale, heightScale * 0.95f);
            }
            pos.y = targetTopY - heightScale * 0.62f;
        }
        else
        {
            pos.y -= sinkOffset;
        }

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
        float maxR = r * 0.90f; // Tight boundary: meadow grass grows right up to the stone edge!
        if (fabsf(dx) > maxR || fabsf(dz) > maxR)
            continue;
        float distSq = dx * dx + dz * dz;
        if (distSq > maxR * maxR)
            continue;
        float coreR = r * 0.65f; // Solid stone core exclusion checked in squared space before sqrtf
        if (distSq <= coreR * coreR)
            return 0.0f;
        float dist = sqrtf(distSq);
        float fade = (dist - coreR) / (r * 0.25f);
        if (fade < suppression)
            suppression = fade;
    }
    return suppression;
}