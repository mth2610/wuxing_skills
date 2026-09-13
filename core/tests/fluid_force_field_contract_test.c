/* C/GLSL wiring guard for the particle receiver force layer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ReadFile(const char *path)
{
    FILE *f=fopen(path,"rb"); long size; char *text;
    if (!f) return NULL;
    fseek(f,0,SEEK_END); size=ftell(f); rewind(f);
    text=(char *)malloc((size_t)size+1);
    if (!text) { fclose(f); return NULL; }
    fread(text,1,(size_t)size,f); text[size]=0; fclose(f); return text;
}
int main(void)
{
    int bad=0;
    char *header=ReadFile("core/force_field.h");
    char *host=ReadFile("core/force_field.c");
    char *shader=ReadFile("core/particles/shaders/gpu/particle_gpu.comp");
    char *backend=ReadFile("core/particles/gpu/particle_gpu_backend.c");
    char *particles=ReadFile("core/particles/particle_system.h");
    char *impact=ReadFile("core/fluid/fluid_impact.c");
    if (!header || !host || !shader || !backend || !particles || !impact) bad++;
    else {
        char *vector=strstr(header,"FORCE_VECTOR_TEXTURE");
        char *receiver=strstr(header,"FORCE_RECEIVER_PLANE");
        bad += !vector || !receiver || receiver<vector;
        bad += strstr(shader,"#define FT_RECEIVER_PLANE 11")==NULL;
        bad += strstr(shader,"if (int(layer.params0.w + 0.5) != FT_RECEIVER_PLANE)")==NULL;
        bad += strstr(host,"L->type != FORCE_RECEIVER_PLANE")==NULL;
        bad += strstr(host,"ForceField_ResolveParticleContacts")==NULL;
        bad += strstr(particles,"#define MAX_GPU_FORCE_FIELDS 16")==NULL;
        bad += strstr(impact,"ForceField coreField;")==NULL;
        bad += strstr(impact,"coreParticle?&body->coreField:&body->field")==NULL;
    }
    free(header); free(host); free(shader); free(backend); free(particles); free(impact);
    printf("fluid force-field contract: %s\n",bad?"FAIL":"PASS");
    return bad!=0;
}
