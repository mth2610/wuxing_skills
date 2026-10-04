/* Water Orb integration guard: profile optics + force-field receiver + CPU cap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ReadFile(const char *path)
{
    FILE *f=fopen(path,"rb"); long n; char *s;
    if (!f) return NULL;
    fseek(f,0,SEEK_END); n=ftell(f); rewind(f);
    s=(char *)malloc((size_t)n+1);
    if (!s) { fclose(f); return NULL; }
    fread(s,1,(size_t)n,f); s[n]=0; fclose(f); return s;
}

static int Count(const char *text, const char *needle)
{
    int count=0; size_t n=strlen(needle);
    for (const char *p=text; (p=strstr(p,needle))!=NULL; p+=n) count++;
    return count;
}

#define CHECK(x) do { if (!(x)) { bad++; printf("FAIL line %d: %s\n",__LINE__,#x); } } while (0)

int main(void)
{
    int bad=0;
    char *surface=ReadFile("core/liquid/liquid_surface.h");
    char *surfaceSource=ReadFile("core/liquid/liquid_surface.c");
    char *orb=ReadFile("core/composition/water/water_orb.inl");
    char *impact=ReadFile("core/liquid/liquid_impact.c");
    char *bench=ReadFile("core/composition/water/liquid_bench.inl");
    char *composer=ReadFile("core/composition/visual_composer.h");
    if (!surface || !surfaceSource || !orb || !impact || !bench || !composer) bad++;
    else {
        CHECK(strstr(surface,"LiquidSurface_ProfileDesc(LiquidMotionProfile profile)")!=NULL);
        CHECK(Count(surfaceSource,"case LIQUID_MOTION_")>=4);
        CHECK(strstr(surfaceSource,"d.liquidClass=LIQUID_CLASS_EMISSIVE")!=NULL);
        CHECK(strstr(surfaceSource,"d.liquidClass=LIQUID_CLASS_CONDUCTOR")!=NULL);
        CHECK(strstr(composer,"VFX_LiquidOrb_Spawn(Vector3 start, Vector3 target,")!=NULL);
        CHECK(strstr(orb,"WUXING_LIQUID_ORB_PROFILE")!=NULL);
        CHECK(strstr(orb,"VFX_LiquidOrb_Spawn(start,target,profile)")!=NULL);
        CHECK(strstr(orb,"LiquidSurface_ProfileDesc(profile)")!=NULL);
        CHECK(strstr(orb,"LiquidSurface_HintBody(orb->center,surfaceRadius)")!=NULL);
        CHECK(strstr(orb,"LiquidSurface_BindMaterial(&orb->material)")!=NULL);
        CHECK(strstr(orb,"LiquidBodyRecipe_BuildField")!=NULL);
        CHECK(strstr(orb,"LiquidBodyRecipe_CrownDuration")!=NULL);
        CHECK(strstr(orb,"LiquidBodyRecipe_VolumeOffset")!=NULL);
        CHECK(strstr(orb,"WaterOrb_SetPhaseField(orb,fieldPhase,dt)")!=NULL);
        CHECK(strstr(orb,"orb->phaseAge=fmaxf(orb->age-orb->travelTime,0.0f)")!=NULL);
        CHECK(strstr(orb,"count=caps->computeShader")!=NULL);
        CHECK(strstr(orb,"384")!=NULL);
        CHECK(strstr(impact,"LiquidSurface_ProfileDesc(event->motionProfile)")!=NULL);
        CHECK(strstr(impact,"d->material.body,d->material.soft")!=NULL);
        CHECK(strstr(bench,"LiquidSurface_ProfileDesc(profile)")!=NULL);
        CHECK(strstr(bench,"LIQUID_MOTION_WATER,LIQUID_MOTION_POISON,LIQUID_MOTION_MUD")!=NULL);
        CHECK(strstr(bench,"LIQUID_MOTION_LAVA,LIQUID_MOTION_LIQUID_METAL")!=NULL);
        CHECK(strstr(bench,"VFX_LiquidWater") == NULL);
    }
    free(surface); free(surfaceSource); free(orb); free(impact); free(bench); free(composer);
    printf("fluid orb profiles: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
