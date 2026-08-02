# Fluid Impact MVP

## Goal

Render water impacts consistently on compute-capable and legacy devices. Gameplay
supplies the authoritative collision; VFX never performs gameplay collision.

## Public contract

`FluidImpact_SpawnWater(const FluidImpactEvent *)` is a one-shot call from a
collision/state transition. `hitPoint` and `hitNormal` identify the receiver;
`impulseDirection` controls ballistic bias; `force01` controls density and mark
radius; `scale` is metres. `bodyColor`, `glowColor` and `softColor` are the
caller-owned optical identity; VFX compositions forward these from
`VFX_Material(VC_MAT_WATER)`. All-zero RGB retains the water preset fallback.
A zero normal becomes world up and a zero impulse becomes the normal.

## Pipeline

1. Build a tangent/bitangent frame from the supplied normal. The GPU PBD seed
   is a compact volume/crown in that frame, rather than a world-XZ disc.
2. Create one pooled residue mark for 3.5 seconds. Horizontal receivers use the
   existing radial flow decal; walls and ceilings use an oriented surface quad.
3. Spawn a fixed pool of CPU-authoritative **hero droplets**. Each integrates
   gravity, drag and up to two bounced contacts through a swept collision query.
   It draws as a small alpha sphere, so its contact response is identical on
   compute and non-compute machines.
4. On compute-capable hardware, simulate the crown in a 2,048-particle GPU PBD
   pool. Collision is against the submitted receiver plane (`hitPoint`,
   `hitNormal`): it reflects only incoming normal velocity and damps tangent
   velocity for settlement. The incoming `initialVelocity` retains its tangent
   component, so oblique shots retain their travel direction. Legacy hardware
   continues to use the bounded hero/background fallback.
5. Every hero-droplet contact emits three micro droplets and, under a global
   two-marks-per-frame cap, a smaller wet residue.

## Screen-space optics

### Render-graph submission rule

`FluidImpact_Draw()` is a **surface-input submission**, not a decal draw. The
engine calls it immediately before `FluidSurface_HasPending()` in the
screen-space VFX composite pass. Never put it behind an active-decal, regular
particle, trail, or debug visibility gate: airborne SSF effects (including
`FluidWaterOrb`) may be the only producer in that frame.

Particles are never shaded individually while they belong to one connected
mass. They first rasterize nearest spherical front-depth into R32F and additive
thickness into a separate R32F target. A separable narrow-range filter
(Truong/Yuksel 2018) reconstructs the shared surface: HIGH uses two rounds,
MED/LOW one. It rejects disconnected foreground sheets, clamps deeper samples
at the local kernel range and preserves detached spray. Normals and all lighting
are evaluated only after that reconstruction. R32F is mandatory: the old RGBA8
intermediate quantized non-linear device depth into horizontal, zoom-dependent
contour bands.

The SSF composite reconstructs view-space position and normals from that shared
depth using the same projection as `MyBeginMode3D`. Additive particle thickness
is converted back to metres and bounded by the raw scene depth behind the water
before applying Beer-Lambert absorption. Absorption and scattering derive from
the submitted material colours rather than a hard-coded water hue. Environment
lighting scales scattering energy without multiplying its RGB identity; real
light colour remains on reflection/specular.

Refraction follows Snell's law (`IOR = 1.333`) and samples the current HDR scene
colour. The refracted UV is rejected when raw scene depth says it crossed onto
geometry in front of the fluid, preventing foreground-edge colour bleeding.
HIGH uses depth-scaled four-tap scattering blur plus small spectral dispersion;
MED uses three colour samples; LOW uses one.

Water specular is a dielectric GGX response plus a broad micro-ripple lobe. Both
use the real Environment sun direction/colour. MED/HIGH also consume up to two
or four active VFX point lights respectively, transformed into the captured
camera's view space; LOW pays no point-light loop. Unresolved particle
silhouettes attenuate grazing Fresnel so individual splats do not regain white
halos.

The PBD population is polydisperse (stable physical and optical radius
variation), which prevents equal-radius Jacobi constraints from crystallizing
the resting sheet into rows. Per-particle settle age is staggered; fully resting
particles no longer receive Jacobi corrections, and the global solve stops once
all viscous tails have ended. This preserves the irregular impact footprint and
removes late-life solver cost.

## Budgets

`force01=0` to `1`: hero pool emits 3–14 collision droplets; the background
emits 4–32 backend-scaled droplets. `FLUID_IMPACT_MAX_HERO_DROPLETS = 48`; a
full pool recycles its oldest round-robin slot. Every impact creates one primary
wet mark; secondary marks are capped at two per frame. The global decal pool
remains 64 slots and retains oldest-slot eviction. No allocation, GPU readback,
GDF, depth collision, or render-texture baking is used.

## Collision providers

`FluidImpact_SetCollisionQuery()` accepts a swept-droplet callback from the
physics/world owner. It must return the contact position and normal for walls,
ceilings, props and moving receivers. With no callback installed, Core uses the
active map's ground-surface query: terrain and heightmaps collide correctly, but
walls/props do not. This makes the current fallback explicit rather than silently
pretending the map provides full 3D collision.

## SSF-only water orb

`FluidWaterOrb_Spawn(const FluidWaterOrbEvent *)` is the authored projectile
path for a coherent water sphere that must not use PBD. It launches one
2,000-particle SSF stream, held together by a moving point-attraction field. At
the target, those same particles receive a centrifugal `FORCE_RADIAL_AXIS`
field around `hitNormal` plus an angle-aware velocity impulse (preserved
tangent + damped normal rebound), vortex and viscosity fields to make the
crown; no replacement impact population is spawned. One orb therefore costs
one emitter and one depth/thickness stream.


## Wetness

The MVP lays a multiplied darkening stamp plus an alpha sheen stamp. This is a
cross-material visual fallback. True per-pixel roughness/specular wetness still
requires each receiver material to expose a world/UV wetness mask; it cannot be
correctly imposed by a standalone decal over arbitrary existing shaders.

## Non-goals for this phase

- PBR wetness that edits surface roughness/specular.
- GPU depth/GDF collision and sub-emitter readback.
- Persistent UV/render-texture baking.

Those require receiver-material and renderer contracts, while this MVP works on
the current Vulkan, OpenGL 3.3, GLES fallback, and CPU/VBO particle paths.
