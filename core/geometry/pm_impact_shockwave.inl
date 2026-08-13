/* ===========================================================================
 * PLANAR IMPACT SHOCKWAVE DISC
 * ===========================================================================*/

static float ImpactShockwave_Clamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/* Geometry owns only the large torn outer edge. Integer frequencies make the
 * first and final longitude numerically identical, so the disc has no seam. */
static float ImpactShockwave_AngularNoise(float angle, int lobes, float phase)
{
    float primary = sinf(angle * (float)lobes + phase);
    float secondary = sinf(angle * (float)(lobes * 2 + 1) - phase * 1.37f);
    return primary * 0.70f + secondary * 0.30f;
}

ImpactShockwaveMeshConfig ProceduralMesh_DefaultImpactShockwaveConfig(void)
{
    ImpactShockwaveMeshConfig cfg = {0};
    cfg.radius = 2.5f;
    cfg.radialJitter = 0.11f;
    cfg.angularLobes = 5;
    cfg.angularPhase = 0.0f;
    return cfg;
}

void ProceduralMesh_BuildImpactShockwave(ImpactShockwaveMeshData *out,
                                         Vector3 center,
                                         const ImpactShockwaveMeshConfig *cfg,
                                         int slices, int radials)
{
    ImpactShockwaveMeshConfig fallback;
    float radius, radialJitter;
    int lobes;

    if (out == NULL) return;
    if (cfg == NULL) {
        fallback = ProceduralMesh_DefaultImpactShockwaveConfig();
        cfg = &fallback;
    }
    if (slices < 8) slices = 8;
    if (slices > IMPACT_SHOCKWAVE_MAX_SLICES) slices = IMPACT_SHOCKWAVE_MAX_SLICES;
    if (radials < 2) radials = 2;
    if (radials > IMPACT_SHOCKWAVE_MAX_RADIALS) radials = IMPACT_SHOCKWAVE_MAX_RADIALS;
    out->slices = slices;
    out->radials = radials;

    radius = fmaxf(cfg->radius, 0.01f);
    radialJitter = ImpactShockwave_Clamp(cfg->radialJitter, 0.0f, radius * 0.22f);
    lobes = cfg->angularLobes;
    if (lobes < 1) lobes = 1;

    for (int s = 0; s <= slices; s++) {
        float u = (float)s / (float)slices;
        float angle = u * 2.0f * PI;
        float ca = cosf(angle);
        float sa = sinf(angle);
        float noise = ImpactShockwave_AngularNoise(angle, lobes, cfg->angularPhase);

        for (int r = 0; r <= radials; r++) {
            float v = (float)r / (float)radials;
            // Keep the centre welded; put the visual tear on the outward
            // pressure front. The shader owns the soft, irregular centre hole.
            float jitter = noise * radialJitter * v * v;
            float ringRadius = radius * v + jitter;
            out->uv[s][r] = (Vector2){u, v};
            out->verts[0][s][r] = (Vector3){
                center.x + ca * ringRadius,
                center.y,
                center.z + sa * ringRadius
            };
            out->normals[0][s][r] = (Vector3){0.0f, 1.0f, 0.0f};
        }
    }
}

void ProceduralMesh_DrawImpactShockwave(const ImpactShockwaveMeshData *data,
                                        const Color *radialColors)
{
    if (data == NULL || data->slices < 1 || data->radials < 1) return;
    rlCheckRenderBatchLimit(data->slices * data->radials * 4);
    rlBegin(RL_QUADS);
    for (int s = 0; s < data->slices; s++) {
        for (int r = 0; r < data->radials; r++) {
            const int ss[4] = {s, s, s + 1, s + 1};
            const int rr[4] = {r, r + 1, r + 1, r};
            for (int v = 0; v < 4; v++) {
                Vector3 p = data->verts[0][ss[v]][rr[v]];
                Vector3 n = data->normals[0][ss[v]][rr[v]];
                Vector2 uv = data->uv[ss[v]][rr[v]];
                Color c = radialColors ? radialColors[rr[v]] : WHITE;
                rlColor4ub(c.r, c.g, c.b, c.a);
                rlNormal3f(n.x, n.y, n.z);
                rlTexCoord2f(uv.x, uv.y);
                rlVertex3f(p.x, p.y, p.z);
            }
        }
    }
    rlEnd();
}
