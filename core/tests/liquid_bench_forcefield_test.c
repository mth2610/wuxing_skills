/* LIQUID BENCH must exercise the shipping force-field fluid path, not the
 * legacy CPU sphere registration path.  This is intentionally a source-level
 * integration guard: the fixture is also our five-material visual/perf lab. */
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

static int Count(const char *text,const char *needle)
{
    int count=0; size_t n=strlen(needle);
    for (const char *p=text;(p=strstr(p,needle))!=NULL;p+=n) count++;
    return count;
}

int main(void)
{
    int bad=0;
    char *bench=ReadFile("core/composition/water/liquid_bench.inl");
    char *composer=ReadFile("core/composition/visual_composer.c");
    if (!bench || !composer) bad++;
    else {
        CHECK(strstr(bench,"#define LIQUID_BENCH_BODIES 5")!=NULL);
        CHECK(strstr(bench,"LIQUID_BENCH_GPU_HIGH_PER_BODY 128")!=NULL);
        CHECK(strstr(bench,"LIQUID_BENCH_CPU_PER_BODY 48")!=NULL);
        CHECK(strstr(bench,"LIQUID_BENCH_CPU_TOTAL 240")!=NULL);
        CHECK(strstr(bench,"FluidSurface_RegisterParticle")==NULL);
        CHECK(strstr(bench,"LiquidBench_Body")==NULL);
        CHECK(strstr(bench,"PARTICLE_RENDER_SURFACE_INPUT")!=NULL);
        CHECK(strstr(bench,"PARTICLE_MODULE_FORCE_FIELD")!=NULL);
        CHECK(Count(bench,"ParticleManager_CreateEmitter(")==1);
        CHECK(strstr(bench,"ParticleManager_EmitBatch")!=NULL);
        CHECK(strstr(bench,"FluidMotion_Get(profile)")!=NULL);
        CHECK(strstr(bench,"FluidSurface_ProfileDesc(profile)")!=NULL);
        CHECK(strstr(bench,"FORCE_RECEIVER_PLANE")!=NULL);
        CHECK(strstr(bench,"static void LiquidBench_Update(float dt)")!=NULL);
        CHECK(strstr(bench,"static void LiquidBench_SubmitSurface(void)")!=NULL);
        CHECK(strstr(composer,"LiquidBench_Update(dt);")!=NULL);
        CHECK(strstr(composer,"LiquidBench_SubmitSurface();")!=NULL);
    }
    free(bench); free(composer);
    printf("liquid bench force-field path: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
