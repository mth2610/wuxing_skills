// Initialization-only contour bake. Runtime draws one cached, textured ribbon.
static bool BoundaryPointFinite(Vector3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static int BoundaryContourCount(const MapBoundaryContour *loop)
{
    int count = loop->count;
    if (!loop->points || count < 3)
        return 0;
    if (Vector3DistanceSqr(loop->points[0], loop->points[count - 1]) < 1e-8f)
        count--;
    return count >= 3 ? count : 0;
}

bool MapProp_SetCloudSeaBoundaryContours(MapCloudSea *cloud,
                                         const MapBoundaryContour *loops, int loopCount,
                                         const MapBoundaryMistStyle *style)
{
    if (!cloud || !cloud->ready || loopCount < 0)
        return false;
    if (!loopCount)
    {
        if (cloud->mistReady)
            UnloadModel(cloud->mistModel);
        cloud->mistModel = (Model){0};
        cloud->mistReady = false;
        return true;
    }
    if (!loops || !style || !cloud->mistTexture.id ||
        !isfinite(style->innerWidth) || !isfinite(style->outerWidth) ||
        !isfinite(style->heightOffset) || !isfinite(style->outerDrop) ||
        !isfinite(style->opacity) || style->innerWidth < 0.0f || style->outerWidth <= 0.0f ||
        style->outerDrop < 0.0f || style->opacity < 0.0f || style->opacity > 1.0f)
        return false;

    int total = 0;
    for (int l = 0; l < loopCount; l++)
    {
        int n = BoundaryContourCount(&loops[l]);
        if (!n || n > 32760 - total)
            return false;
        total += n;
        for (int i = 0; i < n; i++)
        {
            Vector3 a = loops[l].points[i], b = loops[l].points[(i + 1) % n];
            float dx = a.x - b.x, dz = a.z - b.z;
            if (!BoundaryPointFinite(a) || (dx * dx + dz * dz) < 1e-10f)
                return false;
        }
    }

    Mesh mesh = {0};
    mesh.vertexCount = total * 2;
    mesh.triangleCount = total * 2;
    mesh.vertices = MemAlloc((unsigned int)mesh.vertexCount * 3u * sizeof(float));
    mesh.texcoords = MemAlloc((unsigned int)mesh.vertexCount * 2u * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)mesh.vertexCount * 4u);
    mesh.indices = MemAlloc((unsigned int)mesh.triangleCount * 3u * sizeof(unsigned short));
    if (!mesh.vertices || !mesh.texcoords || !mesh.colors || !mesh.indices)
        goto mesh_failure;

    int base = 0, index = 0;
    for (int l = 0; l < loopCount; l++)
    {
        int n = BoundaryContourCount(&loops[l]);
        for (int i = 0; i < n; i++)
        {
            Vector3 p0 = loops[l].points[(i + n - 2) % n];
            Vector3 p1 = loops[l].points[(i + n - 1) % n];
            Vector3 p2 = loops[l].points[i];
            Vector3 p3 = loops[l].points[(i + 1) % n];
            Vector3 p4 = loops[l].points[(i + 2) % n];
            Vector3 p = {
                (p0.x + 2.0f * p1.x + 4.0f * p2.x + 2.0f * p3.x + p4.x) * 0.1f,
                p2.y,
                (p0.z + 2.0f * p1.z + 4.0f * p2.z + 2.0f * p3.z + p4.z) * 0.1f};
            Vector2 tang = {p4.x - p0.x, p4.z - p0.z};
            float tangLenSq = tang.x * tang.x + tang.y * tang.y;
            Vector2 outward = (tangLenSq > 1e-6f)
                                  ? Vector2Scale((Vector2){tang.y, -tang.x}, 1.0f / sqrtf(tangLenSq))
                                  : (Vector2){1.0f, 0.0f};
            // Baked, map-local variation; no runtime noise or moving vertices.
            float variation = 0.5f + 0.25f * sinf(p.x * 0.37f + p.z * 0.29f) + 0.25f * cosf(p.z * 0.41f - p.x * 0.17f);
            float width = 0.85f + 0.30f * variation;
            unsigned char alpha = (unsigned char)(255.0f * style->opacity * (0.65f + 0.35f * variation));
            float uCoord = p.x * 0.071f + p.z * 0.113f;

            for (int side = 0; side < 2; side++)
            {
                int v = (base + i) * 2 + side;
                float shift = (side ? style->outerWidth : -style->innerWidth) * width;
                Vector3 vp = {
                    p.x + outward.x * shift,
                    p.y + style->heightOffset - (side ? style->outerDrop : 0.0f),
                    p.z + outward.y * shift};
                if (!BoundaryPointFinite(vp))
                    goto mesh_failure;
                mesh.vertices[v * 3 + 0] = vp.x;
                mesh.vertices[v * 3 + 1] = vp.y;
                mesh.vertices[v * 3 + 2] = vp.z;
                mesh.texcoords[v * 2 + 0] = uCoord;
                mesh.texcoords[v * 2 + 1] = (float)side;
                mesh.colors[v * 4 + 0] = 255;
                mesh.colors[v * 4 + 1] = 255;
                mesh.colors[v * 4 + 2] = 255;
                mesh.colors[v * 4 + 3] = alpha;
            }
            unsigned short a0 = (unsigned short)((base + i) * 2);
            unsigned short a1 = (unsigned short)(a0 + 1);
            unsigned short b0 = (unsigned short)((base + (i + 1) % n) * 2);
            unsigned short b1 = (unsigned short)(b0 + 1);
            mesh.indices[index++] = a0;
            mesh.indices[index++] = b0;
            mesh.indices[index++] = a1;
            mesh.indices[index++] = a1;
            mesh.indices[index++] = b0;
            mesh.indices[index++] = b1;
        }
        base += n;
    }
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloud->mistTexture;
    if (cloud->mistReady)
        UnloadModel(cloud->mistModel);
    cloud->mistModel = model;
    cloud->mistReady = true;
    TraceLog(LOG_INFO, "Island mist: %d loops, %d vertices, %d triangles, static single draw",
             loopCount, mesh.vertexCount, mesh.triangleCount);
    return true;

mesh_failure:
    MemFree(mesh.vertices);
    MemFree(mesh.texcoords);
    MemFree(mesh.colors);
    MemFree(mesh.indices);
    return false;
}

#define BOUNDARY_NODE_CAP 8192
#define BOUNDARY_HASH_CAP 16384
// Quantized endpoint hashing joins indexed and duplicated triangle vertices alike.
typedef struct
{
    Vector3 p;
    int64_t x, z;
    int next;
    bool used;
} BoundaryNode;

static int BoundaryNodeFind(BoundaryNode *nodes, int *slots, int *count, Vector3 p)
{
    if (!BoundaryPointFinite(p) || fabsf(p.x) > 1e8f || fabsf(p.z) > 1e8f)
        return -1;
    int64_t x = llround((double)p.x * 10000.0);
    int64_t z = llround((double)p.z * 10000.0);
    uint64_t hash = (uint64_t)x * UINT64_C(0x9e3779b185ebca87) ^ (uint64_t)z * UINT64_C(0xc2b2ae3d27d4eb4f);
    unsigned slot = (unsigned)(hash ^ (hash >> 32)) & (BOUNDARY_HASH_CAP - 1);
    while (slots[slot])
    {
        int id = slots[slot] - 1;
        if (nodes[id].x == x && nodes[id].z == z)
            return id;
        slot = (slot + 1) & (BOUNDARY_HASH_CAP - 1);
    }
    if (*count >= BOUNDARY_NODE_CAP)
        return -1;
    int id = (*count)++;
    nodes[id] = (BoundaryNode){.p = p, .x = x, .z = z, .next = -1, .used = false};
    slots[slot] = id + 1;
    return id;
}

// Initialization-only Douglas-Peucker simplification reusing the heap-allocated scratch buffer
// instead of allocating 72 KB on the call stack.
static int BoundarySimplify(Vector3 *points, int count, int *scratchRanges)
{
    if (count <= 3)
        return count;

    unsigned char *keep = (unsigned char *)MemAlloc((unsigned int)count);
    if (!keep)
        return count;
    memset(keep, 0, (size_t)count);

    int *ranges = scratchRanges;
    int stack = 0;
    int far = 1;
    float distance = 0.0f;
    for (int i = 1; i < count - 1; i++)
    {
        float d = Vector3DistanceSqr(points[0], points[i]);
        if (d > distance)
        {
            distance = d;
            far = i;
        }
    }
    keep[0] = keep[far] = keep[count - 1] = 1;
    ranges[stack++] = 0;
    ranges[stack++] = far;
    ranges[stack++] = far;
    ranges[stack++] = count - 1;

    while (stack >= 2)
    {
        int end = ranges[--stack];
        int start = ranges[--stack];
        int split = -1;
        float dx = points[end].x - points[start].x;
        float dz = points[end].z - points[start].z;
        float lengthSq = dx * dx + dz * dz;
        float invLenSq = (lengthSq > 1e-12f) ? (1.0f / lengthSq) : 0.0f;
        float worst = 0.01f;
        for (int i = start + 1; i < end; i++)
        {
            float px = points[i].x - points[start].x;
            float pz = points[i].z - points[start].z;
            float t = (invLenSq > 0.0f) ? fmaxf(0.0f, fminf(1.0f, (px * dx + pz * dz) * invLenSq)) : 0.0f;
            float ex = px - t * dx;
            float ez = pz - t * dz;
            float d = ex * ex + ez * ez;
            if (d > worst)
            {
                worst = d;
                split = i;
            }
        }
        if (split >= 0 && stack + 4 <= BOUNDARY_HASH_CAP)
        {
            keep[split] = 1;
            ranges[stack++] = start;
            ranges[stack++] = split;
            ranges[stack++] = split;
            ranges[stack++] = end;
        }
    }

    int output = 0;
    for (int i = 0; i < count; i++)
    {
        if (keep[i])
            points[output++] = points[i];
    }
    MemFree(keep);
    return output;
}

int MapProp_ExtractBoundaryLoop(const MapGroundSurface *ground, float height, Vector3 *outPoints, int maxPoints)
{
    if (!ground || !ground->ready || !isfinite(height) || !outPoints || maxPoints < 3)
        return 0;
    BoundaryNode *nodes = MemAlloc(BOUNDARY_NODE_CAP * sizeof(*nodes));
    int *slots = MemAlloc(BOUNDARY_HASH_CAP * sizeof(*slots));
    Vector3 *points = MemAlloc(BOUNDARY_NODE_CAP * sizeof(*points));
    int loopLen = 0;
    if (!nodes || !slots || !points)
    {
        if (nodes)
            MemFree(nodes);
        if (slots)
            MemFree(slots);
        if (points)
            MemFree(points);
        return 0;
    }
    memset(slots, 0, BOUNDARY_HASH_CAP * sizeof(*slots));
    int count = 0;
    for (int m = 0; m < ground->model.meshCount; m++)
    {
        const Mesh *mesh = &ground->model.meshes[m];
        if (!mesh->vertices)
            continue;
        for (int t = 0; t < mesh->triangleCount; t++)
        {
            Vector3 p[3], hit[2], high = {0};
            int hits = 0;
            for (int i = 0; i < 3; i++)
            {
                int v = mesh->indices ? mesh->indices[t * 3 + i] : (t * 3 + i);
                if (v < 0 || v >= mesh->vertexCount)
                    goto cleanup;
                p[i] = Vector3Add((Vector3){mesh->vertices[v * 3], mesh->vertices[v * 3 + 1], mesh->vertices[v * 3 + 2]},
                                  ground->drawOffset);
                if (!BoundaryPointFinite(p[i]))
                    goto cleanup;
                if (p[i].y > height)
                    high = p[i];
            }
            for (int i = 0; i < 3; i++)
            {
                Vector3 a = p[i], b = p[(i + 1) % 3];
                if ((a.y > height) == (b.y > height))
                    continue;
                if (a.y > b.y)
                {
                    Vector3 swap = a;
                    a = b;
                    b = swap;
                }
                if (hits < 2)
                    hit[hits++] = Vector3Lerp(a, b, (height - a.y) / (b.y - a.y));
            }
            if (hits != 2 || Vector3DistanceSqr(hit[0], hit[1]) < 1e-10f)
                continue;
            float cross = (hit[1].x - hit[0].x) * (high.z - hit[0].z) - (hit[1].z - hit[0].z) * (high.x - hit[0].x);
            if (cross < 0.0f)
            {
                Vector3 swap = hit[0];
                hit[0] = hit[1];
                hit[1] = swap;
            }
            int a = BoundaryNodeFind(nodes, slots, &count, hit[0]);
            int b = BoundaryNodeFind(nodes, slots, &count, hit[1]);
            if (a < 0 || b < 0)
                continue;
            if (nodes[a].next == -1)
                nodes[a].next = b;
        }
    }

    int bestLen = 0;
    double maxArea = 0.0;
    for (int start = 0; start < count; start++)
    {
        if (nodes[start].used)
            continue;
        int first = 0, current = start;
        double area = 0.0;
        bool closedCleanly = false;
        while (current >= 0 && !nodes[current].used && nodes[current].next >= 0 && first < BOUNDARY_NODE_CAP)
        {
            nodes[current].used = true;
            int next = nodes[current].next;
            area += (double)nodes[current].p.x * (double)nodes[next].p.z -
                    (double)nodes[next].p.x * (double)nodes[current].p.z;
            points[first++] = nodes[current].p;
            current = next;
            if (current == start)
            {
                closedCleanly = true;
                break;
            }
        }
        double absArea = fabs(area);
        if (closedCleanly && first >= 3 && absArea > maxArea)
        {
            int n = BoundarySimplify(points, first, slots);
            if (n >= 3)
            {
                maxArea = absArea;
                if (n > maxPoints)
                    n = maxPoints;
                for (int i = 0; i < n; i++)
                    outPoints[i] = points[i];
                bestLen = n;
            }
        }
    }
    loopLen = bestLen;

cleanup:
    MemFree(nodes);
    MemFree(slots);
    MemFree(points);
    return loopLen;
}

bool MapProp_SetCloudSeaGroundBoundary(MapCloudSea *cloud,
                                       const MapGroundSurface *ground, float height,
                                       const MapBoundaryMistStyle *style)
{
    if (!cloud || !ground || !ground->ready || !isfinite(height) || !style)
        return false;
    Vector3 *points = MemAlloc(BOUNDARY_NODE_CAP * sizeof(*points));
    if (!points)
        return false;
    int n = MapProp_ExtractBoundaryLoop(ground, height, points, BOUNDARY_NODE_CAP);
    bool ok = false;
    if (n >= 3)
    {
        MapBoundaryContour loop = {points, n};
        ok = MapProp_SetCloudSeaBoundaryContours(cloud, &loop, 1, style);
    }
    MemFree(points);
    return ok;
}

#undef BOUNDARY_NODE_CAP
#undef BOUNDARY_HASH_CAP