#include "core/composition/common/vc_params.h"
#include <assert.h>
#include <string.h>
int main(void) {
  float mass=.004f;
  VFX_ParamDef p={.type=VFX_PARAM_FLOAT,.valPtr=&mass,.minFloat=.000001f,
    .maxFloat=1,.stepFloat=.0001f};
  char a[32],b[32];
  VFX_Param_FormatValue(&p,a,sizeof(a));
  assert(!strcmp(a,"0.004"));
  VFX_Param_CycleNext(&p); VFX_Param_FormatValue(&p,b,sizeof(b));
  assert(strcmp(a,b));
  mass=p.minFloat;VFX_Param_CyclePrev(&p);assert(mass==p.maxFloat);
  return 0;
}
