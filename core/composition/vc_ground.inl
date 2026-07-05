void VFX_GroundPattern(GroundPatternStyle style, Vector3 pos, float radius, float progress, float time)
{
    float alpha = 1.0f;
    if (progress > 0.8f) {
        alpha = (1.0f - progress) / 0.2f;
        if (alpha < 0.0f) alpha = 0.0f;
    }

    float currentRadius = radius * fminf(progress / 0.2f, 1.0f);

    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    rlPushMatrix();
    rlTranslatef(pos.x, pos.y + 0.005f, pos.z);

    switch (style)
    {
        case GROUND_CRACK_RADIAL:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
            rlSetTexture(tex.id);
            rlBegin(RL_QUADS);
            rlColor4ub(120, 100, 80, (unsigned char)(255 * alpha));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(currentRadius, 0, currentRadius);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-currentRadius, 0, currentRadius);
            rlEnd();
            break;
        }
        case GROUND_CRACK_LINE:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
            rlSetTexture(tex.id);
            rlBegin(RL_QUADS);
            rlColor4ub(100, 90, 80, (unsigned char)(255 * alpha));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-currentRadius * 0.2f, 0, -currentRadius);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(currentRadius * 0.2f, 0, -currentRadius);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(currentRadius * 0.2f, 0, currentRadius);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-currentRadius * 0.2f, 0, currentRadius);
            rlEnd();
            break;
        }
        case GROUND_MAGIC_CIRCLE:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            rlRotatef(time * 40.0f, 0, 1, 0);
            rlSetTexture(tex.id);
            BeginBlendMode(BLEND_ADDITIVE);
            rlBegin(RL_QUADS);
            rlColor4ub(0, 180, 255, (unsigned char)(200 * alpha));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(currentRadius, 0, currentRadius);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-currentRadius, 0, currentRadius);
            rlEnd();
            break;
        }
        case GROUND_LAVA:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            float scalePulse = 1.0f + sinf(time * 5.0f) * 0.05f;
            float rPulse = currentRadius * scalePulse;
            rlSetTexture(tex.id);
            BeginBlendMode(BLEND_ADDITIVE);
            rlBegin(RL_QUADS);
            rlColor4ub(255, 60, 0, (unsigned char)(220 * alpha));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-rPulse, 0, -rPulse);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(rPulse, 0, -rPulse);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(rPulse, 0, rPulse);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-rPulse, 0, rPulse);
            rlEnd();
            break;
        }
        case GROUND_FROST:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
            rlSetTexture(tex.id);
            BeginBlendMode(BLEND_ADDITIVE);
            rlBegin(RL_QUADS);
            rlColor4ub(160, 230, 255, (unsigned char)(180 * alpha));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(currentRadius, 0, currentRadius);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-currentRadius, 0, currentRadius);
            rlEnd();
            break;
        }
        case GROUND_THORNS:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
            rlSetTexture(tex.id);
            rlBegin(RL_QUADS);
            rlColor4ub(40, 100, 50, (unsigned char)(230 * alpha));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(currentRadius, 0, -currentRadius);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(currentRadius, 0, currentRadius);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-currentRadius, 0, currentRadius);
            rlEnd();
            break;
        }
        case GROUND_RUNE:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            rlRotatef(time * -25.0f, 0, 1, 0);
            rlSetTexture(tex.id);
            BeginBlendMode(BLEND_ADDITIVE);
            rlBegin(RL_QUADS);
            rlColor4ub(180, 80, 255, (unsigned char)(240 * alpha));
            rlTexCoord2f(0.0f, 0.0f); rlVertex3f(-currentRadius * 0.8f, 0, -currentRadius * 0.8f);
            rlTexCoord2f(1.0f, 0.0f); rlVertex3f(currentRadius * 0.8f, 0, -currentRadius * 0.8f);
            rlTexCoord2f(1.0f, 1.0f); rlVertex3f(currentRadius * 0.8f, 0, currentRadius * 0.8f);
            rlTexCoord2f(0.0f, 1.0f); rlVertex3f(-currentRadius * 0.8f, 0, currentRadius * 0.8f);
            rlEnd();
            break;
        }
    }

    rlSetTexture(0);
    rlPopMatrix();

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}
