# Wuxing Skills — Modular 3D Asset & Animation Architecture

This directory contains runtime 3D assets for all **Humanoid** entities (player characters, online teammates/opponents, bosses, and minion swarms).

---

## 1. Modular Entity Tiering (Designed for 4 Players, 2 Bosses, 50 Minions)

The engine segregates entities into 3 tiers to guarantee **locked 60+ FPS** with zero skinning bottlenecks:

| Tier | Scale | Modularity | Animation Strategy | Performance Budget |
|---|---|---|---|---|
| **Hero (Players)** | 1.00m (1.80m height) | Full (Base Body + Torso + Legs + Sockets) | Dedicated per-player state machine | Up to 4 active instances ($< 0.4\text{ms}$) |
| **Boss** | 1.60m–2.00m ($2.9\text{m}-3.6\text{m}$) | Heavy Armor + Sockets (Heroic/Legendary Weapons) | High-impact boss skill clips | Up to 2 active bosses ($< 0.3\text{ms}$) |
| **Swarm Minions** | 0.80m (1.45m height) | Unified Low-Poly Mesh + Socketed Weapon | **Clustered Pose Sharing** (1 skinning pass/squad) | **50+ active units** ($\approx 0.05\text{ms}$ CPU pass) |

---

## 2. Directory Structure

```
assets/characters/
├── README.md                 # This specification
├── player.glb                # Master humanoid skeletal animation library (21+ clips)
├── player_anims.json         # Semantic action slot manifest (idle, walk, slash, cast...)
├── bodies/                   # Base anatomical meshes (human male/female, minion, boss)
│   ├── human_base.glb
│   └── minion_woodling.glb
├── equipment/                # Mix-and-match equipment pieces
│   ├── torso/                # Skinned deforming robes/shirts (Áo Giao Lĩnh, Giáp Thiết Vệ)
│   ├── legs/                 # Skinned deforming pants (Quần Wuxia, Giáp Chân)
│   ├── weapons/              # RIGID SOCKETS (Kiếm Thuận Thiên, Thương, Đao) — skins: 0
│   └── headgear/             # RIGID SOCKETS (Nón Lá, Khăn Đóng, Mặt Nạ) — skins: 0
└── models/                   # Standalone exported bundles
    └── nhanvat1.glb
```

---

## 3. Modular Equipment Types

1. **Rigid Sockets (Zero Skinning Overhead)**:
   - **Weapons** (`MODULAR_SLOT_WEAPON_R`, `MODULAR_SLOT_WEAPON_L`), **Hats** (`MODULAR_SLOT_HEADGEAR`), **Scabbards** (`MODULAR_SLOT_BACK`).
   - Stored as static meshes (`skins: 0`).
   - The engine computes socket transforms via forward kinematics:
     $$\mathbf{M}_{\text{socketWorld}} = \mathbf{M}_{\text{localOffset}} \times \mathbf{M}_{\text{boneWorld}} \times \mathbf{M}_{\text{entityWorld}}$$
   - **Advantage**: ZERO CPU skinning cost. Weapons and hats can be equipped on ANY humanoid entity (player, minion, or boss).

2. **Skinned Deforming Clothing**:
   - **Torso & Legs** (`MODULAR_SLOT_TORSO`, `MODULAR_SLOT_LEGS`).
   - Vertex buffer stores `JOINTS_0` and `WEIGHTS_0` bound to the universal Mixamo Humanoid skeleton.
   - Skeletons and animations are NOT duplicated in equipment files.

---

## 4. Swarm Minion Batching (50+ Minions at 60 FPS)

For 50 minions, running per-instance CPU skinning ($50 \times 20\text{k}$ vertices) would cause severe CPU frame-time spikes.
Instead, `ModularSystem_DrawMinionBatch()` implements **Clustered Pose Sharing**:
- Evaluates the minion march/attack animation **ONCE** on CPU.
- Draws all 50 minions at their respective arena positions using the single deformed vertex buffer.
- Evaluates socketed weapons across the squad with simple matrix translation/rotation.

---

## 5. Network Replication Overhead

In online multiplayer (max 4 players + 2 bosses + 50 minions):
- **On Join / Lobby**: Sync cosmetic appearance IDs:
  `uint16_t bodyId`, `uint16_t outfitId`, `uint16_t weaponId`, `uint16_t hatId` ($\approx 8\text{ bytes}$ total per player).
- **Per Frame**: Sync gameplay state:
  `Vector3 pos`, `float yaw`, `uint8_t animSlot`, `uint16_t animFrame` ($< 20\text{ bytes}$ per entity).
- Zero mesh data or geometry is transmitted across the wire.

---

## 6. Automation Tool (`scripts/modular_asset_pipeline.py`)

Run the automated pipeline to process raw assets from Blender/Mixamo:

```bash
# 1. Extract pure headless animation tracks
/Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/modular_asset_pipeline.py -- --extract-anims raw_model.glb assets/characters/animations/

# 2. Extract rigid socket equipment (weapons, hats, scabbards)
/Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/modular_asset_pipeline.py -- --extract-sockets raw_model.glb assets/characters/equipment/

# 3. Standardize base body to 1-unit-1-meter scale
/Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/modular_asset_pipeline.py -- --standardize-body raw_body.glb assets/characters/bodies/human_base.glb 0.45
```
