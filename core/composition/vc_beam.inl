#define BEAM_RIBBON_PTS 9

// Two crossed planes (a fake "+" cross-section) instead of one billboard so
// the beam reads as solid from any viewing angle, not just face-on. Both
// planes are fixed relative to `dir` (perp1/perp2), NOT camera-facing —
// unlike every other ribbon in the engine this one intentionally does not
// billboard, which is exactly RIBBON_FIXED_NORMAL's use case: pass perp2 as
// the fixed normal to get a plane whose width runs along perp1, and vice
// versa for the second pass.
void VFX_ComposeBeam(VC_MaterialId matId, Vector3 start, Vector3 end, float width, float progress, float time)
{
    float beamLen = Vector3Distance(start, end);
    if (beamLen < 0.01f) return;

    Vector3 dir = Vector3Normalize(Vector3Subtract(end, start));
    Vector3 perp1 = (fabsf(dir.y) < 0.99f) ? Vector3Normalize(Vector3CrossProduct(dir, (Vector3){0, 1, 0}))
                                           : Vector3Normalize(Vector3CrossProduct(dir, (Vector3){1, 0, 0}));
    Vector3 perp2 = Vector3Normalize(Vector3CrossProduct(dir, perp1));

    float currentWidth = width * fminf(progress / 0.1f, 1.0f);
    float halfW = currentWidth * 0.5f;

    // Whole-beam rigid shake for lightning (matches the pre-ribbon version —
    // a single offset applied uniformly, not a per-point wave).
    Vector3 jitterOffset = {0};
    if (matId == VC_MAT_LIGHTNING)
        jitterOffset = Vector3Scale(perp1, sinf(time * 80.0f) * 0.05f);

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    // Sheet ADDITIVE đọc bằng glow (nóng), sheet ALPHA đọc bằng body (đặc/mờ).
    Color col = (mat->blendMode == BLEND_ALPHA) ? VC_WithAlpha(mat->body, 190)
                                                : VC_WithAlpha(mat->glow, 235);
    Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png"); // scrolling mask or simple glow
    float scroll = time * -2.0f;

    static RibbonPoint s_beamRibbon[BEAM_RIBBON_PTS];
    for (int pass = 0; pass < 2; pass++)
    {
        Vector3 fixedNormal = (pass == 0) ? perp2 : perp1; // side = tangent × fixedNormal

        for (int k = 0; k < BEAM_RIBBON_PTS; k++)
        {
            float t = (float)k / (float)(BEAM_RIBBON_PTS - 1);
            s_beamRibbon[k].position  = Vector3Add(Vector3Lerp(start, end, t), jitterOffset);
            s_beamRibbon[k].halfWidth = halfW;
            s_beamRibbon[k].tint      = col;
        }
        Ribbon_ComputeArcLengthUV(s_beamRibbon, BEAM_RIBBON_PTS);
        for (int k = 0; k < BEAM_RIBBON_PTS; k++)
            s_beamRibbon[k].v += scroll; // scroll along length, same as the old U-axis scroll

        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginBlendMode(mat->blendMode);
        DrawRibbonStripEx(s_beamRibbon, BEAM_RIBBON_PTS, tex, (Camera3D){0},
                          RIBBON_FIXED_NORMAL, fixedNormal);
        EndBlendMode();
        rlDrawRenderBatchActive();
        rlEnableDepthMask();
    }

    // Emit particles at impact point
    if (GetRandomValue(0, 100) < 15)
    {
        SpawnParticle((ParticleConfig){
            .position = end,
            .velocity = (Vector3){((float)rand() / (float)RAND_MAX - 0.5f) * 1.5f, ((float)rand() / (float)RAND_MAX) * 1.0f, ((float)rand() / (float)RAND_MAX - 0.5f) * 1.5f},
            .radius = 0.05f * width,
            .lifetime = 0.4f,
            .colorStart = col,
            .colorEnd = (Color){col.r, col.g, col.b, 0}
        });
    }
}

#undef BEAM_RIBBON_PTS
