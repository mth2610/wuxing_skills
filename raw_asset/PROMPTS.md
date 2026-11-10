# Wuxing Skills — Modular Asset Generation Master Prompts

Complete collection of copy-paste production prompts for generating modular 3D wuxia character components using **Three.js WebGL (single-file `index.html`)** with one-click **"Export GLB"** buttons.

---

## Universal Standards (Enforced Across All Prompts)

| Property | Value | Notes |
|---|---|---|
| **Scale** | `1 unit = 1 meter` | Real-world metric scale. Total character height is `1.78m`. |
| **Axes** | `+Y = Up`, `+Z = Forward`, `+X = Left` | Standard glTF 2.0 convention. |
| **Skeleton Hierarchy** | 20 bones (`mixamorig:*`) | Exact Mixamo bone names and joint coordinates. |
| **Skinning Type** | SkinnedMesh vs Rigid Socket | Clothing is SkinnedMesh; Weapons, Hats, and Bracers are Rigid Sockets. |

### Mixamo Humanoid Joint Anchors
```
mixamorig:Hips           [ 0.00, 0.95, 0.00] (Root)
├── mixamorig:Spine      [ 0.00, 1.08, 0.00]
│   └── mixamorig:Spine1 [ 0.00, 1.22, 0.00]
│       └── mixamorig:Spine2 [ 0.00, 1.36, 0.00]
│           ├── mixamorig:Neck [ 0.00, 1.52, 0.00]
│           │   └── mixamorig:Head [ 0.00, 1.66, 0.00]
│           ├── mixamorig:LeftShoulder [ 0.08, 1.48, 0.00]
│           │   └── mixamorig:LeftArm [ 0.22, 1.48, 0.00]
│           │       └── mixamorig:LeftForeArm [ 0.48, 1.48, 0.00]
│           │           └── mixamorig:LeftHand [ 0.72, 1.48, 0.00]
│           └── mixamorig:RightShoulder [-0.08, 1.48, 0.00]
│               └── mixamorig:RightArm [-0.22, 1.48, 0.00]
│                   └── mixamorig:RightForeArm [-0.48, 1.48, 0.00]
│                       └── mixamorig:RightHand [-0.72, 1.48, 0.00]
├── mixamorig:LeftUpLeg  [ 0.10, 0.90, 0.00]
│   └── mixamorig:LeftLeg [ 0.10, 0.48, 0.00]
│       └── mixamorig:LeftFoot [ 0.10, 0.08, 0.00]
└── mixamorig:RightUpLeg [-0.10, 0.90, 0.00]
    └── mixamorig:RightLeg [-0.10, 0.48, 0.00]
        └── mixamorig:RightFoot [-0.10, 0.08, 0.00]
```

---

## Component Folder Mapping in `raw_asset/`

| Component | Target File Path | Type | Attachment Point / Slot |
|---|---|---|---|
| **1. Base Body** | `raw_asset/bodies/human_body.glb` | SkinnedMesh | `MODULAR_SLOT_BASE_BODY` |
| **2. Nón Lá** | `raw_asset/equipment/headgear/non_la.glb` | Rigid Socket | `MODULAR_SLOT_HEADGEAR` (`mixamorig:Head`) |
| **3. Áo (Robe)** | `raw_asset/equipment/torso/ao_giao_linh.glb` | SkinnedMesh | `MODULAR_SLOT_TORSO` |
| **4. Quần (Pants)** | `raw_asset/equipment/legs/quan_wuxia.glb` | SkinnedMesh | `MODULAR_SLOT_LEGS` |
| **5. Giày (Boots)** | `raw_asset/equipment/boots/giay_wuxia.glb` | SkinnedMesh | Footwear / Leg Bindings |
| **6. Hộ Uyển** | `raw_asset/equipment/bracers/ho_uyen.glb` | Rigid Socket | Forearm Sockets (`LeftForeArm` / `RightForeArm`) |
| **7. Kiếm** | `raw_asset/equipment/weapons/kiem_thuan_thien.glb` | Rigid Socket | `MODULAR_SLOT_WEAPON_R` (`mixamorig:RightHand`) |
| **8. Animations** | `raw_asset/animations/combat_moves.glb` | Pure Skeleton Bank | Shared Animation System |

---

## Prompt 1: Base Human Body (`human_body.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, skins, and exports ONLY a standalone athletic wuxia martial artist Base Body (Thể Phách Kiếm Khách) to GLB.

Do NOT include clothing, hat, or weapons. Only the base anatomical body in neutral martial T-pose.

### 1. Metric Scale & Skeleton Standards
- Metric scale: 1 unit = 1 meter. Total character height = 1.78 meters.
- Axes: +Y is Up, +Z is Forward, +X is Left.
- Standard 20-bone Mixamo hierarchy:
  - mixamorig:Hips (Y=0.95m), Spine (Y=1.08m), Spine1 (Y=1.22m), Spine2 (Y=1.36m), Neck (Y=1.52m), Head (Y=1.66m, top Y=1.78m).
  - LeftShoulder/RightShoulder (Y=1.48m, X=±0.08m), LeftArm/RightArm (X=±0.22m), LeftForeArm/RightForeArm (X=±0.48m), LeftHand/RightHand (X=±0.72m).
  - LeftUpLeg/RightUpLeg (Y=0.90m, X=±0.10m), LeftLeg/RightLeg (Y=0.48m), LeftFoot/RightFoot (Y=0.08m, ground at Y=0.0m).

### 2. Anatomical Geometry & Form
- Head & Face: Stylized anime/wuxia aesthetic face, defined jawline, ears, wuxia topknot hair bun (búi tóc củ tỏi) tied at the crown (Y=1.76m to 1.83m).
- Torso: Athletic martial artist physique, defined chest (radius 0.14m), narrow waist (radius 0.115m at Y=0.95m), continuous seamless skin mesh without gaps.
- Limbs: Toned muscular arms and legs, stylized hands with fingers, and bare feet with arched soles.

### 3. Material & Textures
- Warm golden-fair wuxia skin tone (#e8c8a6) with soft sub-surface scattering contouring.
- Hair: Raven black (#161616, roughness 0.6).

### 4. UI & Export
- Three.js OrbitControls, 3-point studio lighting, ground grid, and wireframe toggle.
- "Export Base Body GLB" button downloading `human_body.glb` (SkinnedMesh + Skeleton in T-pose).
```

---

## Prompt 2: Nón Lá Hiệp Khách (`non_la.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, textures, and exports ONLY a standalone Wuxia Conical Bamboo Hat (Nón Lá Hiệp Khách) to GLB.

Do NOT include any body, face, or character mesh. ONLY the hat as an independent socket item.

### 1. Attachment Socket Standard
- Metric Scale: 1 unit = 1 meter.
- Origin (0, 0, 0): Located at the center of the inner headband contact point (where it touches the crown of the head).
- Dimensions: Outer base diameter 0.82m (radius 0.41m), cone height 0.22m, apex angle ~62 degrees.

### 2. Geometry Details
- Conical shell with 48 radial segments, slight downward natural curvature towards the perimeter brim.
- Inner bamboo headband rim (radius 0.09m, thickness 0.012m) located at Y=0.0m.
- 16 concentric bamboo ribbing rings modeled along the cone slope.
- Two red silk tie ribbons hanging down from the inner ring (length 0.45m) with gentle wind curve.

### 3. Procedural Canvas Texture (512x512)
- Generate procedural canvas pattern:
  - Base: Warm sun-bleached natural bamboo straw (#d8be8a).
  - Concentric straw weave rings (#b89a64) and radial leaf seams.
  - Scarlet red (#b22222) silk ribbon cords.

### 4. UI & Export
- Three.js OrbitControls, metalness/roughness lighting, and an "Export Nón Lá GLB" button downloading `non_la.glb`.
```

---

## Prompt 3: Áo Giao Lĩnh (`ao_giao_linh.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, skins, and exports ONLY a traditional Wuxia Cross-Collar Robe (Áo Giao Lĩnh) to GLB.

Do NOT include head, bare skin, hat, or weapons. Only the robe and belt as a deforming SkinnedMesh.

### 1. Skeleton & Skinning Contract
- Standard 20-bone Mixamo hierarchy (Hips at 0.95m, Spine at 1.08m, Spine1 at 1.22m, Spine2 at 1.36m, Neck at 1.52m, Shoulders, Arms, ForeArms).
- Vertices weighted smoothly with bone indices and skin weights.

### 2. Tailored Geometry & Clearance
- Torso Vestment (Y=0.95m to 1.50m):
  - Inner radius 0.165m (provides 2cm outer clearance over base body to guarantee zero clipping).
  - Left lapel folds diagonally over right lapel forming the traditional 'Y' wuxia cross-collar.
- Flowing Skirt / Vạt Áo (Y=0.55m to 0.95m):
  - Extends downwards continuously below the sash past mid-thigh, flared slightly outward (radius 0.22m at hem).
- Silk Sash / Đai Lưng (Y=0.88m to 0.98m):
  - Thick scarlet belt with gold brocade edge and cyan jade buckle; dual silk ribbons drape down to Y=0.58m.
- Martial Sleeves:
  - Flared draping cuffs around arms, skinned smoothly from Shoulders to ForeArms.

### 3. Procedural Silk Texture (512x512 Canvas)
- Deep indigo/teal silk base (#1e293b), gold embroidered cloud patterns on collar and cuffs, scarlet red sash with jade center.

### 4. UI & Export
- T-pose display, wireframe toggle, and "Export Áo GLB" button downloading `ao_giao_linh.glb`.
```

---

## Prompt 4: Quần Võ Hiệp (`quan_wuxia.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, skins, and exports ONLY Wuxia Martial Trousers (Quần Võ Phục) to GLB.

Do NOT include torso, boots, head, or arms. Only the trousers.

### 1. Skeleton & Skinning Contract
- Uses Mixamo leg bones: mixamorig:Hips (Y=0.95m), LeftUpLeg/RightUpLeg (Y=0.90m), LeftLeg/RightLeg (Y=0.48m).
- Vertices weighted smoothly across Hips, UpLegs, and Legs.

### 2. Geometry Details (Y = 0.35m to 0.95m)
- Waistband fits snugly around the hips at Y=0.92m (inner radius 0.145m).
- Loose pleated martial trousers providing full mobility for martial kicks and stances.
- Tapers cleanly into leg bindings below the knee at Y=0.40m to tuck seamlessly into boots.

### 3. Material
- Dark charcoal-blue cloth texture (#18202c, roughness 0.85).

### 4. UI & Export
- "Export Pants GLB" button downloading `quan_wuxia.glb`.
```

---

## Prompt 5: Giày / Ủng Võ Hiệp (`giay_wuxia.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, skins, and exports ONLY a pair of standalone Wuxia Martial Boots (Ủng / Giày Hiệp Khách) to GLB.

Do NOT include torso, head, arms, or upper pants. ONLY the boots and shin bindings.

### 1. Skeleton & Skinning
- Standard Mixamo bones: LeftLeg / RightLeg (Y=0.48m) and LeftFoot / RightFoot (Y=0.08m, ground at Y=0.0m).
- Smooth vertex weights bound to LeftLeg/LeftFoot and RightLeg/RightFoot.

### 2. Geometry Details (Y = 0.0m to 0.42m)
- Tall knee-high wuxia boots (height 0.42m).
- Reinforced curved toe caps, thick dark leather soles, and layered cloth shin wraps with leather criss-cross straps and miniature bronze buckles.

### 3. Material
- Dark oiled leather (#1c1917, roughness 0.4, metalness 0.1) with off-white cloth shin wraps (#e2e8f0).

### 4. UI & Export
- "Export Boots GLB" button downloading `giay_wuxia.glb`.
```

---

## Prompt 6: Hộ Uyển Cổ Tay (`ho_uyen.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models and exports a pair of Rigid Wuxia Forearm Bracers (Hộ Uyển Cổ Tay) to GLB.

Do NOT include hands or arms. Only the bracers as rigid socket attachments.

### 1. Attachment Socket Standard
- Rigid socket (no bones).
- Origin (0, 0, 0): Centered at the forearm/wrist axis.
- Length: 0.22m, inner radius 0.052m, outer radius 0.062m (anatomical forearm taper).

### 2. Geometry & Engravings
- Segmented layered brass/steel armor plates over dark leather backing.
- 3 leather straps with miniature metal buckles wrapping around the inner forearm.
- Embossed bronze tiger/dragon engraving on the outer dorsal plate.

### 3. UI & Export
- Two download buttons:
  - "Export Left Bracer GLB" (`ho_uyen_left.glb`)
  - "Export Right Bracer GLB" (`ho_uyen_right.glb`)
```

---

## Prompt 7: Thuận Thiên Kiếm (`kiem_thuan_thien.glb`)

```markdown
Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that procedurally models, textures, and exports a legendary Vietnamese/Wuxia Double-Edged Sword (Thuận Thiên Kiếm) to GLB.

Do NOT include character hands or body. Only the sword as a rigid socket weapon.

### 1. Socket Mount Standard
- Metric Scale: 1 unit = 1 meter.
- Origin (0, 0, 0): Placed at the EXACT center of the grip handle where the right hand holds the hilt.
- Orientation: Blade extends along +Y, flat edges face +/-Z, guard extends along +/-X.

### 2. Geometry Components
- Blade (Length 0.85m, Width 0.042m, Thickness 0.009m):
  - Double-edged diamond cross-section with central ridge fuller (rãnh kiếm).
  - Tapers smoothly to an acute wuxia piercing tip.
- Crossguard (Y = +0.10m):
  - Ornate golden dragon/lotus guard (width 0.14m, height 0.035m) with forward-swept quillons.
  - Inlaid cyan jade jewel in the guard center.
- Grip Handle (Y = -0.10m to +0.10m, Length 0.20m, Diameter 0.026m):
  - Ergonomic oval grip wrapped with dark ray-skin and criss-cross gold wire.
- Pommel & Tassel (Y = -0.10m):
  - Carved golden ring pommel with dual red silk cords and a dangling crimson tassel (tua kiếm length 0.25m).

### 3. PBR Materials & Textures
- Blade: Polished damascus steel (metalness: 0.95, roughness: 0.18) with cyan runic fuller glow.
- Guard & Pommel: Ancient gilded bronze/gold (metalness: 0.88, roughness: 0.32).
- Tassel: Crimson silk (#9e1b1b, roughness 0.7).

### 4. UI & Export
- Include OrbitControls, environment map reflections, and an "Export Kiếm GLB" button downloading `kiem_thuan_thien.glb`.
```

---

## Prompt 8: New Martial Combat Animations (`combat_moves.glb`)

```markdown
| Property | Value | Notes |
|---|---|---|
| **Scale** | `1 unit = 1 meter` | Real-world metric scale. Total character height is `1.78m`. |
| **Axes** | `+Y = Up`, `+Z = Forward`, `+X = Left` | Standard glTF 2.0 convention. |
| **Skeleton Hierarchy** | 20 bones (`mixamorig:*`) | Exact Mixamo bone names and joint coordinates. |
| **Skinning Type** | SkinnedMesh vs Rigid Socket | Clothing is SkinnedMesh; Weapons, Hats, and Bracers are Rigid Sockets. |

### Mixamo Humanoid Joint Anchors
```
mixamorig:Hips           [ 0.00, 0.95, 0.00] (Root)
├── mixamorig:Spine      [ 0.00, 1.08, 0.00]
│   └── mixamorig:Spine1 [ 0.00, 1.22, 0.00]
│       └── mixamorig:Spine2 [ 0.00, 1.36, 0.00]
│           ├── mixamorig:Neck [ 0.00, 1.52, 0.00]
│           │   └── mixamorig:Head [ 0.00, 1.66, 0.00]
│           ├── mixamorig:LeftShoulder [ 0.08, 1.48, 0.00]
│           │   └── mixamorig:LeftArm [ 0.22, 1.48, 0.00]
│           │       └── mixamorig:LeftForeArm [ 0.48, 1.48, 0.00]
│           │           └── mixamorig:LeftHand [ 0.72, 1.48, 0.00]
│           └── mixamorig:RightShoulder [-0.08, 1.48, 0.00]
│               └── mixamorig:RightArm [-0.22, 1.48, 0.00]
│                   └── mixamorig:RightForeArm [-0.48, 1.48, 0.00]
│                       └── mixamorig:RightHand [-0.72, 1.48, 0.00]
├── mixamorig:LeftUpLeg  [ 0.10, 0.90, 0.00]
│   └── mixamorig:LeftLeg [ 0.10, 0.48, 0.00]
│       └── mixamorig:LeftFoot [ 0.10, 0.08, 0.00]
└── mixamorig:RightUpLeg [-0.10, 0.90, 0.00]
    └── mixamorig:RightLeg [-0.10, 0.48, 0.00]
        └── mixamorig:RightFoot [-0.10, 0.08, 0.00]
```

---

Create a complete, single-file HTML/WebGL application using Three.js (r160) and GLTFExporter that generates 5 NEW martial arts combat animation clips on the standard 20-bone Mixamo humanoid skeleton:

Bones:
mixamorig:Hips, Spine, Spine1, Spine2, Neck, Head,
LeftShoulder, LeftArm, LeftForeArm, LeftHand,
RightShoulder, RightArm, RightForeArm, RightHand,
LeftUpLeg, LeftLeg, LeftFoot, RightUpLeg, RightLeg, RightFoot.

Animations to generate (Euler/Quaternion keyframes):
1. `sword_thrust` (Kiếm Đâm - Fast lunging piercing thrust forward with RightArm extending, body leaning forward, left hand forming sword finger gesture, 35 frames).
2. `sword_combo_3hit` (Tam Liên Trảm - Triple sword strike: diagonal cut, backhand reverse cut, overhead leap chop, 75 frames).
3. `martial_dash` (Khinh Công Lướt - Low evasive burst forward, torso pitched forward at 45 deg, arms trailing behind, 25 frames).
4. `stagger_hit` (Trúng Đòn - Torso recoils backward, head jerks back, legs stagger 1 step to recover balance, 30 frames).
5. `ultimate_cast` (Vạn Kiếm Quy Tông - Floating in mid-air, both hands sweeping in a circular Taiji arc, raising sword fingers to the sky, 90 frames).

Export:
- Exports a GLB containing ONLY the Skeleton and all 5 AnimationClips (NO mesh needed).
- Button: "Export Combat Moves GLB" downloading `combat_moves.glb`.
```

---

## Automated Ingestion Pipeline

Once you generate any of the files above, drop them into their respective folder in `raw_asset/`:
```bash
raw_asset/
├── bodies/human_body.glb
├── equipment/
│   ├── headgear/non_la.glb
│   ├── torso/ao_giao_linh.glb
│   ├── legs/quan_wuxia.glb
│   ├── boots/giay_wuxia.glb
│   ├── bracers/ho_uyen_left.glb
│   └── weapons/kiem_thuan_thien.glb
└── animations/combat_moves.glb
```

Then notify me, and I will execute the engine registration and validation automatically.
