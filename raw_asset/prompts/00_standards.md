# Wuxing Skills — Universal Asset Standards & Skeleton Rigging

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
