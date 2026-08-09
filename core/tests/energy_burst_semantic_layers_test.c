/* Source contract: Energy Burst authors matter and radiance as populations. */
#include <stdio.h>
#include <string.h>

static int Has(const char *needle)
{
    FILE *file = fopen("core/composition/common/vc_energy_burst.inl", "rb");
    char buffer[1024];
    size_t used = 0, count;
    if (file == NULL) return 0;
    while ((count = fread(buffer + used, 1, sizeof(buffer) - 1 - used, file)) != 0) {
        used += count;
        buffer[used] = '\0';
        if (strstr(buffer, needle) != NULL) { fclose(file); return 1; }
        if (used > 256) { memmove(buffer, buffer + used - 256, 256); used = 256; }
    }
    fclose(file);
    return 0;
}

int main(void)
{
    int bad = 0;
    bad += !Has("ENERGY_BURST_FIELD_INSTANCES 6");
    bad += !Has("EnergyBurstField_Spawn(pos, matId, scale, ity);");
    bad += !Has("VFXLayeredAnnulus_DrawBody");
    bad += !Has("VFXLayeredAnnulus_DrawEmission");
    bad += !Has("WUXING_VFX_FIELD_DEBUG_LAYER");
    bad += !Has(".render.blendMode = VFX_BLEND_ALPHA");
    bad += !Has(".render.contrastProfile = VFX_CONTRAST_ENERGY");
    bad += Has("ParticleConfig transitionBody =");
    bad += Has("ParticleConfig coreRadiance =");
    bad += Has("Random01() <");
    puts(bad ? "energy burst semantic layers: FAIL"
             : "energy burst semantic layers: PASS");
    return bad;
}
