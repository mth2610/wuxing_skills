# Fluid Impact MVP

## Goal

Render water impacts consistently on compute-capable and legacy devices. Gameplay
supplies the authoritative collision; VFX never performs gameplay collision.

## Public contract

`FluidImpact_SpawnWater(const FluidImpactEvent *)` is a one-shot call from a
collision/state transition. `hitPoint` and `hitNormal` identify the receiver;
`impulseDirection` controls ballistic bias; `force01` controls density and mark
radius; `scale` is metres. A zero normal becomes world up and a zero impulse
becomes the normal.

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
