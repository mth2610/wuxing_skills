# Wuxing Skills — Boss & Creature Model Standard

This directory contains runtime 3D models and animation assets for **Unique Bosses** (Đại Tinh Linh) and **Non-Humanoid Creatures** (beasts, golems, monsters with non-standard armatures).

---

## 1. Directory Structure

```
assets/models/
├── README.md                   # This specification
├── boss_moclinh_mesh.glb       # Decoupled mesh + skin (0 animations)
├── boss_moclinh_anims.glb      # Decoupled armature + 7 animation clips
└── boss_moclinh.glb            # Bundled fallback (mesh + animations combined)
```

---

## 2. Decoupled vs Bundled Standard

### 2.1. When to Use `assets/models/`
Use this folder when an entity has a **custom rig** that deviates from the Humanoid 65-bone standard:
- Different joint counts (e.g. 40 joints, custom tails, extra wings, floating root branches).
- Proportions and signature attacks tailored to that specific creature (e.g., ground slams, wide sweeps, ferocious roars).

### 2.2. Decoupling Convention
Each boss entity should provide decoupled assets to support clean memory management:
* `<boss_name>_mesh.glb`: Geometry, UVs, skin weights, and materials. No animation tracks.
* `<boss_name>_anims.glb`: Armature hierarchy with all animation takes/clips.
* `<boss_name>.glb`: Bundled fallback file.

### 2.3. Loading Contract in C (`boss/<name>_def.c`)
Engine boss definition files load model and animations through `ResourceManager`:

```c
// Load split assets with transparent fallback to bundled GLB:
s_model = ResourceManager_LoadModel("assets/models/boss_moclinh_mesh.glb");
if (s_model.meshCount == 0) {
    s_model = ResourceManager_LoadModel("assets/models/boss_moclinh.glb");
}
SurfaceMaterial_Apply(&s_model);

s_anims = ResourceManager_LoadModelAnimations("assets/models/boss_moclinh_anims.glb", &s_animCount);
if (s_anims == NULL || s_animCount == 0) {
    s_anims = ResourceManager_LoadModelAnimations("assets/models/boss_moclinh.glb", &s_animCount);
}
```

---

## 3. Technical Standards

1. **Meter Scale:** All models must be authored in 1 unit = 1 meter real-world coordinates.
2. **Animation Frame Rate:** Author clips at 30 FPS or 60 FPS keyframe sampling.
3. **No-Tutorial Phase Readability:** Bosses must render visual element cues (e.g. ground rune rings or chest core glow) that reflect their current combat element.
4. **Shadows:** Bosses should draw a dynamic ground shadow via `Environment_DrawSmartShadow`.
