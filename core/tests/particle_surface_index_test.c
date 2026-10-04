#include "core/particles/gpu/particle_surface_index.h"
#include <stdio.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { bad++; printf("FAIL: %s\n", #x); } } while (0)

typedef struct Slot { float life, active, owner, mode; } Slot;
static int Has(const char *path, const char *needle)
{
    static char text[128*1024];
    FILE *file = fopen(path,"rb");
    if (!file) return 0;
    size_t n = fread(text,1,sizeof(text)-1,file);
    text[n] = 0;
    fclose(file);
    return strstr(text,needle) != NULL;
}
int main(void)
{
    int bad = 0;
    Slot slots[] = {{1,1,11,3}, {0,0,11,3}, {1,1,12,3}, {1,1,11,0}, {1,1,13,3}};
    GpuSurfaceRoute routes[] = {{11,0.25f}, {12,0.5f}};
    GpuSurfaceIndex out[5];
    int n = GpuSurfaceIndex_Build(slots,5,sizeof(Slot),offsetof(Slot,owner),
                                 offsetof(Slot,mode),routes,2,out,5);
    CHECK(n == 3); /* Includes a CPU-dead slot: GPU life remains authoritative. */
    CHECK(out[0].particleIndex == 0 && out[0].materialId == 0.25f);
    CHECK(out[1].particleIndex == 1 && out[1].materialId == 0.25f);
    CHECK(out[2].particleIndex == 2 && out[2].materialId == 0.5f);
    slots[1].owner = 14; /* A ring overwrite must remove the old emitter's slot. */
    CHECK(GpuSurfaceIndex_Build(slots,5,sizeof(Slot),offsetof(Slot,owner),
                               offsetof(Slot,mode),routes,2,out,5) == 2);
    CHECK(GpuSurfaceIndex_Build(slots,5,sizeof(Slot),offsetof(Slot,owner),
                               offsetof(Slot,mode),routes,2,out,1) == -1);
    routes[1] = (GpuSurfaceRoute){11,0.75f};
    CHECK(GpuSurfaceIndex_Build(slots,5,sizeof(Slot),offsetof(Slot,owner),
                               offsetof(Slot,mode),routes,2,out,5) == 1);
    CHECK(out[0].materialId == 0.25f);
    CHECK(sizeof(GpuSurfaceIndex) == 8 && offsetof(GpuSurfaceIndex,materialId) == 4);
    /* A five-body, 640-slot cloud submits 640 splats instead of 5*8192. */
    static Slot pool[8192];
    static GpuSurfaceIndex indices[8192];
    GpuSurfaceRoute bench[5];
    for (int r = 0; r < 5; ++r) {
        bench[r] = (GpuSurfaceRoute){r+1,(float)r/4};
        for (int i = 0; i < 128; ++i)
            pool[r*128+i] = (Slot){1,1,(float)(r+1),3};
    }
    CHECK(GpuSurfaceIndex_Build(pool,8192,sizeof(Slot),offsetof(Slot,owner),
                               offsetof(Slot,mode),bench,5,indices,8192) == 640);
    /* Behavioral helper tests do not observe GPU images/fences. Keep the C/VS
     * wiring pinned and verify reflection + visible captures separately. */
    /* CPU event spawns after dispatch must not relabel still-resident GPU slots.
     * Only the same routing read used at upload updates resident metadata. */
    GpuSurfaceRouting resident = GpuSurfaceRouting_Read(&slots[0],offsetof(Slot,owner),offsetof(Slot,mode));
    slots[0].owner = 14;
    CHECK(GpuSurfaceIndex_Build(&resident,1,sizeof(resident),offsetof(GpuSurfaceRouting,emitterId),
                               offsetof(GpuSurfaceRouting,renderMode),routes,2,out,5) == 1);
    resident = GpuSurfaceRouting_Read(&slots[0],offsetof(Slot,owner),offsetof(Slot,mode));
    CHECK(GpuSurfaceIndex_Build(&resident,1,sizeof(resident),offsetof(GpuSurfaceRouting,emitterId),
                               offsetof(GpuSurfaceRouting,renderMode),routes,2,out,5) == 0);
    CHECK(Has("core/particles/gpu/particle_gpu_backend.c","GpuSurfaceIndex_Build(metadata"));
    CHECK(Has("core/particles/gpu/particle_gpu_backend.c","s_surfaceResidentRouting[slot] = GpuSurfaceRouting_Read(&s_cpu_pool[slot]"));
    CHECK(Has("core/particles/gpu/particle_gpu_backend.c","rlBindShaderBuffer(s_surface_index_ssbo, 1)"));
    CHECK(Has("core/particles/shaders/gpu/liquid_surface_capture.vs","binding = 1"));
    CHECK(Has("core/particles/shaders/gpu/liquid_surface_capture.vs","particles[particleIndex].life_data"));
    CHECK(Has("core/particles/shaders/gpu/liquid_surface_capture.vs","v_materialId = surfaceIndices[gl_InstanceID].materialId"));
    printf("surface index routing: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
