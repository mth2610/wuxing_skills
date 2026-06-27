# WUXING ART DIRECTION — CONDENSED AI REFERENCE
> Compact guidelines on timing, layering, elements styling, and aesthetic standards for skills in the Wuxing engine.

---

## 1. DYNAMIC SKILL LIFECYCLE (THE STORY OF ENERGY)
Every skill must have a clear beginning, middle, and end. Never spawn or destroy effects instantly:
1. **Birth:** Tiny glows, minor air distortion, small rising sparks. Never reveal full scale immediately.
2. **Gather:** Energy concentrates. Spawning rate, density, and light brightness increase.
3. **Tension:** Movement briefly compresses/decelerates, rotation speeds up. Creates anticipation.
4. **Release:** Sharp forward acceleration. Clear direction, high velocity.
5. **Impact:** Short, high-contrast visual peak (screen flash, shockwave, decals, light, particle burst).
6. **Aftermath:** Visual consequences (smoke, settling debris, residual sparks crawling, fading water caustics).
7. **Fade:** Smooth opacity and scale decay. Never abruptly delete active elements.

---

## 2. THE SIX-LAYER COMPOSITION
Never rely on a single giant effect. Build visuals using these independent layers:
* **Core Layer:** The physical projectile/structure mesh. Defines elemental identity. Must always remain visible.
* **Motion Layer:** Speed visualizers (ribbons, trails, motion-blur meshes, velocity-aligned particles).
* **Energy Layer:** Surrounding magical power (flames, floating embers, lightning arcs, mist, glowing particles).
* **Accent Layer:** Eye catchers (tiny bright spark particles, lens flashes, micro-shocks).
* **Atmosphere Layer:** Environmental context (lingering smoke, dust clouds, fog, rising heat distortion).
* **Environment Layer:** Reactions (ground crack decals, screen distortion shockwaves, dynamic lights, camera shake).

---

## 3. VISUAL HIERARCHY & READABILITY
Maintain distinct visual importance values in every frame:
1. **Primary (Focus):** The active gameplay element (projectile path, warning area). Must be highly readable.
2. **Secondary:** Supporting motion indicators (trails, orbits, gathering energy). Guides the player's eyes.
3. **Tertiary (Background):** Atmosphere effects (ground decals, dust clouds, ambient fog). Must never obscure gameplay.

---

## 4. PARTICLE, TRAIL, & LIGHTING GUIDELINES
* **Size & Lifetime Variation:** Never use uniform sizes/lifetimes. Mix massive, medium, and tiny particles. Give different groups distinct lifetimes (e.g. spark = 0.3s, smoke = 2.0s).
* **Particle Breathing:** Particle emission rates must oscillate (Few -> Dense -> Fading) rather than emitting at a flat continuous rate.
* **Color Transitions:** Particles must morph during their lifetime (e.g. White-hot Core -> Saturated Element color -> Dark Ash/Transparent).
* **Lighting:** Point light intensity must breathe (Smooth Fade-in -> Sudden Peak -> Slow Fade-out).

---

## 5. ELEMENT SPECIFIC ART RULES
| Element | Base Color | Primary Force Fields | Core VFX Visuals | World Reactions |
|---|---|---|---|---|
| **Water** | `ELEMENT_COLOR_WATER` | `FORCE_VORTEX` + `FORCE_VISCOSITY` + `FORCE_NOISE_CURL` | Swirling liquid tubes, mist trails, refraction shaders | Decal pools, caustics, splash rings |
| **Wood** | `ELEMENT_COLOR_WOOD` | `FORCE_NOISE_CURL` + `FORCE_WIND` + `FORCE_GRAVITY_POINT` (to target) | Twisting gnarled roots, mossy bark, drifting leaves | Ground cracks, dust bursts, plant shoots |
| **Fire** | `ELEMENT_COLOR_FIRE` | `FORCE_RADIAL_AXIS` + `FORCE_WIND` + `FORCE_NOISE_PERLIN` | Flame plumes, white-hot heat cores, rising embers | Ground ash marks, heat haze distortion |
| **Earth** | `ELEMENT_COLOR_EARTH` | `FORCE_GRAVITY_DIR` (down) + `FORCE_DRAG` + `FORCE_VISCOSITY` | Jagged stone pillars, flying dirt blocks, dust waves | Fractured rock decals, heavy shockwaves |
| **Metal** | `ELEMENT_COLOR_METAL` | `FORCE_DRAG` + `FORCE_WIND` + `FORCE_NOISE_PERLIN` | Flying swords, sharp ribbon blade trails, electric sparks | Scrapes/scratches, metallic glare flashes |
| **Taiji** | `ELEMENT_COLOR_TAIJI` | `FORCE_GRAVITY_POINT` (pull/push) + `FORCE_VORTEX_AXIS` | Yin-yang balance swirls, crawling lightning bolts | Circular array decals, space distortion rings |

---

## 6. SCREEN DISTORTION & CAMERA SHAKE LAWS
* **Camera Shake:** Reserve strictly for massive impact peaks (landings, explosions). Small or flowing spells (like water streams) must **never** shake the camera. Keep shake duration short and sharp.
* **Screen Distortion:** Use for massive energy releases (explosions, portals, shockwaves). Forbidden on minor projectiles.

---

## 7. AI QUALITY CHECKLIST
* **readability:** Is the spell trajectory/danger zone immediately clear?
* **contrast:** Does the spell feature fast vs. slow and bright vs. dark rhythm contrast?
* **layering:** Are there at least 3 distinct layers active?
* **organic:** Does motion have secondary curves/noise jitter to prevent robotic straight lines?
* **consequence:** Does the impact leave a realistic ground mark and lingering dust/smoke aftermath?
