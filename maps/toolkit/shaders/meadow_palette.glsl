// Habitat -> canopy colour. Counterpart of meadow_palette.h. Habitat comes from ecology A.
//
// ===== PALETTE DEFINITION: must stay IDENTICAL in both files =====
// Distant ground takes its colour from here while near blades use the header's baked values,
// so drift between the two shows up as a seam where blades fade into ground.
// maps/tests/test_meadow_palette_sync.py parses the MEADOW_PALETTE_* block below and the
// matching block in meadow_palette.h, and fails on any mismatch. Edit both in one change.
//
// Three bands are blended by two overlapping smoothsteps over habitat:
//   deep moss    -> lush clover : habitat 0.20 .. 0.60
//   lush clover  -> sunlit gold : habitat 0.55 .. 0.90
// (the second blend starts before the first ends, so 0.55 .. 0.60 mixes all three.)
//
// Colour-space note: values are returned unchanged. Whether the caller treats them as linear
// or display-referred is decided by the consuming shader, not here.
const vec3  MEADOW_PALETTE_DEEP_MOSS    = vec3(0.24, 0.40, 0.13);
const vec3  MEADOW_PALETTE_LUSH_CLOVER  = vec3(0.32, 0.50, 0.16);
const vec3  MEADOW_PALETTE_SUNLIT_GOLD  = vec3(0.44, 0.56, 0.22);
const float MEADOW_PALETTE_CLOVER_EDGE0 = 0.20;
const float MEADOW_PALETTE_CLOVER_EDGE1 = 0.60;
const float MEADOW_PALETTE_GOLD_EDGE0   = 0.55;
const float MEADOW_PALETTE_GOLD_EDGE1   = 0.90;

vec3 MeadowCanopyColor(float habitat)
{
    vec3 color = mix(MEADOW_PALETTE_DEEP_MOSS, MEADOW_PALETTE_LUSH_CLOVER,
                     smoothstep(MEADOW_PALETTE_CLOVER_EDGE0, MEADOW_PALETTE_CLOVER_EDGE1, habitat));
    return mix(color, MEADOW_PALETTE_SUNLIT_GOLD,
               smoothstep(MEADOW_PALETTE_GOLD_EDGE0, MEADOW_PALETTE_GOLD_EDGE1, habitat));
}
