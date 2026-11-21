#ifndef MAP_TOOLKIT_MAP_PROPS_VOLCANO_INL
#define MAP_TOOLKIT_MAP_PROPS_VOLCANO_INL

// Geothermal Fumarole & Magma Vent System (#include'd in map_props.c)

static Texture2D s_volcanoPlumeTex = {0};

MapVolcanoVentSystem MapProp_CreateVolcanoVentSystem(const MapVolcanoVent *vents, int count)
{
    MapVolcanoVentSystem sys = {0};
    if (!vents || count <= 0)
        return sys;
    if (count > 16)
        count = 16;

    if (s_volcanoPlumeTex.id == 0 && FileExists("assets/textures/smoke_puff_01.png"))
        s_volcanoPlumeTex = ResourceManager_LoadTexture("assets/textures/smoke_puff_01.png");

    sys.count = count;
    for (int i = 0; i < count; i++)
    {
        sys.vents[i] = vents[i];
        sys.fogVolumeIds[i] = -1;

        // Register localized fire burn zones around vents so nearby grass chars & smolders
        if (i < 8)
            MapProp_AddGrassBurnZone(vents[i].position, vents[i].radius * 1.15f, 0.92f);

        // Spawn gentle drifting sulfuric steam / ash plume at each vent
        LocalFogVolume plume = {
            .shape = FOG_SHAPE_CYLINDER,
            .position = {vents[i].position.x, vents[i].position.y + 0.85f, vents[i].position.z},
            .extents = {vents[i].radius * 0.85f, 1.35f, vents[i].radius * 0.85f},
            .color = {215, 198, 178, 255},
            .density = vents[i].steamDensity > 0.0f ? vents[i].steamDensity : 0.14f,
            .edgeSoftness = 0.92f,
            .driftVelocity = {0.28f, 0.35f, 0.18f},
            .active = true,
        };
        sys.fogVolumeIds[i] = FogVolume_Create(&plume);
    }
    sys.ready = true;
    return sys;
}

void MapProp_UpdateVolcanoVentSystem(MapVolcanoVentSystem *sys, Vector3 cameraPos, float dt)
{
    if (!sys || !sys->ready || sys->count <= 0)
        return;
    sys->time += dt;

    // Pick up to 3 nearest vents within 65m to emit pulsing magma VFXLights
    int bestIdx[3] = {-1, -1, -1};
    float bestDistSq[3] = {65.0f * 65.0f, 65.0f * 65.0f, 65.0f * 65.0f};

    for (int i = 0; i < sys->count; i++)
    {
        float dx = sys->vents[i].position.x - cameraPos.x;
        float dz = sys->vents[i].position.z - cameraPos.z;
        float d2 = dx * dx + dz * dz;
        for (int k = 0; k < 3; k++)
        {
            if (d2 < bestDistSq[k])
            {
                for (int s = 2; s > k; s--)
                {
                    bestDistSq[s] = bestDistSq[s - 1];
                    bestIdx[s] = bestIdx[s - 1];
                }
                bestDistSq[k] = d2;
                bestIdx[k] = i;
                break;
            }
        }
    }

    for (int k = 0; k < 3; k++)
    {
        int idx = bestIdx[k];
        if (idx < 0)
            continue;
        const MapVolcanoVent *v = &sys->vents[idx];
        float pulse = 0.78f + 0.22f * sinf(sys->time * 2.4f + v->phase);
        float radius = v->radius * 2.2f * pulse;
        unsigned char g = (unsigned char)(85 + (int)(40.0f * pulse));
        Vector3 lightPos = {v->position.x, v->position.y + 0.75f, v->position.z};
        VFXLight_Spawn(lightPos, (Color){255, g, 24, 255}, radius, 0.08f, VFX_PRIORITY_LOW);
    }
}

void MapProp_DrawVolcanoPlumes(const MapVolcanoVentSystem *sys, Camera3D cam)
{
    if (!sys || !sys->ready || sys->count <= 0 || s_volcanoPlumeTex.id == 0)
        return;

    Rectangle src = {0.0f, 0.0f, (float)s_volcanoPlumeTex.width, (float)s_volcanoPlumeTex.height};
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    for (int i = 0; i < sys->count; i++)
    {
        const MapVolcanoVent *v = &sys->vents[i];
        float dx = v->position.x - cam.position.x;
        float dz = v->position.z - cam.position.z;
        if (dx * dx + dz * dz > 180.0f * 180.0f)
            continue;

        // 4 soft rising pyroclastic ash/steam puffs per active vent
        for (int p = 0; p < 4; p++)
        {
            float life = fmodf(sys->time * 0.12f + v->phase * 0.15915f + (float)p * 0.25f, 1.0f);
            float rise = life * (6.5f + v->radius * 0.75f);
            float driftX = sinf(sys->time * 0.4f + (float)p * 1.7f + v->phase) * (0.65f + life * 1.6f) + life * 2.2f;
            float driftZ = cosf(sys->time * 0.35f + (float)p * 2.1f) * (0.50f + life * 1.2f) + life * 1.1f;
            Vector3 pos = {v->position.x + driftX, v->position.y + 1.15f + rise, v->position.z + driftZ};
            float size = (3.2f + v->radius * 0.45f) * (0.75f + life * 1.55f);

            // Soft feathered alpha so puffs blend into volcanic haze without sharp edges
            float alphaF = sinf(life * 3.14159265f);
            unsigned char alpha = (unsigned char)fminf(255.0f, fmaxf(0.0f, alphaF * 38.0f));
            // Subtle magma under-glow near vent mouth fading to dark smoky slate aloft
            float heatFade = fmaxf(0.0f, 1.0f - life * 2.6f);
            unsigned char r = (unsigned char)(68.0f + 135.0f * heatFade);
            unsigned char g = (unsigned char)(72.0f + 42.0f * heatFade);
            unsigned char b = (unsigned char)(86.0f - 28.0f * heatFade);
            DrawBillboardRec(cam, s_volcanoPlumeTex, src, pos, (Vector2){size, size}, (Color){r, g, b, alpha});
        }
    }
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
}

void MapProp_UnloadVolcanoVentSystem(MapVolcanoVentSystem *sys)
{
    if (!sys || !sys->ready)
        return;
    for (int i = 0; i < sys->count; i++)
    {
        if (sys->fogVolumeIds[i] >= 0)
        {
            FogVolume_Destroy(sys->fogVolumeIds[i]);
            sys->fogVolumeIds[i] = -1;
        }
    }
    sys->count = 0;
    sys->ready = false;
}

#endif // MAP_TOOLKIT_MAP_PROPS_VOLCANO_INL
