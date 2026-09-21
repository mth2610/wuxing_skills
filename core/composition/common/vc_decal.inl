// Elemental composition supplies its material. Decal material policy and
// semantic surface selection are generated data, not element branches here.
#include "core/decals/decal_material.h"

static void VC_ComposeDecalMaterial(Vector3 pos, VC_MaterialId matId, float scale,
                                    float severity01, float lifetimeScale,
                                    DecalMaterialId materialId)
{
    severity01 = severity01 < 0.0f ? 0.0f : (severity01 > 1.0f ? 1.0f : severity01);
    const VFX_ElementMaterial *material = VFX_Material(matId);
    const DecalMaterialDesc *decal = DecalMaterial_Get(materialId);
    const VFX_SurfaceProfile *surface = VFX_SurfaceRegistry_Get(decal->surface);
    if (surface == NULL || surface->body.id == 0 || scale <= 0.0f) return;
    Color tint = material ? material->body : WHITE;
    if (decal->tintPolicy == DECAL_TINT_SOFT && material != NULL)
        tint = material->soft;
    else if (decal->tintPolicy == DECAL_TINT_WHITE)
        tint = WHITE;
    tint.a = (unsigned char)(decal->alphaBase + decal->alphaSeverity * severity01);
    float radius = scale * (decal->radiusBase + decal->radiusSeverity * severity01);
    float life = surface->lifetimeSeconds *
                 (decal->lifetimeBase + decal->lifetimeSeverity * severity01) * lifetimeScale;
    float phase = Random01() * 6.2831853f +
                  (material ? material->body.b * 0.01f : 0.0f);
    VFXContrastProfileId contrastProfile = VFX_CONTRAST_ENERGY;
    if (materialId == DECAL_MATERIAL_SCORCH)
    {
        contrastProfile = VFX_CONTRAST_FIRE;
    }
    else if (materialId == DECAL_MATERIAL_FROST)
        contrastProfile = VFX_CONTRAST_MAGIC;
    DecalMaterialParams params = {
        .baseTint = material ? material->body : WHITE,
        .emissiveTint = material ? material->glow : WHITE,
        .emissiveThreshold = decal->emissiveThreshold,
        .emissiveIntensity = decal->emissiveIntensity,
        .priority = decal->priority,
        .maxDrawDistance = decal->maxDrawDistance,
        .contrastProfile = contrastProfile
    };
    DecalSystem_AddConformalMaterialEx(pos, Random01() * 360.0f, 0.0f,
                                       radius * 0.90f, radius, surface->body, life,
                                       tint, BLEND_ALPHA, decal->yOffset,
                                       VFX_GroundHeightFromMap, NULL,
                                       VFX_GroundSurfaceFromMap, phase,
                                       surface->fadeInSeconds, surface->fadeOutSeconds,
                                       surface->maxSlopeDegrees, &params);
}

void VFX_ComposeDecal(Vector3 pos, VC_MaterialId matId, float scale,
                      float severity01, float lifetimeScale)
{
    const VFX_ElementMaterial *material = VFX_Material(matId);
    VC_ComposeDecalMaterial(pos, matId, scale, severity01, lifetimeScale,
                            material ? material->decalMaterial : DECAL_MATERIAL_IMPACT);
}

void VFX_ComposeDecalVariant(Vector3 pos, VC_MaterialId matId, float scale,
                             float severity01, float lifetimeScale, VFX_DecalVariant variant)
{
    static const DecalMaterialId materials[] = {
        DECAL_MATERIAL_IMPACT, DECAL_MATERIAL_SCORCH, DECAL_MATERIAL_FROST
    };
    if (variant < 0 || variant >= VFX_DECAL_VARIANT_COUNT)
        variant = VFX_DECAL_VARIANT_IMPACT;
    VC_ComposeDecalMaterial(pos, matId, scale, severity01, lifetimeScale, materials[variant]);
}

const char *VFX_DecalVariant_Name(VFX_DecalVariant variant)
{
    static const char *names[] = {"IMPACT FRACTURE", "FIRE SCORCH", "FROST CRACK"};
    return variant >= 0 && variant < VFX_DECAL_VARIANT_COUNT ? names[variant] : "INVALID";
}
