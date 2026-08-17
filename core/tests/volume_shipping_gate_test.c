// P3: tubes ship only for energy/magic; smoke and fire have P2 emitters.
#include <stdio.h>
#include <string.h>
static int Has(const char *p, const char *n) { FILE *f=fopen(p,"rb"); char b[65536]; size_t z; if(!f)return 0; z=fread(b,1,sizeof(b)-1,f); fclose(f); b[z]=0; return strstr(b,n)!=0; }
int main(void) {
    // The third clause used to assert that vc_projectile.inl requested an ENERGY tube.
    // VFX_ComposeProjectile was deleted on 17/08/2026 (measured worst of the three
    // largest in-band effects: it lost 79% of its body area on a bright background,
    // attenuated only 28.7% of its own footprint, and its internal structure collapsed
    // 10x — see BRIGHT_BACKGROUND_VFX_SPEC.md §11b). The invariant this file guards is
    // unchanged and still fully covered by the two clauses below, which are the gate
    // itself; there is deliberately no replacement clause pointing at another file.
    int ok = Has("core/composition/common/vc_volume_trail.inl", "if (kind != VOL_ENERGY)") &&
             Has("core/composition/common/vc_volume_trail.inl", "use P2 SmokeEmitter/FlameEmitter");
    printf("%s: shipping tubes are energy-only\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
