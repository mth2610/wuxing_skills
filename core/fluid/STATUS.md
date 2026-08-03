# Fluid System Status — 2026-08-03

## Status Endpoint: SSF & Post-SSF Quality Upgrade Completed

The Screen Space Fluid (SSF) rendering and post-processing pipeline has been fully upgraded to deliver realistic, physically-inspired water surfaces while preserving high GPU performance.

## Upgrades Implemented

1. **Airborne Contours & Particle Seams Resolved**:
   - **Smooth Impostor Profile (`fluid_capture_particle.fs`)**: Rounded depth profile near particle edges removes sharp depth steps between overlapping splats.
   - **Range-Weighted Bilateral Filter (`fluid_depth_narrow_range.fs`)**: Upgraded to Gaussian range-weighting ($W_r = \exp(-0.5 (\Delta Z / \sigma_r)^2)$). Samples belonging to continuous liquid sheets contribute smoothly while background/distant sheet samples fall off exponentially to 0. Eliminates dark circular particle contours without blurring outer silhouettes.

2. **Minimum Gradient Normal Reconstruction (`fluid_surface.fs`)**:
   - Compares forward and backward view-space depth derivatives to pick minimum gradient pairs. Prevents normal spikes and edge bleeding at splat boundaries.

3. **Multi-Octave Dynamic Micro-Waves (`fluid_surface.fs`)**:
   - Dual scrolling procedural wave octaves + high-frequency capillary ripples perturb surface normals to create dynamic liquid shimmer and realistic specular glints under light sources.

4. **Screen-Space Reflection (SSR - Real-Time Reflection)**:
   - Raymarches screen-space depth buffer (10 steps + binary refinement) on High/Ultra tier (`u_qualityTier >= 2`). Reflects real-time 3D scene geometry, terrain, and objects onto the water surface.

5. **Screen-Space Underwater Caustics**:
   - Projects animated caustic light patterns onto underwater terrain based on view-space scene depth $D_{water}$, light direction, and water clarity.

6. **Physically-Based Beer-Lambert Volumetric Absorption**:
   - Combines additive thickness proxy with real view-space water column depth $D_{water} = Z_{scene} - Z_{water}$. Shallow shoreline water is crystal clear; deep water attenuates red/green channels into deep cyan/blue.

7. **Shoreline Wetness & Edge Softening Foam**:
   - Smooth depth-gap edge blending removes harsh clip lines where water meets opaque scene geometry.

## Verification

- Built target `wuxing` cleanly on macOS Vulkan backend (`[100%] Built target wuxing`).
- All shaders (`fluid_capture_particle.fs`, `fluid_depth_narrow_range.fs`, `fluid_surface.fs`) compile and execute cleanly on GPU.

