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
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static inline float Rock_Mix(float a, float b, float t)
{
    return a * (1.0f - t) + b * t;
}

static Mesh Nature_BuildProceduralRockMesh(int rockType)
{
    int rings = 26;
    int slices = 36;
    if (rockType == MAP_ROCK_CRAG || rockType == MAP_ROCK_LIMESTONE) {
        rings = 36; slices = 42;
    } else if (rockType == MAP_ROCK_BASALT) {
        rings = 32; slices = 42;
    } else if (rockType == MAP_ROCK_SANDSTONE) {
        rings = 32; slices = 40;
    } else if (rockType == MAP_ROCK_SCREE) {
        rings = 14; slices = 18;
    }
    Mesh mesh = GenMeshSphere(1.0f, rings, slices);

    // Deep polyhedral cleavage planes for sharp, chiseled granite / karst facets
    static const float kBoulderPlanes[9][4] = {
        { 0.78f,  0.48f,  0.39f, 0.52f},
        {-0.72f,  0.52f,  0.46f, 0.50f},
        { 0.18f,  0.44f, -0.88f, 0.51f},
        {-0.32f,  0.65f, -0.69f, 0.48f},
        { 0.55f,  0.74f, -0.38f, 0.45f},
        {-0.64f,  0.28f, -0.71f, 0.54f},
        { 0.12f,  0.92f,  0.37f, 0.44f},
        { 0.84f,  0.18f, -0.51f, 0.53f},
        {-0.22f,  0.58f,  0.78f, 0.49f},
    };

    for (int i = 0; i < mesh.vertexCount; i++) {
        float x = mesh.vertices[i * 3 + 0];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];

        float horizR = sqrtf(x * x + z * z);
        float angle = atan2f(z, x);
        float heightFactor = (y + 1.0f) * 0.5f;

        float vx = x, vy = y, vz = z;
        float uMult = 2.5f, vMult = 2.5f;

        if (rockType == MAP_ROCK_CRAG || rockType == MAP_ROCK_LIMESTONE) {
            // Karst pinnacle / dissolved limestone needle spire with vertical fluting (rillenkarren)
            // Slender towering height with sharp chisel knife-edge summit crest (aiguille)
            float c1 = cosf(3.0f * angle + 0.45f);
            float c2 = sinf(5.0f * angle - 0.85f);
            float c3 = cosf(7.0f * angle + 1.10f);
            float ridge = 1.0f + 0.28f * (c1 >= 0.0f ? sqrtf(c1) : -sqrtf(-c1))
                               + 0.15f * c2 - 0.10f * fabsf(c3);

            // Vertical fluting rain rills (rillenkarren)
            float fluting = 0.14f * cosf(10.0f * angle) - 0.08f * fabsf(sinf(16.0f * angle));

            // Sharp acute apex taper: radius scales with distance from summit
            float distFromTop = (1.05f - y) * 0.5f;
            if (distFromTop < 0.0f) distFromTop = 0.0f;
            float taper = (0.22f + 0.78f * powf(distFromTop, 0.68f)) * 1.18f;

            // Chisel knife-edge summit: squash cross-section into an acute blade
            float summitPinch = Rock_Smoothstep(0.35f, 0.98f, y);
            float ridgeStrike = cosf(angle - 0.4f);
            float ridgeChisel = 1.0f - summitPinch * 0.65f * (1.0f - fabsf(ridgeStrike));

            float newR = (0.35f + 0.65f * horizR) * (ridge + fluting) * taper * ridgeChisel;
            vx = cosf(angle) * newR * 1.25f;
            vz = sinf(angle) * newR * 0.85f;

            // Clip against cleavage planes
            for (int p = 0; p < 9; p++) {
                float nx = kBoulderPlanes[p][0], ny = kBoulderPlanes[p][1], nz = kBoulderPlanes[p][2];
                float d  = kBoulderPlanes[p][3] * 1.12f;
                float dist = vx * nx + y * ny + vz * nz - d;
                if (dist > 0.0f) {
                    vx -= nx * dist * 0.88f; vz -= nz * dist * 0.88f;
                }
            }

            // Towering vertical proportions with stepped karst dissolution terraces
            float strataPhase = y * 16.0f + angle * 1.5f;
            float terrace = 0.07f * (sinf(strataPhase) + 0.35f * sinf(strataPhase * 2.0f));
            float baseWiden = (y < -0.30f) ? (1.0f + (-0.30f - y) * 0.85f) : 1.0f;
            float leanX = 0.22f * powf(heightFactor, 2.0f);
            float leanZ = -0.14f * powf(heightFactor, 2.0f);

            vx = vx * baseWiden + leanX;
            vy = (y + 0.15f) * 2.65f + terrace;
            vz = vz * baseWiden + leanZ;
            uMult = 2.8f; vMult = 4.2f;

        } else if (rockType == MAP_ROCK_BASALT) {
            // Hexagonal Columnar Basalt jointing (Giant's Causeway / Gành Đá Đĩa style)
            // Multi-column bundle: 3 interlocking columns of varying heights
            float hexAngle = angle + 0.52359877f; // PI/6
            float modHex = fmodf(hexAngle + 20.0f * PI, PI / 3.0f) - (PI / 6.0f);
            float hexPrism = 1.0f / cosf(modHex);
            if (hexPrism > 1.155f) hexPrism = 1.155f; // Exact regular hexagon bounds

            // Cluster of 3 bundled columns with staggered heights
            float colHeight = 1.15f;
            float colShiftX = 0.0f, colShiftZ = 0.0f;
            if (angle > -0.6f && angle < 1.5f) {
                colHeight = 1.25f; // Pillar A (North-East: tallest)
                colShiftX = 0.15f; colShiftZ = 0.10f;
            } else if (angle >= 1.5f || angle < -2.3f) {
                colHeight = 0.95f; // Pillar B (West: medium)
                colShiftX = -0.18f; colShiftZ = 0.05f;
            } else {
                colHeight = 1.05f; // Pillar C (South: stepped)
                colShiftX = 0.05f; colShiftZ = -0.15f;
            }

            // Top cap vs vertical column walls:
            // Vertices with y >= 0.65f form the flat polygonal top cleavage cap
            if (y >= 0.65f) {
                float rCap = (1.0f - y) / 0.35f; // 0 at pole, 1.0 at shoulder
                vx = cosf(angle) * hexPrism * rCap * 1.05f + colShiftX;
                vz = sinf(angle) * hexPrism * rCap * 0.95f + colShiftZ;
                vy = colHeight + 0.03f * cosf(6.0f * angle); // Flat cleavage top with subtle dishing
            } else {
                // Vertical columnar prism walls (y from -0.85 to 0.65)
                float wallY = (y - 0.65f) / 1.50f * (colHeight + 0.90f) + colHeight;
                float rWall = hexPrism;
                // Flared base below ground
                if (y < -0.85f) {
                    float flare = (-0.85f - y) / 0.15f;
                    rWall *= (1.0f + flare * 0.35f);
                }
                // Horizontal cooling joints (transverse fractures)
                float coolingJoint = 0.035f * sinf(wallY * 16.0f);
                vx = cosf(angle) * (rWall + coolingJoint) * 1.05f + colShiftX;
                vz = sinf(angle) * (rWall + coolingJoint) * 0.95f + colShiftZ;
                vy = wallY;
            }
            uMult = 2.4f; vMult = 3.6f;

        } else if (rockType == MAP_ROCK_SANDSTONE) {
            // Sedimentary Sandstone: horizontal bedding strata terraces + tabular rectangular block profile
            float ux = cosf(angle);
            float uz = sinf(angle);
            // Super-ellipse box squaring (orthogonal jointing cleavage)
            float sqX = copysignf(powf(fabsf(ux), 0.62f), ux) * 1.30f;
            float sqZ = copysignf(powf(fabsf(uz), 0.62f), uz) * 0.95f;

            if (y >= 0.50f) {
                // Broad flat tabular bench top
                float rCap = (1.0f - y) / 0.50f;
                vx = sqX * (0.35f + 0.65f * rCap);
                vz = sqZ * (0.35f + 0.65f * rCap);
                vy = 0.55f + 0.04f * sinf(sqX * 3.5f);
            } else {
                // Stepped horizontal bedding ledges (3 distinct geological beds)
                float tY = (y + 0.85f) / 1.35f * 3.0f;
                float tIdx = floorf(tY);
                if (tIdx < 0.0f) tIdx = 0.0f;
                if (tIdx > 2.0f) tIdx = 2.0f;
                float tFrac = tY - tIdx;
                float stepY = (tIdx + Rock_Smoothstep(0.12f, 0.88f, tFrac) * 0.88f) / 3.0f * 1.35f - 0.85f;

                // Differential erosion: softer shale layers between sandstone beds are undercut
                float undercut = 1.0f - 0.12f * (1.0f - fabsf(tFrac - 0.5f) * 2.0f);
                float rWall = undercut;
                if (y < -0.85f) {
                    float flare = (-0.85f - y) / 0.15f;
                    rWall *= (1.0f + flare * 0.45f);
                }
                vx = sqX * rWall;
                vz = sqZ * rWall;
                vy = stepY * 0.72f;
            }
            uMult = 3.2f; vMult = 1.8f;

        } else if (rockType == MAP_ROCK_OBSIDIAN) {
            // Obsidian: Vitreous volcanic glass with razor-sharp conchoidal concave shell scoops
            float c1 = cosf(2.0f * angle - 0.4f);
            float c2 = sinf(4.0f * angle + 0.9f);
            float shard = 1.0f + 0.42f * c1 - 0.26f * fabsf(c2);

            float distFromTop = (1.05f - y) * 0.5f;
            if (distFromTop < 0.0f) distFromTop = 0.0f;
            float spireTaper = powf(distFromTop, 0.65f) * 1.28f;

            vx = cosf(angle) * shard * spireTaper * 1.25f;
            vy = y * 1.45f;
            vz = sinf(angle) * shard * spireTaper * 0.85f;

            // Conchoidal concave shell hollows
            static const float kScallops[5][4] = {
                { 0.45f,  0.40f,  0.35f, 0.62f},
                {-0.50f,  0.30f, -0.30f, 0.58f},
                { 0.15f,  0.85f, -0.40f, 0.55f},
                {-0.35f, -0.15f,  0.45f, 0.52f},
                { 0.55f, -0.20f, -0.25f, 0.50f},
            };
            for (int s = 0; s < 5; s++) {
                float dx = vx - kScallops[s][0];
                float dy = vy - kScallops[s][1];
                float dz = vz - kScallops[s][2];
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                float radius = kScallops[s][3];
                if (dist < radius) {
                    float scoop = (radius - dist) * 0.82f;
                    vx -= (dx / dist) * scoop;
                    vy -= (dy / dist) * scoop;
                    vz -= (dz / dist) * scoop;
                }
            }
            uMult = 2.0f; vMult = 2.0f;

        } else if (rockType == MAP_ROCK_SLATE) {
            // Metamorphic Slate / Shale: thin foliated cleavage plates tilted at 24 degree dip
            float cosD = 0.9135f, sinD = 0.4067f; // 24 degrees
            float rotY = y * cosD - z * sinD;
            float rotZ = y * sinD + z * cosD;

            // Stacked parallel cleavage sheets with sharp step-fractures
            float sheetY = rotY * 4.0f;
            float sheetIdx = floorf(sheetY + 0.5f);
            float sheetFrac = sheetY + 0.5f - sheetIdx;
            float steppedY = (sheetIdx + Rock_Smoothstep(0.15f, 0.85f, sheetFrac) * 0.40f - 0.5f) / 4.0f * 0.35f;

            vy = steppedY * cosD + rotZ * sinD;
            vz = -steppedY * sinD + rotZ * cosD;
            vx = x * 1.35f + 0.15f * sinf(angle * 5.0f);
            uMult = 2.8f; vMult = 2.2f;

        } else if (rockType == MAP_ROCK_RIVER_STONE) {
            // River stone / cobblestone: perfectly water-worn, smoothed fluvial pebble / boulder
            // Plump natural water-smoothed dome with stable resting base (aspect ratio ~ 1.6:1)
            float asymmetry = 1.0f + 0.08f * cosf(angle + 0.35f) + 0.05f * sinf(2.0f * angle);
            float newR = horizR * asymmetry;
            vx = cosf(angle) * newR * 1.15f;
            vz = sinf(angle) * newR * 0.92f;

            // Water-worn dome crown: smooth rounded convex top, flat stable resting base
            float normR = fminf(1.0f, sqrtf(vx * vx + vz * vz) / 1.15f);
            float dome = cosf(normR * (PI * 0.5f));
            if (y >= 0.0f) {
                vy = y * 0.72f * (0.80f + 0.20f * powf(dome, 0.75f));
            } else {
                vy = y * 0.40f - 0.02f;
            }
            uMult = 1.6f; vMult = 1.6f;

        } else if (rockType == MAP_ROCK_SCREE) {
            // Scree / Talus gravel: irregular shattered angular chip
            float chipNoise = 0.38f * cosf(3.0f * angle) + 0.28f * sinf(5.0f * angle) - 0.22f * fabsf(cosf(7.0f * angle));
            vx = cosf(angle) * horizR * (1.0f + chipNoise) * 1.15f;
            vy = y * 0.45f + 0.12f * sinf(x * 8.0f);
            vz = sinf(angle) * horizR * (1.0f + chipNoise) * 0.82f;

            for (int p = 0; p < 5; p++) {
                float dist = vx * kBoulderPlanes[p][0] + vy * kBoulderPlanes[p][1] + vz * kBoulderPlanes[p][2] - kBoulderPlanes[p][3] * 0.78f;
                if (dist > 0.0f) {
                    vx -= kBoulderPlanes[p][0] * dist * 0.94f;
                    vy -= kBoulderPlanes[p][1] * dist * 0.94f;
                    vz -= kBoulderPlanes[p][2] * dist * 0.94f;
                }
            }
            uMult = 1.5f; vMult = 1.5f;

        } else {
            // Granite boulder / Tor: massive polyhedral corestone with onion-skin spheroidal weathering
            float c1 = cosf(3.0f * angle + 0.35f);
            float c2 = sinf(4.0f * angle - 0.65f);
            float c3 = cosf(6.0f * angle + 1.2f);
            float cleavage = 0.26f * c1 + 0.16f * c2 - 0.12f * fabsf(c3);

            vx = cosf(angle) * horizR * (1.0f + cleavage) * 1.24f;
            vy = y * 0.82f;
            vz = sinf(angle) * horizR * (1.0f + cleavage) * 0.88f;

            for (int p = 0; p < 9; p++) {
                float nx = kBoulderPlanes[p][0];
                float ny = kBoulderPlanes[p][1];
                float nz = kBoulderPlanes[p][2];
                float d  = kBoulderPlanes[p][3];
                float dist = vx * nx + vy * ny + vz * nz - d;
                if (dist > 0.0f) {
                    float clipStrength = 0.88f;
                    vx -= nx * dist * clipStrength;
                    vy -= ny * dist * clipStrength;
                    vz -= nz * dist * clipStrength;
                }
            }

            // Stepped horizontal bedding ledges & spheroidal rounded corestone summit
            float strata = vy * 10.0f + angle * 1.4f;
            float terrace = 0.045f * (sinf(strata) + 0.30f * sinf(strata * 2.0f));
            vy += terrace;

            // Grounded flared skirt at the base
            if (vy < -0.06f) {
                float skirt = (-0.06f - vy);
                vy = -0.06f - skirt * 0.25f;
                vx *= 1.0f + skirt * 0.55f;
                vz *= 1.0f + skirt * 0.55f;
            }
            uMult = 2.6f; vMult = 2.0f;
        }

        mesh.vertices[i * 3 + 0] = vx;
        mesh.vertices[i * 3 + 1] = vy;
        mesh.vertices[i * 3 + 2] = vz;

        if (rockType == MAP_ROCK_SANDSTONE) {
            if (y >= 0.50f) {
                // Planar mapping across top flat plateau
                mesh.texcoords[i * 2 + 0] = vx * 0.85f + 0.5f;
                mesh.texcoords[i * 2 + 1] = vz * 0.85f + 0.5f;
            } else {
                // Cylindrical mapping: V is strictly proportional to height vy so strata are 100% horizontal
                float uNorm = (angle + PI) / (2.0f * PI);
                mesh.texcoords[i * 2 + 0] = uNorm * 3.2f;
                mesh.texcoords[i * 2 + 1] = (vy + 0.85f) * 1.8f;
            }
        } else if (rockType == MAP_ROCK_RIVER_STONE) {
            mesh.texcoords[i * 2 + 0] = vx * 0.85f + 0.5f;
            mesh.texcoords[i * 2 + 1] = vz * 0.85f + 0.5f;
        } else if (rockType == MAP_ROCK_BASALT) {
            if (y >= 0.65f) {
                mesh.texcoords[i * 2 + 0] = vx * 0.85f + 0.5f;
                mesh.texcoords[i * 2 + 1] = vz * 0.85f + 0.5f;
            } else {
                float uNorm = (angle + PI) / (2.0f * PI);
                mesh.texcoords[i * 2 + 0] = uNorm * 6.0f;
                mesh.texcoords[i * 2 + 1] = (vy + 0.85f) * 1.5f;
            }
        } else {
            mesh.texcoords[i * 2 + 0] *= uMult;
            mesh.texcoords[i * 2 + 1] *= vMult;
        }
    }

    // Recompute normals from actual deformed triangle geometry (unindexed & indexed)
    for (int i = 0; i < mesh.vertexCount * 3; i++) mesh.normals[i] = 0.0f;
    for (int t = 0; t < mesh.triangleCount; t++) {
        int i0 = mesh.indices ? (int)mesh.indices[t * 3 + 0] : (t * 3 + 0);
        int i1 = mesh.indices ? (int)mesh.indices[t * 3 + 1] : (t * 3 + 1);
        int i2 = mesh.indices ? (int)mesh.indices[t * 3 + 2] : (t * 3 + 2);

        Vector3 v0 = {mesh.vertices[i0 * 3 + 0], mesh.vertices[i0 * 3 + 1], mesh.vertices[i0 * 3 + 2]};
        Vector3 v1 = {mesh.vertices[i1 * 3 + 0], mesh.vertices[i1 * 3 + 1], mesh.vertices[i1 * 3 + 2]};
        Vector3 v2 = {mesh.vertices[i2 * 3 + 0], mesh.vertices[i2 * 3 + 1], mesh.vertices[i2 * 3 + 2]};

        Vector3 e1 = Vector3Subtract(v1, v0);
        Vector3 e2 = Vector3Subtract(v2, v0);
        Vector3 n = Vector3CrossProduct(e1, e2);
        float nLen = Vector3Length(n);
        if (nLen > 0.00001f) {
            n.x /= nLen; n.y /= nLen; n.z /= nLen;
        }

        // For chiseled planar rock types (Basalt, Sandstone, Obsidian, Slate),
        // flat facet normals produce crisp dihedral edges and polygonal jointing!
        if (rockType == MAP_ROCK_BASALT || rockType == MAP_ROCK_SANDSTONE ||
            rockType == MAP_ROCK_OBSIDIAN || rockType == MAP_ROCK_SLATE) {
            mesh.normals[i0 * 3 + 0] = n.x; mesh.normals[i0 * 3 + 1] = n.y; mesh.normals[i0 * 3 + 2] = n.z;
            mesh.normals[i1 * 3 + 0] = n.x; mesh.normals[i1 * 3 + 1] = n.y; mesh.normals[i1 * 3 + 2] = n.z;
            mesh.normals[i2 * 3 + 0] = n.x; mesh.normals[i2 * 3 + 1] = n.y; mesh.normals[i2 * 3 + 2] = n.z;
        } else {
            // For organic/weathered types (Granite, Karst, River Stone), accumulate for smooth shading
            mesh.normals[i0 * 3 + 0] += n.x; mesh.normals[i0 * 3 + 1] += n.y; mesh.normals[i0 * 3 + 2] += n.z;
            mesh.normals[i1 * 3 + 0] += n.x; mesh.normals[i1 * 3 + 1] += n.y; mesh.normals[i1 * 3 + 2] += n.z;
            mesh.normals[i2 * 3 + 0] += n.x; mesh.normals[i2 * 3 + 1] += n.y; mesh.normals[i2 * 3 + 2] += n.z;
        }
    }

    if (rockType != MAP_ROCK_BASALT && rockType != MAP_ROCK_SANDSTONE &&
        rockType != MAP_ROCK_OBSIDIAN && rockType != MAP_ROCK_SLATE) {
        for (int i = 0; i < mesh.vertexCount; i++) {
            Vector3 n = {mesh.normals[i * 3 + 0], mesh.normals[i * 3 + 1], mesh.normals[i * 3 + 2]};
            float len = Vector3Length(n);
            if (len > 0.0001f) {
                mesh.normals[i * 3 + 0] = n.x / len;
                mesh.normals[i * 3 + 1] = n.y / len;
                mesh.normals[i * 3 + 2] = n.z / len;
            } else {
                mesh.normals[i * 3 + 1] = 1.0f;
            }
        }
    }

    // Compute robust tangents so normal mapping functions seamlessly on unindexed meshes
    if (mesh.tangents == NULL) {
        mesh.tangents = (float *)RL_MALLOC(mesh.vertexCount * 4 * sizeof(float));
    }
    for (int t = 0; t < mesh.triangleCount; t++) {
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
            (dv2 * e1.z - dv1 * e2.z) * invDet
        };
        float tLen = Vector3Length(tangent);
        if (tLen > 1e-5f) {
            tangent.x /= tLen; tangent.y /= tLen; tangent.z /= tLen;
        } else {
            tangent = (Vector3){1.0f, 0.0f, 0.0f};
        }
        for (int k = 0; k < 3; k++) {
            int vi = (mesh.indices ? (int)mesh.indices[t * 3 + k] : (t * 3 + k)) * 4;
            mesh.tangents[vi + 0] = tangent.x;
            mesh.tangents[vi + 1] = tangent.y;
            mesh.tangents[vi + 2] = tangent.z;
            mesh.tangents[vi + 3] = 1.0f;
        }
    }

    // Re-upload deformed procedural mesh (with its new vertices, UVs, normals, and tangents) to GPU VRAM
    if (mesh.vaoId > 0) {
        rlUnloadVertexArray(mesh.vaoId);
        mesh.vaoId = 0;
    }
    if (mesh.vboId != NULL) {
        for (int i = 0; i < 7; i++) {
            if (mesh.vboId[i] > 0) rlUnloadVertexBuffer(mesh.vboId[i]);
        }
        RL_FREE(mesh.vboId);
        mesh.vboId = NULL;
    }
    UploadMesh(&mesh, false);

    return mesh;
}

MapRockSet MapProp_CreateRocks(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMesh(0);
    MapProp_MeshBounds(&mesh, &rocks.boundsCenter, &rocks.boundsRadius);

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);
        Texture2D normal = MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT);
        Texture2D roughness = MapLoadMippedTexture(roughnessPath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

MapRockSet MapProp_CreateMountainCrags(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMesh(1);
    MapProp_MeshBounds(&mesh, &rocks.boundsCenter, &rocks.boundsRadius);

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);
        Texture2D normal = MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT);
        Texture2D roughness = MapLoadMippedTexture(roughnessPath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

MapRockSet MapProp_CreateRiverStones(const char *diffusePath, const char *normalPath, const char *roughnessPath)
{
    MapRockSet rocks = {0};
    Mesh mesh = Nature_BuildProceduralRockMesh(2);
    MapProp_MeshBounds(&mesh, &rocks.boundsCenter, &rocks.boundsRadius);

    if (normalPath && roughnessPath)
    {
        GenMeshTangents(&mesh);
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);
        Texture2D normal = MapLoadMippedTexture(normalPath, TEXTURE_WRAP_REPEAT);
        Texture2D roughness = MapLoadMippedTexture(roughnessPath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
    }
    else
    {
        rocks.model = LoadModelFromMesh(mesh);
        Texture2D diffuse = MapLoadMippedTexture(diffusePath, TEXTURE_WRAP_REPEAT);

        rocks.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
    }

    rocks.ready = true;
    return rocks;
}

// World-space bounding sphere of one placement. Encloses the mesh
// DrawModelEx will draw at `pos` with `scale`; `pad` widens it for anything
// drawn around the rock (fake blob shadows) that must not disappear with it.
static inline void Rock_PlacementBounds(const MapRockSet *set, Vector3 pos,
                                       Vector3 scale, float pad,
                                       Vector3 *outCenter, float *outRadius)
{
    // Rotation-independent: the placement origin, inflated to cover the mesh's
    // offset bounding centre too. Never smaller than the transformed mesh sphere.
    float maxScale = fmaxf(scale.x, fmaxf(scale.y, scale.z));
    float centerLen = sqrtf(set->boundsCenter.x * set->boundsCenter.x +
                            set->boundsCenter.y * set->boundsCenter.y +
                            set->boundsCenter.z * set->boundsCenter.z);
    *outCenter = pos;
    *outRadius = maxScale * (set->boundsRadius + centerLen) + pad;
}

void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count, bool drawShadow)
{
    if (!rocks->ready)
        return;

    // Tried DrawMeshInstanced() here to collapse the no-shadow case (the
    // mountain ring) into one draw call instead of `count` — reverted.
    // raylib's plain default material/shader (what the NULL,NULL branch of
    // MapProp_CreateRocks uses) isn't compiled with instancing support
    // (no `instanceTransform` attribute), so instances silently failed to
    // place/render correctly. Doing this properly needs a small dedicated
    // instanced-unlit shader — worth adding later if draw-call count (not
    // fill-rate/shadow overdraw, both already fixed) turns out to still
    // matter; not done speculatively.

    MapPropViewFrustum frustum;
    bool cull = MapProp_BuildViewFrustum(&frustum);
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < count; i++)
    {
        const MapRockPlacement *p = &placements[i];

        Vector3 pos = p->position;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};

        // API Môi trường - Đổ bóng giả tự động tính góc sáng. Bỏ qua cho
        // dàn đá dùng làm vách núi/viền — hàng chục cái bóng lớn xếp sát
        // nhau đè lên nhau (alpha overdraw) tốn fill-rate mà không thấy rõ
        // tác dụng.
        if (drawShadow) {
            float waterSurfaceY = 0.0f, waterDepth = 0.0f;
            bool inWater = MapManager_GetWaterInfoAt(p->position.x, p->position.z, &waterSurfaceY, &waterDepth);
            if (!inWater) {
                Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                            p->radiusScale * 2.0f, p->heightScale * 2.0f);
            }
        }

        if (cull)
        {
            Vector3 bc; float br;
            Rock_PlacementBounds(rocks, pos, scale,
                                 drawShadow ? 2.0f * p->radiusScale : 0.0f, &bc, &br);
            if (MapProp_FrustumCullsSphere(&frustum, bc, br)) continue;
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
    for (int i = 0; i < count; i++) {
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
    if (!rocks->ready)
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
    if (maxCount <= 0)
        return 0;

    SetRandomSeed(seed);

    float perimeter = 2.0f * (mapWidth + mapDepth);
    float centerX = mapWidth * 0.5f;
    float centerZ = mapDepth * 0.5f;

    for (int i = 0; i < maxCount; i++)
    {
        // Walk clockwise around the rectangle's border: +X along Z=0, +Z
        // along X=width, -X along Z=depth, -Z back along X=0.
        float t = ((float)i / (float)maxCount) * perimeter;
        float x, z;
        if (t < mapWidth) { x = t; z = 0.0f; }
        else if (t < mapWidth + mapDepth) { x = mapWidth; z = t - mapWidth; }
        else if (t < 2.0f * mapWidth + mapDepth) { x = mapWidth - (t - mapWidth - mapDepth); z = mapDepth; }
        else { x = 0.0f; z = mapDepth - (t - 2.0f * mapWidth - mapDepth); }

        // Jitter outward (away from map center) so peaks don't form a
        // perfectly straight wall, plus a little jitter along the border so
        // spacing doesn't look like an evenly-spaced pearl necklace. Kept
        // small — mapWidth/mapDepth is expected to already be sized a bit
        // smaller than the actual ground extent (see MapProp_CreateGroundHeightmap's
        // flat plateau boundary) so the whole ring — jitter included — stays
        // on FLAT ground; these rocks don't follow terrain height, so if a
        // placement lands on the sloped cliff band the rock visibly floats
        // above (or sinks below) the actual ground surface there.
        float outward = (float)GetRandomValue(-100, 200) / 100.0f;  // -1m..+2m
        float tangent = (float)GetRandomValue(-150, 150) / 100.0f;  // +/-1.5m
        float dx = x - centerX, dz = z - centerZ;
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 0.001f) { dx /= len; dz /= len; }
        x += dx * outward + (-dz) * tangent;
        z += dz * outward + (dx) * tangent;

        outPlacements[i].position = (Vector3){x, 0.0f, z};
        outPlacements[i].radiusScale = (float)GetRandomValue((int)(minRadiusScale * 100.0f), (int)(maxRadiusScale * 100.0f)) / 100.0f;
        outPlacements[i].heightScale = (float)GetRandomValue((int)(minHeightScale * 100.0f), (int)(maxHeightScale * 100.0f)) / 100.0f;
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

    for (int a = 0; a < MAP_ROCK_ARCHETYPE_COUNT; a++) {
        const char *dPath = (FileExists(kArchDiffuse[a])) ? kArchDiffuse[a] : diffusePath;
        const char *nPath = (normalPath && FileExists(kArchNormal[a])) ? kArchNormal[a] : normalPath;
        const char *rPath = (roughnessPath && FileExists(kArchRoughness[a])) ? kArchRoughness[a] : roughnessPath;

        Mesh mesh = Nature_BuildProceduralRockMesh(a);
        MapProp_MeshBounds(&mesh, &sys.models[a].boundsCenter, &sys.models[a].boundsRadius);

        if (nPath && rPath) {
            GenMeshTangents(&mesh);
            sys.models[a].model = LoadModelFromMesh(mesh);
            Texture2D diffuse = MapLoadMippedTexture(dPath, TEXTURE_WRAP_REPEAT);
            Texture2D normal = MapLoadMippedTexture(nPath, TEXTURE_WRAP_REPEAT);
            Texture2D roughness = MapLoadMippedTexture(rPath, TEXTURE_WRAP_REPEAT);
            sys.models[a].model.materials[0] = PropLit_MakeMaterial(diffuse, normal, roughness);
        } else {
            sys.models[a].model = LoadModelFromMesh(mesh);
            Texture2D diffuse = MapLoadMippedTexture(dPath, TEXTURE_WRAP_REPEAT);
            sys.models[a].model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffuse;
        }
        sys.models[a].ready = true;
    }

    // Aliases for backwards compatibility
    sys.boulders = sys.models[MAP_ROCK_BOULDER];
    sys.crags = sys.models[MAP_ROCK_CRAG];
    sys.riverStones = sys.models[MAP_ROCK_RIVER_STONE];

    if (capacity > 0) {
        sys.items = (MapRockItem *)RL_MALLOC(capacity * sizeof(MapRockItem));
        sys.capacity = capacity;
    }
    sys.count = 0;
    sys.ready = true;
    return sys;
}

bool MapProp_AddRockItem(MapRockSystem *sys, Vector3 pos, float rScale, float hScale, float rotDeg, MapRockArchetype archetype)
{
    if (!sys || !sys->ready || sys->count >= sys->capacity) return false;
    sys->items[sys->count++] = (MapRockItem){
        .position = pos,
        .radiusScale = rScale,
        .heightScale = hScale,
        .rotationDeg = rotDeg,
        .archetype = archetype
    };
    return true;
}

int MapProp_AddRockCluster(MapRockSystem *sys, Vector3 center, MapRockArchetype archetype,
                           int count, float clusterRadius, float baseScale, unsigned int seed)
{
    if (!sys || count <= 0) return 0;
    unsigned int rng = seed ? seed : 0x7a3b19u;
    int added = 0;

    // Primary anchor stone
    float anchorRot = Nature_Random01(&rng) * 360.0f;
    float anchorH = (archetype == MAP_ROCK_CRAG || archetype == MAP_ROCK_LIMESTONE) ? (baseScale * (1.6f + 0.6f * Nature_Random01(&rng)))
                                                 : (baseScale * (0.65f + 0.25f * Nature_Random01(&rng)));
    if (MapProp_AddRockItem(sys, center, baseScale, anchorH, anchorRot, archetype)) added++;

    // Companion stones orbiting anchor
    for (int i = 1; i < count; i++) {
        float angle = Nature_Random01(&rng) * 2.0f * PI;
        float dist = clusterRadius * (0.35f + 0.65f * Nature_Random01(&rng));
        Vector3 compPos = {center.x + cosf(angle) * dist, center.y, center.z + sinf(angle) * dist};
        float companionScale = baseScale * (0.35f + 0.45f * Nature_Random01(&rng));
        float compRot = anchorRot + (Nature_Random01(&rng) - 0.5f) * 60.0f;
        float compH = (archetype == MAP_ROCK_CRAG || archetype == MAP_ROCK_LIMESTONE) ? (companionScale * (1.4f + 0.5f * Nature_Random01(&rng)))
                                                   : (companionScale * (0.55f + 0.30f * Nature_Random01(&rng)));
        if (MapProp_AddRockItem(sys, compPos, companionScale, compH, compRot, archetype)) added++;
    }
    return added;
}

void MapProp_DrawRockSystem(const MapRockSystem *sys, bool drawShadows)
{
    if (!sys || !sys->ready || sys->count <= 0) return;

    PropLit_UpdateLighting();
    MapPropViewFrustum frustum;
    bool cull = MapProp_BuildViewFrustum(&frustum);
    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < sys->count; i++) {
        const MapRockItem *p = &sys->items[i];
        const MapRockSet *set;
        if (p->archetype < MAP_ROCK_ARCHETYPE_COUNT && sys->models[p->archetype].ready) {
            set = &sys->models[p->archetype];
        } else if (p->archetype == MAP_ROCK_CRAG) {
            set = &sys->crags;
        } else if (p->archetype == MAP_ROCK_RIVER_STONE) {
            set = &sys->riverStones;
        } else {
            set = &sys->boulders;
        }
        Model model = set->model;
        if (!set->ready) continue;

        Vector3 pos = p->position;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};

        // One DrawModelEx per rock; skip the ones whose enclosing sphere is
        // fully outside the camera frustum (exact: nothing on screen changes).
        if (cull)
        {
            float shadowPad = (drawShadows && p->archetype != MAP_ROCK_CRAG && p->archetype != MAP_ROCK_LIMESTONE)
                                  ? 2.0f * p->radiusScale : 0.0f;
            Vector3 bc; float br;
            Rock_PlacementBounds(set, pos, scale, shadowPad, &bc, &br);
            if (MapProp_FrustumCullsSphere(&frustum, bc, br)) continue;
        }

        if (drawShadows && p->archetype != MAP_ROCK_CRAG && p->archetype != MAP_ROCK_LIMESTONE) {
            float waterSurfaceY = 0.0f, waterDepth = 0.0f;
            bool inWater = MapManager_GetWaterInfoAt(p->position.x, p->position.z, &waterSurfaceY, &waterDepth);
            if (!inWater) {
                Environment_DrawSmartShadow(p->position, ENV_SHAPE_SPHERE,
                                            p->radiusScale * 2.0f, p->heightScale * 2.0f);
            }
        }

        Color rockTint = (Color){235, 230, 222, 255}; // warm weathered granite tor
        if (p->archetype == MAP_ROCK_SANDSTONE) {
            rockTint = (Color){255, 248, 235, 255}; // warm golden glow
        } else if (p->archetype == MAP_ROCK_RIVER_STONE) {
            rockTint = (Color){155, 168, 175, 255}; // natural deep freshwater mineral / wet river stone
        } else if (p->archetype == MAP_ROCK_SCREE) {
            rockTint = (Color){215, 210, 202, 255}; // natural weathered granite talus chips
        } else if (p->archetype == MAP_ROCK_LIMESTONE || p->archetype == MAP_ROCK_CRAG) {
            rockTint = (Color){252, 250, 245, 255}; // pale ivory karst
        }

        DrawModelEx(model, pos, rotAxis, p->rotationDeg, scale, rockTint);
    }
}

void MapProp_DrawRockSystemShadowCasters(MapRockSystem *sys, Shader depthShader)
{
    if (!sys || !sys->ready || sys->count <= 0) return;

    Vector3 rotAxis = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i < sys->count; i++) {
        const MapRockItem *p = &sys->items[i];
        const MapRockSet *set;
        if (p->archetype < MAP_ROCK_ARCHETYPE_COUNT && sys->models[p->archetype].ready) {
            set = &sys->models[p->archetype];
        } else if (p->archetype == MAP_ROCK_CRAG) {
            set = &sys->crags;
        } else if (p->archetype == MAP_ROCK_RIVER_STONE) {
            set = &sys->riverStones;
        } else {
            set = &sys->boulders;
        }

        Model model = set->model;
        if (model.materialCount < 1) continue;
        Shader prev = model.materials[0].shader;
        model.materials[0].shader = depthShader;

        Vector3 pos = p->position;
        Vector3 scale = {p->radiusScale, p->heightScale, p->radiusScale};
        DrawModelEx(model, pos, rotAxis, p->rotationDeg, scale, WHITE);

        rlDrawRenderBatchActive();
        model.materials[0].shader = prev;
    }
}

void MapProp_UnloadRockSystem(MapRockSystem *sys)
{
    if (!sys || !sys->ready) return;
    for (int a = 0; a < MAP_ROCK_ARCHETYPE_COUNT; a++) {
        if (sys->models[a].ready) {
            MapProp_UnloadRocks(&sys->models[a]);
        }
    }
    if (sys->items) {
        RL_FREE(sys->items);
        sys->items = NULL;
    }
    sys->count = 0;
    sys->capacity = 0;
    sys->ready = false;
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
        pos.y -= sinkOffset;
        MapProp_AddRockItem(sys, pos, item->radiusScale, heightScale, item->rotationDeg, item->archetype);
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
            MapProp_AddRockItem(sys, pos, scale, scale * 1.25f, rot, MAP_ROCK_CRAG);
        }
    }
}


