#include "core/emitter/emitter_gpu_lifetime.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    EmissionGpuLifetime s={0};
    assert(!EmissionGpuLifetime_Advance(&s,.1f));
    EmissionGpuLifetime_Bind(&s,.5f);
    for(int i=0;i<100;i++) assert(EmissionGpuLifetime_Advance(&s,.1f));
    EmissionGpuLifetime_Retire(&s);
    for(int i=0;i<4;i++) assert(EmissionGpuLifetime_Advance(&s,.1f));
    assert(s.remaining>.099f && s.remaining<.101f);
    /* Rebinding while older children drain cannot shorten their bound. */
    EmissionGpuLifetime_Bind(&s,.05f);
    assert(s.remaining>=.49f);
    EmissionGpuLifetime_Retire(&s);
    for(int i=0;i<6;i++) EmissionGpuLifetime_Advance(&s,.1f);
    assert(!EmissionGpuLifetime_Advance(&s,.1f));
    assert(s.maxLife==0);
    EmissionGpuLifetime_Bind(&s,.05f);
    assert(s.remaining>.049f && s.remaining<.051f);
    EmissionGpuLifetime_Retire(&s);EmissionGpuLifetime_Retire(&s);
    assert(!EmissionGpuLifetime_Advance(&s,.1f));
    puts("PASS: GPU child completion drains after last parent and resets on reactivation");
    return 0;
}
