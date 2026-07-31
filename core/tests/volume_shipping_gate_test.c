// P3: tubes ship only for energy/magic; smoke and fire have P2 emitters.
#include <stdio.h>
#include <string.h>
static int Has(const char *p, const char *n) { FILE *f=fopen(p,"rb"); char b[65536]; size_t z; if(!f)return 0; z=fread(b,1,sizeof(b)-1,f); fclose(f); b[z]=0; return strstr(b,n)!=0; }
int main(void) {
    int ok = Has("core/composition/common/vc_volume_trail.inl", "if (kind != VOL_ENERGY)") &&
             Has("core/composition/common/vc_volume_trail.inl", "use P2 SmokeEmitter/FlameEmitter") &&
             Has("core/composition/common/vc_projectile.inl", "VOL_ENERGY");
    printf("%s: shipping tubes are energy-only\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
