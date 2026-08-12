/* ===========================================================================
 * SHOCKWAVE ANNULUS
 * ===========================================================================*/

static float ShockwaveMesh_Clamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static float ShockwaveMesh_SmoothStep01(float value)
{
    value = ShockwaveMesh_Clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

static float ShockwaveMesh_Profile(float u, float crestU)
{
    float k;
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    k = logf(0.5f) / logf(crestU);
    return sinf(PI * powf(u, k));
}

/* Two integer-frequency terms make a low-frequency, non-repeating-enough
 * outline while still producing exactly the same value at the UV seam. */
static float ShockwaveMesh_AngularNoise(float angle, int lobes, float phase)
{
    float primary = sinf(angle * (float)lobes + phase);
    float secondary = sinf(angle * (float)(lobes * 2 + 1) - phase * 1.37f);
    return primary * 0.70f + secondary * 0.30f;
}

ShockwaveMeshConfig ProceduralMesh_DefaultShockwaveConfig(void)
{
    ShockwaveMeshConfig cfg = {0};
    cfg.radius = 3.0f;
    cfg.bandWidth = 0.66f;
    cfg.lipHeight = 0.20f;
    cfg.crestU = 0.6666667f;
    cfg.radialJitter = 0.10f;
    cfg.lipJitter = 0.035f;
    cfg.angularLobes = 5;
    cfg.angularPhase = 0.0f;
    cfg.yLift = 0.035f;
    return cfg;
}

void ProceduralMesh_BuildShockwave(ShockwaveMeshData *out, Vector3 center,
                                   const ShockwaveMeshConfig *cfg, int slices,
                                   int radials, GroundHeightSampleFn heightFn,
                                   void *userData)
{
    ShockwaveMeshConfig fallback;
    float heightInner[SHOCKWAVE_HEIGHT_SAMPLES + 1];
    float heightOuter[SHOCKWAVE_HEIGHT_SAMPLES + 1];
    float radius, band, crestU, maxJitter, radialJitter, lipJitter;
    int lobes;

    if (out == NULL) return;
    if (cfg == NULL) {
        fallback = ProceduralMesh_DefaultShockwaveConfig();
        cfg = &fallback;
    }

    if (slices < 8) slices = 8;
    if (slices > SHOCKWAVE_MAX_SLICES) slices = SHOCKWAVE_MAX_SLICES;
    if (radials < 2) radials = 2;
    if (radials > SHOCKWAVE_MAX_RADIALS) radials = SHOCKWAVE_MAX_RADIALS;
    out->slices = slices;
    out->radials = radials;

    radius = fmaxf(cfg->radius, 0.01f);
    band = ShockwaveMesh_Clamp(cfg->bandWidth, 0.002f, radius * 1.50f);
    crestU = ShockwaveMesh_Clamp(cfg->crestU, 0.10f, 0.90f);
    maxJitter = band * 0.35f;
    radialJitter = ShockwaveMesh_Clamp(cfg->radialJitter, 0.0f, maxJitter);
    lipJitter = ShockwaveMesh_Clamp(cfg->lipJitter, 0.0f,
                                    fmaxf(cfg->lipHeight, 0.0f) * 0.75f);
    lobes = cfg->angularLobes;
    if (lobes < 1) lobes = 1;

    /* Terrain is queried only on a 24-sample outer/inner pair. The deformed
     * mesh interpolates those values, so high quality raises draw density but
     * never turns into a per-vertex raycast cost. */
    for (int h = 0; h < SHOCKWAVE_HEIGHT_SAMPLES; h++) {
        float uAngle = (float)h / (float)SHOCKWAVE_HEIGHT_SAMPLES;
        float angle = uAngle * 2.0f * PI;
        float noise = ShockwaveMesh_AngularNoise(angle, lobes, cfg->angularPhase);
        float innerRadius = radius - band * 0.5f;
        float outerRadius = radius + band * 0.5f +
                            noise * radialJitter;
        float ca = cosf(angle);
        float sa = sinf(angle);
        if (heightFn != NULL) {
            heightInner[h] = heightFn(center.x + ca * innerRadius,
                                      center.z + sa * innerRadius, userData);
            heightOuter[h] = heightFn(center.x + ca * outerRadius,
                                      center.z + sa * outerRadius, userData);
        } else {
            heightInner[h] = center.y;
            heightOuter[h] = center.y;
        }
    }
    heightInner[SHOCKWAVE_HEIGHT_SAMPLES] = heightInner[0];
    heightOuter[SHOCKWAVE_HEIGHT_SAMPLES] = heightOuter[0];

    for (int s = 0; s <= slices; s++) {
        float angleU = (float)s / (float)slices;
        float angle = angleU * 2.0f * PI;
        float sampleF = angleU * (float)SHOCKWAVE_HEIGHT_SAMPLES;
        int sampleI = (int)sampleF;
        float sampleT;
        float gInner, gOuter;
        float ca = cosf(angle);
        float sa = sinf(angle);
        float noise = ShockwaveMesh_AngularNoise(angle, lobes, cfg->angularPhase);

        if (sampleI >= SHOCKWAVE_HEIGHT_SAMPLES) sampleI = SHOCKWAVE_HEIGHT_SAMPLES - 1;
        sampleT = sampleF - (float)sampleI;
        gInner = heightInner[sampleI] +
                 (heightInner[sampleI + 1] - heightInner[sampleI]) * sampleT;
        gOuter = heightOuter[sampleI] +
                 (heightOuter[sampleI + 1] - heightOuter[sampleI]) * sampleT;

        for (int i = 0; i <= radials; i++) {
            float u = (float)i / (float)radials;
            float profile = ShockwaveMesh_Profile(u, crestU);
            float jitter = noise * radialJitter * u * u;
            float heightNoise = noise * lipJitter * profile;
            float ringRadius = radius - band * 0.5f + band * u + jitter;
            float groundY = gInner + (gOuter - gInner) * u;

            out->verts[s][i] = (Vector3){center.x + ca * ringRadius,
                                         groundY + cfg->yLift +
                                             cfg->lipHeight * profile + heightNoise,
                                         center.z + sa * ringRadius};
            out->uv[s][i] = (Vector2){angleU, u};
        }
    }

    for (int s = 0; s <= slices; s++) {
        int prevS = (s == 0) ? slices - 1 : s - 1;
        int nextS = (s == slices) ? 1 : s + 1;
        for (int i = 0; i <= radials; i++) {
            int prevI = (i == 0) ? 0 : i - 1;
            int nextI = (i == radials) ? radials : i + 1;
            Vector3 alongAngle = Vector3Subtract(out->verts[nextS][i],
                                                  out->verts[prevS][i]);
            Vector3 acrossBand = Vector3Subtract(out->verts[s][nextI],
                                                  out->verts[s][prevI]);
            Vector3 n = Vector3CrossProduct(alongAngle, acrossBand);
            if (Vector3LengthSqr(n) < 1e-8f) n = (Vector3){0.0f, 1.0f, 0.0f};
            else n = Vector3Normalize(n);
            if (n.y < 0.0f) n = Vector3Negate(n);
            out->normals[s][i] = n;
        }
    }
}

void ProceduralMesh_DrawShockwave(const ShockwaveMeshData *data,
                                  const Color *radialColors)
{
    if (data == NULL || data->slices < 1 || data->radials < 1) return;

    rlCheckRenderBatchLimit(data->slices * data->radials * 4);
    rlBegin(RL_QUADS);
    for (int s = 0; s < data->slices; s++) {
        for (int i = 0; i < data->radials; i++) {
            const int ss[4] = {s, s, s + 1, s + 1};
            const int ii[4] = {i, i + 1, i + 1, i};
            for (int v = 0; v < 4; v++) {
                Vector3 p = data->verts[ss[v]][ii[v]];
                Vector3 n = data->normals[ss[v]][ii[v]];
                Vector2 uv = data->uv[ss[v]][ii[v]];
                Color c = radialColors ? radialColors[ii[v]] : WHITE;
                rlColor4ub(c.r, c.g, c.b, c.a);
                rlNormal3f(n.x, n.y, n.z);
                rlTexCoord2f(uv.x, uv.y);
                rlVertex3f(p.x, p.y, p.z);
            }
        }
    }
    rlEnd();
}
