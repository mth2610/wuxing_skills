// ── Shared Martial Combat Readability: Clash, Deflect, Cleanse, Meditate & Agent Status ──
// Pure procedural geometry + dual-pass BODY/EMISSION (zero autonomous particles,
// zero heap allocation, bright-background compliant).

static VC_MaterialId VC_CombatElemToMat(int elem)
{
    switch (elem) {
        case 0:  return VC_MAT_WATER;
        case 1:  return VC_MAT_WOOD;
        case 2:  return VC_MAT_FIRE;
        case 3:  return VC_MAT_EARTH;
        case 4:  return VC_MAT_METAL;
        default: return VC_MAT_TAIJI;
    }
}

void VFX_ComposeDeflectFlash(Vector3 pos, Vector3 normal, VC_MaterialId mat, float scale)
{
    if (scale <= 0.05f) scale = 1.0f;
    Vector3 n = (Vector3LengthSqr(normal) > 1e-5f) ? Vector3Normalize(normal) : (Vector3){ 0.0f, 1.0f, 0.0f };
    const VFX_ElementMaterial *m = VFX_Material(mat);
    VFX_ShockRingDesc ring = VFX_ShockRing_DefaultDesc(VFX_SHOCK_RING_PARRY);
    VFX_SpawnShockRingEx(pos, n, mat, 1.05f * scale, 0.24f, &ring);
    VFX_ComposeContactSparkMode(pos, mat, 0.80f * scale, 0.90f, CONTACT_SPARK_CENTRIFUGAL);
    ScreenDistort_Add(pos, 1.25f * scale, 0.032f, 0.16f, 1.4f);
    VFXLight_Spawn(pos, m ? m->glow : WHITE, 1.8f * scale, 0.14f, VFX_PRIORITY_LOW);
}

void VFX_ComposeClashBurst(Vector3 pos, int elemA, int elemB, int outcome)
{
    VC_MaterialId matA = VC_CombatElemToMat(elemA);
    VC_MaterialId matB = VC_CombatElemToMat(elemB);
    VC_MaterialId winMat = (outcome == 1) ? matA : ((outcome == 2) ? matB : VC_MAT_TAIJI);
    float sc = (outcome == 0) ? 1.15f : 1.0f;

    VFX_ShockRingDesc ring = VFX_ShockRing_DefaultDesc(VFX_SHOCK_RING_CONCUSSIVE);
    VFX_SpawnShockRingEx(pos, (Vector3){ 0.0f, 1.0f, 0.0f }, winMat, 1.45f * sc, 0.28f, &ring);
    VFX_SpawnShockRing(pos, (Vector3){ 1.0f, 0.0f, 0.0f }, matA, 1.10f * sc, 0.22f);
    VFX_ComposeContactSparkMode(pos, winMat, 0.95f * sc, 0.95f, CONTACT_SPARK_CENTRIFUGAL);
    ScreenDistort_Add(pos, 1.6f * sc, 0.038f, 0.20f, 1.3f);
    Wind_SpawnRadialBlast(pos, 3.8f, 4.5f, 0.30f);

    /* Wuxing Tương Khắc physical reaction accents */
    bool waterFire = (elemA == 0 && elemB == 2) || (elemA == 2 && elemB == 0);
    bool fireMetal = (elemA == 2 && elemB == 4) || (elemA == 4 && elemB == 2);
    bool earthWater = (elemA == 3 && elemB == 0) || (elemA == 0 && elemB == 3);
    if (waterFire) {
        VFX_MistVeilConfig mcfg = VFX_MistVeil_DefaultConfig();
        VFX_MistVeil_ApplyPreset(&mcfg, VFX_MIST_PRESET_PHASE_STEAM_BURST);
        mcfg.radius = 1.6f;
        mcfg.duration = 0.65f;
        mcfg.volumetricFog = false;
        VFX_ComposeMistVeilConfig(pos, pos, &mcfg);
    } else if (fireMetal) {
        VFX_ComposeEmberBurst(pos, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 0.95f, 0.90f);
    } else if (earthWater) {
        VFX_ComposeDebrisShards(pos, (Vector3){ 0.0f, 2.2f, 0.0f }, VC_MAT_EARTH, 0.14f, 6);
    }
}

void VFX_ComposeCleanseBurst(Vector3 pos, VC_MaterialId mat, float radius)
{
    if (radius <= 0.1f) radius = 1.4f;
    Vector3 gp = { pos.x, VFX_GroundHeightFromMap(pos.x, pos.z, NULL) + 0.05f, pos.z };
    Vector3 chest = { pos.x, pos.y + 0.95f, pos.z };
    VFX_ShockRingDesc ring = VFX_ShockRing_DefaultDesc(VFX_SHOCK_RING_PULSE);
    VFX_SpawnShockRingEx(chest, (Vector3){ 0.0f, 1.0f, 0.0f }, mat, radius * 0.78f, 0.30f, &ring);
    VFXLight_Spawn(chest, VFX_Material(mat)->soft, radius * 1.4f, 0.25f, VFX_PRIORITY_LOW);
}

/* Helper: draw a horizontal dual-pass polygonal ring band (BODY + EMISSION) */
static void VC_DrawDualRingBand(Vector3 center, float rIn, float rOut, float yLift,
                                int segs, Color bodyCol, Color glowCol, float phaseRad)
{
    if (segs < 6) segs = 6;
    if (segs > 32) segs = 32;

    for (int pass = 0; pass < 2; pass++) {
        Color col = (pass == 0) ? bodyCol : glowCol;
        if (col.a == 0) continue;
        VFXRenderScope scope = VFXRender_BeginDraw(
            pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION,
            pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_ADDITIVE,
            false);
        rlDrawRenderBatchActive();
        rlDisableBackfaceCulling();
        rlSetTexture(0);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(col.r, col.g, col.b, col.a);
        for (int i = 0; i < segs; i++) {
            float a0 = phaseRad + ((float)i / (float)segs) * 2.0f * PI;
            float a1 = phaseRad + ((float)(i + 1) / (float)segs) * 2.0f * PI;
            float c0 = cosf(a0), s0 = sinf(a0);
            float c1 = cosf(a1), s1 = sinf(a1);
            Vector3 p0i = { center.x + c0 * rIn,  center.y + yLift, center.z + s0 * rIn  };
            Vector3 p0o = { center.x + c0 * rOut, center.y + yLift, center.z + s0 * rOut };
            Vector3 p1i = { center.x + c1 * rIn,  center.y + yLift, center.z + s1 * rIn  };
            Vector3 p1o = { center.x + c1 * rOut, center.y + yLift, center.z + s1 * rOut };
            rlVertex3f(p0i.x, p0i.y, p0i.z);
            rlVertex3f(p0o.x, p0o.y, p0o.z);
            rlVertex3f(p1o.x, p1o.y, p1o.z);
            rlVertex3f(p0i.x, p0i.y, p0i.z);
            rlVertex3f(p1o.x, p1o.y, p1o.z);
            rlVertex3f(p1i.x, p1i.y, p1i.z);
        }
        rlEnd();
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        VFXRender_EndDraw(&scope);
    }
}

/* Helper: draw a compact sculpted 3D octahedron/diamond sigil (BODY + EMISSION) */
static void VC_DrawMarkSigil3D(Vector3 pos, float rx, float ry, float yawRad, Color bodyCol, Color glowCol)
{
    float c = cosf(yawRad), s = sinf(yawRad);
    Vector3 top = { pos.x, pos.y + ry, pos.z };
    Vector3 bot = { pos.x, pos.y - ry, pos.z };
    Vector3 mid[4];
    for (int k = 0; k < 4; k++) {
        float a = yawRad + (float)k * (PI * 0.5f);
        mid[k] = (Vector3){ pos.x + cosf(a) * rx, pos.y, pos.z + sinf(a) * rx };
    }
    (void)c; (void)s;

    for (int pass = 0; pass < 2; pass++) {
        Color col = (pass == 0) ? bodyCol : glowCol;
        if (col.a == 0) continue;
        float sc = (pass == 0) ? 1.0f : 1.18f;
        VFXRenderScope scope = VFXRender_BeginDraw(
            pass == 0 ? VFX_RENDER_PASS_BODY : VFX_RENDER_PASS_EMISSION,
            pass == 0 ? VFX_SURFACE_ALPHA : VFX_SURFACE_ADDITIVE,
            pass == 0);
        rlDrawRenderBatchActive();
        rlSetTexture(0);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(col.r, col.g, col.b, col.a);
        Vector3 t = { pos.x, pos.y + ry * sc, pos.z };
        Vector3 b = { pos.x, pos.y - ry * sc, pos.z };
        for (int k = 0; k < 4; k++) {
            int kn = (k + 1) & 3;
            Vector3 m0 = Vector3Add(pos, Vector3Scale(Vector3Subtract(mid[k], pos), sc));
            Vector3 m1 = Vector3Add(pos, Vector3Scale(Vector3Subtract(mid[kn], pos), sc));
            rlVertex3f(t.x, t.y, t.z); rlVertex3f(m0.x, m0.y, m0.z); rlVertex3f(m1.x, m1.y, m1.z);
            rlVertex3f(b.x, b.y, b.z); rlVertex3f(m1.x, m1.y, m1.z); rlVertex3f(m0.x, m0.y, m0.z);
        }
        rlEnd();
        rlDrawRenderBatchActive();
        VFXRender_EndDraw(&scope);
    }
}

void VFX_ComposeMeditateAura(Vector3 pos, float progress01, float time)
{
    float p = Clamp(progress01, 0.0f, 1.0f);
    float breathe = 0.5f + 0.5f * sinf(time * 2.4f);
    Vector3 gp = { pos.x, VFX_GroundHeightFromMap(pos.x, pos.z, NULL), pos.z };
    float rOuter = 1.05f + 0.16f * breathe + 0.12f * p;
    float rInner = rOuter - 0.08f;

    /* Ground-hugging layer (y <= 0.12m, alpha <= 0.35 per C11) */
    VC_DrawDualRingBand(gp, rInner, rOuter, 0.06f, 24,
                        (Color){ 22, 48, 68, (unsigned char)(85 + 35 * breathe) },
                        (Color){ 110, 215, 255, (unsigned char)(65 + 45 * breathe) },
                        time * 0.45f);
    VC_DrawDualRingBand(gp, rOuter * 0.56f, rOuter * 0.62f, 0.09f, 16,
                        (Color){ 32, 58, 72, 75 },
                        (Color){ 235, 220, 155, (unsigned char)(55 + 40 * p) },
                        -time * 0.72f);
}

void VFX_DrawAgentStatusVisual(const VFX_AgentStatusVisual *v, Camera3D cam)
{
    (void)cam;
    if (!v) return;
    float t = TimeFX_Elapsed();
    Vector3 p = v->position;

    /* 1. 3D Elemental Mark Sigils (1..3 stacks per element) */
    static const Color kMarkBody[5] = {
        { 16,  72, 118, 230 }, /* Water: deep sapphire */
        { 18,  88,  44, 230 }, /* Wood:  deep jade */
        { 118, 28,  14, 230 }, /* Fire:  charred crimson */
        { 92,  66,  32, 230 }, /* Earth: basalt amber */
        { 32,  42,  58, 235 }  /* Metal: tempered steel */
    };
    static const Color kMarkGlow[5] = {
        {  95, 210, 255, 190 },
        { 115, 245, 140, 190 },
        { 255, 145,  45, 210 },
        { 235, 185,  85, 190 },
        { 205, 235, 255, 210 }
    };
    int totalMarks = 0;
    for (int e = 0; e < 5; e++) {
        int st = v->marks[e];
        if (st <= 0) continue;
        if (st > 3) st = 3;
        float orbitR = 0.50f + 0.07f * (float)totalMarks;
        float baseH  = 1.12f + 0.08f * sinf(t * 3.2f + (float)e);
        for (int k = 0; k < st; k++) {
            float ang = t * 2.1f + (float)e * 1.25f + (float)k * (2.0f * PI / (float)st);
            Vector3 mp = { p.x + cosf(ang) * orbitR, p.y + baseH, p.z + sinf(ang) * orbitR };
            float rx = (e == 4) ? 0.042f : 0.058f;
            float ry = (e == 4) ? 0.135f : 0.095f;
            VC_DrawMarkSigil3D(mp, rx, ry, ang * 1.5f, kMarkBody[e], kMarkGlow[e]);
        }
        totalMarks++;
    }

    /* 2. Freeze: 3-prism crystalline frost encasement around torso/legs */
    if (v->frozen) {
        for (int k = 0; k < 3; k++) {
            float ang = (float)k * (2.0f * PI / 3.0f) + 0.35f;
            Vector3 fp = { p.x + cosf(ang) * 0.24f, p.y + 0.65f, p.z + sinf(ang) * 0.24f };
            VC_DrawMarkSigil3D(fp, 0.20f, 0.56f, ang,
                               (Color){ 48, 125, 168, 175 },
                               (Color){ 175, 238, 255, 160 });
        }
        VC_DrawDualRingBand(p, 0.38f, 0.62f, 0.06f, 16,
                            (Color){ 42, 102, 145, 155 },
                            (Color){ 180, 240, 255, 130 }, 0.0f);
    }

    /* 3. Root: dark-slate / hydro ankle shackle band at y = 0.16m */
    if (v->rooted) {
        VC_DrawDualRingBand(p, 0.28f, 0.48f, 0.16f, 12,
                            (Color){ 48, 42, 34, 215 },
                            (Color){ 205, 165, 95, 140 }, -t * 1.6f);
    }

    /* 4. Silence: muted standing-wave sonic collar at throat/head height */
    if (v->silenced) {
        float pulse = 0.04f * sinf(t * 11.0f);
        VC_DrawDualRingBand(p, 0.36f + pulse, 0.44f + pulse, 1.54f, 18,
                            (Color){ 46, 36, 68, 195 },
                            (Color){ 185, 195, 255, 175 }, t * 3.8f);
    }

    /* 5. Stun: 3 orbiting golden-white concussion stars above head */
    if (v->stunned) {
        for (int k = 0; k < 3; k++) {
            float ang = t * 5.2f + (float)k * (2.0f * PI / 3.0f);
            Vector3 sp = { p.x + cosf(ang) * 0.34f, p.y + 2.02f, p.z + sinf(ang) * 0.34f };
            VC_DrawMarkSigil3D(sp, 0.045f, 0.065f, ang * 2.0f,
                               (Color){ 145, 108, 28, 220 },
                               (Color){ 255, 235, 140, 220 });
        }
    }

    /* 6. Vulnerable (Armor Break): broken crimson-amber hexagonal ring at chest */
    if (v->vulnerable) {
        VC_DrawDualRingBand(p, 0.44f, 0.54f, 0.96f, 6,
                            (Color){ 122, 34, 22, 185 },
                            (Color){ 255, 130, 65, 165 }, -t * 1.4f);
    }

    /* 7. Weaken: downward-drifting earthen gravity band */
    if (v->weakened) {
        float drop = fmodf(t * 0.9f, 1.0f);
        float yBand = 1.25f - drop * 0.85f;
        unsigned char aB = (unsigned char)(145.0f * sinf(drop * PI));
        VC_DrawDualRingBand(p, 0.36f, 0.46f, yBand, 12,
                            (Color){ 68, 56, 38, aB },
                            (Color){ 165, 138, 88, (unsigned char)(aB * 0.7f) }, 0.0f);
    }

    /* 8. Burn & Bleed: crisp low-footprint body rings (no autonomous particle spam) */
    if (v->burning) {
        float fl = 0.5f + 0.5f * sinf(t * 12.0f);
        VC_DrawDualRingBand(p, 0.34f, 0.46f, 0.72f + 0.14f * fl, 14,
                            (Color){ 78, 22, 10, 155 },
                            (Color){ 255, 118, 28, (unsigned char)(120 + 75 * fl) }, t * 2.6f);
    }
    if (v->bleeding) {
        float fl = 0.5f + 0.5f * sinf(t * 7.5f);
        VC_DrawDualRingBand(p, 0.32f, 0.42f, 0.88f - 0.10f * fl, 10,
                            (Color){ 115, 14, 18, 175 },
                            (Color){ 235, 45, 55, (unsigned char)(90 + 60 * fl) }, -t * 1.8f);
    }
}
