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

---

### 8. `bindPose` / `keyframePoses` are MODEL-SPACE — the skin matrix order is `inv(bind) * pose`

- **Symptom:** Garments (or bodies) shear and explode the moment a cloth bone is overridden, while the same matrices look right for unmodified bones. Reversing the multiplication "fixes" one rig and breaks the next.
- **Cause:** Raylib accumulates node transforms in `BuildPoseFromParentJoints()`, so both `skeleton.bindPose[i]` and `anim.keyframePoses[f][i]` are **global (model-space)** transforms, not parent-relative. Numerically verified on `ao_giao_linh_2.glb`: `max |globalRest − inv(IBM)| = 4e-6`. Therefore `boneMatrices[i] = inv(bindGlobal) * poseGlobal`.
- **Rule:** When overriding a bone outside raylib (cloth, IK, procedural sway), build the global pose and post-multiply by `MatrixInvert(bindPose[i])` last. Never use `pose * inv(bind)`, and never treat those arrays as local transforms. Cloth segments are solved in world space but exported in **model** space (`SpringBone_GetBoneModelPose`) so the override is independent of character position/yaw/scale.

---

### 9. glTF joint arrays are NOT topologically sorted — a child can precede its parent

- **Symptom:** One sleeve simulates correctly while the mirrored sleeve dangles from the wrist out of its rest pose; the difference is only the order of joints in the file (`cloth_sleeve_l_02` listed before `cloth_sleeve_l_01`, `cloth_sleeve_r_01` before `_02`).
- **Cause:** `skin.joints[]` is an index list, not a hierarchy order; raylib's `BuildPoseFromParentJoints()` requires `parent index < child index` and silently mis-builds the pose when that fails, so any pass that resolves chains in list order inherits a wrong parent/child relation.
- **Rule:** Resolve chain topology by bone **name** and run `SpringBone_TopologicalSort()` after registration, so a segment is always simulated after its parent. Register-then-resolve (register all, then resolve parents and measure rest data), never assume list order matches hierarchy.

---

### 10. One `SpringBoneSystem` per instance, but two garments index the same joint differently

- **Symptom:** After equipping a second skinned garment, the first one's skirt snaps to the body or follows the wrong bone.
- **Cause:** Every garment GLB carries its own `skin.joints[]` order; `cloth_skirt_f_c_02` may be joint 23 on one robe and joint 17 on the trousers. A shared solver that stores only a joint index therefore writes the wrong bone on the other model.
- **Rule:** Store the joint index per segment for **that** model's skeleton, but look the bone up by **name** in the model actually being skinned (`CharacterModel_UpdateSkinnedMeshWithCloth` → `ApplyClothBonePose`). Never reuse a joint index across models, and never assume a garment's bone count matches the base body's 20.

---

### 11. Heavy AI-generated garments: cloth invalidates the skin cache every frame

- **Symptom:** Frame time jumps by several milliseconds whenever a detailed robe is worn, and it does not go away when the character stands still.
- **Cause:** Two separate costs stack up. (a) The CPU-skin cache key includes `spring->stamp`, which the solver bumps every update, so a cloth garment re-skins **all** its vertices every frame. (b) Each re-skin then issues `UpdateMeshBuffer` for positions and normals of every mesh primitive, and rlvk's `rlvkUploadBuffer()` takes the mid-frame arena path for device-local VBOs — it ends the render pass, copies, barriers, and re-begins it (**≈1.4 ms per call, independent of payload size**; a 0.4 MB and a 1.3 MB update cost the same).
- **Measured** (`ao_giao_linh_2.glb`, robe alone, with `WUXING_SKIN_PROFILE=1`):
  - 203,256 tris / 106,650 verts: **7.6 ms/frame** (vertex loop 3.5, upload 4.1 in 3 calls)
  - 60,000 tris (decimated): **6.3 ms/frame** (loop 2.1, upload 4.1)
  - 14,191-vertex base body for comparison: **0.7 ms/frame**
- **Rule:** Budget a garment by vertex count *and* by mesh-primitive count — every extra material slot is another pair of render-pass-splitting uploads per frame. Measure with `WUXING_SKIN_PROFILE=1` (logs `cloth` vs `rigid` ms/frame, loop vs upload, vertex count). Decimation only buys back the loop half; the upload half needs the renderer (host-visible/dynamic VBOs for CPU-skinned meshes). `scripts/decimate_glb.py` decimates a rigged GLB headlessly through Blender and preserves the armature, cloth bones and skin weights — but do not go below ~1/3 of the original triangle count on this asset family: it is built from two nearly-coincident shells (outer teal + ivory lining), and collapsing below that pushes the outer shell through the lining (white speckles).


---

### 12. Auto-discovery silently finds nothing (`LoadDirectoryFilesEx` filter)

- **Symptom:** A GLB dropped into `assets/characters/equipment/<slot>/` never reaches the wardrobe; the scan log says "Catalog initialized with N items" and nothing else.
- **Cause:** The scan passed `"*.glb"` as the filter, which is a glob, not what raylib 6 expects — the call returns 0 files with no error.
- **Rule:** The full trap (measured filter behaviour) is in `ENGINE_LANDMINES.md` §29. Here: enumerate with `NULL` and filter with `IsFileExtension()`; `sandbox/auto_test_cases.c`'s `player_wardrobe_system` case now drops a probe GLB into a throwaway tree and fails if it is not discovered.
