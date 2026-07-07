// Chain archetype — sequentially connects an array of target positions
// (bounce/jump targeting, e.g. chain lightning). `progress` sweeps 0..1
// across the whole chain, same per-segment "wavefront" convention as
// VFX_PathWave: segment i is revealed once progress crosses i/(count-1).

void VFX_ComposeChain(VC_MaterialId matId, const Vector3 *targets, int count, float progress, float time)
{
    if (targets == NULL || count < 2)
        return;

    // Lightning chain đọc bằng glow cyan (hồ quang), còn lại body.
    const VFX_ElementMaterial *m = VFX_Material(matId);
    Color color = (matId == VC_MAT_LIGHTNING) ? m->glow : m->body;

    for (int i = 0; i < count - 1; i++)
    {
        float segStart = (float)i / (float)(count - 1);
        float segEnd = (float)(i + 1) / (float)(count - 1);
        if (progress < segStart)
            break; // wavefront hasn't reached this segment yet

        float segT = (segEnd > segStart) ? fminf((progress - segStart) / (segEnd - segStart), 1.0f) : 1.0f;
        Vector3 a = targets[i];
        Vector3 b = targets[i + 1];

        switch (matId)
        {
            case VC_MAT_LIGHTNING:
            {
                // Flicker the link rather than draw it every frame — reads
                // as an arcing current, not a static beam.
                if (GetRandomValue(0, 100) < 60)
                    VFX_ComposeLightningBolt(a, Vector3Lerp(a, b, segT), 0.5f);

                // Small branches — short jagged forks leaping off a random
                // point of the live segment. A main channel with no forks
                // reads as a rope, not electricity.
                if (GetRandomValue(0, 100) < 30)
                {
                    float bt = 0.25f + Random01() * 0.5f;
                    Vector3 root = Vector3Lerp(a, Vector3Lerp(a, b, segT), bt);
                    Vector3 bdir = {Random01() - 0.5f, Random01() * 0.4f - 0.1f, Random01() - 0.5f};
                    bdir = Vector3Normalize(bdir);
                    Vector3 bEnd = Vector3Add(root, Vector3Scale(bdir, 0.15f + Random01() * 0.2f));
                    Vector3 wp[9];
                    RegenerateLightningWaypoints(wp, root, bEnd, 0.12f);
                    rlDrawRenderBatchActive();
                    BeginBlendMode(BLEND_ADDITIVE);
                    DrawLightningBolt(wp, 0.006f, camera);
                    rlDrawRenderBatchActive();
                    EndBlendMode();
                }
                break;
            }
            case VC_MAT_WOOD:
            {
                Vector3 mid = Vector3Lerp(a, b, 0.5f);
                mid.y += Vector3Distance(a, b) * 0.15f; // slight arch so it doesn't look like a straight rod
                VFX_ComposeGlowingVine(a, b, Vector3Lerp(a, mid, 0.33f), Vector3Lerp(mid, b, 0.66f), b,
                                      fminf(segT * 1.2f, 1.0f), time, 0.6f, i, count - 1);
                break;
            }
            default:
            {
                // Mọi material khác: đoạn nối = beam cùng nguyên tố.
                Vector3 head = Vector3Lerp(a, b, segT);
                VFX_ComposeBeam(matId, a, head, 0.12f, 1.0f, time);
                break;
            }
        }

        // Sparks shedding off the live link — completed segments stay quiet,
        // so detail concentrates where the current is flowing.
        if (GetRandomValue(0, 100) < 15)
        {
            Vector3 sparkPos = Vector3Lerp(a, Vector3Lerp(a, b, segT), Random01());
            SpawnParticle((ParticleConfig){
                .position = sparkPos,
                .velocity = (Vector3){(Random01() - 0.5f) * 0.4f, -0.2f - Random01() * 0.3f, (Random01() - 0.5f) * 0.4f},
                .colorStart = color,
                .colorEnd = ColorAlpha(color, 0.0f),
                .radius = 0.008f + Random01() * 0.006f,
                .lifetime = 0.2f + Random01() * 0.15f});
        }

        // First frame this segment's target is reached — punch it with a glint.
        if (segT >= 0.999f && GetRandomValue(0, 100) < 25)
            VFX_ComposeGlintBurst(b, 6, 0.25f, color);
    }

    // Travel light — one bright pulse riding the wavefront across the whole
    // chain, so the eye follows the jump from target to target instead of
    // seeing segments light up abstractly. progress*(count-1) = fractional
    // segment index of the head.
    float headF = fminf(progress, 1.0f) * (float)(count - 1);
    int headSeg = (int)headF;
    if (headSeg >= count - 1)
        headSeg = count - 2;
    Vector3 headPos = Vector3Lerp(targets[headSeg], targets[headSeg + 1], headF - (float)headSeg);
    VFXLight_Spawn(headPos, color, 1.2f, 0.1f, VFX_PRIORITY_LOW);
    SpawnParticle((ParticleConfig){
        .position = headPos,
        .colorStart = WHITE,
        .colorEnd = ColorAlpha(color, 0.0f),
        .radius = 0.06f,
        .lifetime = 0.08f});
}
