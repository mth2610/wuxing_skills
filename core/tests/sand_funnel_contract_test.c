#include <stdio.h>
#include <string.h>

static int Has(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    static char text[260000];
    size_t count;
    if (!file) return 0;
    count = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[count] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    int bad = 0;
    const char *vc  = "core/composition/earth/vc_sand_funnel.inl";
    const char *api = "core/composition/visual_composer.h";
    const char *fvs = "core/shaders/sand_funnel.vs";
    const char *ffs = "core/shaders/sand_funnel.fs";
    const char *svs = "core/shaders/sand_skirt.vs";
    const char *sfs = "core/shaders/sand_skirt.fs";

    /* Public API and wiring */
    bad += !Has(api, "VFX_ComposeSandFunnel");
    bad += !Has(api, "VFX_SandFunnel_UpdateState");
    bad += !Has("core/composition/earth/earth.inl", "#include \"vc_sand_funnel.inl\"");
    bad += !Has("core/composition/visual_composer.c", "VC_SandFunnel_Update(dt)");
    bad += !Has("core/composition/visual_composer.c", "VC_SandFunnel_Draw3D(cam)");

    /* Bright-background ALPHA BODY contract + rlgl batching safety */
    bad += !Has(vc, "VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false)");
    bad += !Has(vc, "rlDrawRenderBatchActive()");
    bad += !Has(ffs, "VFX_ResolveBody(");
    bad += !Has(sfs, "VFX_ResolveBody(");

    /* Explicit world-space matrices (ENGINE_LANDMINES #9) & Android/GLES highp */
    bad += !Has(fvs, "precision highp float");
    bad += !Has(ffs, "precision highp float");
    bad += !Has(svs, "precision highp float");
    bad += !Has(sfs, "precision highp float");
    bad += !Has(fvs, "u_projection * (u_view * vec4(wp, 1.0))");
    bad += !Has(svs, "u_projection * (u_view * vec4(wp, 1.0))");

    /* Mali-safe noise (zero fract(sin(...))) */
    bad += Has(ffs, "fract(sin(");
    bad += Has(sfs, "fract(sin(");

    printf("sand funnel contract: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
