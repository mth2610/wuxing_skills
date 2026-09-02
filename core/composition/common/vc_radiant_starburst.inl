// ── PRIMARY. VFX_ComposeRadiantStarburst — one procedural flare shader ──────
// The reference is optical structure, not a cloud of sprites: one hot point,
// a gold corona, then uneven fine rays disappearing into haze. Keep all of it
// in one analytic surface so the rays share one centre and never de-phase.

static Shader s_radiantBurstShader = {0};
static int s_radiantBurstBodyLoc = -1;
static int s_radiantBurstGlowLoc = -1;
static int s_radiantBurstIntensityLoc = -1;
static int s_radiantBurstTimeLoc = -1;
static int s_radiantBurstPassLoc = -1;
static int s_radiantBurstProgressLoc = -1;
static int s_radiantBurstModeLoc = -1;

static void RadiantBurst_InitShared(void)
{
    if (s_radiantBurstShader.id != 0) return;
    s_radiantBurstShader = ResourceManager_LoadShader(NULL,
                                                       "core/shaders/radiant_starburst.fs");
    if (s_radiantBurstShader.id == 0) return;
    s_radiantBurstBodyLoc = GetShaderLocation(s_radiantBurstShader, "u_bodyColor");
    s_radiantBurstGlowLoc = GetShaderLocation(s_radiantBurstShader, "u_glowColor");
    s_radiantBurstIntensityLoc = GetShaderLocation(s_radiantBurstShader, "u_intensity");
    s_radiantBurstTimeLoc = GetShaderLocation(s_radiantBurstShader, "u_starTime");
    s_radiantBurstPassLoc = GetShaderLocation(s_radiantBurstShader, "u_pass");
    s_radiantBurstProgressLoc = GetShaderLocation(s_radiantBurstShader, "u_progress");
    s_radiantBurstModeLoc = GetShaderLocation(s_radiantBurstShader, "u_mode");
}

static void RadiantBurst_DrawQuad(Vector3 center, float radius)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    right = Vector3Scale(right, radius);
    up = Vector3Scale(up, radius);

    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(center.x - right.x - up.x, center.y - right.y - up.y, center.z - right.z - up.z);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(center.x + right.x - up.x, center.y + right.y - up.y, center.z + right.z - up.z);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(center.x + right.x + up.x, center.y + right.y + up.y, center.z + right.z + up.z);
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(center.x - right.x + up.x, center.y - right.y + up.y, center.z - right.z + up.z);
    rlEnd();
    rlDrawRenderBatchActive();
}

static void RadiantBurst_Draw(Vector3 center, VC_MaterialId mat, float radius,
                              float time, float progress, int mode)
{
    RadiantBurst_InitShared();
    if (s_radiantBurstShader.id == 0) return;
    if (radius <= 0.0f) radius = 1.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    const VFX_ElementMaterial *m = VFX_Material(mat);
    Vector4 body = {m->body.r / 255.0f, m->body.g / 255.0f, m->body.b / 255.0f, 1.0f};
    Vector4 glow = {m->glow.r / 255.0f, m->glow.g / 255.0f, m->glow.b / 255.0f, 1.0f};
    // An explosion flash is an event, not a charge: it reaches its full screen
    // footprint in the first 12% of its authored time, holds only briefly,
    // then has already disappeared by 42%. The remainder is for the explosion's
    // other layers (debris, gas, ring), never for this optical flash.
    float energy = mode == 0 ?
        (progress / 0.045f < 1.0f ? progress / 0.045f : 1.0f) *
        (progress < 0.12f ? 1.0f : (progress < 0.42f ? (0.42f - progress) / 0.30f : 0.0f)) : 1.0f;
    if (energy < 0.0f) energy = 0.0f;
    float drawRadius = radius * (mode == 0 ? (0.24f + 0.86f * (progress / 0.12f < 1.0f ? progress / 0.12f : 1.0f))
                                            : (0.92f + 0.08f * sinf(time * 2.4f)));

    for (int pass = 0; pass < 2; ++pass)
    {
        VFXRenderScope scope = VFXRender_BeginDraw(
            pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION,
            pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_ADDITIVE, false);
        BeginShaderMode(s_radiantBurstShader);
        SetShaderValue(s_radiantBurstShader, s_radiantBurstBodyLoc, &body, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_radiantBurstShader, s_radiantBurstGlowLoc, &glow, SHADER_UNIFORM_VEC4);
        SetShaderValue(s_radiantBurstShader, s_radiantBurstIntensityLoc, &energy, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_radiantBurstShader, s_radiantBurstTimeLoc, &time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_radiantBurstShader, s_radiantBurstPassLoc, &pass, SHADER_UNIFORM_INT);
        SetShaderValue(s_radiantBurstShader, s_radiantBurstProgressLoc, &progress, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s_radiantBurstShader, s_radiantBurstModeLoc, &mode, SHADER_UNIFORM_INT);
        RadiantBurst_DrawQuad(center, drawRadius);
        EndShaderMode();
        VFXRender_EndDraw(&scope);
    }
}

void VFX_ComposeRadiantStarburst(Vector3 center, VC_MaterialId mat, float radius,
                                 float t01)
{
    RadiantBurst_Draw(center, mat, radius, TimeFX_Elapsed(), t01, 0);
}

void VFX_ComposeRadiantStarburstHead(Vector3 center, VC_MaterialId mat,
                                     float radius, float time)
{
    RadiantBurst_Draw(center, mat, radius, time, 1.0f, 1);
}
