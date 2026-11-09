# Wuxing Skills — Humanoid Model & Animation Standard

This directory contains runtime 3D assets for all **Humanoid** entities (player character, remote heroes, bot allies, and humanoid bosses/minions).

---

## 1. Directory Structure

```
assets/characters/
├── README.md                 # This specification
├── player.glb                # Master humanoid skeletal animation library (13+ clips)
├── player_anims.json         # Semantic action slot manifest (idle, walk, slash, cast...)
└── models/                   # Decoupled humanoid character meshes (skin-only, 0 animations)
    └── character_with_skin.glb
```

---

## 2. Technical Standards & Engine Invariants

### 2.1. Coordinate System & Scale
* **Metric Real-World Scale:** $1.0\text{ unit} = 1.0\text{ meter}$.
* **Height Reference:** Player character height $\approx 1.75\text{m} - 1.85\text{m}$.
* **Orientation:** Right-handed, $+Y$ is UP, forward facing $+Z$ (converted to $-Z$ or $+Z$ per camera yaw).
* **Entity Scaling Rule:** Never alter bone lengths in rigging to make bosses or minions. Scale uniformly via engine draw transform:
  - **Minions:** `scale = 0.80f` ($\approx 1.45\text{m}$)
  - **Hero / Player:** `scale = 1.00f` ($\approx 1.80\text{m}$)
  - **Humanoid Boss:** `scale = 1.60f - 2.00f` ($\approx 2.9\text{m} - 3.6\text{m}$)

### 2.2. Skeletal Hierarchy & Bone Matching
Raylib's skeletal animation validator strictly enforces:
```c
if (model.skeleton.boneCount != anim.boneCount) return false;
```
* **Armature Standard:** Mixamo Humanoid Standard (65 joints including full fingers, or 52 joints).
* **Bone Namespace:** Pure cleaned bone names without software prefixes (`Hips`, `Spine`, `LeftArm`, `RightArm`, `LeftLeg`, etc.). No `mixamorig:`, `Bip01_`, or `Armature|` namespace clutter.
* **Rest Pose:** All humanoid models must share an identical T-Pose rest/bind pose.

### 2.3. Animation Channel Rules
* **Limb Bones (Arms, Legs, Spine, Neck, Head):** **Rotation-Only** keyframes (Quaternion `XYZW`).
  * *Reason:* Translating limb joints will stretch or compress character proportions.
* **Root / Pelvis (`Hips`):** May contain both **Translation** and **Rotation** keyframes for jumping, crouching, and vertical breathing bob.
* **Root Motion:** Horizontal ($XZ$) world travel must be stripped (in-place animations). World velocity is governed by the game engine movement simulation.

### 2.4. Materials & Shaders
* Models must call `SurfaceMaterial_Apply(&model)` once after loading.
* Shading conforms to the engine's stylized-realism pipeline: half-Lambert diffuse, hemispheric ambient, Blinn-sheen, Fresnel rim, and distance fog.

---

## 3. Automation Pipeline

Use [`scripts/pipeline_humanoid_assets.py`](../../scripts/pipeline_humanoid_assets.py) to automatically process and import new humanoid models and animations:

```bash
python3 scripts/pipeline_humanoid_assets.py --models path/to/raw_models --anims path/to/raw_anims
```
* Automatically detects humanoid skeletons vs non-humanoids.
* **Automatic Animation Extraction:** If a raw model contains embedded animations (e.g. FBX with bundled takes/actions), the pipeline automatically extracts, validates, and retargets them, adding them to the shared humanoid animation bank.
* Strips namespace prefixes and retargets bone tracks to standard humanoid names.
* Exports clean skin-only meshes (stripped of redundant animations) to `assets/characters/models/`.
* Combines all animations into `player.glb` and updates `player_anims.json`.
