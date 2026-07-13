// Meditation qi-gather component — soft, calm, readable at night.
// Not a skill: call each frame while an agent is meditating.
//
// progress 0..1 = channel fraction (0 = just started, 1 = about to finish).
// time = world/animation time (seconds).
//
// Layers (quiet hierarchy — never competes with combat projectiles):
//   1. Soft ground disc (gather pull)
//   2. Dual counter-rotating runes
//   3. Rising qi wisps (reuse ComposeAura columns, intensity-gated)
//   4. Spiral-in motes (energy converging to the dantian)
//   5. Soft chest light pulse

void VFX_ComposeMeditate(Vector3 pos, float progress, float time)
{
    if (progress <= 0.0f)
        return;

    // Grow-in first 25%, hold, fade last 12% of the channel.
    float growT = fminf(progress / 0.25f, 1.0f);
    float grow = growT * growT * (3.0f - 2.0f * growT); // smoothstep
    float fade = (progress > 0.88f) ? fmaxf(0.0f, (1.0f - progress) / 0.12f) : 1.0f;
    float intensity = grow * fade;
    if (intensity <= 0.001f)
        return;

    const VFX_ElementMaterial *mat = VFX_Material(VC_MAT_QI);
    float radius = 0.55f * grow * VC_Breathe(time, 1.8f, 0.04f);
    float dt = GetFrameTime();
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    // ── 1. Soft ground disc (inward energy) ──────────────────────────────
    // Slightly dimmer than combat GroundAura — atmosphere layer only.
    {
        float discR = radius * 1.15f;
        unsigned char a = (unsigned char)(45.0f + 70.0f * intensity);
        Texture2D glow = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");

        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();

        VC_DrawGroundRune(glow,
                          (Vector3){pos.x, pos.y + 0.008f, pos.z},
                          discR,
                          -time * 8.0f,
                          VC_WithAlpha(mat->soft, a));
        VC_DrawGroundRune(glow,
                          (Vector3){pos.x, pos.y + 0.01f, pos.z},
                          discR * 0.55f,
                          time * 12.0f,
                          VC_WithAlpha(mat->glow, (unsigned char)(a * 0.55f)));

        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
    }

    // ── 2. Dual counter-rotating runes ───────────────────────────────────
    if (mat->runeDecal)
    {
        Texture2D runeTex = ResourceManager_LoadTexture(mat->runeDecal);
        unsigned char runeA = (unsigned char)(50.0f + 90.0f * intensity);

        rlDrawRenderBatchActive();
        BeginBlendMode(BLEND_ADDITIVE);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();

        VC_DrawGroundRune(runeTex,
                          (Vector3){pos.x, pos.y + 0.015f, pos.z},
                          radius * 0.95f,
                          time * 16.0f,
                          VC_WithAlpha(mat->glow, runeA));
        VC_DrawGroundRune(runeTex,
                          (Vector3){pos.x, pos.y + 0.02f, pos.z},
                          radius * 0.55f,
                          -time * 28.0f,
                          VC_WithAlpha(mat->body, (unsigned char)(runeA * 0.65f)));

        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
        EndBlendMode();
    }

    // ── 3. Rising qi wisps (energy layer — reuse aura columns) ───────────
    // Gate by intensity so birth/fade are sparse, peak channel denser.
    if (intensity > 0.35f)
    {
        // Temporarily lean on ComposeAura; spawn chance inside is already
        // probabilistic. Scale radius down for a body-hugging column feel.
        VFX_ComposeAura(VC_MAT_QI, pos, radius * 0.85f, time);
    }

    // ── 4. Spiral-in motes (gather / charge language) ─────────────────────
    // ~6–10 particles/sec at peak, less during grow/fade.
    {
        float spawnRate = 8.0f * intensity;
        if (Random01() < spawnRate * dt)
        {
            float t01 = Random01(); // which ring radius band
            float phase = Random01() * 2.0f * PI;
            float startR = radius * (0.9f + Random01() * 0.35f);
            // Spawn on outer ring; velocity points inward + slight rise.
            Vector3 spawn = VC_RingPointXZ(pos, startR, phase);
            spawn.y += 0.05f + Random01() * 0.25f;

            Vector3 center = {pos.x, spawn.y * 0.5f + 0.35f, pos.z};
            Vector3 inward = Vector3Subtract(center, spawn);
            float len = Vector3Length(inward);
            if (len > 1e-4f)
                inward = Vector3Scale(inward, 1.0f / len);
            else
                inward = (Vector3){0.0f, 1.0f, 0.0f};

            float speed = 0.35f + Random01() * 0.25f;
            Vector3 vel = Vector3Add(Vector3Scale(inward, speed),
                                    (Vector3){0.0f, 0.12f + Random01() * 0.08f, 0.0f});

            Color c0 = VC_WithAlpha(mat->soft, (unsigned char)(180.0f * intensity));
            Color c1 = VC_WithAlpha(mat->glow, 0);

            SpawnParticle((ParticleConfig){
                .position = spawn,
                .velocity = vel,
                .colorStart = c0,
                .colorEnd = c1,
                .radius = 0.012f + Random01() * 0.01f,
                .lifetime = 0.55f + Random01() * 0.35f,
            });
            (void)t01;
        }
    }

    // ── 5. Soft chest light (accent — never bleach) ──────────────────────
    {
        float pulse = VC_Pulse01(time, 2.2f);
        float lightLife = 0.12f;
        float lightR = (0.35f + 0.12f * pulse) * intensity;
        Vector3 lightPos = {pos.x, pos.y + 0.75f, pos.z};
        Color lc = VC_WithAlpha(mat->soft, (unsigned char)(140.0f + 80.0f * pulse * intensity));
        VFXLight_Spawn(lightPos, lc, lightR, lightLife, VFX_PRIORITY_LOW);
    }
}
