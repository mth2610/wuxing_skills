/* ===========================================================================
 * FREE-SPACE IMPACT SHOCKWAVE SHELL
 * ===========================================================================*/

static float ImpactShockwave_Clamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/* Two integer-frequency terms make the shell uneven while producing exactly
 * the same value at the UV seam. This is geometry noise: it changes the
 * silhouette, unlike a flow map. */
static float ImpactShockwave_AngularNoise(float angle, int lobes, float phase)
{
    float primary = sinf(angle * (float)lobes + phase);
    float secondary = sinf(angle * (float)(lobes * 2 + 1) - phase * 1.37f);
    return primary * 0.70f + secondary * 0.30f;
}

static float ImpactShockwave_LensProfile(float u)
{
    if (u <= 0.0f || u >= 1.0f) return 0.0f;
    return sinf(PI * u);
}

ImpactShockwaveMeshConfig ProceduralMesh_DefaultImpactShockwaveConfig(void)
{
    ImpactShockwaveMeshConfig cfg = {0};
    cfg.radius = 2.5f;
    cfg.bandWidth = 0.65f;
    cfg.halfHeight = 0.38f;
    cfg.radialJitter = 0.11f;
    cfg.heightJitter = 0.06f;
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
    float radius, band, radialJitter, heightJitter;
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
    band = ImpactShockwave_Clamp(cfg->bandWidth, 0.002f, radius * 1.50f);
    radialJitter = ImpactShockwave_Clamp(cfg->radialJitter, 0.0f, band * 0.35f);
    heightJitter = ImpactShockwave_Clamp(cfg->heightJitter, 0.0f,
                                          fmaxf(cfg->halfHeight, 0.0f) * 0.75f);
    lobes = cfg->angularLobes;
    if (lobes < 1) lobes = 1;

    for (int s = 0; s <= slices; s++) {
        float angleU = (float)s / (float)slices;
        float angle = angleU * 2.0f * PI;
        float ca = cosf(angle);
        float sa = sinf(angle);
        float noise = ImpactShockwave_AngularNoise(angle, lobes, cfg->angularPhase);

        for (int i = 0; i <= radials; i++) {
            float u = (float)i / (float)radials;
            float profile = ImpactShockwave_LensProfile(u);
            float jitter = noise * radialJitter * u * u;
            float heightNoise = noise * heightJitter * profile;
            float ringRadius = radius - band * 0.5f + band * u + jitter;

            out->uv[s][i] = (Vector2){angleU, u};
            for (int side = 0; side < IMPACT_SHOCKWAVE_SIDES; side++) {
                float sign = (side == 0) ? 1.0f : -1.0f;
                out->verts[side][s][i] = (Vector3){
                    center.x + ca * ringRadius,
                    center.y + sign * (cfg->halfHeight * profile + heightNoise),
                    center.z + sa * ringRadius
                };
            }
        }
    }

    for (int side = 0; side < IMPACT_SHOCKWAVE_SIDES; side++) {
        for (int s = 0; s <= slices; s++) {
            int prevS = (s == 0) ? slices - 1 : s - 1;
            int nextS = (s == slices) ? 1 : s + 1;
            for (int i = 0; i <= radials; i++) {
                int prevI = (i == 0) ? 0 : i - 1;
                int nextI = (i == radials) ? radials : i + 1;
                Vector3 alongAngle = Vector3Subtract(out->verts[side][nextS][i],
                                                      out->verts[side][prevS][i]);
                Vector3 acrossBand = Vector3Subtract(out->verts[side][s][nextI],
                                                      out->verts[side][s][prevI]);
                Vector3 normal = Vector3CrossProduct(alongAngle, acrossBand);
                if (Vector3LengthSqr(normal) < 1e-8f)
                    normal = (Vector3){0.0f, 1.0f, 0.0f};
                else
                    normal = Vector3Normalize(normal);
                if ((side == 0 && normal.y < 0.0f) ||
                    (side == 1 && normal.y > 0.0f))
                    normal = Vector3Negate(normal);
                out->normals[side][s][i] = normal;
            }
        }
    }
}

void ProceduralMesh_DrawImpactShockwave(const ImpactShockwaveMeshData *data,
                                        const Color *radialColors)
{
    if (data == NULL || data->slices < 1 || data->radials < 1) return;
    rlCheckRenderBatchLimit(IMPACT_SHOCKWAVE_SIDES * data->slices * data->radials * 4);
    rlBegin(RL_QUADS);
    for (int side = 0; side < IMPACT_SHOCKWAVE_SIDES; side++) {
        for (int s = 0; s < data->slices; s++) {
            for (int i = 0; i < data->radials; i++) {
                const int ss[4] = {s, s, s + 1, s + 1};
                const int ii[4] = {i, i + 1, i + 1, i};
                for (int v = 0; v < 4; v++) {
                    Vector3 p = data->verts[side][ss[v]][ii[v]];
                    Vector3 n = data->normals[side][ss[v]][ii[v]];
                    Vector2 uv = data->uv[ss[v]][ii[v]];
                    Color c = radialColors ? radialColors[ii[v]] : WHITE;
                    rlColor4ub(c.r, c.g, c.b, c.a);
                    rlNormal3f(n.x, n.y, n.z);
                    rlTexCoord2f(uv.x, uv.y);
                    rlVertex3f(p.x, p.y, p.z);
                }
            }
        }
    }
    rlEnd();
}
