# VFX Architecture & Composition Standard — Wuxing Skills

> **Normative Architecture Specification for Atomic VFX, Composite VFX, and Sandbox Testing**
> Cross-references: [`COMPOSITION_API.md`](COMPOSITION_API.md), [`API_GUIDE.md`](API_GUIDE.md), [`WUXING_ART_DIRECTION.md`](../../WUXING_ART_DIRECTION.md)

---

## 1. Overview & Core Philosophy

The Wuxing Skills engine structures all visual effects under a unified **Hierarchical Assembly Model**. In this model, effects are strictly classified into two tiers:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   COMPOSITE VFX (VFX TỔ HỢP)                           │
│  - Owns macro geometry / path spine / volume                           │
│  - Generates surface anchor points via VFX_Socket                      │
│  - Governs 4-Phase Lifecycle (Sprout -> Mature -> Reaction -> Decay)   │
│  - Selects child presets & manages detachment transitions              │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                       Samples & passes VFX_Socket[]
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     ATOMIC VFX (VFX ĐƠN)                               │
│  - Indivisible geometric or particle element (leaves, petals, sparks) │
│  - Dual-State: ATTACHED (to host socket) vs FREE (physical simulation) │
│  - Owns morphology (Shape enum) and tonal palette (Style enum)         │
│  - Reacts to Gravity (9.81 m/s²), Aerofoil Drag, Wind, & Force Fields  │
└────────────────────────────────────────────────────────────────────────┘
```

This architecture guarantees:
1. **Independent Testability**: Every atomic effect can be spawned standalone in the air to verify its physical aerodynamics, settling behavior, shading, and bright-background contrast.
2. **Infinite Combinations**: Composite effects can mix, match, and hot-swap child components on the fly (e.g. bare vine, thorny bramble, leafy tendril, or flowering creeper) without duplicating drawing code.
3. **Physical Detachment Hand-off**: When a composite structure is severed, damaged, or incinerated, attached child elements seamlessly detach into the airborne physical simulation pool, inheriting host momentum.
4. **Universal Multi-Element Consistency**: The exact same socket and state contracts apply across all Five Elements (Wood, Water, Fire, Earth, Metal) and Taiji.

---

## 2. Universal Socket Contract (`VFX_Socket`)

A **Socket** represents a localized surface coordinate frame on a parent composite geometry. It defines where and how a child element attaches, orients, and responds to motion.

Defined in [`core/composition/visual_composer.h`](../composition/visual_composer.h):

```c
typedef struct VFX_Socket {
    Vector3 pos;        // Surface attachment anchor in world space (metres)
    Vector3 normal;     // Outward surface normal vector (perpendicular to host surface)
    Vector3 tangent;    // Direction vector along the host spine / flow axis
    union {
        float param;    // Normalized longitudinal parameter along parent curve [0..1]
        float arc;      // Botanical alias
    };
    union {
        float scale;    // Local host thickness / radius scale (metres)
        float stemRadius; // Botanical alias
    };
} VFX_Socket;

typedef struct VFX_Socket VFX_BotanicalSocket; // 100% binary & source compatible
```

### Invariants:
- `pos`: Must be in real-world meter coordinates ($1\text{ unit} = 1\text{ m}$).
- `normal` and `tangent`: Must be normalized unit vectors ($\|n\| = 1$, $\|t\| = 1$).
- `param`: Continuous from $0.0$ (root/source) to $1.0$ (tip/terminus).
- `scale`: Represents parent local radius. Child elements scale their attachment stems proportionally.

---

## 3. The Atomic VFX Standard (Chuẩn Thiết Kế VFX Đơn)

An **Atomic VFX** is a reusable micro-geometry or particle component. Examples: foliage leaves, flower blossoms, sparks, droplets, rock fragments, sword glints.

### 3.1. Mandatory Anatomy
Every atomic VFX must provide:
1. **Morphological Shape Enum** (`enum VFX_<Element><Name>Shape`): Defines geometric silhouettes (e.g. `WOOD_LEAF_SHAPE_OVAL`, `WOOD_LEAF_SHAPE_WILLOW`, `WOOD_LEAF_SHAPE_MAPLE`).
2. **Style / Palette Enum** (`enum VFX_<Element><Name>Style`): Defines material palettes, transmission tints, and subsurface scattering (e.g. `WOOD_VINE_STYLE_JADE_EMERALD`, `WOOD_VINE_STYLE_BLOOD_BRAMBLE`).
3. **Dual-State Contract (`bool attached`)**:
   - **Mode A: `attached == true`**: Anchored to parent `VFX_Socket` array. Position and orientation follow the host spine. Driven by host growth, flutter sway, and parent transformation.
   - **Mode B: `attached == false`**: Free physical airborne simulation. Emitted into the shared simulation pool (e.g. `VFX_FoliageSystem`). Integrates:
     - Real-world gravity: $\vec{g} = (0, -9.81, 0)\text{ m/s}^2$
     - Planar aerodynamic drag: $\vec{F}_d = -\frac{1}{2} \rho C_d A \|\vec{v}_{\text{rel}}\| \vec{v}_{\text{rel}}$
     - Environmental wind: `Wind_EvaluateAcceleration(pos, mass, area)`
     - Force fields / homing suction: `VFX_FoliageSystem_SetHomingTarget(...)`
     - Terrain collision & resting: `BOTANICAL_STATE_SETTLED` flush to ground plane.
4. **Normalized Dynamic Drivers**:
   - `growth` ($0.0 \to 1.0$): Emergence, sprouting, or bud-to-bloom expansion.
   - `wither` or `dissolve` ($0.0 \to 1.0$): Decaying, browning, petal shedding, or burning to ash.
   - `size`, `mass`, `initialVelocity`, `velocitySpread`, `seed`.

### 3.2. C Struct & API Blueprint
```c
typedef struct {
    bool  attached;               // true: anchored to host sockets; false: free physical airborne
    Vector3 origin;               // Center of spawn/distribution (metres)
    float radius;                 // Emission dispersion radius (metres)
    int   count;                  // Number of elements N (e.g. 16 to hundreds)
    float mass;                   // Mass in kg (e.g. 0.004kg for leaf, 0.002kg for petal)
    Vector3 initialVelocity;      // Ejection velocity vector in m/s
    float velocitySpread;         // Random scatter speed in m/s
    const VFX_Socket *sockets;    // Surface attachment sockets (when attached == true)
    int   socketCount;
    float growth;                 // Emergence / bloom progress [0..1]
    float wither;                 // Decay / wither progress [0..1]
    float swayAmp;                // Flutter sway under wind (metres)
    float size;                   // Scale in metres (e.g. 0.16m)
    VFX_<Element>Shape shape;     // Geometric morphology variant
    VFX_<Element>Style style;     // Elemental palette & contrast style
    unsigned int seed;            // Deterministic PRNG seed
} VFX_<Element><Name>Config;

VFX_<Element><Name>Config VFX_<Element><Name>_DefaultConfig(void);
const char*               VFX_<Element><Name>Shape_Name(VFX_<Element><Name>Shape shape);
void                      VFX_Compose<Element><Name>(const VFX_<Element><Name>Config *config);
```

---

## 4. The Composite VFX Standard (Chuẩn Thiết Kế VFX Tổ Hợp)

A **Composite VFX** is a macro visual structure that hosts child atomic components along its body. Examples: creeping vine, flowing water dragon, falling meteor, earth fissure, sword slash formation.

### 4.1. The 4-Phase Lifecycle Timeline
Every composite VFX follows a 4-phase timeline:

```
    Phase 1: SPROUT / GROWTH      Phase 2: MATURE STAND       Phase 3: ELEMENTAL REACTION       Phase 4: DISSOLVE
    (0.0s -> 1.6s)                (1.6s -> 2.6s)              (2.6s -> 5.0s)                   (5.0s -> 6.0s)
┌─────────────────────────────┬───────────────────────────┬─────────────────────────────────┬───────────────────────┐
│ Extends macro spine / path  │ Holds at 100% full length │ Water: foliage & flowers bloom  │ Smoothstep fade-out   │
│ Tapered growth emergence    │ Child elements static     │ Metal: severed cut & tip falls  │ Decays into ash/mist  │
│ (Inspect stem & bark)       │ (Developer visual check)  │ Fire: combustion & charcoal ash │ Resets cycle cleanly  │
└─────────────────────────────┴───────────────────────────┴─────────────────────────────────┴───────────────────────┘
```

> [!IMPORTANT]
> **Phase 2 (Mature Stand / Hold)** is mandatory in all sandbox test fixtures. It allows designers and developers to inspect core geometry, bark textures, and thorns before elemental reactions trigger.

### 4.2. Child Combinatorics (`VFX_<Name>Combo`)
A composite effect must never hardcode its child combination. Instead, it exposes a combination enum and child override fields:

```c
typedef enum {
    WOOD_VINE_COMBO_AUTO = 0,         // Auto: dynamic reaction & style driven
    WOOD_VINE_COMBO_BARE_STEM,        // Bare stem: bark only, no thorns/foliage
    WOOD_VINE_COMBO_THORNY_BRAMBLE,   // Bramble: bark + thorns
    WOOD_VINE_COMBO_LEAFY_TENDRIL,    // Leafy vine: bark + thorns + foliage leaves
    WOOD_VINE_COMBO_LOTUS_BLOOM,      // Sacred Lotus: foliage + Lotus flowers
    WOOD_VINE_COMBO_ORCHID_BLOOM,     // Celestial Orchid: foliage + Orchid flowers
    WOOD_VINE_COMBO_PLUM_BLOSSOM,     // Ironwood Plum: foliage + Plum Blossom flowers
    WOOD_VINE_COMBO_FULL_FLOURISH,    // Full flourish: twin + thorns + leaves + flowers
    WOOD_VINE_COMBO_WITHERED_AUTUMN,  // Withered autumn: withered browning foliage
    WOOD_VINE_COMBO_COUNT
} VFX_WoodVineCombo;

const char* VFX_WoodVineCombo_Name(VFX_WoodVineCombo combo);
```

### 4.2. Embedded Config Composition & The Composability Law
A composite effect must never flatten or duplicate child variables manually. Instead, it embeds the child configs directly:

```c
typedef struct VFX_WoodVineConfig {
    // Macro Geometry & Reaction Drivers [c1, c2, c3, c4...]
    Vector3 startPos, targetPos;
    float   length, baseRadius, growth, wither;
    VFX_WoodVineVariant   variant;
    VFX_WoodVineStyle     style;
    VFX_WoodReactionState reaction;
    VFX_WoodVineCombo     combo;
    bool enableThorns;
    bool enableTwin;
    bool castShadow;
    bool enableLeaves;
    bool enableFlowers;

    // Direct Embedded Child Configs [A, B]
    VFX_WoodLeavesConfig leaves; // Child A: Foliage Leaves
    VFX_WoodFlowerConfig flower; // Child B: Flower Blossoms
} VFX_WoodVineConfig;
```

#### The Parameter Composability Law:
$$\mathcal{P}(C) = \big[ c_1, c_2, \dots, c_m, \; a_1, a_2, \dots, a_n, \; b_1, b_2, \dots, b_k \big]$$

When introspecting parameters for Composite $C$, its `GetParams` function writes its own macro parameters, then directly appends the parameters of its children by passing the addresses of its embedded child configs:

```c
int VFX_WoodVine_GetParams(VFX_WoodVineConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    int n = 0;
    // 1. Macro Vine variables [c1..c7]
    outParams[n++] = (VFX_ParamDef){ .name = "Variant", .group = "Vine", .type = VFX_PARAM_ENUM, .valPtr = &cfg->variant, ... };
    outParams[n++] = (VFX_ParamDef){ .name = "Style",   .group = "Vine", .type = VFX_PARAM_ENUM, .valPtr = &cfg->style, ... };
    ...
    // 2. Child A (Leaves) embedded parameters [a1..a3]
    n += VFX_WoodLeaves_GetParams(&cfg->leaves, outParams + n, maxParams - n);

    // 3. Child B (Flower) embedded parameters [b1..b3]
    n += VFX_WoodFlower_GetParams(&cfg->flower, outParams + n, maxParams - n);

    return n;
}
```

### 4.3. Detachment & Physics Transfer
When a parent composite structure experiences an external event (severed by a blade, blasted by an explosion, or burned by fire):
1. Sockets located beyond the cut point (`socket.param > severArc`) are detached.
2. The system invokes `VFX_FoliageSystem_DetachInRadius(center, radius, impulse)`.
3. Detached elements transition to `BOTANICAL_STATE_FREE`, inheriting the parent's normal and tangent vectors as an ejection velocity impulse.

---

## 5. Universal Parameter Inspector & Sandbox Testing Protocol

To eliminate per-fixture hotkey clutter, all VFX test fixtures in [`sandbox/vfx_test.c`](../../sandbox/vfx_test.c) use the **Universal Parameter Inspector System** ([`core/composition/common/vc_params.h`](../composition/common/vc_params.h)):

```
┌────────────────────────────────────────────────────────────────────────┐
│ [WOOD VINE] PARAMETERS (CapsLock/Tab: Select Var | /: Cycle Value)     │
│ >> [Vine] Variant: SERPENTINE <<        [Leaves] Shape: OVAL           │
│    [Vine] Style: JADE EMERALD           [Leaves] Style: JADE EMERALD   │
│    [Vine] Reaction: WATER (GROWTH)      [Leaves] Mode: ATTACHED        │
│    [Vine] Thorns: ON                    [Flower] Type: SACRED LOTUS    │
│    [Vine] Twin Tendril: ON              [Flower] Style: JADE EMERALD   │
│    [Vine] Leaves Active: ON             [Flower] Mode: ATTACHED        │
│    [Vine] Flowers Active: ON                                           │
│ Controls: CapsLock/Tab: Select Var | / or >: Next Value | ,: Prev Value│
│ Actions: ; Burst | ' Homing (OFF) | \ Detach | V Pause (PLAYING)       │
└────────────────────────────────────────────────────────────────────────┘
```

### 5.1. Standardized Hotkey Contract:
| Input Key | Universal Action | Implementation |
|:---:|---|---|
| **`CapsLock`** (or **`Tab`**) | **Cycle Selected Variable** forward | `s_selectedParam = (s_selectedParam + 1) % s_paramCount;` |
| **`Shift + CapsLock`** / **`Shift + Tab`** | **Cycle Selected Variable** backward | `s_selectedParam = (s_selectedParam - 1 + s_paramCount) % s_paramCount;` |
| **`/` (Slash)** (or **`>`**) | **Cycle Value Forward** for active variable | `VFX_Param_CycleNext(&s_inspectorParams[s_selectedParam]);` |
| **`,` (Comma)** (or **`Shift + /`**) | **Cycle Value Backward** for active variable | `VFX_Param_CyclePrev(&s_inspectorParams[s_selectedParam]);` |
| **`V`** | **Pause / Resume** animation timeline | `s_vfxAnimationPaused = !s_vfxAnimationPaused;` |
| **`;` (Semicolon)** | Trigger physical particle burst | Spawns $N$ free instances into physics pool |
| **`'` (Apostrophe)** | Toggle suction / homing vortex | Engages `VFX_FoliageSystem_SetHomingTarget` |
| **`\` (Backslash)** | Detach attached instances in radius | Calls `DetachInRadius(center, radius, impulse)` |

---

## 6. Comprehensive Elemental VFX Mapping (Ngũ Hành + Thái Cực)

The table below defines how the Atomic & Composite standard maps to all Six Elements:

| Element | Composite VFX (Parent Host) | Atomic VFX (Children) | Sockets Exposed | Elemental Reactions |
|---|---|---|---|---|
| **Wood (Mộc)** | `WOOD VINE`<br>`WOOD BRAMBLE CAGE`<br>`WOOD DRAGON` | `WOOD LEAVES` (Oval, Willow, Maple)<br>`WOOD FLOWER` (Bud-to-bloom Lotus, Orchid, Plum; unattached falls as whole blossom head)<br>`WOOD PETALS` (Standalone free drifting petals)<br>`THORNS` | Surface bark normal, branch tangent, phyllotaxis | **Water**: Hydration bloom (leaves & flowers emerge)<br>**Metal**: Severed cut (tip drops under gravity)<br>**Fire**: Combustion (charcoal bark, ash embers) |
| **Water (Thủy)** | `WATER STREAM`<br>`WATER DRAGON`<br>`CROWN SPLASH`<br>`WATER ORB` | `WATER DROPLET` (Fine mist, splash bead)<br>`WATER FOAM RING`<br>`ICE NEEDLE / CRYSTAL` | Flow velocity tangent, stream surface normal, rim lip | **Earth**: Mud thickening (viscosity increases, flow stops)<br>**Fire**: Vaporization (steam clouds, droplets vanish)<br>**Cold/Metal**: Freezing (water turns to solid ice spikes) |
| **Fire (Hỏa)** | `FIREBALL / METEOR`<br>`INFERNO COLUMN`<br>`FIRE WHIRLWIND` | `FLAME TONGUE` (Rising wisp, curling plume)<br>`FIRE EMBER / SPARK` (Drifting ember)<br>`SMOKE PUFF` | Plume core normal, buoyancy updraft tangent | **Water**: Extinguish (steam hiss, dark soot)<br>**Wood**: Fuel surge (intensity & flame height double)<br>**Wind**: Drift & elongated fire stretch |
| **Earth (Thổ)** | `STONE PILLAR`<br>`EARTH FISSURE`<br>`ROLLING BOULDER` | `ROCK SHARD` (Slab, gravel, jagged block)<br>`DUST PUFF`<br>`SURFACE CRACK / DECAL` | Fissure fault edge, pillar cleavage plane | **Wood**: Root penetration (stone fractures, moss growth)<br>**Water**: Mud softening (earth turns to liquid slurry)<br>**Metal**: Spark strike on impact |
| **Metal (Kim)** | `SWORD SLASH ARC`<br>`SWORD RAIN FORMATION`<br>`AEGIS SHIELD` | `CONTACT SPARK` (Linear streak, starburst)<br>`SWORD GLINT / NEEDLE`<br>`SHRAPNEL SHARD` | Blade cutting edge, impact contact normal | **Fire**: Molten softening (metal glows red, sparks scatter)<br>**Earth**: Mineral resonance (metallic sheen intensifies)<br>**Water**: Quench hardening (steam hiss, sharp luster) |
| **Taiji (Thái Cực)** | `TAIJI BLACK HOLE`<br>`BAGUA DIAGRAM`<br>`INK DRAGON` | `YIN YANG ENERGY MOTE`<br>`INK WISP`<br>`REFRACTION RING` | Accretion disk orbit tangent, event horizon normal | **Harmony**: Balance of Yin (Dark) and Yang (Light)<br>**Chaos**: Gravitational suction collapses nearby particles |

---

## 7. Migration Guide: Converting Existing & Authoring New VFX

To migrate an existing monolithic effect or author a brand new effect according to this standard:

### Step 1: Extract Children into Atomic Composers
- Identify secondary elements (e.g. sparks along a blade slash, droplets trailing a water dragon, embers in a fire plume).
- Create a dedicated config struct `VFX_<Element><Child>Config` supporting `bool attached`, `VFX_Socket *sockets`, and physical airborne simulation.
- Declare `int VFX_<Element><Child>_GetParams(...)` in `visual_composer.h`.

### Step 2: Embed Child Configs into Parent Struct
- In the composite struct `VFX_<Element><Parent>Config`, embed the child structs directly:
  ```c
  typedef struct VFX_WaterDragonConfig {
      Vector3 startPos, targetPos;
      float speed, thickness;
      VFX_WaterDropletConfig droplets; // Embedded child A
      VFX_WaterFoamConfig    foam;     // Embedded child B
  } VFX_WaterDragonConfig;
  ```

### Step 3: Implement Parameter Introspection (`GetParams`)
- In `vc_<element>_<parent>.inl`, write `VFX_<Parent>_GetParams`:
  1. Populate composite macro variables (`VFX_PARAM_ENUM`, `VFX_PARAM_BOOL`, `VFX_PARAM_FLOAT`, `VFX_PARAM_INT`).
  2. Append child A via `VFX_<ChildA>_GetParams(&cfg->childA, outParams + n, maxParams - n)`.
  3. Append child B via `VFX_<ChildB>_GetParams(&cfg->childB, outParams + n, maxParams - n)`.

### Step 4: Register in Sandbox Parameter Inspector (`sandbox/vfx_test.c`)
- In `VFXTest_RefreshInspectorParams`:
  ```c
  else if (VFXTest_IsNewFxNamed("WATER DRAGON"))
  {
      s_inspectorParamCount = VFX_WaterDragon_GetParams(&s_liveWaterDragonConfig, s_inspectorParams, VFX_TEST_MAX_INSPECTOR_PARAMS);
  }
  ```
- No custom hotkey code is required! `CapsLock` and `/` immediately work for all child and parent parameters.

### Step 5: Verification Checklist
- [ ] Compiles with zero heap allocations in draw/update loops.
- [ ] Hotkeys `CapsLock` and `/` dynamically inspect and mutate all aggregated variables.
- [ ] Child components detach and simulate physically when severed or blasted.
- [ ] Passes bright-background contrast verification:
  ```bash
  scripts/render_vfx_matrix.sh "<FIXTURE NAME>" 40 90 140
  ```
  Score must satisfy: `darken% > 70%` and `structure > 0.40` on white and warm background plates.

---

## 8. Visual Quality & Botanical Discipline Laws

1. **Strict Prohibition on Autonomous Particles**: Agents are strictly forbidden from unilaterally adding extraneous particle emitters (spores, embers, motes) into VFX unless explicitly approved by the user. Effects must achieve visual richness and punch through procedural geometry, vertex lighting, and shaders.
2. **Morphological Bud-to-Bloom & Blossom Integrity**: A blooming flower VFX must visibly start as a closed bud (nụ hoa) cradled by calyx sepals and unfold smoothly into full blossom with an expanding incandescent stamen core. If not attached to a host, it drops as an intact whole blossom head (`BOTANICAL_KIND_FLOWER_HEAD`), preserving gravity and aerodynamic tumble physics. Individual petals belong to a separate standalone effect (`VFX_ComposeWoodPetals`).
3. **Default Spiritual Luminescence (Dạng phát sáng, phép thuật)**: All botanical elements in VFX possess default spiritual luminescence (`eL = 0.50f * dL + 0.50f`) so that midribs, veins, petal edges, and stamen centers emit a celestial glow even in shadows and nighttime arenas.
