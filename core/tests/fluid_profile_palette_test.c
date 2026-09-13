/* Perceptual contract for the five canonical liquid profiles.
 *
 * These numbers are not a screenshot test. They pin the colour relationships
 * that make the materials readable under the same light: clear water is blue,
 * poison is yellow-green, mud is opaque earth, lava has a dark skin and warm
 * core, and liquid metal has a nearly neutral conductor F0. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { bad++; printf("FAIL line %d: %s\n",__LINE__,#x); } } while (0)

static char *ReadFile(const char *path)
{
    FILE *f=fopen(path,"rb"); long n; char *s;
    if (!f) return NULL;
    fseek(f,0,SEEK_END); n=ftell(f); rewind(f);
    s=(char *)malloc((size_t)n+1);
    if (!s) { fclose(f); return NULL; }
    fread(s,1,(size_t)n,f); s[n]=0; fclose(f); return s;
}

static float ScatterGain(float opacityPerMetre)
{
    float mediumOpacity=1.0f-expf(-opacityPerMetre*0.20f);
    return 0.24f+(1.0f-0.24f)*mediumOpacity;
}

static float ReferenceTransmissionChroma(float r,float g,float b)
{
    float peak=fmaxf(r,fmaxf(g,b));
    float extinction=expf(-0.30f*0.20f);
    r=(r/peak)*extinction; g=(g/peak)*extinction; b=(b/peak)*extinction;
    return fmaxf(r,fmaxf(g,b))-fminf(r,fminf(g,b));
}

int main(void)
{
    int bad=0;
    char *host=ReadFile("core/fluid/fluid_surface.c");
    char *shader=ReadFile("core/fluid/shaders/fluid_surface.fs");
    char *ring=ReadFile("core/composition/water/water_ring.inl");
    if (!host || !shader || !ring) bad++;
    else {
        /* Exact anchors keep this behavioural mirror attached to production. */
        CHECK(strstr(host,"return FluidSurface_DielectricDesc(m->body,m->glow,m->soft);")!=NULL);
        CHECK(strstr(host,"(Color){185,220,235,255}")==NULL);
        CHECK(strstr(host,"(Color){150,215,55,255}")!=NULL);
        CHECK(strstr(host,"(Color){96,63,38,255}")!=NULL);
        CHECK(strstr(host,"(Color){105,20,6,255}")!=NULL);
        CHECK(strstr(host,"(Color){224,220,212,255}")!=NULL);
        CHECK(strstr(shader,"mix(0.24, 1.0, mediumOpacity)")!=NULL);
        CHECK(strstr(shader,"backdropReflectionBoost")==NULL);
        CHECK(strstr(shader,"backdropTransmission")==NULL);
        CHECK(strstr(ring,"FluidSurface_ProfileDesc(FLUID_MOTION_WATER)")!=NULL);
        CHECK(strstr(ring,"FluidSurface_BindMaterial(&material)")!=NULL);

        /* WATER RING's established preset is selective enough to retain a blue
         * silhouette over green. The rejected near-white profile transmits all
         * channels similarly, so grass remains grass and the water vanishes. */
        CHECK(ReferenceTransmissionChroma(41.0f,128.0f,185.0f)>0.70f);
        CHECK(ReferenceTransmissionChroma(185.0f,220.0f,235.0f)<0.22f);

        /* Clear liquids must not receive the same milky in-scatter as mud. */
        CHECK(fabsf(ScatterGain(0.0f)-0.24f)<0.0001f);
        CHECK(ScatterGain(1.2f)>0.38f && ScatterGain(1.2f)<0.42f);
        CHECK(ScatterGain(32.0f)>0.99f);
    }
    free(host); free(shader); free(ring);
    printf("fluid profile palette: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
