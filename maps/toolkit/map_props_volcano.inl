#ifndef MAP_TOOLKIT_MAP_PROPS_VOLCANO_INL
#define MAP_TOOLKIT_MAP_PROPS_VOLCANO_INL

// Geothermal Fumarole & Magma Vent System (#include'd in map_props.c)

MapVolcanoVentSystem MapProp_CreateVolcanoVentSystem(const MapVolcanoVent *vents, int count)
{
    MapVolcanoVentSystem sys = {0};
    if (!vents || count <= 0)
        return sys;
    if (count > 16)
        count = 16;

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
