#ifndef MAP_TOOLKIT_MAP_PROPS_CLIFF_INL
#define MAP_TOOLKIT_MAP_PROPS_CLIFF_INL

// ============================================================================
// WUXING — Sculpted Island Cliff Skirt
// Bakes a high-detail rock face around the island rim. The skirt is anchored
// at the real turf lip (found by marching inland from a low coast contour),
// tucks its top edge under the meadow, bulges past the low-poly heightfield
// wall so the stretched terrain edge is never visible, and tapers back under
// the cloud sea as a floating-island keel.
// ============================================================================

typedef struct
{
    float depth;  // Metres below the local turf lip
    float out;    // Surface-normal offset in metres (negative = buried)
    float noise;  // Buttress / erosion displacement multiplier
    float ao;     // Base cavity ambient occlusion
    float moss;   // Rain-fed moss & lichen near the turf line
    float haze;   // Cloud-sea dissolve factor
    float strata; // Geological strata phase strength
} CliffRowProfile;

static const CliffRowProfile kCliffRowProfiles[17] = {
    // depth, out, noise, ao,   moss, haze, strata
    {0.00f, -0.60f, 0.00f, 0.95f, 0.85f, 0.00f, 0.55f},  // buried collar under the turf
    {0.10f, 0.10f, 0.00f, 0.90f, 1.00f, 0.00f, 0.75f},   // turf lip emergence
    {0.28f, 0.16f, 0.40f, 0.82f, 0.95f, 0.00f, 0.90f},   // overhang lip
    {0.55f, 0.24f, 0.62f, 0.74f, 0.84f, 0.00f, 1.00f},   // upper mossy face
    {0.90f, 0.30f, 0.76f, 0.66f, 0.60f, 0.00f, 1.05f},   // first ledge
    {1.32f, 0.34f, 0.86f, 0.60f, 0.40f, 0.00f, 1.10f},   // upper buttress
    {1.80f, 0.38f, 0.93f, 0.55f, 0.24f, 0.00f, 1.12f},   // mid wall
    {2.36f, 0.40f, 0.97f, 0.51f, 0.13f, 0.02f, 1.10f},   // deep wall
    {3.00f, 0.40f, 0.97f, 0.48f, 0.06f, 0.06f, 1.06f},   // lower wall
    {3.74f, 0.38f, 0.92f, 0.46f, 0.02f, 0.13f, 1.02f},   // cloud-line entry
    {4.60f, 0.32f, 0.84f, 0.44f, 0.00f, 0.24f, 0.98f},   // cloud immersion
    {5.60f, 0.22f, 0.72f, 0.42f, 0.00f, 0.40f, 0.94f},   // under-cloud shoulder
    {6.90f, 0.08f, 0.58f, 0.40f, 0.00f, 0.58f, 0.90f},   // below the terrain toe
    {8.80f, -0.70f, 0.42f, 0.38f, 0.00f, 0.72f, 0.86f},  // keel start
    {11.60f, -2.20f, 0.30f, 0.36f, 0.00f, 0.84f, 0.82f}, // keel
    {15.60f, -4.60f, 0.18f, 0.35f, 0.00f, 0.93f, 0.78f}, // deep keel
    {21.00f, -8.00f, 0.10f, 0.34f, 0.00f, 0.98f, 0.74f}, // abyss tip
};

#define CLIFF_ROW_COUNT 17

static unsigned int s_cliffCachedShaderId = 0;
static int s_cliffLocCloudWorldFromShader = -1;
static int s_cliffLocLightDir = -1;
static int s_cliffLocLightColor = -1;
static int s_cliffLocAmbientColor = -1;
static int s_cliffLocViewPos = -1;
static int s_cliffLocHazeColor = -1;

static void Cliff_EnsureShaderLocs(Shader shader)
{
    if (shader.id == 0 || s_cliffCachedShaderId == shader.id)
        return;

    s_cliffLocCloudWorldFromShader = GetShaderLocation(shader, "u_cloudWorldFromShader");
    s_cliffLocLightDir = GetShaderLocation(shader, "u_lightDir");
    s_cliffLocLightColor = GetShaderLocation(shader, "u_lightColor");
    s_cliffLocAmbientColor = GetShaderLocation(shader, "u_ambientColor");
    s_cliffLocViewPos = GetShaderLocation(shader, "u_viewPos");
    s_cliffLocHazeColor = GetShaderLocation(shader, "u_hazeColor");
    s_cliffCachedShaderId = shader.id;
}

// ── Deterministic 1D value noise (bake-time only) ───────────────────────────

static inline float Cliff_Hash01(int x, int seed)
{
    unsigned int h = (unsigned int)x * 0x27d4eb2du ^ (unsigned int)seed * 0x165667b1u;
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return (float)(h & 0x00ffffffu) * (1.0f / 16777216.0f);
}

static inline float Cliff_Noise1(float x, int seed)
{
    float fx = floorf(x);
    int i = (int)fx;
    float t = x - fx;
    t = t * t * (3.0f - 2.0f * t);
    float a = Cliff_Hash01(i, seed);
    float b = Cliff_Hash01(i + 1, seed);
    return a + (b - a) * t;
}

static float Cliff_Fbm1(float x, int seed, int octaves)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; o++)
    {
        sum += (Cliff_Noise1(x, seed + o * 131) * 2.0f - 1.0f) * amp;
        norm += amp;
        x *= 2.03f;
        amp *= 0.5f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

static void Cliff_UpdateLighting(Shader shader, Camera3D cam)
{
    if (shader.id == 0)
        return;

    Cliff_EnsureShaderLocs(shader);

    rlDrawRenderBatchActive();
    BeginShaderMode(shader);
    Environment_BindCloudShadowShader(shader);
    Matrix shaderFromWorld = rlGetMatrixTransform();
    if (s_cliffLocCloudWorldFromShader >= 0)
    {
        SetShaderValueMatrix(shader, s_cliffLocCloudWorldFromShader,
                             MatrixInvert(shaderFromWorld));
    }

    if (s_cliffLocLightDir >= 0)
    {
        Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
        Vector3 viewLightDir = {
            shaderFromWorld.m0 * lightDir.x + shaderFromWorld.m4 * lightDir.y + shaderFromWorld.m8 * lightDir.z,
            shaderFromWorld.m1 * lightDir.x + shaderFromWorld.m5 * lightDir.y + shaderFromWorld.m9 * lightDir.z,
            shaderFromWorld.m2 * lightDir.x + shaderFromWorld.m6 * lightDir.y + shaderFromWorld.m10 * lightDir.z};
        viewLightDir = Vector3Normalize(viewLightDir);
        SetShaderValue(shader, s_cliffLocLightDir, &viewLightDir, SHADER_UNIFORM_VEC3);
    }

    if (s_cliffLocLightColor >= 0)
    {
        Vector4 c = ColorNormalize(Environment_GetSunColor());
        Vector3 rgb = Vector3Scale((Vector3){c.x, c.y, c.z}, Environment_GetSunIntensity());
        SetShaderValue(shader, s_cliffLocLightColor, &rgb, SHADER_UNIFORM_VEC3);
    }

    if (s_cliffLocAmbientColor >= 0)
    {
        Vector4 c = ColorNormalize(Environment_GetAmbientColor());
        Vector3 rgb = {c.x, c.y, c.z};
        SetShaderValue(shader, s_cliffLocAmbientColor, &rgb, SHADER_UNIFORM_VEC3);
    }

    if (s_cliffLocViewPos >= 0)
    {
        Vector3 viewPos = Vector3Transform(cam.position, shaderFromWorld);
        SetShaderValue(shader, s_cliffLocViewPos, &viewPos, SHADER_UNIFORM_VEC3);
    }

    if (s_cliffLocHazeColor >= 0)
    {
        EnvFogConfig fog = Environment_GetFogConfig();
        Vector4 fogNorm = ColorNormalize(fog.color);
        Vector3 haze = (Vector3){fogNorm.x, fogNorm.y, fogNorm.z};
        SetShaderValue(shader, s_cliffLocHazeColor, &haze, SHADER_UNIFORM_VEC3);
    }

    MapShadow_UpdateShader(shader);
    EndShaderMode();
}

// ── Ordered coast contour via marching squares ─────────────────────────────
// The triangle-walk extractor can emit unordered / self-intersecting loops
// (large perimeter, near-zero signed area), which makes every coast normal
// point in a random direction. Sample the ground on a regular grid and trace
// the iso-height contour with a consistent orientation instead.

typedef struct
{
    int start;
    int end;
    bool used;
} CliffSeg;

static void Cliff_EdgePoint(const float *h, int nx, int nz, float level,
                            float originX, float originZ, float dx, float dz,
                            int edgeId, Vector3 *out)
{
    int vertBase = nx * nz;
    int horizontal = edgeId < vertBase;
    int id = horizontal ? edgeId : edgeId - vertBase;
    int j = id / nx;
    int i = id - j * nx;
    float ax, az, bx, bz, ha, hb;
    if (horizontal)
    {
        ax = originX + (float)i * dx;
        az = originZ + (float)j * dz;
        bx = ax + dx;
        bz = az;
        ha = h[j * nx + i];
        hb = h[j * nx + i + 1];
    }
    else
    {
        ax = originX + (float)i * dx;
        az = originZ + (float)j * dz;
        bx = ax;
        bz = az + dz;
        ha = h[j * nx + i];
        hb = h[(j + 1) * nx + i];
    }
    float denom = hb - ha;
    float t = (fabsf(denom) > 1e-5f) ? (level - ha) / denom : 0.5f;
    t = fminf(1.0f, fmaxf(0.0f, t));
    out->x = ax + (bx - ax) * t;
    out->y = level;
    out->z = az + (bz - az) * t;
}

static int Cliff_BuildCoastLoop(const MapGroundSurface *ground, Vector3 mapCenter,
                                float level, Vector3 **outPoints)
{
    *outPoints = NULL;
    if (!ground || !ground->ready || ground->model.meshCount < 1)
        return 0;
    const Mesh *mesh = &ground->model.meshes[0];
    if (!mesh->vertices || mesh->vertexCount < 4)
        return 0;

    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int v = 0; v < mesh->vertexCount; v++)
    {
        float x = mesh->vertices[v * 3 + 0];
        float z = mesh->vertices[v * 3 + 2];
        if (x < minX)
            minX = x;
        if (x > maxX)
            maxX = x;
        if (z < minZ)
            minZ = z;
        if (z > maxZ)
            maxZ = z;
    }
    float width = maxX - minX, depth = maxZ - minZ;
    if (width < 2.0f || depth < 2.0f)
        return 0;

    // Stay one metre inside the mesh bounds: `MapProp_SampleGroundHeight`
    // reports the map-centre height when a lookup misses, which would turn
    // the outer border into fake land.
    const float inset = 1.0f;
    minX += inset;
    maxX -= inset;
    minZ += inset;
    maxZ -= inset;
    width = maxX - minX;
    depth = maxZ - minZ;
    if (width < 2.0f || depth < 2.0f)
        return 0;

    float spacing = fmaxf(fmaxf(width, depth) / 360.0f, 0.9f);
    int nx = (int)(width / spacing) + 1;
    int nz = (int)(depth / spacing) + 1;
    if (nx < 4 || nz < 4 || (long long)nx * nz > 300000)
        return 0;
    float dx = width / (float)(nx - 1);
    float dz = depth / (float)(nz - 1);
    float originX = mapCenter.x + ground->drawOffset.x + minX;
    float originZ = mapCenter.z + ground->drawOffset.z + minZ;

    float *h = MemAlloc((size_t)nx * (size_t)nz * sizeof(float));
    if (!h)
        return 0;
    for (int j = 0; j < nz; j++)
    {
        float z = originZ + (float)j * dz;
        int rowOffset = j * nx;
        for (int i = 0; i < nx; i++)
        {
            float x = originX + (float)i * dx;
            Vector3 p, n;
            h[rowOffset + i] = MapProp_SampleGroundSurface(ground, mapCenter, x, z, &p, &n)
                                   ? p.y
                                   : (level - 1000.0f);
        }
    }

    int vertBase = nx * nz;
    int edgeBase = vertBase * 2;
    int *segStart = MemAlloc((size_t)edgeBase * sizeof(int));
    int *segEnd = MemAlloc((size_t)edgeBase * sizeof(int));
    int *outEdge = MemAlloc((size_t)edgeBase * sizeof(int));
    if (!segStart || !segEnd || !outEdge)
    {
        MemFree(h);
        MemFree(segStart);
        MemFree(segEnd);
        MemFree(outEdge);
        return 0;
    }
    for (int i = 0; i < edgeBase; i++)
        outEdge[i] = -1;

    int segCount = 0;
    int verticalBase = vertBase;

    for (int j = 0; j < nz - 1; j++)
    {
        int rowCurr = j * nx;
        int rowNext = (j + 1) * nx;
        for (int i = 0; i < nx - 1; i++)
        {
            float ha = h[rowCurr + i];
            float hb = h[rowCurr + i + 1];
            float hc = h[rowNext + i + 1];
            float hd = h[rowNext + i];
            int idx = (ha > level ? 1 : 0) | (hb > level ? 2 : 0) |
                      (hc > level ? 4 : 0) | (hd > level ? 8 : 0);
            if (idx == 0 || idx == 15)
                continue;

            int p0 = rowCurr + i;                      // bottom (a-b)
            int p1 = verticalBase + rowCurr + (i + 1); // right (b-c)
            int p2 = rowNext + i;                      // top (d-c)
            int p3 = verticalBase + rowCurr + i;       // left (a-d)

            int pairs[4];
            int pairCount = 0;
            switch (idx)
            {
            case 1:
                pairs[0] = p3;
                pairs[1] = p0;
                pairCount = 1;
                break;
            case 2:
                pairs[0] = p0;
                pairs[1] = p1;
                pairCount = 1;
                break;
            case 3:
                pairs[0] = p3;
                pairs[1] = p1;
                pairCount = 1;
                break;
            case 4:
                pairs[0] = p1;
                pairs[1] = p2;
                pairCount = 1;
                break;
            case 5:
                if (ha + hb + hc + hd > level * 4.0f)
                {
                    pairs[0] = p3;
                    pairs[1] = p2;
                    pairs[2] = p1;
                    pairs[3] = p0;
                }
                else
                {
                    pairs[0] = p3;
                    pairs[1] = p0;
                    pairs[2] = p1;
                    pairs[3] = p2;
                }
                pairCount = 2;
                break;
            case 6:
                pairs[0] = p0;
                pairs[1] = p2;
                pairCount = 1;
                break;
            case 7:
                pairs[0] = p3;
                pairs[1] = p2;
                pairCount = 1;
                break;
            case 8:
                pairs[0] = p2;
                pairs[1] = p3;
                pairCount = 1;
                break;
            case 9:
                pairs[0] = p2;
                pairs[1] = p0;
                pairCount = 1;
                break;
            case 10:
                if (ha + hb + hc + hd > level * 4.0f)
                {
                    pairs[0] = p0;
                    pairs[1] = p3;
                    pairs[2] = p2;
                    pairs[3] = p1;
                }
                else
                {
                    pairs[0] = p0;
                    pairs[1] = p1;
                    pairs[2] = p2;
                    pairs[3] = p3;
                }
                pairCount = 2;
                break;
            case 11:
                pairs[0] = p2;
                pairs[1] = p1;
                pairCount = 1;
                break;
            case 12:
                pairs[0] = p1;
                pairs[1] = p3;
                pairCount = 1;
                break;
            case 13:
                pairs[0] = p1;
                pairs[1] = p0;
                pairCount = 1;
                break;
            case 14:
                pairs[0] = p0;
                pairs[1] = p3;
                pairCount = 1;
                break;
            default:
                break;
            }
            for (int p = 0; p < pairCount; p++)
            {
                if (segCount >= edgeBase)
                    break;
                int a = pairs[p * 2 + 0];
                int b = pairs[p * 2 + 1];
                segStart[segCount] = a;
                segEnd[segCount] = b;
                if (outEdge[a] < 0)
                    outEdge[a] = segCount;
                segCount++;
            }
        }
    }

    int bestCount = 0;
    double bestArea = 0.0;
    Vector3 *bestLoop = NULL;
    bool *segDone = MemAlloc((size_t)edgeBase * sizeof(bool));
    if (segDone)
    {
        memset(segDone, 0, (size_t)edgeBase * sizeof(bool));
        for (int s0 = 0; s0 < segCount; s0++)
        {
            if (segDone[s0])
                continue;
            int cap = segCount + 1;
            Vector3 *tmp = MemAlloc((size_t)cap * sizeof(Vector3));
            if (!tmp)
                break;
            int n = 0;
            int cur = s0;
            while (cur >= 0 && !segDone[cur] && n < cap)
            {
                segDone[cur] = true;
                Vector3 p;
                Cliff_EdgePoint(h, nx, nz, level, originX, originZ, dx, dz, segStart[cur], &p);
                tmp[n++] = p;
                int next = outEdge[segEnd[cur]];
                if (next == s0)
                    break;
                cur = next;
            }
            double area = 0.0;
            for (int i = 0; i < n; i++)
            {
                int next = (i + 1) % n;
                area += (double)tmp[i].x * (double)tmp[next].z -
                        (double)tmp[next].x * (double)tmp[i].z;
            }
            if (n > 60)
            {
                float zmin = 1e9f, zmax = -1e9f, xmin = 1e9f, xmax = -1e9f;
                for (int i = 0; i < n; i++)
                {
                    zmin = fminf(zmin, tmp[i].z);
                    zmax = fmaxf(zmax, tmp[i].z);
                    xmin = fminf(xmin, tmp[i].x);
                    xmax = fmaxf(xmax, tmp[i].x);
                }
                TraceLog(LOG_INFO, "CLIFF_LOOP n=%d area=%.0f bbox=(%.0f..%.0f, %.0f..%.0f)",
                         n, area, xmin, xmax, zmin, zmax);
            }
            if (n >= 3 && fabs(area) > bestArea)
            {
                MemFree(bestLoop);
                bestLoop = tmp;
                bestCount = n;
                bestArea = fabs(area);
                tmp = NULL;
            }
            if (tmp)
                MemFree(tmp);
        }
    }
    MemFree(segDone);
    MemFree(segStart);
    MemFree(segEnd);
    MemFree(outEdge);
    MemFree(h);

    if (bestCount < 3)
    {
        MemFree(bestLoop);
        return 0;
    }
    *outPoints = bestLoop;
    return bestCount;
}

// Find the turfy lip above a low coast contour by marching inland.
// Follows the profile up from the deep contour until it levels off onto the coastal rim,
// strictly bounded so it never climbs interior hills near the coast.
static Vector3 Cliff_FindLip(const MapGroundSurface *ground, Vector3 mapCenter,
                             Vector3 contourPoint, Vector2 inward, float contourY)
{
    const float step = 0.28f;
    const int maxSteps = 28; // ~7.84m max inland search to reach tall scarp plateaus
    const float maxRimY = contourY + 6.8f; // allow finding the true plateau rim (up to +5.4m)
    Vector3 samples[32];
    Vector3 normals[32];
    bool valid[32];
    int validCount = 0;
    float peakY = -1e9f;
    int peakIdx = 0;

    for (int k = 0; k < maxSteps; k++)
    {
        float d = (float)(k + 1) * step;
        Vector3 p, n;
        valid[k] = MapProp_SampleGroundSurface(ground, mapCenter,
                                               contourPoint.x + inward.x * d,
                                               contourPoint.z + inward.y * d, &p, &n);
        if (valid[k])
        {
            if (p.y > maxRimY)
                p.y = maxRimY;
            samples[k] = p;
            normals[k] = n;
            validCount++;
            if (p.y > peakY)
            {
                peakY = p.y;
                peakIdx = k;
            }
        }
        else
        {
            samples[k] = contourPoint;
            normals[k] = (Vector3){0, 1, 0};
        }
    }

    if (validCount == 0)
        return contourPoint;

    // Search from contour inward for the transition onto the plateau lip
    for (int k = 1; k < maxSteps - 2; k++)
    {
        if (!valid[k])
            continue;
        float y = samples[k].y;
        float ny = normals[k].y;

        // Stop immediately once we reach the coastal turf lip ceiling
        if (y >= maxRimY - 0.05f)
            return samples[k];

        // Must have climbed out of the deep drop
        if (y < contourY + 0.65f && y < -0.45f)
            continue;

        int kAhead = (k + 2 < maxSteps && valid[k + 2]) ? k + 2 : k + 1;
        float forwardRise = samples[kAhead].y - y;

        bool leveledOff = (forwardRise <= 0.16f && ny >= 0.78f);
        bool nearPeak = (y >= peakY - 0.20f && forwardRise <= 0.14f && ny >= 0.75f);
        bool crest = (valid[k + 1] && samples[k + 1].y < y - 0.03f && y >= -0.45f);

        if (leveledOff || nearPeak || crest)
        {
            return samples[k];
        }
    }

    if (valid[peakIdx])
        return samples[peakIdx];
    return contourPoint;
}

MapCliffSkirt MapProp_CreateCliffSkirt(const MapGroundSurface *ground, Vector3 mapCenter, float sliceHeight, float bottomY)
{
    (void)bottomY;
    MapCliffSkirt cliff = {0};
    if (!ground || !ground->ready)
        return cliff;

    Vector3 *worldPoints = NULL;
    int rawCount = Cliff_BuildCoastLoop(ground, mapCenter, sliceHeight, &worldPoints);
    if (rawCount < 3 || !worldPoints)
    {
        TraceLog(LOG_WARNING, "MapCliffSkirt: coast contour trace failed (count=%d)", rawCount);
        if (worldPoints)
            MemFree(worldPoints);
        return cliff;
    }

    double signedArea = 0.0;
    for (int i = 0; i < rawCount; i++)
    {
        int next = (i + 1) % rawCount;
        signedArea += (double)worldPoints[i].x * (double)worldPoints[next].z -
                      (double)worldPoints[next].x * (double)worldPoints[i].z;
    }
    float orient = (signedArea >= 0.0) ? 1.0f : -1.0f;

    // Total perimeter arc length
    float totalPerimeter = 0.0f;
    for (int i = 0; i < rawCount; i++)
    {
        int next = (i + 1) % rawCount;
        float dx = worldPoints[next].x - worldPoints[i].x;
        float dz = worldPoints[next].z - worldPoints[i].z;
        totalPerimeter += sqrtf(dx * dx + dz * dz);
    }
    if (totalPerimeter < 1.0f)
    {
        MemFree(worldPoints);
        return cliff;
    }

    // Target ~0.95m column spacing, clustered in chunks of 48 columns
    int desiredCols = (int)(totalPerimeter / 0.95f + 0.5f);
    int numChunks = (desiredCols + 47) / 48;
    if (numChunks < 4)
        numChunks = 4;
    if (numChunks > MAX_CLIFF_CHUNKS)
        numChunks = MAX_CLIFF_CHUNKS;
    int M = numChunks * 48;
    float colStep = totalPerimeter / (float)M;

    // Resample the polyline evenly into M coast columns
    Vector3 *coastPos = MemAlloc((size_t)M * sizeof(Vector3));
    float *colArcLen = MemAlloc((size_t)M * sizeof(float));
    if (!coastPos || !colArcLen)
    {
        MemFree(worldPoints);
        MemFree(coastPos);
        MemFree(colArcLen);
        return cliff;
    }

    float segAccum = 0.0f;
    int segIdx = 0;
    float dx0 = worldPoints[1].x - worldPoints[0].x;
    float dz0 = worldPoints[1].z - worldPoints[0].z;
    float segLen = sqrtf(dx0 * dx0 + dz0 * dz0);
    for (int i = 0; i < M; i++)
    {
        float targetDist = (float)i * colStep;
        colArcLen[i] = targetDist;
        while (segAccum + segLen < targetDist && segIdx < rawCount - 1)
        {
            segAccum += segLen;
            segIdx++;
            int nextIdx = (segIdx + 1) % rawCount;
            float sdx = worldPoints[nextIdx].x - worldPoints[segIdx].x;
            float sdz = worldPoints[nextIdx].z - worldPoints[segIdx].z;
            segLen = sqrtf(sdx * sdx + sdz * sdz);
        }
        float t = (segLen > 1e-5f) ? ((targetDist - segAccum) / segLen) : 0.0f;
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;
        int nextIdx = (segIdx + 1) % rawCount;
        coastPos[i] = (Vector3){
            worldPoints[segIdx].x + (worldPoints[nextIdx].x - worldPoints[segIdx].x) * t,
            worldPoints[segIdx].y + (worldPoints[nextIdx].y - worldPoints[segIdx].y) * t,
            worldPoints[segIdx].z + (worldPoints[nextIdx].z - worldPoints[segIdx].z) * t};
    }
    MemFree(worldPoints);

    // Coast outward normals from a wide stencil (immune to resampling jitter)
    Vector2 *coastNorm = MemAlloc((size_t)M * sizeof(Vector2));
    if (!coastNorm)
    {
        MemFree(coastPos);
        MemFree(colArcLen);
        return cliff;
    }
    for (int i = 0; i < M; i++)
    {
        int a = (i - 4 + M) % M;
        int b = (i + 4) % M;
        float dx = coastPos[b].x - coastPos[a].x;
        float dz = coastPos[b].z - coastPos[a].z;
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 1e-5f)
        {
            float invLen = 1.0f / len;
            dx *= invLen;
            dz *= invLen;
        }
        coastNorm[i] = (Vector2){dz * orient, -dx * orient};
    }

    // Find the true turf lip at every column, then smooth the resulting rim.
    Vector3 *rimPos = MemAlloc((size_t)M * sizeof(Vector3));
    float *rimY = MemAlloc((size_t)M * sizeof(float));
    if (!rimPos || !rimY)
    {
        MemFree(coastPos);
        MemFree(colArcLen);
        MemFree(coastNorm);
        MemFree(rimPos);
        MemFree(rimY);
        return cliff;
    }
    for (int i = 0; i < M; i++)
    {
        Vector2 inward = {-coastNorm[i].x, -coastNorm[i].y};
        Vector3 lip = Cliff_FindLip(ground, mapCenter, coastPos[i], inward, sliceHeight);
        // Anchor just behind the lip so the collar is buried by the meadow.
        rimPos[i] = (Vector3){
            lip.x + inward.x * 0.22f,
            lip.y,
            lip.z + inward.y * 0.22f};
        Vector3 gp, gn;
        if (MapProp_SampleGroundSurface(ground, mapCenter, rimPos[i].x, rimPos[i].z, &gp, &gn))
            rimY[i] = fminf(gp.y, sliceHeight + 6.80f);
        else
            rimY[i] = fminf(lip.y, sliceHeight + 6.80f);
    }

    // Light smoothing keeps the rim line organic but kills stair steps.
    Vector3 *scratch = MemAlloc((size_t)M * sizeof(Vector3));
    float *scratchY = MemAlloc((size_t)M * sizeof(float));
    if (!scratch || !scratchY)
    {
        MemFree(coastPos);
        MemFree(colArcLen);
        MemFree(coastNorm);
        MemFree(rimPos);
        MemFree(rimY);
        MemFree(scratch);
        MemFree(scratchY);
        return cliff;
    }
    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < M; i++)
        {
            int prev = (i - 1 + M) % M;
            int next = (i + 1) % M;
            scratch[i].x = (rimPos[prev].x + 2.0f * rimPos[i].x + rimPos[next].x) * 0.25f;
            scratch[i].y = (rimPos[prev].y + 2.0f * rimPos[i].y + rimPos[next].y) * 0.25f;
            scratch[i].z = (rimPos[prev].z + 2.0f * rimPos[i].z + rimPos[next].z) * 0.25f;
            scratchY[i] = (rimY[prev] + 2.0f * rimY[i] + rimY[next]) * 0.25f;
        }
        memcpy(rimPos, scratch, (size_t)M * sizeof(Vector3));
        memcpy(rimY, scratchY, (size_t)M * sizeof(float));
    }
    MemFree(scratch);
    MemFree(scratchY);
    for (int i = 0; i < M; i++)
        rimPos[i].y = rimY[i] - 0.02f;
    MemFree(coastPos);

    float rimMin = 1e9f, rimMax = -1e9f;
    for (int i = 0; i < M; i++)
    {
        rimMin = fminf(rimMin, rimPos[i].y);
        rimMax = fmaxf(rimMax, rimPos[i].y);
    }
    TraceLog(LOG_INFO, "MapCliffSkirt: rimY=[%.2f, %.2f] sample=(%.1f,%.1f,%.1f)",
             rimMin, rimMax, rimPos[0].x, rimPos[0].y, rimPos[0].z);

    // Rim normals from the smoothed rim line
    Vector2 *rimNorm = MemAlloc((size_t)M * sizeof(Vector2));
    if (!rimNorm)
    {
        MemFree(colArcLen);
        MemFree(coastNorm);
        MemFree(rimPos);
        MemFree(rimY);
        return cliff;
    }
    for (int i = 0; i < M; i++)
    {
        int a = (i - 4 + M) % M;
        int b = (i + 4) % M;
        float dx = rimPos[b].x - rimPos[a].x;
        float dz = rimPos[b].z - rimPos[a].z;
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 1e-5f)
        {
            float invLen = 1.0f / len;
            dx *= invLen;
            dz *= invLen;
        }
        rimNorm[i] = (Vector2){dz * orient, -dx * orient};
    }
    MemFree(coastNorm);

    // ── Trace the real cross-section of the heightfield edge ────────────────
    float *profD = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(float));
    float *profNO = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(float));
    float *profNU = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(float));
    if (!profD || !profNO || !profNU)
    {
        MemFree(colArcLen);
        MemFree(rimNorm);
        MemFree(rimPos);
        MemFree(rimY);
        MemFree(profD);
        MemFree(profNO);
        MemFree(profNU);
        return cliff;
    }

    const float kProbeStep = 0.28f;
    const int kProbeMax = 108; // ~30 m of horizontal run
    int dbgShort = 0, dbgNoDrop = 0;
    for (int i = 0; i < M; i++)
    {
        float prevD = 0.0f;
        float prevY = rimY[i];
        float lastD = 0.0f;
        int nextRow = 0;
        for (int k = 1; k <= kProbeMax && nextRow < CLIFF_ROW_COUNT; k++)
        {
            float d = (float)k * kProbeStep;
            Vector3 gp, gn;
            float y;
            if (!MapProp_SampleGroundSurface(ground, mapCenter,
                                             rimPos[i].x + rimNorm[i].x * d,
                                             rimPos[i].z + rimNorm[i].y * d, &gp, &gn))
            {
                // Beyond mesh bounds: fell off the island edge into the abyss (-8.0m)
                y = -8.0f;
            }
            else
            {
                y = gp.y;
            }
            if (y > prevY + 0.05f)
                y = prevY; // keep the profile monotone
            lastD = d;
            float segD = d - prevD;
            float segY = y - prevY;
            float segLen = sqrtf(segD * segD + segY * segY);
            float nOut = 1.0f, nUp = 0.0f;
            if (segLen > 1e-5f)
            {
                float invSegLen = 1.0f / segLen;
                nOut = -segY * invSegLen;
                nUp = segD * invSegLen;
            }
            while (nextRow < CLIFF_ROW_COUNT)
            {
                float targetY = rimY[i] - kCliffRowProfiles[nextRow].depth;
                if (y > targetY)
                    break;
                float t = (prevY > y) ? (prevY - targetY) / (prevY - y) : 0.0f;
                if (t < 0.0f)
                    t = 0.0f;
                if (t > 1.0f)
                    t = 1.0f;
                profD[nextRow * M + i] = prevD + segD * t;
                profNO[nextRow * M + i] = nOut;
                profNU[nextRow * M + i] = nUp;
                nextRow++;
            }
            prevD = d;
            prevY = y;
        }
        // Rows the terrain never reached continue straight down from the last probed sample.
        for (int r = nextRow; r < CLIFF_ROW_COUNT; r++)
        {
            profD[r * M + i] = lastD + 0.08f * (float)(r - nextRow + 1);
            profNO[r * M + i] = 1.0f;
            profNU[r * M + i] = 0.0f;
        }
        if (nextRow > 0 && lastD < 2.5f)
        {
            dbgShort++;
            if (dbgShort < 8)
                TraceLog(LOG_INFO, "CLIFF_SHORT i=%d rim=(%.1f,%.1f,%.1f) filled=%d lastD=%.2f",
                         i, rimPos[i].x, rimPos[i].y, rimPos[i].z, nextRow, lastD);
        }
        if (nextRow == 0)
            dbgNoDrop++;
    }
    TraceLog(LOG_INFO, "CLIFF_PROFILE short=%d nodrop=%d of %d", dbgShort, dbgNoDrop, M);

    // ── Build the cliff grid in world space ─────────────────────────────────
    Vector3 *gridPos = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(Vector3));
    Vector2 *gridUV = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(Vector2));
    Color *gridCol = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(Color));
    float *rowArc = MemAlloc((size_t)M * sizeof(float));
    if (!gridPos || !gridUV || !gridCol || !rowArc)
    {
        MemFree(colArcLen);
        MemFree(rimNorm);
        MemFree(rimPos);
        MemFree(rimY);
        MemFree(profD);
        MemFree(profNO);
        MemFree(profNU);
        MemFree(gridPos);
        MemFree(gridUV);
        MemFree(gridCol);
        MemFree(rowArc);
        return cliff;
    }

    for (int i = 0; i < M; i++)
        rowArc[i] = 0.0f;

    for (int r = 0; r < CLIFF_ROW_COUNT; r++)
    {
        const CliffRowProfile *rp = &kCliffRowProfiles[r];
        float spacing = (r + 1 < CLIFF_ROW_COUNT)
                            ? (kCliffRowProfiles[r + 1].depth - rp->depth)
                            : (rp->depth - kCliffRowProfiles[r - 1].depth);
        float tiltAmp = fminf(0.34f, spacing * 0.60f);
        for (int i = 0; i < M; i++)
        {
            float s = colArcLen[i];
            float strataTilt = (sinf(s * 0.024f + (float)r * 0.85f) * 0.62f +
                                (Cliff_Noise1(s * 0.09f + (float)r * 3.7f, 17 + r) - 0.5f) * 0.75f) *
                               tiltAmp;
            float depth = rp->depth + strataTilt;
            if (depth < 0.0f)
                depth = 0.0f;

            float d = profD[r * M + i];
            float nOut = profNO[r * M + i];
            float nUp = profNU[r * M + i];

            float buttress = Cliff_Fbm1(s * 0.030f, 41, 3);
            float gully = Cliff_Fbm1(s * 0.140f, 73, 2);
            float groove = Cliff_Noise1(s * 0.58f + (float)r * 2.7f, 97) - 0.5f;
            float relief = (buttress * 0.70f + gully * 0.34f + groove * 0.18f) * rp->noise + 0.70f * rp->noise;
            if (relief < 0.0f)
                relief = 0.0f;

            float lift = rp->out + groove * 0.22f * rp->noise;
            float outTotal = d + relief + lift * nOut;

            Vector3 vPos = {
                rimPos[i].x + rimNorm[i].x * outTotal,
                rimY[i] - depth + lift * nUp,
                rimPos[i].z + rimNorm[i].y * outTotal};
            gridPos[r * M + i] = vPos;

            if (r > 0)
            {
                Vector3 up = gridPos[(r - 1) * M + i];
                float ax = vPos.x - up.x;
                float ay = vPos.y - up.y;
                float az = vPos.z - up.z;
                rowArc[i] += sqrtf(ax * ax + ay * ay + az * az);
            }
            float u = (s + Cliff_Noise1(s * 0.05f, 11) * 3.0f) * 0.38f;
            float v = rowArc[i] * 0.38f;
            gridUV[r * M + i] = (Vector2){u, v};

            // R: cavity AO
            float cavity = rp->ao;
            cavity *= 1.0f - fabsf(relief) * 0.10f;
            cavity *= 0.94f + 0.06f * Cliff_Noise1(s * 0.28f + (float)r * 5.1f, 53);
            cavity = fminf(1.0f, fmaxf(0.28f, cavity));
            unsigned char rAO = (unsigned char)(cavity * 255.0f);

            // G: moss & lichen
            float moss = rp->moss;
            if (moss > 0.0f)
            {
                moss *= 0.55f + 0.45f * Cliff_Noise1(s * 0.21f + (float)r * 1.7f, 61);
                if (depth > 4.2f)
                    moss = 0.0f;
            }
            unsigned char gMoss = (unsigned char)(fminf(1.0f, fmaxf(0.0f, moss)) * 255.0f);

            // B: cloud-sea dissolve haze
            float haze = rp->haze;
            unsigned char bHaze = (unsigned char)(fminf(1.0f, fmaxf(0.0f, haze)) * 255.0f);

            // A: geological strata variation
            float strata = rp->strata * (0.82f + 0.18f * sinf(s * 0.045f - depth * 0.72f));
            unsigned char aStrata = (unsigned char)(fminf(1.0f, fmaxf(0.0f, strata)) * 255.0f);

            gridCol[r * M + i] = (Color){rAO, gMoss, bHaze, aStrata};
        }
    }
    MemFree(rowArc);

    // Finite-difference normals & orthogonalized tangents
    Vector3 *gridNorm = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(Vector3));
    Vector4 *gridTan = MemAlloc((size_t)(M * CLIFF_ROW_COUNT) * sizeof(Vector4));
    if (!gridNorm || !gridTan)
    {
        MemFree(colArcLen);
        MemFree(rimNorm);
        MemFree(rimPos);
        MemFree(rimY);
        MemFree(profD);
        MemFree(profNO);
        MemFree(profNU);
        MemFree(gridPos);
        MemFree(gridUV);
        MemFree(gridCol);
        MemFree(gridNorm);
        MemFree(gridTan);
        return cliff;
    }

    for (int r = 0; r < CLIFF_ROW_COUNT; r++)
    {
        int rpIdx = (r == 0) ? 0 : r - 1;
        int rnIdx = (r == CLIFF_ROW_COUNT - 1) ? r : r + 1;
        for (int i = 0; i < M; i++)
        {
            int prevI = (i - 1 + M) % M;
            int nextI = (i + 1) % M;
            Vector3 pa = gridPos[rpIdx * M + i];
            Vector3 pb = gridPos[rnIdx * M + i];

            Vector3 tanS = Vector3Normalize(Vector3Subtract(gridPos[r * M + nextI], gridPos[r * M + prevI]));
            Vector3 tanY = Vector3Normalize(Vector3Subtract(pa, pb));

            // Profile tangent in the (radial, vertical) cross-section
            float dr = (pb.x - pa.x) * rimNorm[i].x + (pb.z - pa.z) * rimNorm[i].y;
            float dy = pb.y - pa.y;
            float profLenSq = dr * dr + dy * dy;
            if (profLenSq < 1e-8f)
            {
                dr = 0.0f;
                dy = -1.0f;
            }
            else
            {
                float invProfLen = 1.0f / sqrtf(profLenSq);
                dr *= invProfLen;
                dy *= invProfLen;
            }
            // Since rimNorm[i] is unit 2D and (-dy, dr) is unit 2D, expected is already unit length
            Vector3 expected = {
                rimNorm[i].x * (-dy),
                dr,
                rimNorm[i].y * (-dy)};

            Vector3 n = Vector3CrossProduct(tanS, tanY);
            float nLenSq = n.x * n.x + n.y * n.y + n.z * n.z;
            if (nLenSq < 1e-10f)
                n = expected;
            else
                n = Vector3Scale(n, 1.0f / sqrtf(nLenSq));
            if (Vector3DotProduct(n, expected) < 0.0f)
            {
                n = Vector3Negate(n);
                tanS = Vector3Negate(tanS);
            }
            gridNorm[r * M + i] = n;
            gridTan[r * M + i] = (Vector4){tanS.x, tanS.y, tanS.z, 1.0f};
        }
    }

    // Load cliff shader and setup uniforms/locs
    cliff.shader = ResourceManager_LoadShader("maps/toolkit/shaders/cliff_rock.vs",
                                              "maps/toolkit/shaders/cliff_rock.fs");
    if (cliff.shader.id != 0 && cliff.shader.locs != NULL)
    {
        cliff.shader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(cliff.shader, "vertexPosition");
        cliff.shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(cliff.shader, "vertexTexCoord");
        cliff.shader.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(cliff.shader, "vertexNormal");
        cliff.shader.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocationAttrib(cliff.shader, "vertexTangent");
        cliff.shader.locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(cliff.shader, "vertexColor");

        cliff.shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(cliff.shader, "mvp");
        cliff.shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(cliff.shader, "matModel");
        cliff.shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(cliff.shader, "colDiffuse");
        cliff.shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(cliff.shader, "texture0");
        cliff.shader.locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(cliff.shader, "texture2");
        cliff.shader.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(cliff.shader, "texture3");
        cliff.shader.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(cliff.shader, "u_cloudNoise");

        Cliff_EnsureShaderLocs(cliff.shader);
        MapShadow_ConfigureShader(cliff.shader);
        VFXLight_RegisterShader(cliff.shader);
    }

    Texture2D diffTex = MapLoadMippedTexture("assets/textures/rocks/granite_diffuse.png", TEXTURE_WRAP_REPEAT);
    Texture2D normTex = MapLoadMippedTexture("assets/textures/rocks/granite_normal.png", TEXTURE_WRAP_REPEAT);
    Texture2D roughTex = MapLoadMippedTexture("assets/textures/rocks/granite_roughness.png", TEXTURE_WRAP_REPEAT);
    Texture2D cloudNoise = Environment_GetCloudShadowFrame().noiseTexture;

    cliff.chunkCount = numChunks;
    const float seamUOffset = totalPerimeter * 0.38f;

    for (int k = 0; k < numChunks; k++)
    {
        MapCliffChunk *chunk = &cliff.chunks[k];
        const int colsPerChunk = 48;
        int chunkVertCount = (colsPerChunk + 1) * CLIFF_ROW_COUNT;
        int chunkTriCount = colsPerChunk * (CLIFF_ROW_COUNT - 1) * 2;

        Mesh mesh = {0};
        mesh.vertexCount = chunkVertCount;
        mesh.triangleCount = chunkTriCount;
        mesh.vertices = MemAlloc((size_t)chunkVertCount * 3u * sizeof(float));
        mesh.texcoords = MemAlloc((size_t)chunkVertCount * 2u * sizeof(float));
        mesh.normals = MemAlloc((size_t)chunkVertCount * 3u * sizeof(float));
        mesh.tangents = MemAlloc((size_t)chunkVertCount * 4u * sizeof(float));
        mesh.colors = MemAlloc((size_t)chunkVertCount * 4u * sizeof(unsigned char));
        mesh.indices = MemAlloc((size_t)chunkTriCount * 3u * sizeof(unsigned short));

        if (!mesh.vertices || !mesh.texcoords || !mesh.normals ||
            !mesh.tangents || !mesh.colors || !mesh.indices)
        {
            MemFree(mesh.vertices);
            MemFree(mesh.texcoords);
            MemFree(mesh.normals);
            MemFree(mesh.tangents);
            MemFree(mesh.colors);
            MemFree(mesh.indices);
            continue;
        }

        Vector3 minBound = {1e9f, 1e9f, 1e9f};
        Vector3 maxBound = {-1e9f, -1e9f, -1e9f};

        for (int r = 0; r < CLIFF_ROW_COUNT; r++)
        {
            for (int c = 0; c <= colsPerChunk; c++)
            {
                int rawCol = k * colsPerChunk + c;
                int globalI = rawCol % M;
                int vertIdx = r * (colsPerChunk + 1) + c;
                int srcIdx = r * M + globalI;

                Vector3 p = gridPos[srcIdx];
                mesh.vertices[vertIdx * 3 + 0] = p.x;
                mesh.vertices[vertIdx * 3 + 1] = p.y;
                mesh.vertices[vertIdx * 3 + 2] = p.z;

                if (p.x < minBound.x)
                    minBound.x = p.x;
                if (p.y < minBound.y)
                    minBound.y = p.y;
                if (p.z < minBound.z)
                    minBound.z = p.z;
                if (p.x > maxBound.x)
                    maxBound.x = p.x;
                if (p.y > maxBound.y)
                    maxBound.y = p.y;
                if (p.z > maxBound.z)
                    maxBound.z = p.z;

                Vector2 uv = gridUV[srcIdx];
                // Keep U monotonic across the closing seam of the loop so textureGrad never stretches
                if (rawCol >= M)
                    uv.x += seamUOffset;
                mesh.texcoords[vertIdx * 2 + 0] = uv.x;
                mesh.texcoords[vertIdx * 2 + 1] = uv.y;

                Vector3 n = gridNorm[srcIdx];
                mesh.normals[vertIdx * 3 + 0] = n.x;
                mesh.normals[vertIdx * 3 + 1] = n.y;
                mesh.normals[vertIdx * 3 + 2] = n.z;

                Vector4 t = gridTan[srcIdx];
                mesh.tangents[vertIdx * 4 + 0] = t.x;
                mesh.tangents[vertIdx * 4 + 1] = t.y;
                mesh.tangents[vertIdx * 4 + 2] = t.z;
                mesh.tangents[vertIdx * 4 + 3] = t.w;

                Color col = gridCol[srcIdx];
                mesh.colors[vertIdx * 4 + 0] = col.r;
                mesh.colors[vertIdx * 4 + 1] = col.g;
                mesh.colors[vertIdx * 4 + 2] = col.b;
                mesh.colors[vertIdx * 4 + 3] = col.a;
            }
        }

        // Winding is resolved per triangle against the smoothed vertex normals
        int triIdx = 0;
        int rowStride = colsPerChunk + 1;
        for (int r = 0; r < CLIFF_ROW_COUNT - 1; r++)
        {
            for (int c = 0; c < colsPerChunk; c++)
            {
                unsigned short v00 = (unsigned short)(r * rowStride + c);
                unsigned short v10 = (unsigned short)(r * rowStride + (c + 1));
                unsigned short v01 = (unsigned short)((r + 1) * rowStride + c);
                unsigned short v11 = (unsigned short)((r + 1) * rowStride + (c + 1));

                int ng = (k * colsPerChunk + c) % M;
                int ng1 = (k * colsPerChunk + c + 1) % M;
                Vector3 expected = Vector3Add(
                    Vector3Add(gridNorm[r * M + ng], gridNorm[r * M + ng1]),
                    Vector3Add(gridNorm[(r + 1) * M + ng], gridNorm[(r + 1) * M + ng1]));

                for (int tri = 0; tri < 2; tri++)
                {
                    unsigned short a = (tri == 0) ? v00 : v10;
                    unsigned short b = (tri == 0) ? v10 : v11;
                    unsigned short d = v01;
                    Vector3 pa = {mesh.vertices[a * 3], mesh.vertices[a * 3 + 1], mesh.vertices[a * 3 + 2]};
                    Vector3 pb = {mesh.vertices[b * 3], mesh.vertices[b * 3 + 1], mesh.vertices[b * 3 + 2]};
                    Vector3 pd = {mesh.vertices[d * 3], mesh.vertices[d * 3 + 1], mesh.vertices[d * 3 + 2]};
                    Vector3 faceN = Vector3CrossProduct(Vector3Subtract(pb, pa), Vector3Subtract(pd, pa));
                    if (Vector3DotProduct(faceN, expected) < 0.0f)
                    {
                        unsigned short swap = b;
                        b = d;
                        d = swap;
                    }
                    mesh.indices[triIdx++] = a;
                    mesh.indices[triIdx++] = b;
                    mesh.indices[triIdx++] = d;
                }
            }
        }

        UploadMesh(&mesh, false);
        chunk->model = LoadModelFromMesh(mesh);
        chunk->model.materials[0].shader = cliff.shader;
        chunk->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffTex;
        chunk->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = (Color){214, 206, 192, 255};
        chunk->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = normTex;
        chunk->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = roughTex;
        if (cloudNoise.id != 0)
            chunk->model.materials[0].maps[MATERIAL_MAP_METALNESS].texture = cloudNoise;
        MapShadow_AttachMaterial(&chunk->model.materials[0]);

        Vector3 center = Vector3Scale(Vector3Add(minBound, maxBound), 0.5f);
        float maxDistSq = 0.0f;
        for (int v = 0; v < chunkVertCount; v++)
        {
            float dx = mesh.vertices[v * 3 + 0] - center.x;
            float dy = mesh.vertices[v * 3 + 1] - center.y;
            float dz = mesh.vertices[v * 3 + 2] - center.z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > maxDistSq)
                maxDistSq = d2;
        }
        chunk->center = center;
        chunk->radius = sqrtf(maxDistSq);
        chunk->ready = true;
    }

    cliff.ready = true;
    TraceLog(LOG_INFO, "MapCliffSkirt: generated %d chunks, %d perimeter columns (L=%.1fm)",
             cliff.chunkCount, M, totalPerimeter);
    TraceLog(LOG_INFO, "MapCliffSkirt: chunk0 center=(%.1f,%.1f,%.1f) r=%.1f v0=(%.1f,%.1f,%.1f)",
             cliff.chunks[0].center.x, cliff.chunks[0].center.y, cliff.chunks[0].center.z,
             cliff.chunks[0].radius,
             gridPos[0].x, gridPos[0].y, gridPos[0].z);

    MemFree(colArcLen);
    MemFree(rimNorm);
    MemFree(rimPos);
    MemFree(rimY);
    MemFree(profD);
    MemFree(profNO);
    MemFree(profNU);
    MemFree(gridPos);
    MemFree(gridUV);
    MemFree(gridCol);
    MemFree(gridNorm);
    MemFree(gridTan);

    return cliff;
}

void MapProp_DrawCliffSkirt(const MapCliffSkirt *cliff, Camera3D cam)
{
    if (!cliff || !cliff->ready || cliff->chunkCount <= 0)
        return;

    Cliff_UpdateLighting(cliff->shader, cam);

    MapPropViewFrustum frustum;
    bool cull = MapProp_BuildViewFrustum(&frustum);
    Texture2D cloudNoise = Environment_GetCloudShadowFrame().noiseTexture;

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (int k = 0; k < cliff->chunkCount; k++)
    {
        const MapCliffChunk *chunk = &cliff->chunks[k];
        if (!chunk->ready)
            continue;
        if (cull && MapProp_FrustumCullsSphere(&frustum, chunk->center, chunk->radius))
        {
            continue;
        }
        if (chunk->model.materialCount > 0)
        {
            if (cloudNoise.id != 0)
                chunk->model.materials[0].maps[MATERIAL_MAP_METALNESS].texture = cloudNoise;
            MapShadow_AttachMaterial(&chunk->model.materials[0]);
        }
        DrawModel(chunk->model, Vector3Zero(), 1.0f, WHITE);
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void MapProp_UnloadCliffSkirt(MapCliffSkirt *cliff)
{
    if (!cliff || !cliff->ready)
        return;
    for (int k = 0; k < cliff->chunkCount; k++)
    {
        if (cliff->chunks[k].ready)
        {
            for (int m = 0; m < cliff->chunks[k].model.materialCount; m++)
            {
                cliff->chunks[k].model.materials[m].maps[MATERIAL_MAP_METALNESS].texture = (Texture2D){0};
                cliff->chunks[k].model.materials[m].maps[MATERIAL_MAP_OCCLUSION].texture = (Texture2D){0};
                cliff->chunks[k].model.materials[m].maps[MATERIAL_MAP_HEIGHT].texture = (Texture2D){0};
            }
            UnloadModel(cliff->chunks[k].model);
            cliff->chunks[k].ready = false;
        }
    }
    cliff->chunkCount = 0;
    cliff->ready = false;
}

#endif // MAP_TOOLKIT_MAP_PROPS_CLIFF_INL