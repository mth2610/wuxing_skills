#ifndef MAP_TOOLKIT_MAP_PROPS_CLIFF_INL
#define MAP_TOOLKIT_MAP_PROPS_CLIFF_INL

// ============================================================================
// WUXING — Sculpted Island Cliff Skirt
// Generates a multi-tiered, chunk-culled perimeter rock mesh with fluting
// buttresses, crevice gullies, cavity AO, ledge moss colonization,
// underside inward keel taper, and cloud-sea dissolve haze.
// ============================================================================

typedef struct {
    float depthRel;      // Nominal depth below crest rim
    float radialOffset;  // Base outward (+) or inward (-) displacement
    float fluteScale;    // Multiplier on procedural sinusoidal fluting
    float ao;            // Base cavity ambient occlusion factor
    float moss;          // Base ledge moss colonization factor
    float haze;          // Base cloud-sea dissolve factor
    float strata;        // Geological strata phase
} CliffRowProfile;

static const CliffRowProfile kCliffRowProfiles[12] = {
    // Row 0: Crest rim penetrating 8cm under plateau turf (guaranteed zero seam)
    {  0.0f, -0.38f, 0.00f, 0.95f, 0.90f, 0.00f, 0.85f },
    // Row 1: Upper cliff rim
    {  0.65f, 0.28f, 0.35f, 0.90f, 0.85f, 0.00f, 0.90f },
    // Row 2: Top ledge shelf (moss collects here!)
    {  1.60f, 0.72f, 0.75f, 0.78f, 0.95f, 0.00f, 0.95f },
    // Row 3: Crevice drop
    {  3.30f, 0.42f, 0.90f, 0.65f, 0.50f, 0.00f, 1.00f },
    // Row 4: Mid-tier buttress terrace
    {  5.60f, 1.15f, 1.00f, 0.80f, 0.65f, 0.00f, 1.05f },
    // Row 5: Deep vertical wall drop
    {  8.40f, 0.80f, 0.95f, 0.72f, 0.25f, 0.00f, 1.00f },
    // Row 6: Lower terrace shelf
    { 11.60f, 0.55f, 0.85f, 0.70f, 0.05f, 0.00f, 0.95f },
    // Row 7: Cloud immersion entry (cloud line at ~ -14.5m)
    { 14.50f, 0.15f, 0.70f, 0.65f, 0.00f, 0.18f, 0.90f },
    // Row 8: Under-hang / Inward taper start
    { 17.20f, -1.20f, 0.50f, 0.55f, 0.00f, 0.45f, 0.85f },
    // Row 9: Deep underside keel
    { 19.80f, -3.40f, 0.35f, 0.48f, 0.00f, 0.70f, 0.80f },
    // Row 10: Lower keel
    { 22.20f, -6.00f, 0.20f, 0.42f, 0.00f, 0.88f, 0.75f },
    // Row 11: Abyss root
    { 24.60f, -9.20f, 0.05f, 0.38f, 0.00f, 1.00f, 0.70f },
};

static void Cliff_UpdateLighting(Shader shader, Camera3D cam)
{
    if (shader.id == 0) return;
    rlDrawRenderBatchActive();
    BeginShaderMode(shader);
    Environment_BindCloudShadowShader(shader);
    Matrix shaderFromWorld = rlGetMatrixTransform();
    SetShaderValueMatrix(shader, GetShaderLocation(shader, "u_cloudWorldFromShader"),
                         MatrixInvert(shaderFromWorld));

    int lightDirLoc = GetShaderLocation(shader, "u_lightDir");
    if (lightDirLoc >= 0) {
        Vector3 lightDir = Vector3Negate(Environment_GetSunDirection());
        Vector3 shaderOrigin = Vector3Transform((Vector3){0}, shaderFromWorld);
        lightDir = Vector3Normalize(Vector3Subtract(
            Vector3Transform(lightDir, shaderFromWorld), shaderOrigin));
        SetShaderValue(shader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    }

    int lightColorLoc = GetShaderLocation(shader, "u_lightColor");
    if (lightColorLoc >= 0) {
        Vector4 c = ColorNormalize(Environment_GetSunColor());
        Vector3 rgb = Vector3Scale((Vector3){c.x, c.y, c.z}, Environment_GetSunIntensity());
        SetShaderValue(shader, lightColorLoc, &rgb, SHADER_UNIFORM_VEC3);
    }

    int ambientLoc = GetShaderLocation(shader, "u_ambientColor");
    if (ambientLoc >= 0) {
        Vector4 c = ColorNormalize(Environment_GetAmbientColor());
        Vector3 rgb = {c.x, c.y, c.z};
        SetShaderValue(shader, ambientLoc, &rgb, SHADER_UNIFORM_VEC3);
    }

    int viewPosLoc = GetShaderLocation(shader, "u_viewPos");
    if (viewPosLoc >= 0) {
        Vector3 viewPos = Vector3Transform(cam.position, shaderFromWorld);
        SetShaderValue(shader, viewPosLoc, &viewPos, SHADER_UNIFORM_VEC3);
    }

    int hazeLoc = GetShaderLocation(shader, "u_hazeColor");
    if (hazeLoc >= 0) {
        Vector3 haze = (Vector3){0.82f, 0.86f, 0.92f};
        SetShaderValue(shader, hazeLoc, &haze, SHADER_UNIFORM_VEC3);
    }

    MapShadow_UpdateShader(shader);
    EndShaderMode();
}

MapCliffSkirt MapProp_CreateCliffSkirt(const MapGroundSurface *ground, Vector3 mapCenter, float sliceHeight, float bottomY)
{
    MapCliffSkirt cliff = {0};
    if (!ground || !ground->ready) return cliff;

    const int maxRaw = 8192;
    Vector3 *rawPoints = MemAlloc(maxRaw * sizeof(Vector3));
    if (!rawPoints) return cliff;

    int rawCount = MapProp_ExtractBoundaryLoop(ground, sliceHeight, rawPoints, maxRaw);
    if (rawCount < 3) {
        TraceLog(LOG_WARNING, "MapCliffSkirt: extract boundary loop failed (count=%d)", rawCount);
        MemFree(rawPoints);
        return cliff;
    }

    // Convert raw points to world space
    Vector3 *worldPoints = MemAlloc(rawCount * sizeof(Vector3));
    for (int i = 0; i < rawCount; i++) {
        worldPoints[i] = (Vector3){
            rawPoints[i].x + mapCenter.x,
            rawPoints[i].y + mapCenter.y,
            rawPoints[i].z + mapCenter.z
        };
    }
    MemFree(rawPoints);

    // Calculate total perimeter arc length
    float totalPerimeter = 0.0f;
    for (int i = 0; i < rawCount; i++) {
        int next = (i + 1) % rawCount;
        totalPerimeter += hypotf(worldPoints[next].x - worldPoints[i].x, worldPoints[next].z - worldPoints[i].z);
    }

    // Target ~1.2m column spacing, clustered in chunks of 48 columns
    int desiredCols = (int)(totalPerimeter / 1.25f + 0.5f);
    int numChunks = (desiredCols + 47) / 48;
    if (numChunks < 4) numChunks = 4;
    if (numChunks > MAX_CLIFF_CHUNKS) numChunks = MAX_CLIFF_CHUNKS;
    int M = numChunks * 48;
    float colStep = totalPerimeter / (float)M;

    // Resample polyline evenly into M columns
    Vector3 *colPos = MemAlloc(M * sizeof(Vector3));
    float *colArcLen = MemAlloc(M * sizeof(float));

    float currentDist = 0.0f;
    int segIdx = 0;
    float segAccum = 0.0f;
    float segLen = hypotf(worldPoints[1].x - worldPoints[0].x, worldPoints[1].z - worldPoints[0].z);

    for (int i = 0; i < M; i++) {
        float targetDist = (float)i * colStep;
        colArcLen[i] = targetDist;
        while (segAccum + segLen < targetDist && segIdx < rawCount - 1) {
            segAccum += segLen;
            segIdx++;
            int nextIdx = (segIdx + 1) % rawCount;
            segLen = hypotf(worldPoints[nextIdx].x - worldPoints[segIdx].x, worldPoints[nextIdx].z - worldPoints[segIdx].z);
        }
        float t = (segLen > 1e-5f) ? ((targetDist - segAccum) / segLen) : 0.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int nextIdx = (segIdx + 1) % rawCount;
        colPos[i] = (Vector3){
            worldPoints[segIdx].x + (worldPoints[nextIdx].x - worldPoints[segIdx].x) * t,
            worldPoints[segIdx].y + (worldPoints[nextIdx].y - worldPoints[segIdx].y) * t,
            worldPoints[segIdx].z + (worldPoints[nextIdx].z - worldPoints[segIdx].z) * t
        };
    }
    MemFree(worldPoints);

    // Apply 2 passes of Laplacian smoothing to eliminate single-cell staircase artifacts
    Vector3 *smoothPos = MemAlloc(M * sizeof(Vector3));
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < M; i++) {
            int prev = (i - 1 + M) % M;
            int next = (i + 1) % M;
            smoothPos[i].x = (colPos[prev].x + 2.0f * colPos[i].x + colPos[next].x) * 0.25f;
            smoothPos[i].y = colPos[i].y;
            smoothPos[i].z = (colPos[prev].z + 2.0f * colPos[i].z + colPos[next].z) * 0.25f;
        }
        memcpy(colPos, smoothPos, M * sizeof(Vector3));
    }
    MemFree(smoothPos);

    // Compute robust outward normal vector for each column
    Vector2 *colNorm = MemAlloc(M * sizeof(Vector2));
    for (int i = 0; i < M; i++) {
        int prev = (i - 1 + M) % M;
        int next = (i + 1) % M;
        float dx = colPos[next].x - colPos[prev].x;
        float dz = colPos[next].z - colPos[prev].z;
        float len = hypotf(dx, dz);
        if (len > 1e-5f) { dx /= len; dz /= len; }
        // 2D perpendicular normal: (dz, -dx)
        Vector2 n = { dz, -dx };
        // Ensure pointing away from island center
        float toCenterX = colPos[i].x - mapCenter.x;
        float toCenterZ = colPos[i].z - mapCenter.z;
        if (n.x * toCenterX + n.y * toCenterZ < 0.0f) {
            n.x = -n.x;
            n.y = -n.y;
        }
        colNorm[i] = n;
    }

    // Determine crest rim attachment elevation (tucking under meadow turf)
    float *crestY = MemAlloc(M * sizeof(float));
    for (int i = 0; i < M; i++) {
        Vector3 testP = {
            colPos[i].x - colNorm[i].x * 0.40f,
            colPos[i].y,
            colPos[i].z - colNorm[i].y * 0.40f
        };
        float gh = MapProp_SampleGroundHeight(ground, mapCenter, testP.x, testP.z);
        if (isfinite(gh) && gh > colPos[i].y) {
            crestY[i] = gh - 0.08f;
        } else {
            crestY[i] = colPos[i].y - 0.08f;
        }
    }

    // Generate grid of (M x 12) vertices in full 3D coordinates
    const int R = 12;
    Vector3 *gridPos = MemAlloc(M * R * sizeof(Vector3));
    Vector2 *gridUV = MemAlloc(M * R * sizeof(Vector2));
    Color *gridCol = MemAlloc(M * R * sizeof(Color));

    for (int r = 0; r < R; r++) {
        const CliffRowProfile *rp = &kCliffRowProfiles[r];
        for (int i = 0; i < M; i++) {
            float s = colArcLen[i];
            float y = crestY[i] - rp->depthRel;
            if (bottomY < 0.0f && y < bottomY) y = bottomY;

            // Multi-octave sinusoidal geological fluting and crag buttress displacement
            float flute = 1.35f * sinf(s * 0.042f)
                        + 0.70f * sinf(s * 0.118f + 1.25f)
                        + 0.40f * sinf(s * 0.285f - y * 0.40f);

            float totalOffset = rp->radialOffset + flute * rp->fluteScale;
            Vector3 vPos = {
                colPos[i].x + colNorm[i].x * totalOffset,
                y,
                colPos[i].z + colNorm[i].y * totalOffset
            };
            gridPos[r * M + i] = vPos;

            // Uniform arc-length texture coordinates (broad 19m geological scale)
            gridUV[r * M + i] = (Vector2){ s * 0.052f, -y * 0.052f };

            // Vertex Colors:
            // R: Cavity AO modulated by crevice depth
            float aoMod = rp->ao + (flute * 0.18f);
            aoMod = fminf(1.0f, fmaxf(0.25f, aoMod));
            unsigned char rAO = (unsigned char)(aoMod * 255.0f);

            // G: Ledge moss colonization
            float mossFactor = rp->moss;
            if (y < -7.0f) mossFactor = 0.0f;
            unsigned char gMoss = (unsigned char)(fminf(1.0f, fmaxf(0.0f, mossFactor)) * 255.0f);

            // B: Cloud sea dissolve haze
            float hazeFactor = rp->haze;
            unsigned char bHaze = (unsigned char)(fminf(1.0f, fmaxf(0.0f, hazeFactor)) * 255.0f);

            // A: Geological strata variation
            float strataFactor = rp->strata * (0.85f + 0.15f * sinf(s * 0.05f + y * 0.65f));
            unsigned char aStrata = (unsigned char)(fminf(1.0f, fmaxf(0.0f, strataFactor)) * 255.0f);

            gridCol[r * M + i] = (Color){ rAO, gMoss, bHaze, aStrata };
        }
    }

    // Compute smooth finite-difference normals and tangents
    Vector3 *gridNorm = MemAlloc(M * R * sizeof(Vector3));
    Vector4 *gridTan = MemAlloc(M * R * sizeof(Vector4));

    for (int r = 0; r < R; r++) {
        for (int i = 0; i < M; i++) {
            int prevI = (i - 1 + M) % M;
            int nextI = (i + 1) % M;
            Vector3 pS_prev = gridPos[r * M + prevI];
            Vector3 pS_next = gridPos[r * M + nextI];
            Vector3 tanS = Vector3Normalize(Vector3Subtract(pS_next, pS_prev));

            Vector3 pY_prev = (r == 0) ? gridPos[0 * M + i] : gridPos[(r - 1) * M + i];
            Vector3 pY_next = (r == R - 1) ? gridPos[(R - 1) * M + i] : gridPos[(r + 1) * M + i];
            Vector3 tanY = Vector3Normalize(Vector3Subtract(pY_prev, pY_next)); // pointing upward

            Vector3 n = Vector3Normalize(Vector3CrossProduct(tanS, tanY));
            // Ensure normal points outward matching column outward normal
            Vector2 cn = colNorm[i];
            if (n.x * cn.x + n.z * cn.y < 0.0f) {
                n = Vector3Negate(n);
                tanS = Vector3Negate(tanS);
            }

            gridNorm[r * M + i] = n;
            gridTan[r * M + i] = (Vector4){ tanS.x, tanS.y, tanS.z, 1.0f };
        }
    }

    // Load cliff shader and setup uniforms/locs
    cliff.shader = ResourceManager_LoadShader("maps/toolkit/shaders/cliff_rock.vs",
                                              "maps/toolkit/shaders/cliff_rock.fs");
    if (cliff.shader.id != 0 && cliff.shader.locs != NULL) {
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

        MapShadow_ConfigureShader(cliff.shader);
        VFXLight_RegisterShader(cliff.shader);
    }

    // Build shared rock material (weathered geological slate with moss & quartz strata)
    Texture2D diffTex = MapLoadMippedTexture("assets/textures/rocks/slate_diffuse.png", TEXTURE_WRAP_REPEAT);
    Texture2D normTex = MapLoadMippedTexture("assets/textures/rocks/slate_normal.png", TEXTURE_WRAP_REPEAT);
    Texture2D roughTex = MapLoadMippedTexture("assets/textures/rocks/slate_roughness.png", TEXTURE_WRAP_REPEAT);
    Texture2D cloudNoise = Environment_GetCloudShadowFrame().noiseTexture;

    // Build chunk models (48 columns each, plus 1 overlap column = 49 columns, 12 rows = 588 verts)
    cliff.chunkCount = numChunks;
    for (int k = 0; k < numChunks; k++) {
        MapCliffChunk *chunk = &cliff.chunks[k];
        const int colsPerChunk = 48;
        int chunkVertCount = (colsPerChunk + 1) * R;
        int chunkTriCount = colsPerChunk * (R - 1) * 2;

        Mesh mesh = {0};
        mesh.vertexCount = chunkVertCount;
        mesh.triangleCount = chunkTriCount;
        mesh.vertices = MemAlloc(chunkVertCount * 3 * sizeof(float));
        mesh.texcoords = MemAlloc(chunkVertCount * 2 * sizeof(float));
        mesh.normals = MemAlloc(chunkVertCount * 3 * sizeof(float));
        mesh.tangents = MemAlloc(chunkVertCount * 4 * sizeof(float));
        mesh.colors = MemAlloc(chunkVertCount * 4 * sizeof(unsigned char));
        mesh.indices = MemAlloc(chunkTriCount * 3 * sizeof(unsigned short));

        Vector3 minBound = { 1e9f, 1e9f, 1e9f };
        Vector3 maxBound = { -1e9f, -1e9f, -1e9f };

        // Fill vertices
        for (int r = 0; r < R; r++) {
            for (int c = 0; c <= colsPerChunk; c++) {
                int globalI = (k * colsPerChunk + c) % M;
                int vertIdx = r * (colsPerChunk + 1) + c;
                int srcIdx = r * M + globalI;

                Vector3 p = gridPos[srcIdx];
                mesh.vertices[vertIdx * 3 + 0] = p.x;
                mesh.vertices[vertIdx * 3 + 1] = p.y;
                mesh.vertices[vertIdx * 3 + 2] = p.z;

                minBound.x = fminf(minBound.x, p.x);
                minBound.y = fminf(minBound.y, p.y);
                minBound.z = fminf(minBound.z, p.z);
                maxBound.x = fmaxf(maxBound.x, p.x);
                maxBound.y = fmaxf(maxBound.y, p.y);
                maxBound.z = fmaxf(maxBound.z, p.z);

                Vector2 uv = gridUV[srcIdx];
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

        // Fill indices
        int triIdx = 0;
        int rowStride = colsPerChunk + 1;
        for (int r = 0; r < R - 1; r++) {
            for (int c = 0; c < colsPerChunk; c++) {
                unsigned short v00 = (unsigned short)(r * rowStride + c);
                unsigned short v10 = (unsigned short)(r * rowStride + (c + 1));
                unsigned short v01 = (unsigned short)((r + 1) * rowStride + c);
                unsigned short v11 = (unsigned short)((r + 1) * rowStride + (c + 1));

                mesh.indices[triIdx++] = v00;
                mesh.indices[triIdx++] = v10;
                mesh.indices[triIdx++] = v01;

                mesh.indices[triIdx++] = v10;
                mesh.indices[triIdx++] = v11;
                mesh.indices[triIdx++] = v01;
            }
        }

        UploadMesh(&mesh, false);
        chunk->model = LoadModelFromMesh(mesh);
        chunk->model.materials[0].shader = cliff.shader;
        chunk->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = diffTex;
        chunk->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = (Color){ 205, 210, 218, 255 };
        chunk->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = normTex;
        chunk->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = roughTex;
        chunk->model.materials[0].maps[MATERIAL_MAP_METALNESS].texture = cloudNoise;
        MapShadow_AttachMaterial(&chunk->model.materials[0]);

        Vector3 center = Vector3Scale(Vector3Add(minBound, maxBound), 0.5f);
        float radius = 0.0f;
        for (int v = 0; v < chunkVertCount; v++) {
            Vector3 p = { mesh.vertices[v * 3 + 0], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2] };
            float d = Vector3Distance(p, center);
            if (d > radius) radius = d;
        }
        chunk->center = center;
        chunk->radius = radius;
        chunk->ready = true;
    }

    cliff.ready = true;
    TraceLog(LOG_INFO, "MapCliffSkirt: generated %d chunks, %d perimeter columns (L=%.1fm)",
             cliff.chunkCount, M, totalPerimeter);

    MemFree(colPos);
    MemFree(colArcLen);
    MemFree(colNorm);
    MemFree(crestY);
    MemFree(gridPos);
    MemFree(gridUV);
    MemFree(gridCol);
    MemFree(gridNorm);
    MemFree(gridTan);

    return cliff;
}

void MapProp_DrawCliffSkirt(const MapCliffSkirt *cliff, Camera3D cam)
{
    if (!cliff || !cliff->ready || cliff->chunkCount <= 0) return;

    Cliff_UpdateLighting(cliff->shader, cam);

    MapPropViewFrustum frustum;
    bool cull = MapProp_BuildViewFrustum(&frustum);

    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    for (int k = 0; k < cliff->chunkCount; k++) {
        const MapCliffChunk *chunk = &cliff->chunks[k];
        if (!chunk->ready) continue;
        if (cull && MapProp_FrustumCullsSphere(&frustum, chunk->center, chunk->radius)) {
            continue;
        }
        DrawModel(chunk->model, Vector3Zero(), 1.0f, WHITE);
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void MapProp_UnloadCliffSkirt(MapCliffSkirt *cliff)
{
    if (!cliff || !cliff->ready) return;
    for (int k = 0; k < cliff->chunkCount; k++) {
        if (cliff->chunks[k].ready) {
            UnloadModel(cliff->chunks[k].model);
            cliff->chunks[k].ready = false;
        }
    }
    cliff->chunkCount = 0;
    cliff->ready = false;
}

#endif // MAP_TOOLKIT_MAP_PROPS_CLIFF_INL
