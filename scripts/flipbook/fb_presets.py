"""Per-effect physics for the flipbook pipeline.

ADDING A NEW FLIPBOOK SHOULD MEAN ADDING AN ENTRY HERE, not writing a script.
That is the whole reason the pipeline was split into bake / render / pack: the
physics of an explosion differs from a flame, but the ray-marcher and the packer
do not care which one produced the grids.

Fields
    domain_scale     (x, y, z) half-extents. The CELL is square, so a tall
                     domain is what makes a tall silhouette possible at all —
                     a cubic domain cannot exceed height/width ~1, which is
                     exactly how E4's first fire sheet failed (measured 1.00).
    fuel_radius/z    the inflow blob
    flow_type        'FIRE' (fuel + flame + soot) or 'SMOKE'
    buoyancy         domain beta — lift per unit temperature
    alpha            domain alpha — density's own weight (negative = falls)
    vorticity        swirl preservation; higher = more curl retained
    burn             burning rate (FIRE only)
    flame_smoke      soot produced per unit of flame
    temperature      inflow temperature
    velocity_normal  inflow speed along the emitter's normal
    fuel_cutoff      fraction of the sheet after which fuel stops, so the tail
                     is the effect DYING. A one-shot flipbook (ANIM_ONCE) needs
                     an ending, not a loop.
    dissolve         smoke dissolve speed, or None for conserved soot. Leaving
                     it None on a FIRE preset fills the domain within a dozen
                     frames and the silhouette becomes the box wall (measured:
                     66.8% coverage with a rectangular outline).
"""

PRESETS = {
    "fire": dict(
        # Wider than it looks like it needs to be: at 0.7 the soot reached the
        # side walls before it dissipated and the last frames of the sheet were
        # a filled RECTANGLE — the silhouette became the domain, not the smoke.
        # Height chosen from the MEASURED flame, not from intuition: at z=2.0 the
        # plume only ever filled the bottom third, so inside a square cell it
        # read as wide-and-flat (height/width 0.60). The domain has to be about
        # as tall as the fire actually gets.
        domain_scale=(1.0, 1.0, 1.6), fuel_radius=0.26, fuel_z=0.30,
        flow_type='FIRE', buoyancy=3.0, alpha=-0.35, vorticity=0.35,
        burn=0.9, flame_smoke=0.22, temperature=1.4, velocity_normal=1.4,
        fuel_cutoff=0.78, dissolve=14),

    # Slower, colder, wider — and no flame channel worth speaking of.
    "smoke": dict(
        domain_scale=(1.0, 1.0, 1.6), fuel_radius=0.30, fuel_z=0.25,
        flow_type='SMOKE', buoyancy=1.0, alpha=-0.2, vorticity=0.2,
        burn=0.75, flame_smoke=1.0, temperature=1.0, velocity_normal=1.0,
        fuel_cutoff=0.35, dissolve=None),

    # A blast: fuel for a couple of frames only, high initial velocity, and a
    # near-cubic domain because an explosion expands in every direction rather
    # than rising.
    "explosion": dict(
        domain_scale=(1.2, 1.2, 1.2), fuel_radius=0.35, fuel_z=0.6,
        flow_type='FIRE', buoyancy=2.0, alpha=-0.1, vorticity=0.6,
        burn=1.5, flame_smoke=0.6, temperature=1.8, velocity_normal=4.0,
        fuel_cutoff=0.06, dissolve=40),

    # Ground-hugging: almost no lift, wide and flat, for shockwave/dust sheets.
    "dust": dict(
        domain_scale=(1.6, 1.6, 0.7), fuel_radius=0.30, fuel_z=0.18,
        flow_type='SMOKE', buoyancy=0.25, alpha=-0.05, vorticity=0.45,
        burn=0.5, flame_smoke=1.0, temperature=0.6, velocity_normal=3.0,
        fuel_cutoff=0.10, dissolve=34),
}
