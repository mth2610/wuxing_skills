/* ===========================================================================
 * LOW-POLY ROCKS & SHARD CLUSTERS (OPTIMIZED)
 * =========================================================================*/

#define ROCK_ICOSA_VERTS 12
#define ROCK_ICOSA_FACES 20

static void ProceduralMesh__BuildIcosahedron(Vector3 verts[ROCK_MESH_MAX_VERTS], int faces[ROCK_MESH_MAX_FACES][3], int *outVertCount, int *outFaceCount) {
    const float t = (1.0f + sqrtf(5.0f)) * 0.5f; 
    Vector3 base[ROCK_ICOSA_VERTS] = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, 
        {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
    };
    for (int i = 0; i < ROCK_ICOSA_VERTS; i++) verts[i] = Vector3Normalize(base[i]);

    int baseFaces[ROCK_ICOSA_FACES][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };
    for (int i = 0; i < ROCK_ICOSA_FACES; i++) {
        faces[i][0] = baseFaces[i][0]; faces[i][1] = baseFaces[i][1]; faces[i][2] = baseFaces[i][2];
    }
    *outVertCount = ROCK_ICOSA_VERTS;
    *outFaceCount = ROCK_ICOSA_FACES;
}

static void ProceduralMesh__SubdivideIcosphere(Vector3 verts[ROCK_MESH_MAX_VERTS], int faces[ROCK_MESH_MAX_FACES][3], int *vertCount, int *faceCount) {
    int newFaces[ROCK_MESH_MAX_FACES][3];
    int newFaceCount = 0;
    int v = *vertCount;

    for (int f = 0; f < *faceCount; f++) {
        if (newFaceCount + 4 > ROCK_MESH_MAX_FACES || v + 3 > ROCK_MESH_MAX_VERTS) break; 
        int ia = faces[f][0], ib = faces[f][1], ic = faces[f][2];
        Vector3 a = verts[ia], b = verts[ib], c = verts[ic];

        int iab = v++, ibc = v++, ica = v++;
        verts[iab] = Vector3Normalize(Vector3Lerp(a, b, 0.5f));
        verts[ibc] = Vector3Normalize(Vector3Lerp(b, c, 0.5f));
        verts[ica] = Vector3Normalize(Vector3Lerp(c, a, 0.5f));

        newFaces[newFaceCount][0] = ia;  newFaces[newFaceCount][1] = iab; newFaces[newFaceCount][2] = ica; newFaceCount++;
        newFaces[newFaceCount][0] = iab; newFaces[newFaceCount][1] = ib;  newFaces[newFaceCount][2] = ibc; newFaceCount++;
        newFaces[newFaceCount][0] = ica; newFaces[newFaceCount][1] = ibc; newFaces[newFaceCount][2] = ic;  newFaceCount++;
        newFaces[newFaceCount][0] = iab; newFaces[newFaceCount][1] = ibc; newFaces[newFaceCount][2] = ica; newFaceCount++;
    }
    for (int f = 0; f < newFaceCount; f++) {
        faces[f][0] = newFaces[f][0]; faces[f][1] = newFaces[f][1]; faces[f][2] = newFaces[f][2];
    }
    *faceCount = newFaceCount; *vertCount = v;
}

void ProceduralMesh_BuildRock(RockMeshData *out, Vector3 center, float radius, float jitterAmount, int seed, int subdivisions) {
    if (out == NULL) return;
    if (subdivisions < 0) subdivisions = 0;
    if (subdivisions > 2) subdivisions = 2;

    static Vector3 cachedVerts[3][ROCK_MESH_MAX_VERTS];
    static int cachedFaces[3][ROCK_MESH_MAX_FACES][3];
    static int cachedVertCount[3] = {0, 0, 0};
    static int cachedFaceCount[3] = {0, 0, 0};

    if (cachedVertCount[subdivisions] == 0) {
        ProceduralMesh__BuildIcosahedron(cachedVerts[subdivisions], cachedFaces[subdivisions], &cachedVertCount[subdivisions], &cachedFaceCount[subdivisions]);
        for (int s = 0; s < subdivisions; s++) {
            ProceduralMesh__SubdivideIcosphere(cachedVerts[subdivisions], cachedFaces[subdivisions], &cachedVertCount[subdivisions], &cachedFaceCount[subdivisions]);
        }
    }

    out->vertCount = cachedVertCount[subdivisions];
    out->faceCount = cachedFaceCount[subdivisions];

    float scaleX = 0.6f + 0.8f * ((float)(ProceduralMesh__Hash(seed ^ 19283) % 1000) / 1000.0f); 
    float scaleY = 0.6f + 0.8f * ((float)(ProceduralMesh__Hash(seed ^ 57291) % 1000) / 1000.0f);
    float scaleZ = 0.6f + 0.8f * ((float)(ProceduralMesh__Hash(seed ^ 84726) % 1000) / 1000.0f);
    float offX = (float)(ProceduralMesh__Hash(seed ^ 111) % 100);
    float offY = (float)(ProceduralMesh__Hash(seed ^ 222) % 100);
    float offZ = (float)(ProceduralMesh__Hash(seed ^ 333) % 100);

    for (int i = 0; i < out->vertCount; i++) {
        Vector3 v = cachedVerts[subdivisions][i];
        union { float f; unsigned int i; } ux, uy, uz;
        ux.f = v.x; uy.f = v.y; uz.f = v.z;
        unsigned int hash = ((ux.i >> 12) * 73856093) ^ ((uy.i >> 12) * 19349663) ^ ((uz.i >> 12) * 83492791);
        
        float hfNoise = ProceduralMesh__Noise2(hash, 0, seed);
        float lfNoise = sinf(v.x * 3.0f + offX) * sinf(v.y * 3.0f + offY) * sinf(v.z * 3.0f + offZ);
        float finalNoise = (lfNoise * 0.6f + hfNoise * 0.4f) * jitterAmount;
        float r = radius * (1.0f + finalNoise);

        out->verts[i] = (Vector3){ center.x + v.x * r * scaleX, center.y + v.y * r * scaleY, center.z + v.z * r * scaleZ };
    }

    for (int f = 0; f < out->faceCount; f++) {
        out->faceVertIdx[f][0] = cachedFaces[subdivisions][f][0];
        out->faceVertIdx[f][1] = cachedFaces[subdivisions][f][1];
        out->faceVertIdx[f][2] = cachedFaces[subdivisions][f][2];

        Vector3 a = out->verts[out->faceVertIdx[f][0]], b = out->verts[out->faceVertIdx[f][1]], c = out->verts[out->faceVertIdx[f][2]];
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        if (Vector3DotProduct(normal, Vector3Subtract(a, center)) < 0.0f) normal = Vector3Negate(normal);
        out->faceNormals[f] = normal;
    }
}

void ProceduralMesh_DrawRock(const RockMeshData *data, Color color) {
    if (data == NULL) return;
    rlCheckRenderBatchLimit(data->faceCount * 3);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int f = 0; f < data->faceCount; f++) {
        rlNormal3f(data->faceNormals[f].x, data->faceNormals[f].y, data->faceNormals[f].z);
        Vector3 a = data->verts[data->faceVertIdx[f][0]]; rlVertex3f(a.x, a.y, a.z);
        Vector3 b = data->verts[data->faceVertIdx[f][1]]; rlVertex3f(b.x, b.y, b.z);
        Vector3 c = data->verts[data->faceVertIdx[f][2]]; rlVertex3f(c.x, c.y, c.z);
    }
    rlEnd();
}

Mesh ProceduralMesh_BuildRockTemplateMesh(float radius, float jitterAmount, int seed, int subdivisions) {
    Mesh mesh = {0};

    static RockMeshData s_rock;
    ProceduralMesh_BuildRock(&s_rock, Vector3Zero(), radius, jitterAmount, seed, subdivisions);
    if (s_rock.faceCount <= 0) return mesh;

    int vertCount = s_rock.faceCount * 3; // flat-shaded: 3 unique verts/face (matches DrawRock's per-face normal)
    mesh.vertices = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.normals = (float *)MemAlloc(vertCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(vertCount * 2 * sizeof(float));

    for (int f = 0; f < s_rock.faceCount; f++) {
        Vector3 verts3[3] = {
            s_rock.verts[s_rock.faceVertIdx[f][0]],
            s_rock.verts[s_rock.faceVertIdx[f][1]],
            s_rock.verts[s_rock.faceVertIdx[f][2]],
        };
        for (int k = 0; k < 3; k++) {
            int v = f * 3 + k;
            mesh.vertices[v * 3 + 0] = verts3[k].x;
            mesh.vertices[v * 3 + 1] = verts3[k].y;
            mesh.vertices[v * 3 + 2] = verts3[k].z;
            mesh.normals[v * 3 + 0] = s_rock.faceNormals[f].x;
            mesh.normals[v * 3 + 1] = s_rock.faceNormals[f].y;
            mesh.normals[v * 3 + 2] = s_rock.faceNormals[f].z;
            mesh.texcoords[v * 2 + 0] = 0.0f;
            mesh.texcoords[v * 2 + 1] = 0.0f;
        }
    }

    mesh.vertexCount = vertCount;
    mesh.triangleCount = s_rock.faceCount;
    UploadMesh(&mesh, false);
    return mesh;
}

ShardClusterConfig ProceduralMesh_DefaultShardClusterConfig(void) {
    ShardClusterConfig cfg = {0};
    cfg.spreadAngle = 35.0f * (PI / 180.0f); cfg.thicknessMin = 0.06f; cfg.thicknessMax = 0.14f; cfg.tipSharpness = 0.85f; cfg.sides = 5;
    return cfg;
}

void ProceduralMesh_BuildShardCluster(ShardClusterMeshData *out, Vector3 origin, Vector3 mainDirection, int shardCount, float minLength, float maxLength, int seed, const ShardClusterConfig *cfg) {
    if (out == NULL) return;
    ShardClusterConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = ProceduralMesh_DefaultShardClusterConfig(); cfg = &defaultCfg; }

    if (shardCount > SHARD_CLUSTER_MAX_SHARDS) shardCount = SHARD_CLUSTER_MAX_SHARDS;
    if (shardCount < 1) shardCount = 1;
    int sides = cfg->sides;
    if (sides > SHARD_MAX_SIDES) sides = SHARD_MAX_SIDES;
    if (sides < 3) sides = 3;
    out->shardCount = shardCount; out->sides = sides;

    Vector3 dir = Vector3Normalize(mainDirection);
    Vector3 helper = (fabsf(dir.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(helper, dir));
    Vector3 up = Vector3CrossProduct(dir, right);

    float cosA[SHARD_MAX_SIDES]; float sinA[SHARD_MAX_SIDES];
    for (int j = 0; j < sides; j++) {
        float phi = (float)j * (2.0f * PI) / (float)sides;
        cosA[j] = cosf(phi); sinA[j] = sinf(phi);
    }

    for (int s = 0; s < shardCount; s++) {
        float rYaw = ProceduralMesh__Noise2(s, 0, seed) * PI; 
        float rSpread = (ProceduralMesh__Noise2(s, 1, seed) * 0.5f + 0.5f) * cfg->spreadAngle; 
        float length = minLength + (maxLength - minLength) * (ProceduralMesh__Noise2(s, 2, seed) * 0.5f + 0.5f);
        float baseRadius = length * (cfg->thicknessMin + (cfg->thicknessMax - cfg->thicknessMin) * (ProceduralMesh__Noise2(s, 3, seed) * 0.5f + 0.5f));
        float tipRadius = baseRadius * (1.0f - cfg->tipSharpness);
        float rTwist = ProceduralMesh__Noise2(s, 4, seed) * PI;

        float scaleX = 0.4f + 1.2f * (ProceduralMesh__Noise2(s, 5, seed) * 0.5f + 0.5f);
        float scaleY = 0.4f + 1.2f * (ProceduralMesh__Noise2(s, 6, seed) * 0.5f + 0.5f);

        Vector3 tiltAxis = Vector3Add(Vector3Scale(right, cosf(rYaw)), Vector3Scale(up, sinf(rYaw)));
        Vector3 shardDir = Vector3Normalize(Vector3Add(Vector3Scale(dir, cosf(rSpread)), Vector3Scale(tiltAxis, sinf(rSpread))));

        Vector3 shardHelper = (fabsf(shardDir.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 sRight = Vector3Normalize(Vector3CrossProduct(shardHelper, shardDir));
        Vector3 sUp = Vector3CrossProduct(shardDir, sRight);
        
        Vector3 rotRight = Vector3Add(Vector3Scale(sRight, cosf(rTwist)), Vector3Scale(sUp, sinf(rTwist)));
        Vector3 rotUp = Vector3Add(Vector3Scale(sUp, cosf(rTwist)), Vector3Scale(sRight, -sinf(rTwist)));

        Vector3 tipOffsetVec = Vector3Add(Vector3Scale(rotRight, ProceduralMesh__Noise2(s, 7, seed) * baseRadius * 0.7f), 
                                          Vector3Scale(rotUp, ProceduralMesh__Noise2(s, 8, seed) * baseRadius * 0.7f));
        
        out->baseCenter[s] = origin;
        out->tipCenter[s] = Vector3Add(Vector3Add(origin, Vector3Scale(shardDir, length)), tipOffsetVec);

        for (int j = 0; j < sides; j++) {
            Vector3 radial = Vector3Add(Vector3Scale(rotRight, cosA[j] * scaleX), Vector3Scale(rotUp, sinA[j] * scaleY));
            out->baseRing[s][j] = Vector3Add(origin, Vector3Scale(radial, baseRadius));
            out->tipRing[s][j] = Vector3Add(out->tipCenter[s], Vector3Scale(radial, tipRadius));
            out->baseNormal[s][j] = Vector3Normalize(radial);
        }
    }
}

void ProceduralMesh_DrawShardCluster(const ShardClusterMeshData *data, Color color) {
    if (data == NULL) return;
    const int sides = data->sides;

    rlCheckRenderBatchLimit(data->shardCount * sides * 7);
    for (int s = 0; s < data->shardCount; s++) {
        Vector3 axis = Vector3Normalize(Vector3Subtract(data->tipCenter[s], data->baseCenter[s]));

        rlBegin(RL_QUADS);
        rlColor4ub(color.r, color.g, color.b, color.a);
        for (int j = 0; j < sides; j++) {
            int nextJ = (j + 1) % sides;
            rlNormal3f(data->baseNormal[s][j].x, data->baseNormal[s][j].y, data->baseNormal[s][j].z);
            rlVertex3f(data->baseRing[s][j].x, data->baseRing[s][j].y, data->baseRing[s][j].z);
            rlNormal3f(data->baseNormal[s][nextJ].x, data->baseNormal[s][nextJ].y, data->baseNormal[s][nextJ].z);
            rlVertex3f(data->baseRing[s][nextJ].x, data->baseRing[s][nextJ].y, data->baseRing[s][nextJ].z);
            rlVertex3f(data->tipRing[s][nextJ].x, data->tipRing[s][nextJ].y, data->tipRing[s][nextJ].z);
            rlNormal3f(data->baseNormal[s][j].x, data->baseNormal[s][j].y, data->baseNormal[s][j].z);
            rlVertex3f(data->tipRing[s][j].x, data->tipRing[s][j].y, data->tipRing[s][j].z);
        }
        rlEnd();

        rlBegin(RL_TRIANGLES);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlNormal3f(-axis.x, -axis.y, -axis.z);
        for (int j = 0; j < sides; j++) {
            int nextJ = (j + 1) % sides;
            rlVertex3f(data->baseCenter[s].x, data->baseCenter[s].y, data->baseCenter[s].z);
            rlVertex3f(data->baseRing[s][nextJ].x, data->baseRing[s][nextJ].y, data->baseRing[s][nextJ].z);
            rlVertex3f(data->baseRing[s][j].x, data->baseRing[s][j].y, data->baseRing[s][j].z);
        }
        rlNormal3f(axis.x, axis.y, axis.z);
        for (int j = 0; j < sides; j++) {
            int nextJ = (j + 1) % sides;
            rlVertex3f(data->tipCenter[s].x, data->tipCenter[s].y, data->tipCenter[s].z);
            rlVertex3f(data->tipRing[s][j].x, data->tipRing[s][j].y, data->tipRing[s][j].z);
            rlVertex3f(data->tipRing[s][nextJ].x, data->tipRing[s][nextJ].y, data->tipRing[s][nextJ].z);
        }
        rlEnd();
    }
}