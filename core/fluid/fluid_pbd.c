#include "core/fluid/fluid_pbd.h"
#include "core/gfx_quality.h"
#include "core/map_manager.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

#define PBD_SOLVER_ITERATIONS 5
#define PBD_GRAVITY 9.81f
#define PBD_CELL_SIZE 0.095f
#define PBD_GRID_X 64
#define PBD_GRID_Y 32
#define PBD_GRID_Z 64
#define PBD_GRID_CELLS (PBD_GRID_X*PBD_GRID_Y*PBD_GRID_Z)

typedef struct FluidPBDParticle {
    Vector3 position, previous, velocity;
    float radius, life;
    bool grounded, active;
} FluidPBDParticle;

static FluidPBDParticle s_particles[FLUID_PBD_MAX_PARTICLES];
static int s_nextParticle;
static bool s_initialized;
static int s_hashHead[PBD_GRID_CELLS];
static int s_hashNext[FLUID_PBD_MAX_PARTICLES];

static int PBD_CellIndex(Vector3 p, int *x, int *y, int *z)
{
    *x=Clamp((int)floorf(p.x/PBD_CELL_SIZE)+PBD_GRID_X/2,0,PBD_GRID_X-1);
    *y=Clamp((int)floorf(p.y/PBD_CELL_SIZE)+8,0,PBD_GRID_Y-1);
    *z=Clamp((int)floorf(p.z/PBD_CELL_SIZE)+PBD_GRID_Z/2,0,PBD_GRID_Z-1);
    return (*z*PBD_GRID_Y + *y)*PBD_GRID_X + *x;
}

static Vector3 PBD_SafeNormal(Vector3 v, Vector3 fallback)
{
    return Vector3LengthSqr(v) > 0.000001f ? Vector3Normalize(v) : fallback;
}

static void PBD_Basis(Vector3 normal, Vector3 *tangent, Vector3 *bitangent)
{
    Vector3 reference = fabsf(normal.y) < 0.95f ? (Vector3){0,1,0} : (Vector3){1,0,0};
    *tangent = Vector3Normalize(Vector3CrossProduct(reference, normal));
    *bitangent = Vector3Normalize(Vector3CrossProduct(normal, *tangent));
}

void FluidPBD_Init(void)
{
    if (s_initialized) return;
    memset(s_particles, 0, sizeof(s_particles));
    s_nextParticle = 0;
    s_initialized = true;
}

void FluidPBD_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale)
{
    FluidPBD_Init();
    normal = PBD_SafeNormal(normal, (Vector3){0,1,0});
    impulse = PBD_SafeNormal(impulse, normal);
    float f = Clamp(force01, 0.0f, 1.0f);
    /* CPU fallback: keep the two mesh capture passes within a 60 FPS budget.
     * Higher counts belong to the forthcoming GPU-instanced PBD backend. */
    int count = GfxQuality_Get() <= GFX_LOW ? 48 : (GfxQuality_Get() >= GFX_HIGH ? 160 : 96);
    count = (int)((float)count * (0.45f + 0.55f*f));
    float radius = scale * 0.042f;
    Vector3 tangent, bitangent;
    PBD_Basis(normal, &tangent, &bitangent);
    for (int i = 0; i < count; ++i) {
        float a = ((float)GetRandomValue(0, 359))*DEG2RAD;
        float ring = sqrtf((float)GetRandomValue(0, 1000)/1000.0f);
        float lift = (float)GetRandomValue(0, 1000)/1000.0f;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(a)*ring), Vector3Scale(bitangent, sinf(a)*ring));
        FluidPBDParticle *p = &s_particles[s_nextParticle++ % FLUID_PBD_MAX_PARTICLES];
        Vector3 start = Vector3Add(point, Vector3Add(Vector3Scale(radial, radius*3.5f), Vector3Scale(normal, radius*(0.8f + lift*2.0f))));
        float outward = scale*(0.8f + 1.8f*f);
        Vector3 velocity = Vector3Add(Vector3Scale(impulse, scale*(1.5f + 3.0f*f)),
                                      Vector3Add(Vector3Scale(radial, outward), Vector3Scale(normal, scale*(0.6f + lift))));
        *p = (FluidPBDParticle){ .position=start, .previous=start, .velocity=velocity,
                                 .radius=radius, .life=4.0f, .active=true };
    }
}

void FluidPBD_Update(float dt)
{
    if (!s_initialized || dt <= 0.0f) return;
    dt = fminf(dt, 1.0f/30.0f);
    for (int i=0;i<FLUID_PBD_MAX_PARTICLES;i++) {
        FluidPBDParticle *p=&s_particles[i]; if (!p->active) continue;
        p->life -= dt; if (p->life <= 0.0f) { p->active=false; continue; }
        p->previous=p->position;
        p->velocity.y -= PBD_GRAVITY*dt;
        p->velocity=Vector3Scale(p->velocity, p->grounded ? 0.985f : 0.997f);
        p->position=Vector3Add(p->position, Vector3Scale(p->velocity,dt));
        p->grounded=false;
    }
    for (int iteration=0;iteration<PBD_SOLVER_ITERATIONS;iteration++) {
        for (int bucket=0; bucket<PBD_GRID_CELLS; ++bucket) s_hashHead[bucket]=-1;
        for (int i=0;i<FLUID_PBD_MAX_PARTICLES;i++) {
            if (!s_particles[i].active) continue;
            int x,y,z; int bucket=PBD_CellIndex(s_particles[i].position,&x,&y,&z); s_hashNext[i]=s_hashHead[bucket]; s_hashHead[bucket]=i;
        }
        for (int i=0;i<FLUID_PBD_MAX_PARTICLES;i++) {
            FluidPBDParticle *a=&s_particles[i]; if (!a->active) continue;
            int cx,cy,cz; PBD_CellIndex(a->position,&cx,&cy,&cz);
            for (int oz=-1;oz<=1;oz++) for (int oy=-1;oy<=1;oy++) for (int ox=-1;ox<=1;ox++) {
                int nx=cx+ox, ny=cy+oy, nz=cz+oz;
                if (nx<0||ny<0||nz<0||nx>=PBD_GRID_X||ny>=PBD_GRID_Y||nz>=PBD_GRID_Z) continue;
                for (int j=s_hashHead[(nz*PBD_GRID_Y + ny)*PBD_GRID_X + nx]; j>=0; j=s_hashNext[j]) {
                if (j<=i) continue; FluidPBDParticle *b=&s_particles[j]; if (!b->active) continue;
                Vector3 delta=Vector3Subtract(b->position,a->position);
                float d2=Vector3LengthSqr(delta), rest=(a->radius+b->radius)*0.88f;
                if (d2 >= rest*rest) continue;
                float d=sqrtf(fmaxf(d2,0.0000001f));
                Vector3 n=Vector3Scale(delta,1.0f/d);
                float correction=(rest-d)*0.5f;
                a->position=Vector3Subtract(a->position,Vector3Scale(n,correction));
                b->position=Vector3Add(b->position,Vector3Scale(n,correction));
                }
            }
        }
        for (int i=0;i<FLUID_PBD_MAX_PARTICLES;i++) {
            FluidPBDParticle *p=&s_particles[i]; if (!p->active) continue;
            float ground=MapManager_GetGroundHeightAt(p->position.x,p->position.z);
            if (p->position.y-p->radius < ground) { p->position.y=ground+p->radius; p->grounded=true; }
        }
    }
    for (int i=0;i<FLUID_PBD_MAX_PARTICLES;i++) {
        FluidPBDParticle *a=&s_particles[i]; if (!a->active) continue;
        a->velocity=Vector3Scale(Vector3Subtract(a->position,a->previous),1.0f/dt);
        int cx,cy,cz; PBD_CellIndex(a->position,&cx,&cy,&cz);
        for (int oz=-1;oz<=1;oz++) for (int oy=-1;oy<=1;oy++) for (int ox=-1;ox<=1;ox++) { int nx=cx+ox,ny=cy+oy,nz=cz+oz; if(nx<0||ny<0||nz<0||nx>=PBD_GRID_X||ny>=PBD_GRID_Y||nz>=PBD_GRID_Z) continue; for (int j=s_hashHead[(nz*PBD_GRID_Y+ny)*PBD_GRID_X+nx]; j>=0; j=s_hashNext[j]) {
            if (j<=i) continue; FluidPBDParticle *b=&s_particles[j]; if (!b->active) continue;
            Vector3 delta=Vector3Subtract(b->position,a->position);
            float h=(a->radius+b->radius)*2.4f, d=Vector3Length(delta);
            if (d >= h) continue;
            float w=(1.0f-d/h)*0.07f;
            Vector3 avg=Vector3Scale(Vector3Add(a->velocity,b->velocity),0.5f);
            a->velocity=Vector3Lerp(a->velocity,avg,w);
            b->velocity=Vector3Lerp(b->velocity,avg,w);
        }}
        if (a->grounded) { a->velocity.y=0.0f; a->velocity.x*=0.82f; a->velocity.z*=0.82f; }
    }
}

int FluidPBD_GetRenderParticles(FluidPBDRenderParticle *outParticles, int maxParticles)
{
    int count=0; if (!outParticles || maxParticles<=0) return 0;
    for (int i=0;i<FLUID_PBD_MAX_PARTICLES && count<maxParticles;i++) {
        FluidPBDParticle *p=&s_particles[i]; if (!p->active) continue;
        Vector3 radii={p->radius,p->radius,p->radius};
        if (p->grounded) radii=(Vector3){p->radius*2.1f,p->radius*0.40f,p->radius*2.1f};
        outParticles[count++]=(FluidPBDRenderParticle){p->position,radii};
    }
    return count;
}
