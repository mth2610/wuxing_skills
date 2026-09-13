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
    char *surface=ReadFile("core/fluid/fluid_surface.h");
    char *surfaceSource=ReadFile("core/fluid/fluid_surface.c");
    char *orb=ReadFile("core/composition/water/water_orb.inl");
    char *impact=ReadFile("core/fluid/fluid_impact.c");
    char *bench=ReadFile("core/composition/water/liquid_bench.inl");
    char *composer=ReadFile("core/composition/visual_composer.h");
    if (!surface || !surfaceSource || !orb || !impact || !bench || !composer) bad++;
    else {
        CHECK(strstr(surface,"FluidSurface_ProfileDesc(FluidMotionProfile profile)")!=NULL);
        CHECK(Count(surfaceSource,"case FLUID_MOTION_")>=4);
        CHECK(strstr(surfaceSource,"d.liquidClass=FLUID_LIQUID_EMISSIVE")!=NULL);
        CHECK(strstr(surfaceSource,"d.liquidClass=FLUID_LIQUID_CONDUCTOR")!=NULL);
        CHECK(strstr(composer,"VFX_FluidOrb_Spawn(Vector3 start, Vector3 target,")!=NULL);
        CHECK(strstr(orb,"WUXING_FLUID_ORB_PROFILE")!=NULL);
        CHECK(strstr(orb,"VFX_FluidOrb_Spawn(start,target,profile)")!=NULL);
        CHECK(strstr(orb,"FluidSurface_ProfileDesc(profile)")!=NULL);
        CHECK(strstr(orb,"FluidSurface_HintBody(orb->center,surfaceRadius)")!=NULL);
        CHECK(strstr(orb,"FluidSurface_BindMaterial(&orb->material)")!=NULL);
        CHECK(Count(orb,"FORCE_RECEIVER_PLANE")>=2);
        CHECK(strstr(orb,".strength=-orb->motion.splashField")!=NULL);
        CHECK(strstr(orb,".strength=orb->motion.gatherStrength,.radius=0.0f")!=NULL);
        CHECK(strstr(orb,"count=caps->computeShader")!=NULL);
        CHECK(strstr(orb,"384")!=NULL);
        CHECK(strstr(impact,"FluidSurface_ProfileDesc(event->motionProfile)")!=NULL);
        CHECK(strstr(impact,"d->material.body,d->material.soft")!=NULL);
        CHECK(strstr(bench,"FluidSurface_ProfileDesc(profile)")!=NULL);
        CHECK(strstr(bench,"FLUID_MOTION_WATER,FLUID_MOTION_POISON,FLUID_MOTION_MUD")!=NULL);
        CHECK(strstr(bench,"FLUID_MOTION_LAVA,FLUID_MOTION_LIQUID_METAL")!=NULL);
        CHECK(strstr(bench,"VFX_LiquidWater") == NULL);
    }
    free(surface); free(surfaceSource); free(orb); free(impact); free(bench); free(composer);
    printf("fluid orb profiles: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
