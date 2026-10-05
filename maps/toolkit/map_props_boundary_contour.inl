// Initialization-only contour bake. Runtime draws one cached, textured ribbon.
static bool BoundaryPointFinite(Vector3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static int BoundaryContourCount(const MapBoundaryContour *loop)
{
    int count = loop->count;
    if (!loop->points || count < 3) return 0;
    if (Vector3DistanceSqr(loop->points[0], loop->points[count-1]) < 1e-8f) count--;
    return count >= 3 ? count : 0;
}

bool MapProp_SetCloudSeaBoundaryContours(MapCloudSea *cloud,
    const MapBoundaryContour *loops, int loopCount, const MapBoundaryMistStyle *style)
{
    if (!cloud || !cloud->ready || loopCount < 0) return false;
    if (!loopCount) {
        if (cloud->mistReady) UnloadModel(cloud->mistModel);
        cloud->mistModel = (Model){0}; cloud->mistReady = false;
        return true;
    }
    if (!loops || !style || !cloud->mistTexture.id ||
        !isfinite(style->innerWidth) || !isfinite(style->outerWidth) ||
        !isfinite(style->heightOffset) || !isfinite(style->outerDrop) ||
        !isfinite(style->opacity) || style->innerWidth < 0 || style->outerWidth <= 0 ||
        style->outerDrop < 0 || style->opacity < 0 || style->opacity > 1) return false;
    int total = 0;
    for (int l = 0; l < loopCount; l++) {
        int n = BoundaryContourCount(&loops[l]);
        if (!n || n > 32760-total) return false;
        total += n;
        for (int i = 0; i < n; i++) {
            Vector3 a = loops[l].points[i], b = loops[l].points[(i+1)%n];
            if (!BoundaryPointFinite(a) || hypotf(a.x-b.x, a.z-b.z) < 1e-5f) return false;
        }
    }
    Mesh mesh = {0};
    mesh.vertexCount = total * 2; mesh.triangleCount = total * 2;
    mesh.vertices = MemAlloc(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = MemAlloc(mesh.vertexCount * 2 * sizeof(float));
    mesh.colors = MemAlloc(mesh.vertexCount * 4);
    mesh.indices = MemAlloc(mesh.triangleCount * 3 * sizeof(unsigned short));
    if (!mesh.vertices || !mesh.texcoords || !mesh.colors || !mesh.indices) goto mesh_failure;
    int base = 0, index = 0;
    for (int l = 0; l < loopCount; l++) {
        int n = BoundaryContourCount(&loops[l]);
        for (int i = 0; i < n; i++) {
            Vector3 p = loops[l].points[i], before = loops[l].points[(i+n-1)%n], after = loops[l].points[(i+1)%n];
            Vector2 a = Vector2Normalize((Vector2){p.z-before.z, before.x-p.x});
            Vector2 b = Vector2Normalize((Vector2){after.z-p.z, p.x-after.x});
            Vector2 outward = Vector2Normalize(Vector2Add(a, b));
            if (Vector2Length(outward) < 0.1f) outward = b;
            float miter = 1.0f / fmaxf(0.5f, Vector2DotProduct(outward, b));
            // Baked, map-local variation; no runtime noise or moving vertices.
            float variation = 0.5f + 0.25f*sinf(p.x*0.37f+p.z*0.29f) + 0.25f*cosf(p.z*0.41f-p.x*0.17f);
            float width = 0.8f + 0.4f*variation;
            for (int side = 0; side < 2; side++) {
                int v = (base+i)*2+side;
                float shift = (side ? style->outerWidth : -style->innerWidth) * width * miter;
                mesh.vertices[v*3] = p.x + outward.x * shift;
                mesh.vertices[v*3+1] = p.y + style->heightOffset - (side ? style->outerDrop : 0);
                mesh.vertices[v*3+2] = p.z + outward.y * shift;
                if (!BoundaryPointFinite((Vector3){mesh.vertices[v*3],mesh.vertices[v*3+1],mesh.vertices[v*3+2]})) goto mesh_failure;
                mesh.texcoords[v*2] = p.x*0.071f+p.z*0.113f;
                mesh.texcoords[v*2+1] = (float)side;
                mesh.colors[v*4] = mesh.colors[v*4+1] = mesh.colors[v*4+2] = 255;
                mesh.colors[v*4+3] = (unsigned char)(255*style->opacity*(0.65f+0.35f*variation));
            }
            unsigned short a0 = (base+i)*2, a1 = a0+1;
            unsigned short b0 = (base+(i+1)%n)*2, b1 = b0+1;
            mesh.indices[index++] = a0; mesh.indices[index++] = b0; mesh.indices[index++] = a1;
            mesh.indices[index++] = a1; mesh.indices[index++] = b0; mesh.indices[index++] = b1;
        }
        base += n;
    }
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloud->mistTexture;
    if (cloud->mistReady) UnloadModel(cloud->mistModel);
    cloud->mistModel = model; cloud->mistReady = true;
    TraceLog(LOG_INFO, "Island mist: %d loops, %d vertices, %d triangles, static single draw", loopCount, mesh.vertexCount, mesh.triangleCount);
    return true;
mesh_failure:
    MemFree(mesh.vertices); MemFree(mesh.texcoords); MemFree(mesh.colors); MemFree(mesh.indices);
    return false;
}

#define BOUNDARY_NODE_CAP 8192
#define BOUNDARY_HASH_CAP 16384
// Quantized endpoint hashing joins indexed and duplicated triangle vertices alike.
typedef struct { Vector3 p; int64_t x, z; int next; bool used; } BoundaryNode;
static int BoundaryNodeFind(BoundaryNode *nodes, int *slots, int *count, Vector3 p)
{
    if (!BoundaryPointFinite(p) || fabsf(p.x)>1e8f || fabsf(p.z)>1e8f) return -1;
    int64_t x = llround((double)p.x*10000), z = llround((double)p.z*10000);
    uint64_t hash = (uint64_t)x*UINT64_C(0x9e3779b185ebca87) ^ (uint64_t)z*UINT64_C(0xc2b2ae3d27d4eb4f);
    unsigned slot = (unsigned)(hash ^ (hash>>32)) & (BOUNDARY_HASH_CAP-1);
    while (slots[slot]) {
        int id = slots[slot]-1;
        if (nodes[id].x==x && nodes[id].z==z) return id;
        slot = (slot+1) & (BOUNDARY_HASH_CAP-1);
    }
    if (*count == BOUNDARY_NODE_CAP) return -1;
    int id = (*count)++;
    nodes[id] = (BoundaryNode){.p=p,.x=x,.z=z,.next=-1};
    slots[slot] = id+1;
    return id;
}

// Initialization-only Douglas-Peucker simplification. Ten centimetres is small
// relative to the soft ribbon width; terrain geometry itself is never changed.
static int BoundarySimplify(Vector3 *points, int count)
{
    unsigned char keep[BOUNDARY_NODE_CAP] = {0};
    int ranges[BOUNDARY_NODE_CAP*2], stack = 0;
    int far = 1; float distance = 0;
    for (int i = 1; i < count-1; i++) {
        float d = Vector3DistanceSqr(points[0],points[i]);
        if (d>distance) {distance=d;far=i;}
    }
    keep[0]=keep[far]=keep[count-1]=1;
    ranges[stack++]=0;ranges[stack++]=far;
    ranges[stack++]=far;ranges[stack++]=count-1;
    while (stack) {
        int end=ranges[--stack], start=ranges[--stack], split=-1;
        float dx=points[end].x-points[start].x, dz=points[end].z-points[start].z;
        float length=dx*dx+dz*dz, worst=0.01f;
        for (int i=start+1;i<end;i++) {
            float px=points[i].x-points[start].x,pz=points[i].z-points[start].z;
            float t=length>1e-12f?fmaxf(0,fminf(1,(px*dx+pz*dz)/length)):0;
            float ex=px-t*dx,ez=pz-t*dz,d=ex*ex+ez*ez;
            if(d>worst){worst=d;split=i;}
        }
        if (split>=0) {
            keep[split]=1;
            ranges[stack++]=start;ranges[stack++]=split;
            ranges[stack++]=split;ranges[stack++]=end;
        }
    }
    int output=0;
    for(int i=0;i<count;i++)if(keep[i])points[output++]=points[i];
    return output;
}

bool MapProp_SetCloudSeaGroundBoundary(MapCloudSea *cloud,
    const MapGroundSurface *ground, float height, const MapBoundaryMistStyle *style)
{
    if (!cloud || !ground || !ground->ready || !isfinite(height)) return false;
    BoundaryNode *nodes = MemAlloc(BOUNDARY_NODE_CAP*sizeof(*nodes));
    int *slots = MemAlloc(BOUNDARY_HASH_CAP*sizeof(*slots));
    Vector3 *points = MemAlloc(BOUNDARY_NODE_CAP*sizeof(*points));
    MapBoundaryContour *loops = MemAlloc((BOUNDARY_NODE_CAP/3)*sizeof(*loops));
    bool ok = false;
    if (!nodes || !slots || !points || !loops) goto cleanup;
    memset(slots, 0, BOUNDARY_HASH_CAP*sizeof(*slots));
    int count = 0;
    for (int m = 0; m < ground->model.meshCount; m++) {
        const Mesh *mesh = &ground->model.meshes[m];
        for (int t = 0; t < mesh->triangleCount; t++) {
            Vector3 p[3], hit[2], high = {0}; int hits = 0;
            for (int i = 0; i < 3; i++) {
                int v = mesh->indices ? mesh->indices[t*3+i] : t*3+i;
                if (v < 0 || v >= mesh->vertexCount || !mesh->vertices) goto cleanup;
                p[i] = Vector3Add((Vector3){mesh->vertices[v*3],mesh->vertices[v*3+1],mesh->vertices[v*3+2]},ground->drawOffset);
                if (!BoundaryPointFinite(p[i])) goto cleanup;
                if (p[i].y > height) high = p[i];
            }
            for (int i = 0; i < 3; i++) {
                Vector3 a=p[i], b=p[(i+1)%3];
                if ((a.y>height)==(b.y>height)) continue;
                if (a.y>b.y) {Vector3 swap=a;a=b;b=swap;}
                if (hits<2) hit[hits++] = Vector3Lerp(a,b,(height-a.y)/(b.y-a.y));
            }
            if (hits!=2 || Vector3DistanceSqr(hit[0],hit[1])<1e-10f) continue;
            float cross = (hit[1].x-hit[0].x)*(high.z-hit[0].z) - (hit[1].z-hit[0].z)*(high.x-hit[0].x);
            if (cross<0) { Vector3 swap=hit[0];hit[0]=hit[1];hit[1]=swap; }
            int a=BoundaryNodeFind(nodes,slots,&count,hit[0]), b=BoundaryNodeFind(nodes,slots,&count,hit[1]);
            if (a<0 || b<0 || (nodes[a].next!=-1 && nodes[a].next!=b)) goto cleanup;
            nodes[a].next=b;
        }
    }
    int output = 0, loopCount = 0;
    for (int start = 0; start < count; start++) {
        if (nodes[start].used) continue;
        int first = output, current = start;
        double area = 0;
        do {
            if (current<0 || nodes[current].used || nodes[current].next<0) goto cleanup;
            nodes[current].used = true;
            int next = nodes[current].next;
            area += (double)nodes[current].p.x*nodes[next].p.z - (double)nodes[next].p.x*nodes[current].p.z;
            points[output++] = nodes[current].p;
            current = next;
        } while (current!=start);
        int n = output-first;
        // Negative winding is a depression inside the plateau (e.g. the lake).
        if (n>=3 && area>1e-6) {
            n=BoundarySimplify(points+first,n);
            output=first+n;
            loops[loopCount++] = (MapBoundaryContour){points+first,n};
        }
        else output=first;
    }
    if (!loopCount) goto cleanup;
    ok = MapProp_SetCloudSeaBoundaryContours(cloud,loops,loopCount,style);
cleanup:
    MemFree(nodes);MemFree(slots);MemFree(points);MemFree(loops);
    return ok;
}
#undef BOUNDARY_NODE_CAP
#undef BOUNDARY_HASH_CAP
