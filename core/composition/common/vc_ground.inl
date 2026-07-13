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

    unsigned char a255 = (unsigned char)(255 * alpha);
    switch (style)
    {
        case GROUND_CRACK_RADIAL:
        {
            // Nâu đất trung tính có chủ ý — vết nứt vật lý, không mang màu nguyên tố.
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
            VC_DrawGroundQuadXZ(tex, currentRadius, currentRadius, (Color){120, 100, 80, a255});
            break;
        }
        case GROUND_CRACK_LINE:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
            VC_DrawGroundQuadXZ(tex, currentRadius * 0.2f, currentRadius, (Color){100, 90, 80, a255});
            break;
        }
        case GROUND_MAGIC_CIRCLE:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            rlRotatef(time * 40.0f, 0, 1, 0);
            BeginBlendMode(BLEND_ADDITIVE);
            VC_DrawGroundQuadXZ(tex, currentRadius, currentRadius,
                                VC_WithAlpha(VFX_Material(VC_MAT_QI)->glow, (unsigned char)(200 * alpha)));
            break;
        }
        case GROUND_LAVA:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            float rPulse = currentRadius * VC_Breathe(time, 5.0f, 0.05f);
            BeginBlendMode(BLEND_ADDITIVE);
            VC_DrawGroundQuadXZ(tex, rPulse, rPulse,
                                VC_WithAlpha(VFX_Material(VC_MAT_FIRE)->glow, (unsigned char)(220 * alpha)));
            break;
        }
        case GROUND_FROST:
        {
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_crack.png");
            BeginBlendMode(BLEND_ADDITIVE);
            VC_DrawGroundQuadXZ(tex, currentRadius, currentRadius,
                                VC_WithAlpha(VFX_Material(VC_MAT_ICE)->glow, (unsigned char)(180 * alpha)));
            break;
        }
        case GROUND_THORNS:
        {
            // Xanh gai tối có chủ ý — tối hơn hẳn WOOD.body để đọc thành gai/rễ già.
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
            VC_DrawGroundQuadXZ(tex, currentRadius, currentRadius, (Color){40, 100, 50, (unsigned char)(230 * alpha)});
            break;
        }
        case GROUND_RUNE:
        {
            // Tím arcane sáng có chủ ý — rực hơn VOID.glow để rune nổi trên nền tối.
            Texture2D tex = ResourceManager_LoadTexture("assets/textures/decals/decal_burn.png");
            rlRotatef(time * -25.0f, 0, 1, 0);
            BeginBlendMode(BLEND_ADDITIVE);
            VC_DrawGroundQuadXZ(tex, currentRadius * 0.8f, currentRadius * 0.8f,
                                (Color){180, 80, 255, (unsigned char)(240 * alpha)});
            break;
        }
    }

    rlPopMatrix();

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}
