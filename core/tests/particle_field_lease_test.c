#include "core/particles/gpu/particle_field_lease.h"
#include "core/particles/particle_field_reference.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { bad++; printf("FAIL: %s\n", #x); } } while (0)
int main(void)
{
    int bad = 0;
    int fields[20];
    GpuParticleFieldLease leases[16] = {{0}};
    for (int i = 0; i < 16; ++i)
        CHECK(GpuParticleFieldLease_Register(leases,16,&fields[i],1) == i);
    CHECK(GpuParticleFieldLease_InUse(leases,16,&fields[0]));
    CHECK(!GpuParticleFieldLease_InUse(leases,16,NULL));
    CHECK(!GpuParticleFieldLease_InUse(leases,16,&fields[19]));
    CHECK(GpuParticleFieldLease_Register(leases,16,&fields[16],1) == -1);
    CHECK(GpuParticleFieldLease_Register(leases,16,&fields[2],2) == 2);
    GpuParticleFieldLease_Advance(leases,16,1);
    CHECK(leases[2].field == &fields[2] && leases[2].remaining == 1);
    CHECK(!GpuParticleFieldLease_InUse(leases,16,&fields[0]));
    CHECK(GpuParticleFieldLease_InUse(leases,16,&fields[2]));
    CHECK(GpuParticleFieldLease_Register(leases,16,&fields[16],1) == 0);
    CHECK(GpuParticleFieldLease_Register(leases,16,&fields[17],0) == -1);
    CHECK(GpuParticleFieldLease_Register(leases,16,&fields[2],0) == 2);
    /* Primary and arrival share the full spawn lifetime; arrival lookups cannot
     * silently prolong a field forever. Compare the actual float life updates. */
    leases[0] = (GpuParticleFieldLease){0};
    float life = 0.7f;
    CHECK(GpuParticleFieldLease_Register(leases,16,&fields[18],life) == 0);
    for (int i = 0; i < 100; ++i) {
        life -= 0.016f;
        GpuParticleFieldLease_Advance(leases,16,0.016f);
        CHECK((life > 0) == (leases[0].field != NULL));
        if (life > 0) CHECK(GpuParticleFieldLease_Register(leases,16,&fields[18],life) == 0);
    }
    /* Repeated waves of new field pointers reuse slots without exhausting 16. */
    for (int wave = 0; wave < 100; ++wave) {
        GpuParticleFieldLease_Advance(leases,16,2);
        for (int i = 0; i < 16; ++i)
            CHECK(GpuParticleFieldLease_Register(leases,16,&fields[(i+wave)%20],1) >= 0);
    }
    CHECK(ParticleFieldReference_Matches(true,&fields[0],&fields[0],NULL));
    CHECK(ParticleFieldReference_Matches(true,&fields[1],&fields[0],&fields[1]));
    CHECK(!ParticleFieldReference_Matches(false,&fields[0],&fields[0],&fields[0]));
    CHECK(!ParticleFieldReference_Matches(true,NULL,NULL,NULL));
    CHECK(!ParticleFieldReference_Matches(true,&fields[2],&fields[0],&fields[1]));
    /* Execute production lease math above; additionally pin its usage in the
     * actual backend. GPU dt transport and lifetime rejection need runtime QA. */
    static char host[128*1024];
    FILE *file = fopen("core/particles/gpu/particle_gpu_backend.c","rb");
    CHECK(file != NULL);
    if (file) {
        size_t n = fread(host,1,sizeof(host)-1,file);
        host[n] = 0;
        fclose(file);
        CHECK(strstr(host,"GpuParticleFieldLease_Register(s_fieldLeases") != NULL);
        CHECK(strstr(host,"GpuParticleFieldLease_Advance(s_fieldLeases, MAX_GPU_FORCE_FIELDS, dt)") != NULL);
        CHECK(strstr(host,"(Vector3){0}, (Vector3){0}, cfg.lifetime)") != NULL);
        CHECK(strstr(host,"(Vector3){0}, (Vector3){0}, p->life_rem)") != NULL);
        CHECK(strstr(host,"memset(&packed[i], 0, sizeof(packed[i]))") != NULL);
    }
    printf("force-field stable lifetime leases: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
