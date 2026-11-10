# Character Module — Landmines

> Runtime traps, gotchas, and lessons learned in the `character/` module.
> Governed by `DOC_ARCHITECTURE.md` (3 parts per entry: **Symptom → Cause → Rule**).
> Cross-cutting engine traps live in `ENGINE_LANDMINES.md`.

---

### 1. Extra Cloth Spring Bones vs Raylib 20-Bone Standard
- **Symptom:** Outfits with cloth bones (e.g. `ao_giao_linh.glb` with 28 bones: 20 Mixamo + 8 `cloth_*`) crashed inside Raylib's `UpdateModelAnimation()` with segmentation faults or buffer overflows.
- **Cause:** Raylib's internal `UpdateModelAnimation()` assumes that `model.skeleton.boneCount` strictly equals `anim.boneCount` (20). When a model has extra bones, Raylib writes bone matrices past the allocated animation array bounds.
- **Rule:** Never pass models with extra bones (`skeleton.boneCount > anim.boneCount`) into Raylib's `UpdateModelAnimation()`. Use `CharacterModel_UpdateSkinnedMeshWithCloth()` which skips extraneous bone transforms and performs safe CPU skinning.

---

### 2. Backface Culling & Single-Sided Clothing Shells
- **Symptom:** The inside of hats (Nón Lá), skirts, wide bell sleeves, or inner collar lapels disappear completely or become invisible when viewed from below/inside.
- **Cause:** The Vulkan / OpenGL rasterizer enforces backface culling (`CullMode = BACK`) by default. Single-sided 2D surfaces have front-facing normals on one side only; when viewed from reverse, fragments are culled.
- **Rule:** All clothing, hats, sleeves, and pants must be exported as **solidified 2-sided shells with $2.0\text{mm} - 2.5\text{mm}$ physical thickness** and consistent outward normals.

---

### 3. Body Mesh Penetration Through Outerwear (Poke-Through)
- **Symptom:** Chest, shoulders, or thighs poke through clothing fabrics during animations (e.g., bare skin clipping through dark robe lapels or pants).
- **Cause:** Base body meshes have full muscular volume ($Z = 0.143\text{m}$ chest depth). When clothing deforms under animation, slight interpolation differences cause underlying vertices to intersect the outer shell.
- **Rule:** In `CharacterModel_DrawCustomEx()`, activate engine-level **Selective Body Deflation** (`deflateTorso` retracts chest/spine vertices inward by $16\%$, `deflateLegs` retracts thigh vertices inward radially by $22\%$). Author base body prompts with lean athletic wuxia contours ($Z_{\max} \le +0.115\text{m}$).

---

### 4. SkinnedMesh Inverse Bind Matrix Calculation Order
- **Symptom:** 3D models look fine in static T-pose in WebGL viewers, but when loaded into the game engine with skeletal animations, vertices explode, detach, or stretch across the world origin.
- **Cause:** Three.js `skeleton.calculateInverses()` was called before updating the world matrices of the bone hierarchy, causing identity or uninitialized inverse bind matrices to be baked into the glTF accessors.
- **Rule:** In WebGL / Three.js generators, always follow the strict order:
  1. `scene.add(hips);`
  2. `scene.updateMatrixWorld(true); hips.updateWorldMatrix(true, true);`
  3. `skeleton.calculateInverses();`
  4. `skinnedMesh.bind(skeleton);`

---

### 5. Socket Attachment Origin Invariants
- **Symptom:** Hat floats above the character's head or sinks into the neck; sword floats 30cm away from the palm or points backwards.
- **Cause:** The origin `(0, 0, 0)` of standalone socket attachments was placed at the mesh geometric center instead of the true anatomical contact anchor point.
- **Rule:** Standalone socket assets MUST place their origin `(0, 0, 0)` at their contact point:
  - **Nón Lá / Headgear**: Origin at the center of the inner headband touching the skull crown ($Y = 0.0\text{m}$).
  - **Kiếm / Weapon**: Origin at the center of the palm handle grip ($Y = 0.0\text{m}$), blade extending along $+Y$.
  - **Hộ Uyển / Bracers**: Origin at the wrist joint ring ($X = 0.0\text{m}, Y = 0.0\text{m}, Z = 0.0\text{m}$).

---

### 6. Disconnected Primitive Chunks vs 1-Island Watertight Lofting
- **Symptom:** The character body renders as a wooden dummy / mannequin made of 11 disconnected floating boxes and cylinders with gaping holes at elbows and knees when animated.
- **Cause:** Prompt instructed primitives (`BoxGeometry`, `CylinderGeometry`) merged haphazardly instead of a continuous parametric surface.
- **Rule:** Human base bodies must be authored as a **1-Island Watertight SkinnedMesh** using continuous parametric lofted cross-section rings (`torsoRings`) and capped limb tubes with deep overlapping sockets ($3.5\text{cm}$).

---

### 7. Mixamo Shoulder & Thigh Quaternion Orientation Frames
- **Symptom:** Arms bend forward $90^\circ$ like artillery barrels or legs twist inwards by $180^\circ$ when applying Mixamo animation clips.
- **Cause:** Mixamo arm and leg chains use non-identity rotational coordinate frames (`LeftShoulder` uses quaternion `(-0.5, -0.5, -0.5, 0.5)` so child bone local $+Y$ points along World $+X$). Using identity quaternion `(0, 0, 0, 1)` breaks the coordinate alignment.
- **Rule:** Mixamo arm bones must use shoulder quaternion `(-0.5, ±0.5, ±0.5, 0.5)` and thigh bones must use upleg quaternion `(-1.0, 0.0, 0.0, 0.0)`. Never reset shoulder or upleg quaternions to identity.
