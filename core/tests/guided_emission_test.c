/* Production scheduler arithmetic; emitter/field lifetime wiring is guarded
 * separately below. This test does not validate renderer or GPU submission. */
#include "core/composition/vc_emission.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(c, name) do { if (!(c)) { printf("FAIL: %s\n", name); failures++; } \
    else printf("PASS: %s\n", name); } while (0)
static int TotalAt(float dt) {
    VFX_EmissionSchedule s = {.durationSeconds=2, .ratePerSecond=160};
    int count=0;
    for (int i=0; i<1000 && !VFX_EmissionComplete(&s); i++)
        count += VFX_EmissionAdvance(&s, dt, 2048);
    return count;
}
int main(void) {
    CHECK(TotalAt(1.0f/30)==320 && TotalAt(1.0f/60)==320 &&
          TotalAt(1.0f/120)==320, "continuous emission preserves density across timesteps");
    VFX_EmissionSchedule s = {.durationSeconds=1, .ratePerSecond=100};
    CHECK(VFX_EmissionAdvance(&s, .75f, 2048)==75 &&
          VFX_EmissionAdvance(&s, .75f, 2048)==25,
          "last frame clips emission to its independent lifetime");
    CHECK(VFX_EmissionComplete(&s) && VFX_EmissionAdvance(&s, .5f, 2048)==0,
          "completed source emits no additional particles");
    s=(VFX_EmissionSchedule){.durationSeconds=1,.ratePerSecond=100};
    CHECK(VFX_EmissionAdvance(&s, 2, 32)==32 && !VFX_EmissionComplete(&s),
          "bounded emission retains long-frame overflow");
    int count=32;
    for(int i=0;i<4;i++) count+=VFX_EmissionAdvance(&s,.01f,32);
    CHECK(count==100 && VFX_EmissionComplete(&s), "overflow drains without losing density");
    s=(VFX_EmissionSchedule){.durationSeconds=1,.ratePerSecond=100};
    CHECK(VFX_EmissionAdvance(&s,NAN,2048)==0 && s.ageSeconds==0,
          "nonfinite timestep cannot corrupt emission state");
    FILE *f=fopen("core/composition/common/vc_guided_particle.inl","rb");
    char text[24000]={0};
    if(f) { fread(text,1,sizeof(text)-1,f); fclose(f); }
    CHECK(strstr(text,"VFX_EmissionAdvance(&s->emission") &&
          !strstr(text,"MotionFields_IsAlive(s->guide)"),
          "guide expiration cannot stop an independently scheduled source");
    return failures ? 1 : 0;
}
