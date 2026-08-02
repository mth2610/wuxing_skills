#ifndef DECAL_MATERIAL_H
#define DECAL_MATERIAL_H

#include "core/vfx_surface_registry.h"
#include "core/decals/decal_material_types.h"

typedef enum {
    DECAL_TINT_BODY = 0,
    DECAL_TINT_SOFT,
    DECAL_TINT_WHITE
} DecalTintPolicy;

typedef struct {
    VFX_SurfaceId surface;
    DecalTintPolicy tintPolicy;
    float radiusBase;
    float radiusSeverity;
    float lifetimeBase;
    float lifetimeSeverity;
    float yOffset;
    float alphaBase;
    float alphaSeverity;
    float emissiveThreshold;
    float emissiveIntensity;
    int priority;
    float maxDrawDistance;
} DecalMaterialDesc;

static const DecalMaterialDesc s_DecalMaterials[DECAL_MATERIAL_COUNT] = {
#include "core/decals/decal_materials.generated.inl"
};

static inline const DecalMaterialDesc *DecalMaterial_Get(DecalMaterialId id)
{
    if (id < 0 || id >= DECAL_MATERIAL_COUNT)
        id = DECAL_MATERIAL_IMPACT;
    return &s_DecalMaterials[id];
}

#endif // DECAL_MATERIAL_H
