# Fluid System Status — 2026-08-01

## Session endpoint

Work is intentionally paused after restoring the state immediately before the
experimental zoom-adaptive depth-filter footprint. That experiment enlarged
the SSF smoothing stride according to projected particle radius; it did not
remove the airborne contours in the user's close view and made the liquid look
plastic again, so it has been fully reverted.

## Current working state

- GPU PBD remains the coherent high-quality simulation path with 2,048
  particles and direct SSBO rendering.
- Impact input accepts direction, velocity, contact point, and contact normal.
  The incoming body is compact; receiver collision creates the crown and spray.
- Surface reconstruction uses curved particle depth, additive thickness, two
  separable narrow-range smoothing rounds on High, and R32F scalar targets.
- Final composite uses material-driven colour, Beer–Lambert absorption,
  screen-space refraction, depth-aware refraction rejection, Fresnel,
  environment/light specular, and shared-surface foam.
- Scene-depth ordering rejects water behind opaque geometry and fades true
  intersections.
- Thin unresolved splats receive reduced Fresnel; airborne lower-hemisphere
  reflection receives scene/sky fill; airborne curvature foam is suppressed.

## Known unresolved visual defect

Airborne water still shows dark circular contours around and between visible
particle splats in a close camera view. The contours largely disappear after
the fluid settles and flattens against the receiver.

Latest user evidence:

- `/Users/mth2610/Desktop/Screen Shot 2026-08-01 at 23.21.16.png`

The latest image shows contours inside the coherent body as well as around its
outer silhouette. This means the remaining defect must not be treated as only
an opaque-scene intersection halo.

## Attempts that did not solve the defect

1. Original-UV scene-depth rejection and a narrow intersection fade fixed
   ordering logic but did not remove airborne particle contours.
2. Rejecting refracted samples that cross opaque depth layers prevented
   character/background disocclusion sampling but did not remove the contours.
3. Reducing thin-splat Fresnel, filling dark lower-hemisphere reflection from
   scene/sky colour, and suppressing airborne curvature foam improved the
   automatic distant fixture but did not fix the user's close view.
4. Expanding the narrow-range smoothing sample stride with projected kernel
   radius failed visually and restored a plastic appearance. This experiment
   is reverted and must not be reintroduced unchanged.

## Best current diagnosis

The contours correlate with individual airborne splats and their overlap
boundaries. Likely contributors still needing isolated debug views are:

- nearest-depth ownership changes between overlapping sphere impostors;
- normal reconstruction amplifying residual depth curvature/seams;
- specular/Fresnel making those reconstructed normals visible.

Do not continue tuning final colour to hide this. The next session should add
temporary debug outputs for raw depth, smoothed depth, reconstructed normals,
thickness, Fresnel, specular, and foam, then compare the same close-camera
frame. This will identify the first pass in which the circles appear before
another algorithm change is attempted.

## Verification at pause

- Both fluid fragment shaders compile with `glslangValidator`.
- The Vulkan `wuxing` build succeeds.
- Automated fixture index 32 was inspected at airborne and settled phases.
- The automated fixture uses a fixed distant camera and therefore does not
  reproduce the user's close-camera failure reliably.
- Performance optimization remains paused by user request until water quality
  is accepted.
