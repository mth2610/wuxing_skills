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

    // Intersecting planes
    Vector3 p1 = Vector3Add(start, Vector3Scale(perp1, -halfW));
    Vector3 p2 = Vector3Add(end, Vector3Scale(perp1, -halfW));
    Vector3 p3 = Vector3Add(end, Vector3Scale(perp1, halfW));
    Vector3 p4 = Vector3Add(start, Vector3Scale(perp1, halfW));

    Vector3 q1 = Vector3Add(start, Vector3Scale(perp2, -halfW));
    Vector3 q2 = Vector3Add(end, Vector3Scale(perp2, -halfW));
    Vector3 q3 = Vector3Add(end, Vector3Scale(perp2, halfW));
    Vector3 q4 = Vector3Add(start, Vector3Scale(perp2, halfW));

    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png"); // scrolling mask or simple glow
    rlSetTexture(tex.id);

    float scroll = time * -2.0f;

    const VFX_ElementMaterial *mat = VFX_Material(matId);
    // Sheet ADDITIVE đọc bằng glow (nóng), sheet ALPHA đọc bằng body (đặc/mờ).
    Color col = (mat->blendMode == BLEND_ALPHA) ? VC_WithAlpha(mat->body, 190)
                                                : VC_WithAlpha(mat->glow, 235);
    BeginBlendMode(mat->blendMode);

    if (matId == VC_MAT_LIGHTNING)
    {
        // Jitter for electricity
        float jitter = sinf(time * 80.0f) * 0.05f;
        Vector3 offset = Vector3Scale(perp1, jitter);
        p1 = Vector3Add(p1, offset); p2 = Vector3Add(p2, offset);
        p3 = Vector3Add(p3, offset); p4 = Vector3Add(p4, offset);
    }

    // Draw first plane
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlTexCoord2f(scroll, 0.0f); rlVertex3f(p1.x, p1.y, p1.z);
    rlTexCoord2f(scroll + 1.0f, 0.0f); rlVertex3f(p2.x, p2.y, p2.z);
    rlTexCoord2f(scroll + 1.0f, 1.0f); rlVertex3f(p3.x, p3.y, p3.z);
    rlTexCoord2f(scroll, 1.0f); rlVertex3f(p4.x, p4.y, p4.z);
    rlEnd();

    // Draw second plane
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlTexCoord2f(scroll, 0.0f); rlVertex3f(q1.x, q1.y, q1.z);
    rlTexCoord2f(scroll + 1.0f, 0.0f); rlVertex3f(q2.x, q2.y, q2.z);
    rlTexCoord2f(scroll + 1.0f, 1.0f); rlVertex3f(q3.x, q3.y, q3.z);
    rlTexCoord2f(scroll, 1.0f); rlVertex3f(q4.x, q4.y, q4.z);
    rlEnd();

    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();

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
