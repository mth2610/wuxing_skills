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

Inside the composite configuration:
```c
typedef struct {
    // Macro Geometry
    Vector3 startPos, targetPos;
    float   length, baseRadius, growth, wither;
    
    // Child Toggles & Combination Presets
    bool               enableThorns;
    bool               enableTwin;
    bool               enableLeaves;
    bool               enableFlowers;
    VFX_WoodVineCombo  combo;      // Preset combination
    VFX_WoodLeafShape  leafShape;  // Child leaf shape override
    VFX_WoodFlowerType flowerType; // Child flower type override
    
    // Elemental Reactions & Physics
    float waterFactor;  // Thủy sinh Mộc: Hydration bloom
    float severArc;     // Kim khắc Mộc: Severed cut position [0..1]
    float fireFactor;   // Hỏa thiêu Mộc: Combustion progress [0..1]
    
    VFX_WoodVineVariant variant;
    VFX_WoodVineStyle   style;
    unsigned int seed;
} VFX_WoodVineConfig;
```

### 4.3. Detachment & Physics Transfer
When a parent composite structure experiences an external event (severed by a blade, blasted by an explosion, or burned by fire):
1. Sockets located beyond the cut point (`socket.param > severArc`) are detached.
2. The system invokes `VFX_FoliageSystem_DetachInRadius(center, radius, impulse)`.
3. Detached elements transition to `BOTANICAL_STATE_FREE`, inheriting the parent's normal and tangent vectors as an ejection velocity impulse.

---

## 5. Universal Testing Protocol & Sandbox Controls

To ensure testing combinations and swapping variants is effortless, all VFX test fixtures in [`sandbox/vfx_test.c`](../../sandbox/vfx_test.c) adhere to a **unified hotkey contract**:

| Key | Atomic VFX Action | Composite VFX Action |
|:---:|---|---|
| `,` / `.` | Cycle morphology shape (`shape` / `type`) | Cycle macro spine variant (`variant`) |
| `M` | Cycle elemental style / palette (`style`) | Cycle elemental style / palette (`style`) |
| `[` / `]` | *(N/A)* | **Cycle child combination preset (`combo`)** |
| `/` | **Toggle Dual-Mode (`ATTACHED` vs `FREE`)** | **Cycle elemental reaction state (`reaction`)** |
| `;` | Trigger physical burst of $N$ free instances | Trigger physical burst of $N$ free instances |
| `'` | Toggle force field / homing vortex | Toggle force field / homing vortex |
| `\` | Detach attached instances in radius | Detach attached instances in radius |
| `V` | **Pause / resume animation timeline** | **Pause / resume animation timeline** |

### HUD Standard:
The fixture HUD must render informative status lines:
1. **Line 1 (Identity)**: `[NAME]: [Variant] | Style: [Style] | Combo: [Combo] | Mode/Reaction: [State]`
2. **Line 2 (Controls)**: `CONTROLS: >/, var | M style | [ / ] child combo | / rxn | ; burst | ' vortex | V pause`
3. **Line 3 (Active Simulation)**: `[System]: N active | Mass: X.XXXkg | Homing: [ACTIVE/OFF]`

---

## 6. Comprehensive Elemental VFX Mapping (Ngũ Hành + Thái Cực)

The table below defines how the Atomic & Composite standard maps to all Six Elements:

| Element | Composite VFX (Parent Host) | Atomic VFX (Children) | Sockets Exposed | Elemental Reactions |
|---|---|---|---|---|
| **Wood (Mộc)** | `WOOD VINE`<br>`WOOD BRAMBLE CAGE`<br>`WOOD DRAGON` | `WOOD LEAVES` (Oval, Willow, Maple)<br>`WOOD FLOWER` (Lotus, Orchid, Plum)<br>`THORNS`, `SPORES` | Surface bark normal, branch tangent, phyllotaxis | **Water**: Hydration bloom (leaves & flowers emerge)<br>**Metal**: Severed cut (tip drops under gravity)<br>**Fire**: Combustion (charcoal bark, ash embers) |
| **Water (Thủy)** | `WATER STREAM`<br>`WATER DRAGON`<br>`CROWN SPLASH`<br>`WATER ORB` | `WATER DROPLET` (Fine mist, splash bead)<br>`WATER FOAM RING`<br>`ICE NEEDLE / CRYSTAL` | Flow velocity tangent, stream surface normal, rim lip | **Earth**: Mud thickening (viscosity increases, flow stops)<br>**Fire**: Vaporization (steam clouds, droplets vanish)<br>**Cold/Metal**: Freezing (water turns to solid ice spikes) |
| **Fire (Hỏa)** | `FIREBALL / METEOR`<br>`INFERNO COLUMN`<br>`FIRE WHIRLWIND` | `FLAME TONGUE` (Rising wisp, curling plume)<br>`FIRE EMBER / SPARK` (Drifting ember)<br>`SMOKE PUFF` | Plume core normal, buoyancy updraft tangent | **Water**: Extinguish (steam hiss, dark soot)<br>**Wood**: Fuel surge (intensity & flame height double)<br>**Wind**: Drift & elongated fire stretch |
| **Earth (Thổ)** | `STONE PILLAR`<br>`EARTH FISSURE`<br>`ROLLING BOULDER` | `ROCK SHARD` (Slab, gravel, jagged block)<br>`DUST PUFF`<br>`SURFACE CRACK / DECAL` | Fissure fault edge, pillar cleavage plane | **Wood**: Root penetration (stone fractures, moss growth)<br>**Water**: Mud softening (earth turns to liquid slurry)<br>**Metal**: Spark strike on impact |
| **Metal (Kim)** | `SWORD SLASH ARC`<br>`SWORD RAIN FORMATION`<br>`AEGIS SHIELD` | `CONTACT SPARK` (Linear streak, starburst)<br>`SWORD GLINT / NEEDLE`<br>`SHRAPNEL SHARD` | Blade cutting edge, impact contact normal | **Fire**: Molten softening (metal glows red, sparks scatter)<br>**Earth**: Mineral resonance (metallic sheen intensifies)<br>**Water**: Quench hardening (steam hiss, sharp luster) |
| **Taiji (Thái Cực)** | `TAIJI BLACK HOLE`<br>`BAGUA DIAGRAM`<br>`INK DRAGON` | `YIN YANG ENERGY MOTE`<br>`INK WISP`<br>`REFRACTION RING` | Accretion disk orbit tangent, event horizon normal | **Harmony**: Balance of Yin (Dark) and Yang (Light)<br>**Chaos**: Gravitational suction collapses nearby particles |

---

## 7. Migration Roadmap for Existing VFX

To migrate existing monolithic effects to the new standard without breaking gameplay skills:

### Migration Steps:
1. **Phase 1: Abstract Children into Atomic Composers**
   - Identify embedded particles, sparks, or secondary sprites in the monolithic `.inl`.
   - Extract them into a standalone atomic composer (`VFX_Compose<Element><Child>`) with `attached` vs `free` dual-mode support.
2. **Phase 2: Introduce `VFX_Socket` Sampling**
   - Replace hardcoded coordinate math in the parent with procedural socket generation along the spine/mesh surface.
3. **Phase 3: Implement Combination Presets (`VFX_<Parent>Combo`)**
   - Add the `combo` enum to the parent config and delegate child rendering to the extracted atomic composers.
4. **Phase 4: Wire Sandbox Test Fixtures**
   - Register the atomic children as standalone fixtures in `scripts/sync_vfx_test.py` and `sandbox/vfx_test.c`.
   - Wire standard hotkeys (`,`, `.`, `M`, `/`, `[`, `]`, `;`, `'`, `V`).
5. **Phase 5: Measure Bright-Background Matrix**
   - Execute `scripts/render_vfx_matrix.sh "<FIXTURE NAME>" 40 90 140` to guarantee high darken% (>80%) on pure white backgrounds before closing the task.
