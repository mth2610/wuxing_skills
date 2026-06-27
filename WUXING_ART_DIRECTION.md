# WUXING ART DIRECTION — CONDENSED REFERENCE

> **This document defines the artistic philosophy for every skill in the Wuxing engine.**
> The Core API explains *how* to build a skill. This document explains *how to make it beautiful.*
> Every AI generating code for this engine MUST follow these principles before writing a single line of code.

---

## CHAPTER 1 — CORE PHILOSOPHY

### 1.1 Design Goal

The goal of every skill is not to display as many particles as possible. It is to immediately communicate **power, weight, speed, elemental identity, intention, and emotional impact**. A beautiful skill is remembered because it tells a visual story. Every visual decision must support gameplay readability while maximizing spectacle. Never create effects whose only purpose is "looking cool." Everything must have intention.

### 1.2 The Three Questions (Answer Before Every Effect)

**Q1 — Where does the energy come from?**
The source determines how particles begin moving. Examples: inside the caster, underground, the sky, a summoned weapon, around the target, another dimension.

**Q2 — Where does the energy go?**
Effects must always have a clear direction. Never create energy that appears from nowhere and disappears without purpose. Examples: into the enemy, around the player, into a projectile, across the battlefield.

**Q3 — What remains after the energy leaves?**
The aftermath makes a spell feel believable. Examples: smoke, dust, floating sparks, burn marks, cracks, fog, residual light, slowly fading particles.

### 1.3 Every Skill Tells a Story

A skill is a short movie. Think in terms of narrative, never in terms of particles. Every skill must have a **beginning, middle, and end**:

```
Birth → Gather → Tension → Release → Impact → Aftermath → Fade
```

- **Birth**: Small glow, tiny sparks, air distortion, dust moving. Creates curiosity. Never reveal full power.
- **Gather**: Energy concentrates, visual density increases, particles converge, lighting grows. Audience should feel: "Something powerful is about to happen."
- **Tension**: Movement slows, compression increases, particles rotate faster, light intensifies. Creates anticipation — without anticipation, impact feels weak.
- **Release**: Fast acceleration, clear direction, strong contrast. Audience knows immediately: "The attack has begun."
- **Impact**: The emotional peak — camera shake, flash, screen distortion, light burst, decals, sparks, shockwave. Lasts only a brief moment. Never allow the peak to continue long.
- **Aftermath**: Power never disappears instantly. Smoke rises, fragments settle, residual electricity crawls, water ripples, leaves drift. Gives weight to the attack.
- **Fade**: Everything slowly returns to normal. Brightness and motion decrease. Never abruptly destroy every particle.

### 1.4 Beauty Through Contrast

The eye notices *change*, not intensity. Contrast is more important than brightness.

- **Bright vs Dark**: Dark → Bright Flash → Dark. The flash becomes much stronger.
- **Fast vs Slow**: Combine slow preparation + instant attack + slow aftermath.
- **Large vs Small**: Mix huge structures, medium debris, tiny sparks, microscopic dust. This creates richness.
- **Dense vs Empty**: Leave empty space. Do not fill the entire screen with particles.
- **Silence vs Explosion**: A brief pause before impact often feels stronger than adding more particles.

### 1.5 Readability Before Spectacle

The player must always understand where the attack is, where danger is, where the caster is, and what direction the attack travels. Beautiful effects that hide gameplay are failures.

### 1.6 Motion Has Meaning

Every movement communicates emotion:
- **Straight**: disciplined, sharp, precise
- **Curved**: elegant, alive, natural
- **Spiral**: mystical, magical, unstable
- **Chaotic**: violent, explosive, uncontrollable
- **Floating**: divine, spiritual, peaceful

Always choose motion based on emotion. Never choose randomness without purpose.

### 1.7 The World Should React

Magic affects more than the projectile. Nearby air, ground, light, particles, smoke, enemies, and camera all react. The environment must acknowledge powerful events to create immersion.

### 1.8 Less Is Often More

Adding more particles rarely improves quality. Instead improve: timing, composition, layering, gradients, lighting, motion, silhouettes. Ten meaningful effects are better than one hundred random effects.

### 1.9 Consistency

Every skill belongs to the same universe. Players should immediately recognize "This belongs to Wuxing." Consistency is achieved through shared timing philosophy, layering philosophy, motion quality, visual hierarchy, post-processing style, and lighting philosophy.

### 1.10 Golden Rules (Override Every Other Artistic Decision)

1. Every skill tells a story.
2. Energy always has a source.
3. Energy always has a destination.
4. Power leaves consequences.
5. Motion always communicates emotion.
6. Contrast creates beauty.
7. The player must always understand what is happening.
8. Large effects are built from many simple layers — never rely on one giant effect.
9. Every visual decision must have purpose. Never add particles "just because."
10. When in doubt, choose elegance over complexity.

---

## CHAPTER 2 — UNIVERSAL VFX LANGUAGE

### 2.1 The Layer Philosophy

Every beautiful skill is built from multiple simple layers. Think like a painter — each layer has exactly one purpose:

```
Core Layer       → spell identity (if everything else disappears, attack is still recognizable)
Motion Layer     → speed (ribbons, trails, motion blur, velocity particles)
Energy Layer     → magical power (glow, sparks, arcs, dust, flames, mist — wraps core, never hides it)
Accent Layer     → attention (tiny bright particles, lens flashes — occupies very little screen space)
Atmosphere Layer → immersion (smoke, dust, fog, heat haze, ash — moves slowly, survives after attack ends)
Environment Layer→ world reaction (cracks, decals, shockwaves, dynamic lights, camera shake — expensive, use only at important moments)
```

### 2.2 Visual Hierarchy

Every frame should answer "What should the player look at first?" Use three levels:
- **Primary**: The attack itself. Must always be readable.
- **Secondary**: Supporting motion — trails, particles, energy. Guide the player's eye.
- **Tertiary**: Background atmosphere — smoke, dust, fog, ground effects. Must never compete with Primary.

### 2.3 Particle Philosophy

Each particle must have a role: energy, debris, smoke, sparks, ash, leaves, mist, dust, embers. Never spawn particles without purpose.

- **Lifetime**: Different particles die at different times. Flash ~0.05s, Spark ~0.3s, Energy ~0.8s, Smoke ~2.5s. Uniform lifetime feels artificial.
- **Scale**: Always mix sizes — huge, large, medium, small, tiny. Avoid identical particle sizes.
- **Density**: Must vary over time. Bad: 50 particles every frame. Good: Few → Many → Few. Visual breathing feels natural.

### 2.4 Trail Philosophy

Trails describe movement — where something came from, where it goes, how fast it moves. Good trail: clear direction, consistent thickness, soft fade, natural curvature. Bad trail: perfectly straight, uniform brightness, uniform width, no fading, no movement. Width should breathe. Long trails imply elegance; short trails imply speed.

### 2.5 Ribbon Philosophy

Ribbon represents flowing energy. Never appear rigid. Use for: magic, wind, silk, flowing water, spiritual energy. Avoid for heavy objects.

### 2.6 Force Philosophy

ForceField is not physics — it is emotion. Every force supports the intended feeling:
- **Gravity**: compression, weight, falling, collapse
- **Point Gravity**: gathering, charging, absorption, meditation
- **Vortex**: mystery, magic, swirling energy, spiritual movement
- **Curl Noise**: life, organic movement, natural chaos
- **Drag**: weight, resistance, density
- **Wind**: freedom, speed, flow

Never rely on a single force. Combine: Point Gravity + Curl Noise + Drag = feels alive. Wind + Noise + Vortex = feels magical.

### 2.7 Motion Language

Every moving object must have three levels:
- **Primary**: gameplay movement (e.g., forward)
- **Secondary**: supporting (e.g., gentle spiral)
- **Micro**: procedural life (e.g., noise wobble)

This combination prevents robotic motion.

### 2.8 Color Philosophy

Every effect should include Highlight → Midtone → Shadow → Fade. A single flat color appears lifeless. Prefer gradients over solid colors. Gradients communicate temperature, age, energy, decay. A particle should evolve during its lifetime.

### 2.9 Lighting Philosophy

Light must breathe: Fade In → Peak → Fade Out. The strongest light exists only briefly. Constant brightness feels fake.

### 2.10 Flow Map Philosophy

Flow maps represent internal energy — movement inside an object. Use for: energy blades, magic circles, portals, fire, water, spirit weapons. Avoid on rigid stone or metal unless magic is actively flowing.

### 2.11 Camera Philosophy

Camera movement is punctuation, not background noise. Shake only during: impact, landing, explosion, massive collision. Small attacks require no shake. Strong attacks: short, sharp shake. Never shake continuously.

### 2.12 Screen Distortion

Represents intense energy. Appropriate: explosion, portal, meteor, massive fire, space distortion. Forbidden: tiny sparks, normal projectiles, weak melee attacks. Distortion should remain rare — rare effects feel powerful.

### 2.13 Environmental Response

Possible world reactions: ground dust, leaves blown away, water ripples, snow displacement, burn marks, ice crystals, shockwaves, dynamic lights, residual smoke. Not every attack requires every reaction — choose only what strengthens the illusion.

### 2.14 Timing

Beautiful effects have rhythm: Birth → Build → Peak → Recovery. Never remain at maximum intensity. Variation creates drama.

### 2.15 Silence

Moments with fewer particles allow stronger moments to shine. Do not fear empty space or slow movement. The audience needs time to perceive beauty.

### 2.16 The Five-Layer Rule

Every major skill should contain at least five visual categories: Main Body, Motion, Energy, Atmosphere, Environment. Missing one usually makes a skill feel incomplete.

### 2.17 AI Quality Checklist (Verify Before Writing Code)

- Does the skill have a clear visual focus?
- Does every layer have a unique purpose?
- Does energy visibly flow from source to destination?
- Are particles varied in size, lifetime, and speed?
- Does motion include primary, secondary, and micro movement?
- Are gradients used instead of flat colors?
- Is lighting animated instead of constant?
- Does the environment react appropriately?
- Is gameplay readability preserved?
- Does the effect leave a believable aftermath?

If any answer is **No**, improve the design before writing code.

---

## CHAPTER 3 — THE LANGUAGE OF THE SIX STATES

> Wuxing elements differ not only in color but in **personality, motion language, visual rhythm, energy behavior, and physical feeling**. If a player watches a skill in pure grayscale, they should still identify its element from movement alone.

### The Six States: Metal · Wood · Water · Fire · Earth · Taiji

**Taiji is not an element.** It is the state of primordial energy before the separation of Yin and Yang. Electricity belongs to Taiji — unstable equilibrium, polarity, transformation, instantaneous release. Electric effects MUST NOT behave like Fire.

---

### 3.1 METAL — *Precision*

Every movement feels deliberate. Every strike feels inevitable.

**Emotion**: sharp, cold, clean, elegant, ruthless, disciplined

**Shape**: straight lines, long blades, needles, spears, thin shards, hexagonal patterns, symmetry. *Avoid*: blobs, organic curves, soft smoke, random splashes.

**Motion**: Primary = linear. Secondary = very small spiral. Micro = tiny vibration. Metal almost never drifts — it commits.

**Rhythm**: Silence → Instant movement → Silence. Very little anticipation, almost no lingering. Everything is decisive.

**Forces**: Prefer Drag, Wind, Directional Gravity. Avoid strong Curl Noise, heavy Vortex. Metal resists chaos.

**Particles**: bright sparks, tiny fragments, blade trails, thin ribbons, flying chips. *Avoid*: thick smoke, puffy particles.

**Lighting**: small, bright, focused, needle-like. **Camera**: minimal shake.

**Keywords**: Cut, Slice, Pierce, Split, Reflect, Polish, Discipline

---

### 3.2 WOOD — *Life*

Nothing grows instantly. Everything expands naturally.

**Emotion**: growth, harmony, patience, nature, persistence

**Shape**: roots, branches, leaves, flowers, vines, moss, bark. *Avoid*: perfect geometry.

**Motion**: always organic — nothing moves perfectly straight. Everything bends. Everything seeks life.

**Rhythm**: Slow → Growing → Bloom → Spread

**Forces**: Prefer Curl Noise, Wind, Weak Point Gravity. Wood follows natural flow.

**Particles**: leaves, seeds, spores, petals, tiny pollen. *Never*: metallic sparks.

**Lighting**: soft, diffuse, warm, natural. **Camera**: almost none. Growth is calm.

**Keywords**: Grow, Bloom, Wrap, Spread, Root, Heal, Expand

---

### 3.3 WATER — *Adaptation*

Water never fights directly. It surrounds, flows, erodes.

**Emotion**: calm, elegant, endless, flexible, graceful

**Shape**: curves, waves, streams, drops, mist, foam. *Never*: rigid polygons.

**Motion**: continuous, smooth, no sudden stops. Acceleration feels fluid.

**Rhythm**: Flow → Flow → Flow → Crash → Flow

**Forces**: Prefer Vortex, Curl Noise, Viscosity, Wind. Water should never look static.

**Particles**: mist, droplets, foam, ripples, splashes, tiny bubbles.

**Lighting**: soft blue highlights, reflections, transparency. **Camera**: only during large impacts.

**Keywords**: Flow, Wash, Encircle, Pull, Ripple, Wave, Current

---

### 3.4 FIRE — *Consumption*

Everything about Fire wants to become larger.

**Emotion**: aggressive, violent, passionate, explosive, unstable

**Shape**: flames, explosions, jagged tongues, ash, smoke, shockwaves.

**Motion**: expansion, burst, collapse — everything pushes outward.

**Rhythm**: Charge → Explosion → Ash → Smoke

**Forces**: Prefer Radial, Wind, Noise. *Avoid*: long-lasting stability.

**Particles**: flames, embers, ash, smoke, burning fragments.

**Lighting**: largest of all elements, brightest peak, fast fade. **Camera**: strong impact, strong distortion.

**Keywords**: Ignite, Burn, Consume, Explode, Scorch, Melt

---

### 3.5 EARTH — *Weight*

Earth does not rush. Earth moves because nothing can stop it.

**Emotion**: heavy, ancient, stable, massive, dominant

**Shape**: rocks, mountains, pillars, crystal, sand, dust, fragments.

**Motion**: slow acceleration, huge momentum, very difficult to redirect.

**Rhythm**: Stillness → Lift → Crash → Dust → Silence

**Forces**: Prefer Gravity, Drag, Viscosity. *Avoid*: high frequency noise.

**Particles**: dust, pebbles, rock fragments, sand, debris.

**Lighting**: low intensity, large area, warm neutral colors. **Camera**: strongest shake among all elements — weight should be felt physically.

**Keywords**: Crush, Rise, Collapse, Fortify, Anchor, Shatter

---

### 3.6 TAIJI — *Primordial Balance*

Taiji exists before the five elements. Electricity is merely one visible manifestation of Taiji energy. Do not think of Electric as "blue fire" — think of it as unstable balance searching for equilibrium.

**Emotion**: divine, mysterious, intelligent, unpredictable, transcendent

**Shape**: circles, Yin-Yang rotation, arcs, fractures, concentric rings, floating geometry, energy veins. *Avoid*: natural shapes, smoke-heavy visuals.

**Motion**: alternates between absolute stillness and instant movement. Movement is discontinuous — it appears, disappears, reconnects elsewhere.

**Rhythm**: Stillness → Compression → Flash → Silence → Flash → Silence. Taiji values interruption more than continuity.

**Forces**: Point Gravity + Vortex + Curl Noise + Wind. The resulting motion should feel *intelligent* rather than random.

**Particles**: electric arcs, energy filaments, floating symbols, tiny plasma sparks, orbiting particles, glowing dust. *Never*: thick smoke.

**Lighting**: highest contrast, extremely bright flashes, very short duration, frequent light pulses.

**Camera**: minimal movement. Instead of violent shaking, use screen distortion, bloom, chromatic aberration, light pulses. Reality itself appears unstable.

**Electric specifics**: Electricity is the consequence, not the source. Arcs jump between nearby surfaces, connections constantly reorganize, lightning never travels in a perfectly straight beam — every arc searches for the next destination.

**Yin**: slow, cold, contracting, dark, absorbing. **Yang**: expanding, radiant, explosive, ascending, warm.

**Taiji Principle**: Every Taiji skill must contain both Compression and Expansion, Stillness and Movement, Light and Shadow, Order and Chaos. Without duality, Taiji loses its identity.

---

### 3.7 Cross-Element Rules

Each element remains recognizable even if all colors are removed. Identity comes from: motion, timing, shape, force, rhythm, and layer composition — not from hue alone.

### 3.8 AI Decision Matrix

| Element | Motion      | Rhythm      | Forces                       | Density    | Aftermath       |
|---------|-------------|-------------|------------------------------|------------|-----------------|
| Metal   | Linear      | Instant     | Drag + Direction             | Low        | Sparks          |
| Wood    | Organic     | Growing     | Curl + Wind                  | Medium     | Leaves          |
| Water   | Flowing     | Continuous  | Vortex + Viscosity           | Medium     | Mist            |
| Fire    | Expanding   | Burst       | Radial + Noise               | High       | Smoke           |
| Earth   | Heavy       | Slow Impact | Gravity + Drag               | High       | Dust            |
| Taiji   | Alternating | Pulse       | Mixed Intelligent Forces     | Low–Medium | Residual Energy |

**Final Principle**: The player should never think "That is a blue Fire skill." They should immediately recognize "That is Taiji." Every state speaks its own visual language through motion, force, timing, and shape — color is only the final accent.

---

## CHAPTER 4 — SKILL TIMELINE DESIGN

> The quality of a skill depends more on **timing** than on particle count. A good timeline creates expectation. A great timeline creates emotion.

### 4.1 The Standard Timeline

```
Birth → Charge → Compression → Release → Travel (optional) → Impact → Expansion → Aftermath → Fade
```

Not every skill requires every phase, but removing too many makes the effect feel abrupt and artificial.

### 4.2 Phase Details

**Birth** (0.05–0.20s): Tell the player "Something has begun." Never visually loud. Examples: small glow, tiny sparks, floating dust, air distortion, weapon awakening. Creates curiosity. Never reveal full power immediately.

**Charge** (0.2–1.5s): Energy begins gathering. Particles converge, glow increases, rotation accelerates, trails lengthen, lighting intensifies. Player subconsciously predicts: "The attack is becoming dangerous."

**Compression** (most forgotten phase): The final inhale. Everything becomes smaller, denser, brighter, faster. Energy: Large sphere → Medium → Small → Flash. Particles: Wide → Tight → Very tight. Camera: almost still, no shake yet. Compression dramatically increases the perceived strength of Release.

**Release** (1–4 frames): Must feel instantaneous. Maximum acceleration, bright flash, clear direction, high contrast. Never allow Release to linger.

**Travel** (projectiles only): Communicates direction, speed, weight. Must never feel static. Long travel (flying swords, dragons, meteors): elegant motion, beautiful trails, flowing particles, micro motion. Short travel (dash attacks, lightning): explosive, very little lingering.

**Impact** (emotional climax): Possible layers: flash, explosion, sparks, camera shake, dynamic light, distortion, ground decal, shockwave. Never introduce new visual ideas — impact is the culmination of everything established earlier. Perception order: Flash → Shape → Motion → Particles → Smoke.

**Expansion**: After impact, energy expands — explosion, shockwave, ripples, flames, branches, water rings, dust clouds. Releases accumulated tension.

**Aftermath**: Power leaves traces — smoke, fog, residual lightning, ash, floating embers, leaves, mist, ground glow. Very slow. Players should have time to appreciate it.

**Fade**: Every visual layer disappears independently. Never destroy everything simultaneously. Example: Flash 0.05s → Light 0.3s → Energy 0.7s → Smoke 2.5s → Mist 3.5s. Staggered fade creates realism.

### 4.3 The Four Curves

**Intensity curve**: Birth (low) → Charge (building) → Peak (100%) → Aftermath (falling) → Fade (zero). Never keep constant.

**Density curve**: Bad: `████████████████`. Good: `▂▄▆█▇▅▃▂`. Density changes create rhythm.

**Motion curve**: Birth (almost still) → Charge (slow rotation) → Release (maximum speed) → Impact (explosion) → Aftermath (slow drift) → Fade (almost motionless).

**Lighting curve**: Small glow → Brighter → Blinding flash → Soft illumination → Darkness. Players remember flashes, not constant brightness.

### 4.4 Layer Activation Timeline (Stagger Layers)

```
Core Body        ██████████████
Energy             ███████████
Trail               █████████
Flash                    █
Light               ████████
Smoke                    ███████████
Dust                      ██████████
Decal                     ███████
Distortion                 ██
```

Layer staggering greatly improves realism.

### 4.5 API Mapping by Phase

| Phase       | API Systems                                              |
|-------------|----------------------------------------------------------|
| Birth       | Small SpawnParticle, Weak Emitter, Light begins          |
| Charge      | ForceField, Ribbon, Trail, Gradient, Flow Map            |
| Compression | Increase ForceField, tighten emission, increase glow     |
| Release     | Spawn projectile, Trail, Flash, Dynamic light            |
| Travel      | Trail, Ribbon, Live Emitters, Flow Map                   |
| Impact      | Camera Shake, Distortion, Decal, Sparks, Light Burst     |
| Aftermath   | Smoke Emitter, Ash, Residual particles, Lingering lights |
| Fade        | Stop emitters, kill trails, dissolve shaders             |

### 4.6 Common Timeline Mistakes

❌ Everything starts immediately — no anticipation.
❌ Maximum brightness lasts too long — eye adapts, impact feels weak.
❌ No aftermath — power feels weightless.
❌ Constant particle emission — no rhythm.
❌ Camera shaking continuously — fatiguing.
❌ Smoke appears before explosion — incorrect cause and effect.
❌ All particles disappear together — artificial.

### 4.7 AI Timeline Checklist

- Does the skill begin quietly?
- Is there visible energy accumulation?
- Does compression increase tension?
- Is Release nearly instantaneous?
- Is motion readable during travel?
- Does Impact represent the visual climax?
- Does Expansion release stored energy?
- Does the environment react?
- Does the world remain changed briefly?
- Do all layers fade independently?

If any answer is **No**, redesign the timeline.

---

## CHAPTER 5 — AI DESIGN RULES (MANDATORY)

> If multiple rules conflict, prioritize: **Gameplay Readability → Artistic Identity → Spectacle**

### 5.1 Think Like a Director

Before creating any effect, ask: What is the player's eye looking at? What emotion should the player feel? What is the strongest moment? What should happen immediately after? Never begin from the API. Begin from the experience.

### 5.2 Design Before Code

Always follow this order: Gameplay Purpose → Element Identity → Timeline → Motion → Layers → Visual Effects → API Selection → Code. Never reverse this process.

### 5.3 One Idea Per Skill

Every skill should have one dominant visual idea.
- **Good**: "A sword splits space." / "Roots imprison the enemy." / "A dragon circles before striking."
- **Bad**: "Fire. Lightning. Rocks. Smoke. Portal. Dragon. Tornado. Explosion. Everything together."

A memorable skill communicates one clear fantasy.

### 5.4 One Purpose Per Layer

| Layer      | Purpose                         |
|------------|---------------------------------|
| Core       | Attack identity                 |
| Trail      | Show movement                   |
| Ribbon     | Show energy flow                |
| Smoke      | Show aftermath                  |
| Sparks     | Add intensity                   |
| Light      | Highlight impact                |
| Distortion | Represent overwhelming energy   |

Never create duplicate layers. If two layers serve the same purpose, merge them.

### 5.5 Never Use Everything

Having access to many systems does not mean every system should be used. A large explosion may use Particle + Trail + Light + Decal + Camera Shake + Distortion. A small sword slash may only need Trail + Sparks + Tiny light. Using every system for every skill destroys visual hierarchy.

### 5.6 Every Effect Needs a Reason

Smoke = something burned. Dust = something struck the ground. Sparks = hard materials collided. Mist = water condensed. Lightning = energy discharged. Never add effects without physical or magical justification.

### 5.7 Respect the Player's Eyes

The brightest object should always be the most important object. Do not allow background particles to outshine the attack. Avoid excessive bloom, permanent flashing, and visual fatigue. Beauty should invite attention, not demand it.

### 5.8 Control Visual Noise

Noise is detail, not information. Excessive noise: random particles everywhere, constant screen shake, continuous distortion, excessive sparks, too many colors. When everything moves, nothing feels alive.

### 5.9 Build Large Effects From Small Systems

Never create giant monolithic effects — compose instead. Dragon Breath = Dragon Head + Flame Core + Smoke + Embers + Heat Distortion + Ground Burn + Light + Shockwave. Every layer remains simple; the combination becomes spectacular.

### 5.10 Motion Always Has Three Levels

Primary (gameplay movement) + Secondary (supporting movement) + Micro (tiny procedural movement). Without micro motion, everything appears robotic.

### 5.11 Timing Is More Important Than Quantity

A perfectly timed explosion with 20 particles is more beautiful than 500 poorly timed particles. Timing always wins. Never increase particle count before improving timing.

### 5.12 Preserve Silhouettes

The player should recognize the skill instantly. Never bury the main object beneath particles. Silhouettes — sword, dragon, meteor, tree, shield, portal — should always remain visually identifiable.

### 5.13 Respect Gameplay

Art never overrides gameplay. Do not hide enemies, projectiles, danger zones, or collision direction. Visual beauty should enhance clarity, not reduce it.

### 5.14 Escalation

Basic attacks must look weaker than ultimate abilities. Leave room for escalation. Players should immediately recognize increasing power.

### 5.15 Every Ultimate Must Feel Different

Characteristics: longer anticipation, larger environmental reaction, greater contrast, unique lighting, distinctive aftermath. The player should instinctively know "This is special."

### 5.16 Respect Element Identity

Metal should not ripple like water. Water should not explode like fire. Earth should not float like Taiji. Fire should not move with perfect discipline. Taiji should never resemble ordinary electricity from modern technology. Identity is created through motion, timing, force, shape, and rhythm.

### 5.17 Use the API as an Orchestra

Particles = Strings. Trails = Brush strokes. Lights = Spotlights. Force Fields = Choreography. Shaders = Surface detail. Camera = Audience attention. Do not allow one instrument to dominate.

### 5.18 Common Design Mistakes to Avoid

- **Rainbow Syndrome**: too many colors — choose a limited palette.
- **Particle Spam**: adding particles instead of improving design.
- **Constant Maximum Intensity**: no contrast, no emotional rhythm.
- **Infinite Motion**: nothing ever rests.
- **Layer Confusion**: multiple layers competing for attention.
- **Random Force Fields**: movement without intention.
- **Excessive Bloom**: everything glows, nothing shines.
- **Giant Smoke Clouds**: smoke supports the attack, it does not replace it.

### 5.19 AI Self-Review (Answer Before Writing Code)

- **Identity**: Can this skill be recognized without color?
- **Story**: Does the skill tell a beginning, middle, and end?
- **Motion**: Does every movement express emotion?
- **Layering**: Does every layer have exactly one responsibility?
- **Readability**: Can the player instantly understand the attack?
- **Timeline**: Does intensity naturally rise and fall?
- **Environment**: Does the world react appropriately?
- **Element**: Does the motion language match the assigned element?
- **Simplicity**: Can anything be removed without reducing quality? If not, the design is likely balanced.

### 5.20 Forbidden Patterns (Never Generate Unless Explicitly Requested)

❌ Random particles filling the screen.
❌ Flat constant lighting.
❌ One-color gradients.
❌ Perfectly straight robotic motion.
❌ Trails with constant width and brightness.
❌ Camera shaking every frame.
❌ Full-screen bloom throughout the skill.
❌ Effects appearing without a visual cause.
❌ Every particle having identical lifetime.
❌ Every particle having identical size.
❌ Identical timing across every skill.
❌ Copying another element's motion language.

### 5.21 Excellence Checklist (Skill Complete Only If All = YES)

| Question                                         |
|--------------------------------------------------|
| Does it have one clear visual idea?             |
| Does it tell a complete story?                  |
| Does each layer have one purpose?               |
| Is the timeline emotionally satisfying?         |
| Is the silhouette always readable?              |
| Does motion feel alive?                         |
| Does the environment react?                     |
| Does the aftermath leave a lasting impression?  |
| Does it preserve gameplay readability?          |
| Does it clearly belong to Wuxing?               |

If any answer is **NO**, redesign before writing code.

---

## CHAPTER 6 — VFX COOKBOOK (REUSABLE PATTERNS)

> These are design patterns, not hard rules. Treat them as building blocks to combine creatively.

Every skill = Core Object + Motion + Energy + Impact + Aftermath

### 6.1 Pattern Recipes

**Projectile** (flying swords, fireballs, spears, ice shards):
Projectile → Trail → Live Particle Emitter → Light → Impact → Smoke/Dust

**Beam** (laser, lightning beam, spirit ray):
Beam Core + Flow Map + Glow + Edge Sparks + Impact Burst. Beam stays readable; effects wrap around it, never bury it.

**Sword Slash** (katana, energy blade, metal slash):
Sword Motion → Trail → Ribbon → Spark → Tiny Flash → Ground Decal. Avoid large smoke or explosions — a slash should feel clean.

**Explosion** (fire, earth burst, magical detonation):
Flash → Shockwave → Main Explosion → Debris → Smoke → Residual Light. Flash always comes first, smoke always comes last.

**Portal** (teleport, summoning, dimensional gate):
Circle → Flow Map → Ribbon → Orbiting Particles → Distortion → Glow. Portal energy rotates — it does not explode.

**Aura** (buff, charge, meditation):
Emitter → Orbit → Curl Noise → Ribbon → Soft Glow. Should feel alive, not noisy.

**Dragon** (fire, water, spirit dragon):
Dragon Head → Body Motion → Ribbon Spine → Emitter → Glow → Trail → Impact. Dragon itself stays visible — particles enhance, not hide, the dragon.

**Meteor** (fire, earth):
Meteor → Long Trail → Smoke → Light → Ground Impact → Debris → Shockwave → Dust. Travel phase is as important as impact.

**Tornado**:
Vortex Motion → Ribbon Spiral → Particles → Debris → Mist → Leaves. Rotation comes from forces, not from manually placing particles in circles.

**Summon** (sword, spirit beast, stone guardian):
Portal → Materialization → Glow → Shape Reveal → Idle Aura. Do not instantly spawn — reveal it.

**Shield**:
Core Shape → Flow Map → Energy Edge → Ripple On Hit → Light Pulse. Should feel stable, not explosive.

**Chain Lightning** (Taiji):
Charge → Main Arc → Secondary Arcs → Ground Sparks → Residual Electricity. Every arc seeks a destination. Never draw perfectly straight lines.

**Healing**:
Soft Glow → Particles Rise → Leaves/Light → Pulse → Fade. Calm, never explosive.

**Charge** (before ultimates):
Weak Glow → Particles Gather → Rotation → Compression → Flash → Skill Release. Player feels increasing pressure.

**Dash**:
Character Motion → Afterimage → Trail → Dust → Arrival Flash. Movement stays readable; do not completely hide the character.

**Impact** (every powerful hit):
Hit → Flash → Light → Sparks → Camera Shake → Distortion → Smoke. Not every impact needs all layers.

### 6.2 Layer Combinations by Element

| Element | Good Combination                                         |
|---------|----------------------------------------------------------|
| Metal   | Trail + Ribbon + Spark + Tiny Flash                     |
| Water   | Trail + Mist + Ribbon + Droplets                        |
| Fire    | Flames + Smoke + Embers + Light                         |
| Earth   | Dust + Debris + Shockwave + Ground Crack                |
| Wood    | Leaves + Roots + Spores + Soft Glow                     |
| Taiji   | Energy Filaments + Electric Arcs + Orbiting Particles + Bloom Pulse + Distortion |

### 6.3 API Selection Guide

| Artistic Goal          | Preferred API                      |
|------------------------|------------------------------------|
| Continuous movement    | Trail                              |
| Long elegant energy    | Ribbon                             |
| Persistent atmosphere  | Emitter                            |
| Random detail          | Particle                           |
| Internal motion        | Flow Map                           |
| Organic movement       | ForceField                         |
| Strong impact          | Camera + Distortion + Light        |
| Ground reaction        | Decal                              |
| Color transition       | Gradient                           |
| Animated texture       | Sprite Animation                   |

Do not solve every problem with `SpawnParticle()`.

### 6.4 Layer Build Order

Always build in this order: Gameplay → Core Shape → Motion → Energy → Lighting → Environment → Atmosphere → Fine Detail. If time is limited, remove lowest layers first. Never remove Core Shape or Motion.

### 6.5 Recipes to Avoid

❌ Ribbon without motion. ❌ Smoke without impact. ❌ Light without a source. ❌ Distortion on weak attacks. ❌ Camera shake every hit. ❌ Huge bloom on every skill. ❌ Ten emitters doing the same thing. ❌ Trails longer than the projectile's readable path. ❌ Sparks floating slowly. ❌ Fire behaving like liquid.

---

## CHAPTER 7 — AI SKILL CREATION WORKFLOW

> **Never start from code.** Every skill is designed from the gameplay fantasy downward until it naturally maps to the Wuxing Core API.

### 7.1 The Design Pyramid (Never Skip a Level)

```
Fantasy → Gameplay → Element → Timeline → Visual Language →
Layer Composition → Force Design → API Mapping → Implementation
```

If a higher level is weak, the lower levels cannot save the design.

### 7.2 The 10-Step Process

**Step 1 — Define the Fantasy**
The first question is never "How do I implement this?" It is "What fantasy is the player experiencing?"
- Good: "Summon celestial swords." / "Become a flowing river." / "Split the earth open." / "Trap enemies inside roots."
- Bad: "Spawn particles." / "Draw ribbon." / "Create explosion." Technology is never the fantasy.

**Step 2 — Define Gameplay**
Choose one primary purpose: Projectile, AOE, Dash, Trap, Shield, Buff, Summon, Beam, Orbit, Rain. Once gameplay is fixed, visual decisions become much easier.

**Step 3 — Choose the Element**
Choose based on motion, force, rhythm, shape, and emotion — not only color. Fast precise projectile → Metal. Flowing projectile → Water. Violent projectile → Fire.

**Step 4 — Build the Timeline**
Determine major phases: Birth → Charge → Release → Travel → Impact → Aftermath → Fade. This becomes the backbone of the entire skill.

**Step 5 — Define the Core Shape**
Ask: "If every particle disappears, what remains?" Examples: Sword, Dragon, Fireball, Tree, Meteor, Portal. The Core Shape is the identity — everything else supports it.

**Step 6 — Build the Layers**
Build from large to small: Core Shape → Motion → Energy → Lighting → Environment → Atmosphere → Details. Never begin with details.

**Step 7 — Design Motion**
Primary (gameplay movement) + Secondary (natural support) + Micro (procedural life). Example for a projectile: Primary = Forward, Secondary = Gentle spiral, Micro = Noise.

**Step 8 — Design Forces**
Every force must have a reason. Fire = Radial + Noise + Wind. Water = Vortex + Viscosity + Curl. Metal = Drag + Tiny directional gravity. Never randomly combine forces.

**Step 9 — Select API**
Only now should API be selected. Need continuous motion → Trail. Need persistent aura → Emitter. Need flowing texture → Flow Map. Need impact → Light + Camera + Distortion. API is chosen because of artistic needs, never the opposite.

**Step 10 — Write Code**
Implementation should now be straightforward. The code simply executes the design. Good code follows the artistic blueprint. Bad code invents the design while programming.

### 7.3 Example Workflow — Metal Flying Sword

| Step       | Answer                                           |
|------------|--------------------------------------------------|
| Fantasy    | The player commands a divine sword               |
| Gameplay   | Projectile                                       |
| Element    | Metal                                            |
| Timeline   | Charge → Launch → Flight → Impact → Fade        |
| Core Shape | Sword                                            |
| Motion     | Fast, straight, tiny vibration                   |
| Forces     | Directional gravity, light drag                  |
| Layers     | Sword + Trail + Ribbon + Spark + Light           |
| API        | Trail, Particle, Light, Gradient, Trail Update   |
| Code       | Implementation only — no artistic decisions remain |

### 7.4 Example Workflow — Fire Meteor

| Step       | Answer                                                  |
|------------|---------------------------------------------------------|
| Fantasy    | A burning star falls from heaven                        |
| Gameplay   | AOE Projectile                                          |
| Timeline   | Sky → Meteor → Long Fall → Ground Impact → Explosion → Smoke |
| Core Shape | Meteor                                                  |
| Layers     | Rock + Fire + Smoke + Trail + Light + Shockwave         |
| Forces     | Radial + Noise + Wind                                   |

### 7.5 AI Self-Correction After Generating

Review the skill and ask: Does every layer have a reason? Does every force support the fantasy? Is every particle contributing? Can something be removed? Can timing improve? Can readability improve? If yes, improve before finalizing.

### 7.6 Modifying an Existing Skill

Do NOT immediately change code. Instead evaluate the pyramid: Fantasy → Timeline → Identity → Layers → Motion → API → Implementation. Many problems originate from design, not implementation.

### 7.7 AI Final Checklist (Verify Before Returning Any Implementation)

**Design**
- □ The fantasy is clear.
- □ The gameplay purpose is obvious.
- □ The element's visual language is respected.

**Timeline**
- □ The skill has anticipation.
- □ The impact is the visual climax.
- □ The aftermath exists.

**Composition**
- □ The Core Shape is always readable.
- □ Every layer has exactly one purpose.
- □ Motion includes primary, secondary, and micro movement.

**Technical**
- □ The selected APIs match the artistic intent.
- □ No API is used without purpose.
- □ The implementation follows the Core API architecture.

**Quality**
- □ The skill could belong anywhere else? **No.**
- □ The skill is unmistakably Wuxing? **Yes.**

Only when every item passes should the AI generate the final implementation.

---

## FINAL PRINCIPLE

The Wuxing Core API provides the **tools**.
The Art Direction provides the **vision**.
The workflow above is the **bridge** between them.

An AI that follows this process will not merely generate effects — it will design complete skills with clear identity, coherent structure, strong gameplay readability, and a consistent artistic language worthy of the Wuxing universe.

> A great skill is not a collection of visual effects. It is a carefully directed performance where every frame prepares the next, every layer has a purpose, and the climax feels both inevitable and unforgettable.