/* REFERENCE BANDS — the calibration target for the HDR pipeline.
 *
 * THE GAP THIS FILLS. Three instruments existed and none covered the case that
 * matters:
 *   bright_vfx (rlvk)  synthetic and exact, but an ANALOGUE of the game's post
 *                      chain — its own file says "PostFX_Draw analogue". It
 *                      proves the backend, not the pipeline.
 *   gradient_probe     goes through the real chain, but draws a RAMP. It answers
 *                      "is this still smooth", not "is this still 2.0".
 *   render_vfx_matrix  the real chain and real effects — but the effects are
 *                      art, and art can be wrong in the same direction as the
 *                      pipeline, at which point the two agree and both are wrong.
 *
 * This is the missing quadrant: KNOWN radiance through the REAL path. Flat
 * patches at values chosen off §7.6's scale, drawn as a VFX — through
 * VFXRender's target/blend/depth policy, into the scene target, out through
 * bloom, exposure, tone map, grade, LUT, vignette, dither and FXAA.
 *
 * WHAT IT LETS YOU ASSERT, without owning a single art asset:
 *   1. write N  -> the scene target contains N          (format + write path)
 *   2. scene N  -> the frame contains grade(ACES(N))    (the whole post chain)
 *   3. on a WHITE background, an additive patch may never LOWER any channel
 *      (this is exactly the hue-restore defect of 19/08 — see §12.1)
 *   4. patches below the bloom threshold do not bloom; above it, they do
 *
 * DELIBERATELY BORING. No animation, no RNG, no time term: two runs must agree
 * bit for bit, or the harness cannot be used to prove anything (ENERGY ORB does
 * not, and that is why it is not an oracle — §11b).
 */

#define REF_BAND_COUNT 8

/* Off §7.6's scale, and each one is a question:
 *   0.18  mid scenery              1.00  a white surface — the number every
 *   0.50  a body in-band                 authoring decision is judged against
 *   1.25  the bloom threshold      2.00  corona
 *   5.00  the white-hot floor      8.00 / 12.00  a genuinely hot core        */
static const float k_refBandLevel[REF_BAND_COUNT] = {
    0.18f, 0.50f, 1.00f, 1.25f, 2.00f, 5.00f, 8.00f, 12.00f
};

static bool  s_refBandsOn = false;
static Vector3 s_refBandsPos = {0};
static float s_refBandsScale = 1.0f;
static Shader s_refBandsShader = {0};
static int   s_refBandsRadianceLoc = -1;
static bool  s_refBandsInit = false;

/* Achromatic ON PURPOSE. A grey patch makes "the value changed" and "the hue
 * changed" separable — with a coloured patch a per-channel curve and a
 * hue-preserving one produce different numbers and you cannot say which moved. */
int VFX_ComposeRefBands(Vector3 pos, float scale)
{
    if (!s_refBandsInit) {
        s_refBandsShader = LoadShader(0, "core/shaders/ref_bands.fs");
        s_refBandsRadianceLoc = GetShaderLocation(s_refBandsShader, "u_radiance");
        s_refBandsInit = true;
    }
    s_refBandsPos = pos;
    s_refBandsScale = (scale > 0.0f) ? scale : 1.0f;
    s_refBandsOn = true;
    TraceLog(LOG_INFO, "VFX_REF_BANDS: %d patches at %.2f .. %.2f scene-referred",
             REF_BAND_COUNT, k_refBandLevel[0], k_refBandLevel[REF_BAND_COUNT - 1]);
    return 0;
}

void VFX_KillRefBands(int id) { (void)id; s_refBandsOn = false; }

void VC_RefBands_Draw3D(Camera3D cam)
{
    if (!s_refBandsOn || s_refBandsShader.id == 0) return;

    /* Camera-facing, so the patch area on screen does not depend on where the
       fixture camera happens to sit. */
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
    Vector3 up = Vector3CrossProduct(right, fwd);

    /* SIZED TO CLEAR THE VIGNETTE. The first version spanned the full frame and
       the two outermost patches sat in it, which darkened exactly the extreme
       bands — 0.18 read 17% low and 12.00's blue channel came out BELOW 8.00's,
       looking like a non-monotone pipeline. It was the instrument, not the
       pipeline. A calibration target has to be measured where nothing else is
       acting on it, so the whole row now lives in the middle of the frame. */
    const float w = 0.21f * s_refBandsScale;   /* half-width of one patch  */
    const float h = 0.45f * s_refBandsScale;   /* half-height              */
    const float pitch = 0.50f * s_refBandsScale;

    VFXRenderScope scope =
        VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
    BeginShaderMode(s_refBandsShader);

    for (int i = 0; i < REF_BAND_COUNT; i++)
    {
        float lvl = k_refBandLevel[i];
        float rgb[3] = {lvl, lvl, lvl};
        if (s_refBandsRadianceLoc >= 0)
            SetShaderValue(s_refBandsShader, s_refBandsRadianceLoc, rgb, SHADER_UNIFORM_VEC3);
        /* One patch per draw: the uniform changes between them, so they cannot
           share a batch. Flushed explicitly rather than trusting the batcher —
           an unflushed uniform change is how every patch ends up the same
           value and the whole instrument silently reads as a pass. */
        rlDrawRenderBatchActive();

        float off = ((float)i - (float)(REF_BAND_COUNT - 1) * 0.5f) * pitch;
        Vector3 c = Vector3Add(s_refBandsPos, Vector3Scale(right, off));
        Vector3 rw = Vector3Scale(right, w), uh = Vector3Scale(up, h);

        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);   /* unused by the shader; kept legal */
        rlTexCoord2f(0.0f, 0.0f); rlVertex3f(c.x - rw.x - uh.x, c.y - rw.y - uh.y, c.z - rw.z - uh.z);
        rlTexCoord2f(1.0f, 0.0f); rlVertex3f(c.x + rw.x - uh.x, c.y + rw.y - uh.y, c.z + rw.z - uh.z);
        rlTexCoord2f(1.0f, 1.0f); rlVertex3f(c.x + rw.x + uh.x, c.y + rw.y + uh.y, c.z + rw.z + uh.z);
        rlTexCoord2f(0.0f, 1.0f); rlVertex3f(c.x - rw.x + uh.x, c.y - rw.y + uh.y, c.z - rw.z + uh.z);
        rlEnd();
        rlDrawRenderBatchActive();
    }

    EndShaderMode();
    VFXRender_EndDraw(&scope);
}
