# Analysis & Handoff Report: VFX Testbed Expansion (BURST Tab)

## 1. Observation

Direct observations from the codebase reveal the following structures and interaction patterns:

- **Tab enum and counts in `sandbox/vfx_test.c` (Lines 23-30):**
  ```c
  typedef enum {
      TEST_CAT_IMPACT = 0,
      TEST_CAT_CAST,
      TEST_CAT_PROJECTILE,
      TEST_CAT_COMPOSER,
      TEST_CAT_MESH,
      TEST_CAT_COUNT
  } PrefabTestCategory;
  ```
  The category count `TEST_CAT_COUNT` is used dynamically to size the tabs and background container boxes (e.g. `Rectangle bgBox = { startX - 10, startY - 10, (tabW + spacing) * TEST_CAT_COUNT + 10, 300 };` on Line 428).

- **Tab Names and Rendering in `sandbox/vfx_test.c` (Lines 433-434):**
  ```c
  const char* tabNames[] = { "IMPACT", "CAST", "PROJECTILE", "COMPOSER", "MESH" };
  for (int i = 0; i < TEST_CAT_COUNT; i++) {
  ```
  This renders tabs in the UI.

- **Button grid checks in `sandbox/vfx_test.c` (Lines 304-306 and Lines 448-450):**
  ```c
  if (s_testCategory == TEST_CAT_IMPACT || s_testCategory == TEST_CAT_CAST || s_testCategory == TEST_CAT_PROJECTILE) { maxIdx = 8; names = s_elementNames; }
  ```
  This controls which button grid is displayed for the active category tab.

- **3D Viewport Clicks in `sandbox/vfx_test.c` (Lines 335-337):**
  ```c
  // Nếu click ra ngoài UI -> Spawn
  if (clicked && !s_clickedOnUI) {
      s_prefabStartPos = mouseTarget3D;
  ```
  Spawns effects when clicking outside UI bounds.

- **Impact Burst Signature and Struct in `core/composition/visual_composer.h` (Lines 8-31 & Line 57):**
  ```c
  typedef struct {
      /* --- Step 1: screen distortion --- */
      bool  distortEnabled;
      float distortRadius, distortStrength, distortLife, distortSpeed;

      /* --- Step 2: ground decal --- */
      bool     decalEnabled;
      Texture2D decalTex;
      float     decalScale;   /* multiplied by sizeScale at call time */
      float     decalLife;
      Color     decalTint;
      bool      decalRandomRotation; /* true = GetRandomValue(0,360), false = use decalFixedRotation */
      float     decalFixedRotation;

      /* --- Step 3: point light flash --- */
      bool  lightEnabled;
      Color lightColor;
      float lightRadius;  /* multiplied by sizeScale at call time */
      float lightLife;

      /* --- Step 4: radial particle burst --- */
      bool particlesEnabled;
      ParticleRadialBurstConfig particles;
  } ImpactBurstConfig;
  ```
  ```c
  void VFX_ComposeTriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);
  ```

- **Particle Burst Configuration in `core/particle_system.h` (Lines 153-171):**
  ```c
  typedef struct ParticleRadialBurstConfig {
      int   countMin, countMax;
      float speedMin, speedMax;
      float radiusMin, radiusMax;
      float lifetimeMin, lifetimeMax;
      float pitchRange;
      float upwardBias;
      Color colorStart, colorEnd;
      const ColorGradient *gradient;
      const ForceField *forceField;
      ...
  } ParticleRadialBurstConfig;
  ```

## 2. Logic Chain

From the observations above, the step-by-step logic for integrating the `'BURST'` tab is as follows:

1. **Category Addition**: Adding a new member `TEST_CAT_BURST` right before `TEST_CAT_COUNT` in `PrefabTestCategory` will correctly increment the category count to 6.
2. **Tab UI Integration**: Adding `"BURST"` to the `tabNames` array will make the tab visible. The loop `for (int i = 0; i < TEST_CAT_COUNT; i++)` will automatically render the tab and handle switches to `TEST_CAT_BURST`.
3. **Preset Button Grid**: Checking for `s_testCategory == TEST_CAT_BURST` alongside `TEST_CAT_IMPACT`, `TEST_CAT_CAST`, and `TEST_CAT_PROJECTILE` will populate the BURST tab with the 8 standard elements (Fire, Ice, Water, Lightning, Earth, Wood, Metal, Taiji). This allows selecting different preset impact bursts.
4. **Viewport Interaction**: Clicks outside the UI bounds when `s_testCategory == TEST_CAT_BURST` will construct a custom `ImpactBurstConfig` and invoke `VFX_ComposeTriggerImpactBurst`.
5. **Decal and Config Parameter Resolution**: By using the selected `s_testIndex` (0 to 7), the implementation can map each element to matching decal texture paths (loaded via `ResourceManager_LoadTexture`) and colors (e.g. `ELEMENT_COLOR_FIRE`, `ELEMENT_COLOR_WATER`) defined in `core/skill_manager.h` to execute element-themed bursts with appropriate particle counts, speeds, and light properties.

## 3. Caveats

- **Z-fighting**: Decals spawned near or at floor level might experience z-fighting if the height offset (`yOffset`) is not handled properly inside `DecalSystem_Add` or `VFX_ComposeTriggerImpactBurst`. However, the core system handles this via default offsets (e.g. 0.02f).
- **Scale factor**: The `sizeScale` passed to `VFX_ComposeTriggerImpactBurst` multiplies `decalScale` and `lightRadius`. If the baseline configuration is already in meters, setting `sizeScale = 1.0f` ensures proper metric rendering.
- **Resource loading**: Deployed assets (`assets/textures/decals/*`) must exist. `ResourceManager_LoadTexture` handles loading, caching, and VRAM management.

## 4. Conclusion

The planning of the `'BURST'` tab is complete and solid. The proposed changes have been written to the patch file `proposed_burst_tab.patch` in this working directory. The plan utilizes Raylib's texture loader, standard game UI structures, and the existing 4-step VFX composer interface in a clean, maintainable way.

## 5. Verification Method

To verify the changes after implementation:

1. **Clean and Compile**:
   Execute the build command from the workspace root:
   ```bash
   mkdir -p build && cd build
   cmake ..
   make wuxing
   ```
2. **Execute Interactive Sandbox**:
   Run the build executable `./wuxing` and navigate to the VFX Sandbox interface.
3. **Verify Tab Presence and Switches**:
   Ensure the new **"BURST"** tab is visible at the top menu. Switch to it and check if it shows the 8 element buttons (FIRE, ICE, etc.).
4. **Trigger Visual Effects**:
   Select an element preset in the "BURST" tab and click in the 3D viewport. Confirm visually that all 4 steps fire:
   - Screen Distortion (camera ripple)
   - Ground Decal (e.g., lava cracks for FIRE, frost for ICE)
   - Point Light Flash (colored point light at target)
   - Radial Particle Burst (upward-biased, colored particle explosion)
