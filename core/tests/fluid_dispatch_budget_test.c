/* Fluid simulation dispatch budget.
 *
 * Measured by the Vulkan perf_dispatch_count probe on Intel Iris 6000:
 * nine in-frame dispatches cost 4.458 ms. Water Orb is the force-field fixture
 * and owns one GPU emitter, so its simulation must remain one dispatch/frame.
 * Explicit High PBD intentionally pays one integration dispatch plus four
 * grid-build/constraint pairs (nine total); it is retained for cases that need
 * incompressibility, not used as the default artistic-fluid backend. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { bad++; printf("FAIL: %s\n",#x); } } while (0)

static char *ReadFile(const char *path)
{
    FILE *f=fopen(path,"rb");
    if (!f) return NULL;
    fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
    char *s=(char *)malloc((size_t)n+1);
    if (!s) { fclose(f); return NULL; }
    fread(s,1,(size_t)n,f); s[n]=0; fclose(f); return s;
}

static int Count(const char *text,const char *needle)
{
    int n=0; size_t len=strlen(needle);
    for (const char *p=text;(p=strstr(p,needle))!=NULL;p+=len) n++;
    return n;
}

int main(void)
{
    int bad=0;
    char *particles=ReadFile("core/particles/gpu/particle_gpu_backend.c");
    char *pbd=ReadFile("core/fluid/fluid_pbd_gpu.c");
    char *orb=ReadFile("core/composition/water/water_orb.inl");
    if (!particles || !pbd || !orb) bad++;
    else {
        /* One update dispatch at the generic GPU emitter backend. */
        CHECK(Count(particles,"rlComputeShaderDispatch(")==1);
        /* One Water Orb, one emitter: no split core/foam simulation streams. */
        CHECK(Count(orb,"ParticleManager_CreateEmitter(")==1);
        CHECK(strstr(orb,"desc.moduleFlags=PARTICLE_MODULE_FORCE_FIELD")!=NULL);

        /* High PBD: 1 integration + 4*(grid build + solve) = 9. The separate
         * clear site is stamp-wrap maintenance, not a normal per-frame pass. */
        CHECK(strstr(pbd,"GfxQuality_Get()>=GFX_HIGH?4:3")!=NULL);
        CHECK(strstr(pbd,"phase=3;")!=NULL);
        CHECK(strstr(pbd,"phase=2;")!=NULL);
        CHECK(Count(pbd,"rlComputeShaderDispatch(")==4);
    }
    free(particles); free(pbd); free(orb);
    printf("fluid dispatch budget: %s (force-field 1, High PBD 9)\n",
           bad?"FAIL":"PASS");
    return bad!=0;
}
