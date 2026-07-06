/* ===========================================================================
 * WAVE PLANE & CURLING WAVES
 * =========================================================================*/

WavePlaneConfig ProceduralMesh_DefaultWavePlaneConfig(void) {
    WavePlaneConfig cfg = {0};
    cfg.wavelength = 220.0f;
    cfg.amplitude = 18.0f;
    cfg.direction = (Vector3){1.0f, 0.0f, 0.2f};
    cfg.crestSharpness = 1.5f;
    return cfg;
}

void ProceduralMesh_BuildWavePlane(WavePlaneMeshData *out, Vector3 center, float width, float length, int segmentsX, int segmentsZ, float time, const WavePlaneConfig *cfg) {
    if (out == NULL) return;

    WavePlaneConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = ProceduralMesh_DefaultWavePlaneConfig(); cfg = &defaultCfg; }

    if (segmentsX > WAVE_PLANE_MAX_SEGMENTS_X) segmentsX = WAVE_PLANE_MAX_SEGMENTS_X;
    if (segmentsZ > WAVE_PLANE_MAX_SEGMENTS_Z) segmentsZ = WAVE_PLANE_MAX_SEGMENTS_Z;
    if (segmentsX < 1) segmentsX = 1; if (segmentsZ < 1) segmentsZ = 1;
    out->segmentsX = segmentsX; out->segmentsZ = segmentsZ;

    Vector3 dir = cfg->direction;
    float dlen = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (dlen < 0.0001f) { dir = (Vector3){1.0f, 0.0f, 0.0f}; } else { dir.x /= dlen; dir.z /= dlen; }
    dir.y = 0.0f;
    Vector3 perp = {-dir.z, 0.0f, dir.x};

    float k = (2.0f * PI) / fmaxf(cfg->wavelength, 1.0f);
    float hw = width * 0.5f, hl = length * 0.5f;

    for (int i = 0; i <= segmentsX; i++) {
        float px = -hw + ((float)i / segmentsX) * width;
        for (int j = 0; j <= segmentsZ; j++) {
            float pz = -hl + ((float)j / segmentsZ) * length;
            float proj = px * dir.x + pz * dir.z;      
            float projPerp = px * perp.x + pz * perp.z; 

            float s1 = sinf(proj * k + time * 1.6f);
            if (cfg->crestSharpness > 0.0f) {
                float sign = (s1 >= 0.0f) ? 1.0f : -1.0f;
                s1 = sign * powf(fabsf(s1), 1.0f / (1.0f + cfg->crestSharpness));
            }

            float s2 = sinf((proj * 0.6f + projPerp * 0.5f) * k * 2.3f + time * 2.3f + 1.7f);
            float s3 = sinf(projPerp * k * 0.4f - time * 0.9f);
            float n = ProceduralMesh__Noise2(i, j, 1337) * 0.15f;
            float h = cfg->amplitude * (0.55f * s1 + 0.30f * s2 + 0.15f * s3 + n);

            out->verts[i][j] = (Vector3){center.x + px, center.y + h, center.z + pz};
        }
    }

    for (int i = 0; i <= segmentsX; i++) {
        for (int j = 0; j <= segmentsZ; j++) {
            Vector3 xNeg = out->verts[(i > 0) ? i - 1 : i][j];
            Vector3 xPos = out->verts[(i < segmentsX) ? i + 1 : i][j];
            Vector3 zNeg = out->verts[i][(j > 0) ? j - 1 : j];
            Vector3 zPos = out->verts[i][(j < segmentsZ) ? j + 1 : j];

            Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(zPos, zNeg), Vector3Subtract(xPos, xNeg)));
            if (normal.y < 0.0f) normal = Vector3Negate(normal);
            out->normals[i][j] = normal;
        }
    }
}

void ProceduralMesh_DrawWavePlane(const WavePlaneMeshData *data, Color color) {
    if (data == NULL) return;
    const int segX = data->segmentsX, segZ = data->segmentsZ;

    rlCheckRenderBatchLimit(segX * segZ * 4);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < segX; i++) {
        float u1 = (float)i / segX, u2 = (float)(i + 1) / segX;
        for (int j = 0; j < segZ; j++) {
            float v1 = (float)j / segZ, v2 = (float)(j + 1) / segZ;

            rlNormal3f(data->normals[i][j].x, data->normals[i][j].y, data->normals[i][j].z);
            rlTexCoord2f(u1, v1); rlVertex3f(data->verts[i][j].x, data->verts[i][j].y, data->verts[i][j].z);
            rlNormal3f(data->normals[i + 1][j].x, data->normals[i + 1][j].y, data->normals[i + 1][j].z);
            rlTexCoord2f(u2, v1); rlVertex3f(data->verts[i + 1][j].x, data->verts[i + 1][j].y, data->verts[i + 1][j].z);
            rlNormal3f(data->normals[i + 1][j + 1].x, data->normals[i + 1][j + 1].y, data->normals[i + 1][j + 1].z);
            rlTexCoord2f(u2, v2); rlVertex3f(data->verts[i + 1][j + 1].x, data->verts[i + 1][j + 1].y, data->verts[i + 1][j + 1].z);
            rlNormal3f(data->normals[i][j + 1].x, data->normals[i][j + 1].y, data->normals[i][j + 1].z);
            rlTexCoord2f(u1, v2); rlVertex3f(data->verts[i][j + 1].x, data->verts[i][j + 1].y, data->verts[i][j + 1].z);
        }
    }
    rlEnd();
}

CurlingWaveConfig ProceduralMesh_DefaultCurlingWaveConfig(void) {
    CurlingWaveConfig cfg = {0};
    cfg.curlAmount = 0.6f; cfg.height = 160.0f; cfg.archWidth = 400.0f;
    return cfg;
}

void ProceduralMesh_BuildCurlingWave(CurlingWaveMeshData *out, Vector3 baseCenter, Vector3 widthDirection, const CurlingWaveConfig *cfg, int profileSegs, int widthSegs) {
    if (out == NULL) return;

    CurlingWaveConfig defaultCfg;
    if (cfg == NULL) { defaultCfg = ProceduralMesh_DefaultCurlingWaveConfig(); cfg = &defaultCfg; }

    if (profileSegs > CURLING_WAVE_MAX_PROFILE_SEGS) profileSegs = CURLING_WAVE_MAX_PROFILE_SEGS;
    if (widthSegs > CURLING_WAVE_MAX_WIDTH_SEGS) widthSegs = CURLING_WAVE_MAX_WIDTH_SEGS;
    if (profileSegs < 2) profileSegs = 2; if (widthSegs < 1) widthSegs = 1;
    out->profileSegs = profileSegs; out->widthSegs = widthSegs;

    Vector3 width = Vector3Normalize(widthDirection);
    Vector3 depth = Vector3Normalize(Vector3CrossProduct((Vector3){0.0f, 1.0f, 0.0f}, width));
    float sweepAngle = PI * 0.5f + (PI * 0.5f * cfg->curlAmount); 

    for (int p = 0; p <= profileSegs; p++) {
        float tp = (float)p / profileSegs;
        float angle = -PI * 0.5f + sweepAngle * (tp * tp * (3.0f - 2.0f * tp)); 
        float cy = sinf(angle) * cfg->height + cfg->height; 
        float cd = cosf(angle) * cfg->height;               

        for (int w = 0; w <= widthSegs; w++) {
            float jitter = ProceduralMesh__Noise2(p, w, 4242) * (cfg->height * 0.015f) * (0.3f + 0.7f * tp);
            Vector3 pos = Vector3Add(baseCenter, Vector3Scale(width, (-0.5f + (float)w / widthSegs) * cfg->archWidth));
            pos = Vector3Add(pos, Vector3Scale((Vector3){0.0f, 1.0f, 0.0f}, cy + jitter));
            out->verts[w][p] = Vector3Add(pos, Vector3Scale(depth, cd + jitter));
        }
    }

    for (int w = 0; w <= widthSegs; w++) {
        for (int p = 0; p <= profileSegs; p++) {
            Vector3 pNeg = out->verts[w][(p > 0) ? p - 1 : p];
            Vector3 pPos = out->verts[w][(p < profileSegs) ? p + 1 : p];
            Vector3 wNeg = out->verts[(w > 0) ? w - 1 : w][p];
            Vector3 wPos = out->verts[(w < widthSegs) ? w + 1 : w][p];
            out->normals[w][p] = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(pPos, pNeg), Vector3Subtract(wPos, wNeg)));
        }
    }
}

void ProceduralMesh_DrawCurlingWave(const CurlingWaveMeshData *data, Color color) {
    if (data == NULL) return;
    const int widthSegs = data->widthSegs, profileSegs = data->profileSegs;

    rlCheckRenderBatchLimit(widthSegs * profileSegs * 4);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int w = 0; w < widthSegs; w++) {
        float u1 = (float)w / widthSegs, u2 = (float)(w + 1) / widthSegs;
        for (int p = 0; p < profileSegs; p++) {
            float v1 = (float)p / profileSegs, v2 = (float)(p + 1) / profileSegs;

            rlNormal3f(data->normals[w][p].x, data->normals[w][p].y, data->normals[w][p].z);
            rlTexCoord2f(u1, v1); rlVertex3f(data->verts[w][p].x, data->verts[w][p].y, data->verts[w][p].z);
            rlNormal3f(data->normals[w + 1][p].x, data->normals[w + 1][p].y, data->normals[w + 1][p].z);
            rlTexCoord2f(u2, v1); rlVertex3f(data->verts[w + 1][p].x, data->verts[w + 1][p].y, data->verts[w + 1][p].z);
            rlNormal3f(data->normals[w + 1][p + 1].x, data->normals[w + 1][p + 1].y, data->normals[w + 1][p + 1].z);
            rlTexCoord2f(u2, v2); rlVertex3f(data->verts[w + 1][p + 1].x, data->verts[w + 1][p + 1].y, data->verts[w + 1][p + 1].z);
            rlNormal3f(data->normals[w][p + 1].x, data->normals[w][p + 1].y, data->normals[w][p + 1].z);
            rlTexCoord2f(u1, v2); rlVertex3f(data->verts[w][p + 1].x, data->verts[w][p + 1].y, data->verts[w][p + 1].z);
        }
    }
    rlEnd();
}