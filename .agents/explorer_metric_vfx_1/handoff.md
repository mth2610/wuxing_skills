# Handoff Report: Metric VFX Conversion Analysis (`sandbox/vfx_test.c`)

This report identifies and catalogs all instances of centimeter coordinates, sizes, forces, offsets, and velocities within `sandbox/vfx_test.c` that must be converted to the metric system (divided by 100) to comply with project requirement R1.

---

## 1. Observation

Exact file path: `sandbox/vfx_test.c`

Below are the direct observations of lines and values identified as candidates for conversion from centimeters to meters:

### A. Point Light & Decals (KEY_T Handler)
- **Line 81**: Light Radius
  ```c
  VFXLight_Spawn(playerPos, (Color){255, 180, 50, 255}, 150.0f, 9999.0f, VFX_PRIORITY_LOW);
  ```
- **Line 82**: Decal Scale
  ```c
  DecalSystem_Add(playerPos, (float)GetRandomValue(0, 360), 40.0f,
              globalParticleTex, 3.0f, ORANGE);
  ```

### B. Screen Distortion (KEY_T Handler)
- **Line 79**: Distortion Radius and Speed
  ```c
  ScreenDistort_Add(playerPos, 120.0f, 0.8f, 1.2f, 250.0f);
  ```

### C. Particle Radii & Offsets (KEY_T Handler)
- **Line 107**: Death child particle radius
  ```c
  deathChildConfig.radius = 25.0f;
  ```
- **Line 119**: Live child particle radius
  ```c
  liveChildConfig.radius = 30.0f;
  ```
- **Line 131**: Mother particle radius
  ```c
  motherConfig.radius = 40.0f;
  ```
- **Line 116**: Live child velocity
  ```c
  liveChildConfig.velocity = (Vector3){0.0f, 10.0f, 0.0f};
  ```
- **Lines 128-129**: Mother spawn offset position
  ```c
  motherConfig.position =
      Vector3Add(playerPos, (Vector3){-60.0f, 15.0f, 0.0f});
  ```
- **Line 130**: Mother velocity
  ```c
  motherConfig.velocity = (Vector3){120.0f, 0.0f, 0.0f};
  ```

### D. Trail Ribbon (KEY_T Handler)
- **Lines 145-146**: Trail spawn offset and velocity
  ```c
  tConfig.pos = Vector3Add(playerPos, (Vector3){75.0f, 30.0f, 0.0f});
  tConfig.vel = (Vector3){220.0f, 0.0f, 0.0f};
  ```
- **Lines 147-149**: Trail length, thickness, and history length
  ```c
  tConfig.len = 50.0f;
  tConfig.thick = 6.0f;
  tConfig.trailLength = 80.0f;
  ```

### E. Vortex Force Field (KEY_F Handler)
- **Lines 179-181**: Vortex force layer origin offset and strength
  ```c
  vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});
  ...
  vortex.strength = 400.0f;
  ```
- **Lines 186-194**: Particle spawn center offset, velocity, and radius
  ```c
  Vector3 center = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});
  ...
  cfg.velocity = (Vector3){cosf(ang) * 150.0f, 0.0f, sinf(ang) * 150.0f};
  ...
  cfg.radius = 6.0f;
  ```

### F. Vector Field (KEY_Y Handler)
- **Lines 236-240**: Vector field origin offset, box half-extents, and force strength
  ```c
  vf.origin    = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});
  vf.direction = (Vector3){300.0f, 0.0f, 300.0f}; // half-extent box (xz)
  vf.strength  = 250.0f;
  ```
- **Lines 244-253**: Spawn position offset, random offset, and particle radius
  ```c
  Vector3 spawnPos =
      Vector3Add(playerPos, (Vector3){-250.0f, 40.0f, 0.0f}); // mép trái box
  ...
  cfg.position = Vector3Add(
      spawnPos, (Vector3){0.0f, 0.0f, (float)GetRandomValue(-80, 80)});
  ...
  cfg.radius = 8.0f;
  ```

### G. Projectile Test Shooting Distance (Mouse Click Handler)
- **Line 338**: Projectile target endpoint offset
  ```c
  Vector3 endPos = Vector3Add(s_prefabStartPos, (Vector3){40.0f, 0.0f, 0.0f});
  ```

---

## 2. Logic Chain

The reasoning for each proposed change is derived step-by-step from Requirement R1 and the codebase structure:

1. **Point Light & Decals**:
   - `VFXLight_Spawn` radius `150.0f` is in cm. Dividing by 100 transforms it to meters (`1.5f`).
   - `DecalSystem_Add` scale `40.0f` is in cm. Dividing by 100 transforms it to meters (`0.4f`).
2. **Screen Distortion**:
   - `ScreenDistort_Add` takes `radius` and `speed` as spatial parameters. Therefore, `120.0f` becomes `1.2f`, and `250.0f` becomes `2.5f`.
3. **Particle Radii**:
   - Particle configs specify `.radius` in physical size. Converting `25.0f`, `30.0f`, `40.0f`, `6.0f`, and `8.0f` gives `.25f`, `.3f`, `.4f`, `.06f`, and `.08f` respectively.
4. **Offsets & Positions**:
   - Spawning offsets like `(Vector3){-60.0f, 15.0f, 0.0f}` and `(Vector3){75.0f, 30.0f, 0.0f}` define distance in game space. They must scale to `(Vector3){-0.6f, 0.15f, 0.0f}` and `(Vector3){0.75f, 0.3f, 0.0f}`.
   - Flow box coordinates like `(Vector3){-250.0f, 40.0f, 0.0f}` and half-extents `(Vector3){300.0f, 0.0f, 300.0f}` must scale to `(Vector3){-2.5f, 0.4f, 0.0f}` and `(Vector3){3.0f, 0.0f, 3.0f}`.
   - The random offset `GetRandomValue(-80, 80)` along the Z-axis spans -80cm to +80cm. It must be scaled to `(float)GetRandomValue(-80, 80) / 100.0f` (representing -0.8m to +0.8m).
5. **Forces**:
   - Vortex strength `400.0f` and vector field strength `250.0f` apply acceleration/force and should be scaled down by 100 to `4.0f` and `2.5f`.
6. **Velocities**:
   - Velocity values `(Vector3){0.0f, 10.0f, 0.0f}`, `(Vector3){120.0f, 0.0f, 0.0f}`, `(Vector3){220.0f, 0.0f, 0.0f}`, and `150.0f` are in cm/s. Dividing by 100 yields `0.1f`, `1.2f`, `2.2f`, and `1.5f` respectively.
7. **Trail Ribbon Dimensions**:
   - `tConfig.len` and `tConfig.thick` are spatial ribbon length/thickness. `50.0f` and `6.0f` become `0.5f` and `0.06f`.
   - `tConfig.trailLength` represents the number of history nodes (cast to `int` in `trail_system.c:131`). Therefore, `80.0f` is a count, not a spatial coordinate, and should remain unchanged.
8. **Projectile Shooting Distance**:
   - Requirement R1 explicitly changes the test projectile endpoint offset from `40.0f` to `8.0f`. Therefore, `(Vector3){40.0f, 0.0f, 0.0f}` becomes `(Vector3){8.0f, 0.0f, 0.0f}`.

---

## 3. Caveats

- **Mesh scale**: The scale passed to `DrawEffectMesh` in line 366 is `(Vector3){2.0f, 2.0f, 2.0f}`. This represents 2 meters in the metric system, which is a sensible default visual size. No division by 100 is needed since it's not a legacy centimeter scale (which would have been `200.0f`).
- **Trail Length**: As analyzed, `tConfig.trailLength` represents a count of history segments and must not be divided by 100.

---

## 4. Conclusion

To implement R1 in `sandbox/vfx_test.c`, the following replacements must be made:

| Line | Original Code | Proposed Metric Code | Type |
|---|---|---|---|
| **79** | `ScreenDistort_Add(playerPos, 120.0f, 0.8f, 1.2f, 250.0f);` | `ScreenDistort_Add(playerPos, 1.2f, 0.8f, 1.2f, 2.5f);` | Radius & Speed |
| **81** | `VFXLight_Spawn(playerPos, ..., 150.0f, 9999.0f, ...);` | `VFXLight_Spawn(playerPos, ..., 1.5f, 9999.0f, ...);` | Light Radius |
| **82** | `DecalSystem_Add(..., 40.0f, ...);` | `DecalSystem_Add(..., 0.4f, ...);` | Decal Scale |
| **107** | `deathChildConfig.radius = 25.0f;` | `deathChildConfig.radius = 0.25f;` | Particle Radius |
| **116** | `liveChildConfig.velocity = (Vector3){0.0f, 10.0f, 0.0f};` | `liveChildConfig.velocity = (Vector3){0.0f, 0.1f, 0.0f};` | Particle Velocity |
| **119** | `liveChildConfig.radius = 30.0f;` | `liveChildConfig.radius = 0.3f;` | Particle Radius |
| **129** | `Vector3Add(playerPos, (Vector3){-60.0f, 15.0f, 0.0f});` | `Vector3Add(playerPos, (Vector3){-0.6f, 0.15f, 0.0f});` | Spawn Offset |
| **130** | `motherConfig.velocity = (Vector3){120.0f, 0.0f, 0.0f};` | `motherConfig.velocity = (Vector3){1.2f, 0.0f, 0.0f};` | Particle Velocity |
| **131** | `motherConfig.radius = 40.0f;` | `motherConfig.radius = 0.4f;` | Particle Radius |
| **145** | `tConfig.pos = Vector3Add(playerPos, (Vector3){75.0f, 30.0f, 0.0f});` | `tConfig.pos = Vector3Add(playerPos, (Vector3){0.75f, 0.3f, 0.0f});` | Spawn Offset |
| **146** | `tConfig.vel = (Vector3){220.0f, 0.0f, 0.0f};` | `tConfig.vel = (Vector3){2.2f, 0.0f, 0.0f};` | Trail Velocity |
| **147** | `tConfig.len = 50.0f;` | `tConfig.len = 0.5f;` | Trail Length |
| **148** | `tConfig.thick = 6.0f;` | `tConfig.thick = 0.06f;` | Trail Thickness |
| **179** | `vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});` | `vortex.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.4f, 0.0f});` | Vortex Offset |
| **181** | `vortex.strength = 400.0f;` | `vortex.strength = 4.0f;` | Vortex Strength |
| **186** | `Vector3 center = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});` | `Vector3 center = Vector3Add(playerPos, (Vector3){0.0f, 0.4f, 0.0f});` | Vortex Offset |
| **191** | `cfg.velocity = (Vector3){cosf(ang) * 150.0f, 0.0f, sinf(ang) * 150.0f};` | `cfg.velocity = (Vector3){cosf(ang) * 1.5f, 0.0f, sinf(ang) * 1.5f};` | Particle Velocity |
| **194** | `cfg.radius = 6.0f;` | `cfg.radius = 0.06f;` | Particle Radius |
| **236** | `vf.origin = Vector3Add(playerPos, (Vector3){0.0f, 40.0f, 0.0f});` | `vf.origin = Vector3Add(playerPos, (Vector3){0.0f, 0.4f, 0.0f});` | VF Offset |
| **237** | `vf.direction = (Vector3){300.0f, 0.0f, 300.0f};` | `vf.direction = (Vector3){3.0f, 0.0f, 3.0f};` | VF Box Extents |
| **238** | `vf.strength = 250.0f;` | `vf.strength = 2.5f;` | VF Strength |
| **245** | `Vector3Add(playerPos, (Vector3){-250.0f, 40.0f, 0.0f});` | `Vector3Add(playerPos, (Vector3){-2.5f, 0.4f, 0.0f});` | VF Spawn Offset |
| **249** | `spawnPos, (Vector3){0.0f, 0.0f, (float)GetRandomValue(-80, 80)});` | `spawnPos, (Vector3){0.0f, 0.0f, (float)GetRandomValue(-80, 80) / 100.0f});` | VF Spawn Random Offset |
| **253** | `cfg.radius = 8.0f;` | `cfg.radius = 0.08f;` | Particle Radius |
| **338** | `Vector3 endPos = Vector3Add(s_prefabStartPos, (Vector3){40.0f, 0.0f, 0.0f});` | `Vector3 endPos = Vector3Add(s_prefabStartPos, (Vector3){8.0f, 0.0f, 0.0f});` | Projectile Shooting Distance |

---

## 5. Verification Method

- **Compile Test**: Run the project build command (`make`) after these modifications to ensure no syntax/compilation issues occur.
- **Visual Inspection**:
  1. Launch the application.
  2. Press `T` to check the point light, decal, particle trail, and screen distortion. They should spawn close to the player (within 1-2 meters) rather than flying way off-screen, and should have appropriate sizes relative to the player character.
  3. Press `F` to trigger the GPU particle vortex. The particles should swirl at a radius of `0.4f` meters instead of 40 meters.
  4. Press `Y` to trigger the vector field test. The bounding box of the field should match the 3.0-meter boundaries, and particles should spawn along the boundary and float smoothly.
  5. Go to the "PROJECTILE" tab, click in the viewport to shoot a projectile. The distance it travels should be exactly `8.0f` meters.
