# WUXING_ART_DIRECTION.md

# Chapter 1 - Core Philosophy

> **This document defines the artistic philosophy behind every skill created for the Wuxing engine.**
>
> The Core API explains **how** to build a skill.
> This document explains **how to make it beautiful.**
>
> Every AI generating code for this engine MUST follow these principles before writing a single line of code.

---

# 1.1 Design Goal

The purpose of every skill is **not** to display as many particles as possible.

The goal is to create an effect that immediately communicates:

* Power
* Weight
* Speed
* Elemental identity
* Intention
* Emotional impact

A beautiful skill is remembered because it tells a visual story, not because it contains many effects.

Every visual decision should support gameplay readability while maximizing spectacle.

Never create effects whose only purpose is "looking cool."

Everything must have intention.

---

# 1.2 The Three Questions

Before creating any effect, the AI MUST answer three questions.

## Question 1

**Where does the energy come from?**

Examples

* Inside the caster
* Underground
* The sky
* A summoned weapon
* Around the target
* Another dimension

The source determines how particles begin moving.

---

## Question 2

**Where does the energy go?**

Examples

* Into the enemy
* Around the player
* Into a projectile
* Across the battlefield
* Toward the sky

Effects should always have a clear direction.

Never create energy that appears from nowhere and disappears without purpose.

---

## Question 3

**What remains after the energy leaves?**

Powerful magic changes the world.

Examples

* Smoke
* Dust
* Floating sparks
* Burn marks
* Cracks
* Fog
* Residual light
* Slowly fading particles

The aftermath is often what makes a spell feel believable.

---

# 1.3 Every Skill Tells A Story

A skill is a short movie.

Never think in terms of particles.

Think in terms of narrative.

Every skill should contain a beginning, middle and ending.

Typical structure

```
Birth

↓

Gather

↓

Tension

↓

Release

↓

Impact

↓

Aftermath

↓

Fade
```

Each stage has a purpose.

---

## Birth

The audience should understand that something is beginning.

Examples

* Small glow
* Tiny sparks
* Air distortion
* Dust moving
* Ground vibration

Birth should create curiosity.

---

## Gather

Energy begins concentrating.

Visual density increases.

Movement becomes more organized.

Particles begin converging.

Lighting slowly grows.

Nothing explodes yet.

The audience should feel

> "Something powerful is about to happen."

---

## Tension

Movement slows.

Compression increases.

Particles rotate faster.

Light intensifies.

Camera remains relatively stable.

This moment exists to create anticipation.

Without anticipation, impact feels weak.

---

## Release

Release should happen quickly.

Fast acceleration.

Clear direction.

Strong contrast.

The audience should immediately know

> "The attack has begun."

---

## Impact

This is the emotional peak.

Everything aligns.

Examples

* Camera shake
* Flash
* Screen distortion
* Light burst
* Decals
* Sparks
* Shockwave

Impact lasts only a brief moment.

Never allow the peak to continue for a long time.

---

## Aftermath

Power never disappears instantly.

The world reacts.

Smoke rises.

Fragments settle.

Residual electricity crawls.

Water ripples.

Leaves continue drifting.

The aftermath gives weight to the attack.

---

## Fade

Everything slowly returns to normal.

Visual noise decreases.

Brightness decreases.

Motion decreases.

Never abruptly destroy every particle.

---

# 1.4 Beauty Through Contrast

The eye notices change.

Not intensity.

Therefore contrast is more important than brightness.

Contrast exists in every dimension.

---

## Bright vs Dark

A completely bright effect feels flat.

Instead

```
Dark

↓

Bright Flash

↓

Dark
```

The flash becomes much stronger.

---

## Fast vs Slow

If everything moves quickly,
nothing feels fast.

Combine

* slow preparation
* instant attack
* slow aftermath

---

## Large vs Small

Never spawn particles of one size.

Mix

* huge structures
* medium debris
* tiny sparks
* microscopic dust

This creates richness.

---

## Dense vs Empty

Leave empty space.

The empty areas allow important shapes to stand out.

Do not fill the entire screen with particles.

---

## Silence vs Explosion

Quiet moments increase emotional impact.

A brief pause before impact often feels stronger than adding more particles.

---

# 1.5 Readability Before Spectacle

The player must always understand

* where the attack is
* where danger is
* where the caster is
* what direction the attack travels

Never sacrifice gameplay readability.

Beautiful effects that hide gameplay are failures.

---

# 1.6 Motion Has Meaning

Motion is language.

Every movement communicates emotion.

Examples

Straight movement

Feels

* disciplined
* sharp
* precise

Curved movement

Feels

* elegant
* alive
* natural

Spiral movement

Feels

* mystical
* magical
* unstable

Chaotic movement

Feels

* violent
* explosive
* uncontrollable

Floating movement

Feels

* divine
* spiritual
* peaceful

Always choose motion based on emotion.

Never choose randomness without purpose.

---

# 1.7 The World Should React

Magic affects more than the projectile.

Nearby air reacts.

Ground reacts.

Light reacts.

Particles react.

Smoke reacts.

Enemies react.

Camera reacts.

The environment should acknowledge powerful events.

This creates immersion.

---

# 1.8 Less Is Often More

Adding more particles rarely improves quality.

Instead improve

* timing
* composition
* layering
* gradients
* lighting
* motion
* silhouettes

Ten meaningful effects are better than one hundred random effects.

---

# 1.9 Consistency

Every skill belongs to the same universe.

Although elements differ dramatically,

they must all obey the same artistic language.

Players should immediately recognize

> "This belongs to Wuxing."

Consistency is achieved through

* shared timing philosophy
* shared layering philosophy
* shared motion quality
* shared visual hierarchy
* shared post-processing style
* shared lighting philosophy

Never create an effect whose artistic style feels disconnected from the rest of the game.

---

# 1.10 Golden Rules

The following rules override every other artistic decision.

## Rule 1

Every skill tells a story.

---

## Rule 2

Energy always has a source.

---

## Rule 3

Energy always has a destination.

---

## Rule 4

Power leaves consequences.

---

## Rule 5

Motion always communicates emotion.

---

## Rule 6

Contrast creates beauty.

---

## Rule 7

The player must always understand what is happening.

---

## Rule 8

Large effects are built from many simple layers.

Never rely on one giant effect.

---

## Rule 9

Every visual decision must have purpose.

Never add particles "just because."

---

## Rule 10

When in doubt,

choose elegance over complexity.

# Chapter 2 - Universal VFX Language

> This chapter defines the universal artistic language shared by every skill in Wuxing.
>
> Regardless of element, weapon, or gameplay category, every visual effect MUST follow these principles.
>
> The goal is to create a cohesive visual identity across the entire game.

---

# 2.1 The Layer Philosophy

Every beautiful skill is built from multiple simple layers.

Never attempt to create beauty with a single effect.

Think like a painter.

One layer provides shape.

Another provides motion.

Another provides light.

Another provides atmosphere.

Another provides detail.

Each layer has exactly one purpose.

---

## Recommended Layer Hierarchy

```
Core Layer
    ↓
Motion Layer
    ↓
Energy Layer
    ↓
Accent Layer
    ↓
Atmosphere Layer
    ↓
Environment Layer
```

---

## Core Layer

The Core Layer defines the spell's identity.

Examples

* Sword
* Fireball
* Dragon
* Boulder
* Water spear
* Lightning bolt

If every other layer disappears, the player should still recognize the attack.

---

## Motion Layer

Motion communicates speed.

Examples

* Ribbon
* Trail
* Motion blur
* Flow Map
* Velocity particles

Motion should never replace the Core Layer.

It only amplifies movement.

---

## Energy Layer

Energy shows magical power.

Examples

* Glow
* Sparks
* Electric arcs
* Magical dust
* Flames
* Mist

Energy wraps around the core.

It should not hide it.

---

## Accent Layer

Accent exists to attract attention.

Examples

* Tiny bright particles
* Lens-like flashes
* High intensity sparks
* Sharp highlights

Accent occupies very little screen space.

Too much accent destroys visual hierarchy.

---

## Atmosphere Layer

Atmosphere creates immersion.

Examples

* Smoke
* Dust
* Fog
* Heat haze
* Floating leaves
* Ash

Atmosphere moves slowly.

It survives after the attack ends.

---

## Environment Layer

Powerful magic changes the world.

Examples

* Ground cracks
* Decals
* Shockwaves
* Dynamic lights
* Camera shake
* Screen distortion

Environment effects are expensive.

Use them only during important moments.

---

# 2.2 Visual Hierarchy

Not everything should demand attention.

Every frame should answer

"What should the player look at first?"

Use three levels.

## Primary

The attack itself.

Must always be readable.

---

## Secondary

Supporting motion.

Trails.

Particles.

Energy.

These guide the player's eye.

---

## Tertiary

Background atmosphere.

Smoke.

Dust.

Fog.

Ground effects.

These should never compete with the Primary layer.

---

# 2.3 Particle Philosophy

Particles are not decoration.

Each particle should have a role.

Common roles

* Energy
* Debris
* Smoke
* Sparks
* Ash
* Leaves
* Mist
* Dust
* Embers

Never spawn particles without purpose.

---

## Particle Lifetime

Different particles should die at different times.

Example

```
Flash

0.05s

Spark

0.3s

Energy

0.8s

Smoke

2.5s
```

Uniform lifetime feels artificial.

---

## Particle Scale

Always mix scales.

```
Huge

Large

Medium

Small

Tiny
```

Avoid identical particle sizes.

---

## Particle Density

Density should vary over time.

Bad

```
50 particles every frame
```

Good

```
Few

↓

Many

↓

Few
```

Visual breathing feels natural.

---

# 2.4 Trail Philosophy

Trails describe movement.

They are not decorative ribbons.

A good trail tells the player

* where something came from
* where it is going
* how fast it moves

---

## Good Trail

Clear direction.

Consistent thickness.

Soft fade.

Natural curvature.

---

## Bad Trail

Perfectly straight.

Uniform brightness.

Uniform width.

No fading.

No movement.

---

## Trail Width

Width should breathe.

Never remain perfectly constant.

Slight variation creates life.

---

## Trail Lifetime

Long trails imply elegance.

Short trails imply speed.

Choose according to the desired emotion.

---

# 2.5 Ribbon Philosophy

Ribbon represents flowing energy.

It should never appear rigid.

Use ribbon when representing

* magic
* wind
* silk
* flowing water
* spiritual energy

Avoid ribbon for heavy objects.

---

# 2.6 Force Philosophy

ForceField is not physics.

ForceField is emotion.

Every force should support the intended feeling.

---

## Gravity

Compression.

Weight.

Falling.

Collapse.

---

## Point Gravity

Gathering.

Charging.

Absorption.

Meditation.

---

## Vortex

Mystery.

Magic.

Swirling energy.

Spiritual movement.

---

## Curl Noise

Life.

Organic movement.

Natural chaos.

---

## Drag

Weight.

Resistance.

Density.

---

## Wind

Freedom.

Speed.

Flow.

---

## Combining Forces

Never rely on a single force.

Example

```
Point Gravity

+

Curl Noise

+

Drag
```

Feels alive.

Example

```
Wind

+

Noise

+

Vortex
```

Feels magical.

---

# 2.7 Motion Language

Everything should move in layers.

```
Primary Motion

+

Secondary Motion

+

Micro Motion
```

Example

Projectile

Primary

Forward

Secondary

Small spiral

Micro

Noise wobble

This combination prevents robotic motion.

---

# 2.8 Color Philosophy

Color defines emotion.

Never rely on brightness alone.

Every effect should include

Highlight

↓

Midtone

↓

Shadow

↓

Fade

A single flat color appears lifeless.

---

## Gradient

Prefer gradients over solid colors.

Gradients communicate

* temperature
* age
* energy
* decay

A particle should evolve during its lifetime.

---

# 2.9 Lighting Philosophy

Light should breathe.

Never instantly appear.

Typical curve

```
Fade In

↓

Peak

↓

Fade Out
```

The strongest light should exist only briefly.

Constant brightness feels fake.

---

# 2.10 Flow Map Philosophy

Flow maps represent internal energy.

Use them to imply movement inside an object.

Examples

Energy blades

Magic circles

Portals

Fire

Water

Spirit weapons

Avoid using Flow Maps on rigid stone or metal unless magic is actively flowing.

---

# 2.11 Camera Philosophy

Camera movement is punctuation.

Not background noise.

Camera shake should happen only when

* impact
* landing
* explosion
* massive collision

Small attacks usually require no shake.

Strong attacks require short, sharp shake.

Never shake continuously.

---

# 2.12 Screen Distortion

Distortion represents intense energy.

Appropriate uses

Explosion

Portal

Meteor

Massive fire

Space distortion

Forbidden uses

Tiny sparks

Normal projectiles

Weak melee attacks

Distortion should remain rare.

Rare effects feel powerful.

---

# 2.13 Environmental Response

The world should acknowledge power.

Possible reactions

Ground dust

Leaves blown away

Water ripples

Snow displacement

Burn marks

Ice crystals

Shockwaves

Dynamic lights

Residual smoke

Not every attack requires every reaction.

Choose only what strengthens the illusion.

---

# 2.14 Timing

Beautiful effects have rhythm.

```
Birth

↓

Build

↓

Peak

↓

Recovery
```

Never remain at maximum intensity.

Variation creates drama.

---

# 2.15 Silence

Silence is part of visual design.

Moments with fewer particles allow stronger moments to shine.

Do not fear empty space.

Do not fear slow movement.

The audience needs time to perceive beauty.

---

# 2.16 The Five-Layer Rule

Every major skill should contain at least five visual categories.

```
Main Body

Motion

Energy

Atmosphere

Environment
```

Additional layers are optional.

Missing one of these layers usually makes a skill feel incomplete.

---

# 2.17 AI Quality Checklist

Before generating code, verify the following:

* Does the skill have a clear visual focus?
* Does every layer have a unique purpose?
* Does energy visibly flow from source to destination?
* Are particles varied in size, lifetime, and speed?
* Does motion include primary, secondary, and micro movement?
* Are gradients used instead of flat colors?
* Is lighting animated instead of constant?
* Does the environment react appropriately?
* Is gameplay readability preserved?
* Does the effect leave a believable aftermath?

If any answer is **No**, improve the design before writing code.

---

## Final Principle

A great VFX is not created by adding more particles.

It is created by carefully orchestrating many simple systems into one coherent visual performance.

The player should never notice the individual particles.

They should remember the feeling.

# Chapter 3 - The Language of the Six States

> Wuxing is **not** a game where elements differ only by color.
>
> Every element has its own **personality**, **motion language**, **visual rhythm**, **energy behavior**, and **physical feeling**.
>
> If a player watches a skill in pure grayscale, they should still be able to identify its element from movement alone.

---

# The Six States

Wuxing consists of six visual identities.

```
Metal
Wood
Water
Fire
Earth
Taiji
```

Taiji is **not an element**.

It is the state of primordial energy before the separation of Yin and Yang.

Electricity belongs to Taiji because it represents unstable equilibrium, polarity, transformation and instantaneous release.

Therefore, Electric effects MUST NOT behave like Fire.

---

# 3.1 METAL

## Identity

Metal is precision.

Not violence.

Not speed.

Precision.

Every movement should feel deliberate.

Every strike feels inevitable.

---

## Emotion

The player should feel

* Sharp
* Cold
* Clean
* Elegant
* Ruthless
* Disciplined

---

## Shape Language

Metal prefers

* Straight lines
* Long blades
* Needles
* Spears
* Thin shards
* Hexagonal patterns
* Symmetry

Avoid

* Blobs
* Organic curves
* Soft smoke
* Random splashes

---

## Motion

Primary

Linear.

Secondary

Very small spiral.

Micro

Tiny vibration.

Metal almost never drifts.

It commits.

---

## Rhythm

```
Silence

↓

Instant movement

↓

Silence
```

Very little anticipation.

Almost no lingering.

Everything is decisive.

---

## Force Philosophy

Prefer

* Drag
* Wind
* Directional Gravity

Avoid

* Strong Curl Noise
* Heavy Vortex

Metal resists chaos.

---

## Particle Language

Use

* Bright sparks
* Tiny fragments
* Blade trails
* Thin ribbons
* Flying chips

Avoid

* Thick smoke
* Puffy particles

---

## Lighting

Small.

Bright.

Focused.

Needle-like.

---

## Camera

Minimal shake.

High precision.

---

## Keywords

Cut

Slice

Pierce

Split

Reflect

Polish

Discipline

---

# 3.2 WOOD

## Identity

Wood is life.

Nothing grows instantly.

Everything expands naturally.

---

## Emotion

The player should feel

* Growth
* Harmony
* Patience
* Nature
* Persistence

---

## Shape Language

Use

* Roots
* Branches
* Leaves
* Flowers
* Vines
* Moss
* Bark

Avoid

Perfect geometry.

---

## Motion

Always organic.

Nothing moves perfectly straight.

Everything bends.

Everything seeks life.

---

## Rhythm

```
Slow

↓

Growing

↓

Bloom

↓

Spread
```

---

## Force Philosophy

Prefer

* Curl Noise
* Wind
* Weak Point Gravity

Wood follows natural flow.

---

## Particle Language

Leaves.

Seeds.

Spores.

Petals.

Tiny pollen.

Never metallic sparks.

---

## Lighting

Soft.

Diffuse.

Warm.

Natural.

---

## Camera

Almost none.

Growth is calm.

---

## Keywords

Grow

Bloom

Wrap

Spread

Root

Heal

Expand

---

# 3.3 WATER

## Identity

Water adapts.

Water never fights directly.

It surrounds.

It flows.

It erodes.

---

## Emotion

* Calm
* Elegant
* Endless
* Flexible
* Graceful

---

## Shape Language

Curves.

Waves.

Streams.

Drops.

Mist.

Foam.

Never rigid polygons.

---

## Motion

Continuous.

Smooth.

No sudden stops.

Acceleration should feel fluid.

---

## Rhythm

```
Flow

↓

Flow

↓

Flow

↓

Crash

↓

Flow
```

---

## Force Philosophy

Prefer

* Vortex
* Curl Noise
* Viscosity
* Wind

Water should never look static.

---

## Particle Language

Mist.

Droplets.

Foam.

Ripples.

Splashes.

Tiny bubbles.

---

## Lighting

Soft blue highlights.

Reflections.

Transparency.

---

## Camera

Only during large impacts.

---

## Keywords

Flow

Wash

Encircle

Pull

Ripple

Wave

Current

---

# 3.4 FIRE

## Identity

Fire consumes.

Everything about Fire wants to become larger.

---

## Emotion

* Aggressive
* Violent
* Passionate
* Explosive
* Unstable

---

## Shape Language

Flames.

Explosions.

Jagged tongues.

Ash.

Smoke.

Shockwaves.

---

## Motion

Expansion.

Burst.

Collapse.

Everything pushes outward.

---

## Rhythm

```
Charge

↓

Explosion

↓

Ash

↓

Smoke
```

---

## Force Philosophy

Prefer

* Radial
* Wind
* Noise

Avoid

Long-lasting stability.

---

## Particle Language

Flames.

Embers.

Ash.

Smoke.

Burning fragments.

---

## Lighting

Largest of all elements.

Brightest peak.

Fast fade.

---

## Camera

Strong impact.

Strong distortion.

---

## Keywords

Ignite

Burn

Consume

Explode

Scorch

Melt

---

# 3.5 EARTH

## Identity

Earth is weight.

Earth does not rush.

Earth moves because nothing can stop it.

---

## Emotion

* Heavy
* Ancient
* Stable
* Massive
* Dominant

---

## Shape Language

Rocks.

Mountains.

Pillars.

Crystal.

Sand.

Dust.

Fragments.

---

## Motion

Slow acceleration.

Huge momentum.

Very difficult to redirect.

---

## Rhythm

```
Stillness

↓

Lift

↓

Crash

↓

Dust

↓

Silence
```

---

## Force Philosophy

Prefer

* Gravity
* Drag
* Viscosity

Avoid

High frequency noise.

---

## Particle Language

Dust.

Pebbles.

Rock fragments.

Sand.

Debris.

---

## Lighting

Low intensity.

Large area.

Warm neutral colors.

---

## Camera

Strongest shake among all elements.

Weight should be felt physically.

---

## Keywords

Crush

Rise

Collapse

Fortify

Anchor

Shatter

---

# 3.6 TAIJI

## Identity

Taiji is primordial balance.

It exists before the five elements.

Electricity is merely one visible manifestation of Taiji energy.

Do not think of Electric as "blue fire."

Think of it as unstable balance searching for equilibrium.

---

## Emotion

* Divine
* Mysterious
* Intelligent
* Unpredictable
* Transcendent

---

## Shape Language

Circles.

Yin-Yang rotation.

Arcs.

Fractures.

Concentric rings.

Floating geometry.

Energy veins.

Avoid natural shapes.

Avoid smoke-heavy visuals.

---

## Motion

Alternates between

Absolute stillness

and

Instant movement.

Movement is discontinuous.

It appears.

Disappears.

Reconnects elsewhere.

---

## Rhythm

```
Stillness

↓

Compression

↓

Flash

↓

Silence

↓

Flash

↓

Silence
```

Taiji values interruption more than continuity.

---

## Force Philosophy

Prefer combinations of

* Point Gravity
* Vortex
* Curl Noise
* Wind

The resulting motion should feel intelligent rather than random.

---

## Particle Language

Electric arcs.

Energy filaments.

Floating symbols.

Tiny plasma sparks.

Orbiting particles.

Glowing dust.

Never thick smoke.

---

## Lighting

Highest contrast.

Extremely bright flashes.

Very short duration.

Frequent light pulses.

---

## Camera

Minimal movement.

Instead of violent shaking, use

* screen distortion
* bloom
* chromatic aberration
* light pulses

Reality itself appears unstable.

---

## Electric

Electricity is not the source.

Electricity is the consequence.

The audience should feel

energy seeking equilibrium.

Arcs should jump between nearby surfaces.

Connections should constantly reorganize.

Lightning should never travel as a perfectly straight beam.

Every arc should search for the next destination.

---

## Yin

Motion

Slow.

Cold.

Contracting.

Dark.

Absorbing.

---

## Yang

Motion

Expanding.

Radiant.

Explosive.

Ascending.

Warm.

---

## Taiji Principle

Every Taiji skill should contain both

Compression

and

Expansion.

Stillness

and

Movement.

Light

and

Shadow.

Order

and

Chaos.

Without duality,

Taiji loses its identity.

---

# 3.7 Cross-Element Rules

Each element should remain recognizable even if all colors are removed.

Identity comes from:

* Motion
* Timing
* Shape
* Force
* Rhythm
* Layer composition

—not from hue alone.

---

# 3.8 AI Decision Matrix

When designing a new skill, first determine its elemental language.

| Element | Motion      | Rhythm      | Force                    | Density    | Aftermath       |
| ------- | ----------- | ----------- | ------------------------ | ---------- | --------------- |
| Metal   | Linear      | Instant     | Drag + Direction         | Low        | Sparks          |
| Wood    | Organic     | Growing     | Curl + Wind              | Medium     | Leaves          |
| Water   | Flowing     | Continuous  | Vortex + Viscosity       | Medium     | Mist            |
| Fire    | Expanding   | Burst       | Radial + Noise           | High       | Smoke           |
| Earth   | Heavy       | Slow Impact | Gravity + Drag           | High       | Dust            |
| Taiji   | Alternating | Pulse       | Mixed Intelligent Forces | Low–Medium | Residual Energy |

---

# Final Principle

The player should never think:

> "That is a blue Fire skill."

Instead, they should immediately recognize:

> "That is Taiji."

Likewise, Metal should not be "gray Earth," Water should not be "blue Wind," and Fire should not be "orange Explosion."

Every state must speak its own visual language through motion, force, timing, and shape—color is only the final accent.

# Chapter 4 - Skill Timeline Design

> Every skill is a performance.
>
> Beautiful VFX is not created by individual particles.
>
> It is created by **carefully orchestrating time**.
>
> This chapter defines how every skill evolves from the first frame until the last remaining particle disappears.

---

# 4.1 The Timeline Philosophy

The player experiences a skill through time.

Not through screenshots.

Therefore, the quality of a skill depends more on **timing** than on particle count.

A good timeline creates expectation.

A great timeline creates emotion.

---

## The Standard Timeline

Every major skill should follow this structure.

```text
Birth
    ↓
Charge
    ↓
Compression
    ↓
Release
    ↓
Travel (optional)
    ↓
Impact
    ↓
Expansion
    ↓
Aftermath
    ↓
Fade
```

Not every skill requires every phase.

However, removing too many phases makes the effect feel abrupt and artificial.

---

# 4.2 Birth

Duration

```
0.05 ~ 0.20 seconds
```

Purpose

Tell the player

> "Something has begun."

Birth should never be visually loud.

Examples

* Small glow
* Tiny sparks
* Floating dust
* Air distortion
* Slight movement
* Weapon awakening

Birth creates curiosity.

Never reveal full power immediately.

---

# 4.3 Charge

Duration

```
0.2 ~ 1.5 seconds
```

Purpose

Energy begins gathering.

The audience should feel increasing tension.

Visual characteristics

* Particles converge
* Glow increases
* Rotation accelerates
* Trails become longer
* Force fields become stronger
* Lighting slowly intensifies

The player should subconsciously predict

> "The attack is becoming dangerous."

---

# 4.4 Compression

This is the most commonly forgotten phase.

Compression is the final inhale before the strike.

Everything becomes smaller.

Denser.

Brighter.

Faster.

Examples

Energy

```text
Large sphere

↓

Medium

↓

Small

↓

Flash
```

Particles

```text
Wide

↓

Tight

↓

Very tight
```

Camera

Almost still.

No shake yet.

Compression dramatically increases the perceived strength of Release.

---

# 4.5 Release

Duration

Usually

```
1~4 frames
```

Release must feel instantaneous.

The player should experience a sudden transition from

Potential

↓

Action

Characteristics

* Maximum acceleration
* Bright flash
* Clear direction
* High contrast

Never allow Release to linger.

---

# 4.6 Travel

Not every attack has this phase.

Projectile skills do.

Melee skills usually do not.

Travel exists to communicate

* direction
* speed
* weight

Travel should never feel static.

---

## Long Travel

Examples

* Flying swords
* Dragons
* Spears
* Meteors

Characteristics

Elegant motion.

Beautiful trails.

Flowing particles.

Micro motion.

---

## Short Travel

Examples

* Dash attacks
* Lightning

Motion should feel explosive.

Very little lingering.

---

# 4.7 Impact

Impact is the emotional climax.

Everything should support this exact moment.

Possible layers

* Flash
* Explosion
* Sparks
* Camera shake
* Dynamic light
* Distortion
* Ground decal
* Shockwave
* Enemy reaction

Never introduce entirely new visual ideas during impact.

Impact should be the culmination of everything established earlier.

---

## Impact Priority

The player should perceive

Flash

↓

Shape

↓

Motion

↓

Particles

↓

Smoke

In exactly that order.

---

# 4.8 Expansion

After impact,

energy expands.

Examples

Explosion

Shockwave

Ripples

Flames

Branches

Water rings

Dust clouds

Expansion releases the accumulated tension.

---

# 4.9 Aftermath

Power leaves traces.

Examples

Smoke

Fog

Residual lightning

Ash

Floating embers

Leaves

Mist

Ground glow

Aftermath is slow.

Very slow.

Players should have time to appreciate it.

---

# 4.10 Fade

Every visual layer disappears differently.

Never destroy everything simultaneously.

Example

Flash

```
0.05s
```

Light

```
0.3s
```

Energy

```
0.7s
```

Smoke

```
2.5s
```

Mist

```
3.5s
```

A staggered fade creates realism.

---

# 4.11 Intensity Curve

Never keep intensity constant.

A typical skill follows this curve.

```text
Intensity

100% ┤                ▲
 80% ┤              / │ \
 60% ┤            /   │  \
 40% ┤         /      │   \
 20% ┤      /         │    \
  0% └────────────────────────────

 Birth Charge Peak Aftermath Fade
```

Intensity should breathe.

---

# 4.12 Density Curve

Particle count should also breathe.

Bad

```text
████████████████████████
```

Good

```text
▂▄▆█▇▅▃▂
```

Density changes over time create rhythm.

---

# 4.13 Motion Curve

Motion also evolves.

Birth

Almost still.

Charge

Slow rotation.

Release

Maximum speed.

Impact

Explosion.

Aftermath

Slow drift.

Fade

Almost motionless.

---

# 4.14 Lighting Curve

Light should never remain constant.

Typical progression

```text
Small glow

↓

Brighter

↓

Blinding flash

↓

Soft illumination

↓

Darkness
```

Players remember flashes.

Not constant brightness.

---

# 4.15 Camera Curve

Bad

Camera shakes throughout the skill.

Good

```text
No movement

↓

Tiny anticipation

↓

Strong shake

↓

Recovery
```

Camera should react to force,

not existence.

---

# 4.16 Sound Synchronization Philosophy

Even though this document focuses on visuals,

every visual peak should naturally align with sound.

Examples

Compression

↓

Brief silence

↓

Explosion

↓

Echo

↓

Residual ambience

The player should almost "hear" the skill by watching it.

---

# 4.17 Layer Activation Timeline

Not every layer begins simultaneously.

Example

```text
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

---

# 4.18 API Mapping

The timeline should determine which systems become active.

## Birth

* Small `SpawnParticle`
* Weak `Emitter`
* Light begins

---

## Charge

* ForceField
* Ribbon
* Trail
* Gradient
* Flow Map

---

## Compression

* Increase ForceField
* Tighten particle emission
* Increase glow

---

## Release

* Spawn projectile
* Spawn trail
* Flash
* Dynamic light

---

## Travel

* Trail
* Ribbon
* Live Emitters
* Flow Map

---

## Impact

* Camera Shake
* Distortion
* Decal
* Sparks
* Explosion
* Light Burst

---

## Aftermath

* Smoke Emitter
* Ash
* Residual particles
* Lingering lights

---

## Fade

* Stop emitters
* Kill trails
* Dissolve shaders
* Fade gradients

---

# 4.19 Common Timeline Mistakes

Avoid the following.

## Everything starts immediately.

The player has no anticipation.

---

## Maximum brightness lasts too long.

The eye adapts.

Impact feels weak.

---

## No aftermath.

Power feels weightless.

---

## Constant particle emission.

No rhythm.

---

## Camera shaking continuously.

Fatiguing.

---

## Smoke appears before explosion.

Incorrect cause and effect.

---

## All particles disappear together.

Artificial.

---

# 4.20 AI Timeline Checklist

Before generating code, verify:

* Does the skill begin quietly?
* Is there visible energy accumulation?
* Does compression increase tension?
* Is Release nearly instantaneous?
* Is motion readable during travel?
* Does Impact represent the visual climax?
* Does Expansion release stored energy?
* Does the environment react?
* Does the world remain changed for a short time?
* Do all layers fade independently?

If any answer is **No**, redesign the timeline.

---

# Final Principle

The player should not remember the particles.

The player should remember the **moment**.

A great skill is not a collection of visual effects.

It is a carefully directed performance where every frame prepares the next, every layer has a purpose, and the climax feels both inevitable and unforgettable.

# Chapter 5 - AI Design Rules

> This chapter defines the mandatory rules every AI must follow when designing a skill.
>
> These are **not suggestions**.
>
> They exist to guarantee that every generated skill shares the same artistic quality, gameplay readability, and architectural philosophy.
>
> If multiple rules conflict, prioritize:
>
> **Gameplay Readability → Artistic Identity → Spectacle**

---

# 5.1 Think Like a Director

Do **NOT** think like a particle generator.

Think like a film director.

Before creating any effect, imagine the entire scene.

Ask:

* What is the player's eye looking at?
* What emotion should the player feel?
* What is the strongest moment?
* What should happen immediately after?

Only after answering these questions should visual systems be chosen.

Never begin from the API.

Begin from the experience.

---

# 5.2 Design Before Code

Always follow this order.

```text
Gameplay Purpose

↓

Element Identity

↓

Timeline

↓

Motion

↓

Layers

↓

Visual Effects

↓

API Selection

↓

Code
```

Never reverse this process.

---

# 5.3 One Idea Per Skill

Every skill should have **one dominant visual idea**.

Examples

Good

> A sword splits space.

> Roots imprison the enemy.

> A dragon circles before striking.

> A meteor slowly falls from the heavens.

Bad

> Fire.
>
> Lightning.
>
> Rocks.
>
> Smoke.
>
> Portal.
>
> Dragon.
>
> Tornado.
>
> Explosion.
>
> Everything together.

A memorable skill communicates one clear fantasy.

---

# 5.4 One Purpose Per Layer

Every visual layer must answer one question.

Examples

| Layer      | Purpose                       |
| ---------- | ----------------------------- |
| Core       | Attack identity               |
| Trail      | Show movement                 |
| Ribbon     | Show energy flow              |
| Smoke      | Show aftermath                |
| Sparks     | Add intensity                 |
| Light      | Highlight impact              |
| Distortion | Represent overwhelming energy |

Never create duplicate layers.

If two layers serve the same purpose,

merge them.

---

# 5.5 Never Use Everything

Having access to many systems does **not** mean every system should be used.

Example

Large explosion

May use

* Particle
* Trail
* Light
* Decal
* Camera Shake
* Distortion

Small sword slash

May only need

* Trail
* Sparks
* Tiny light

Using every system for every skill destroys visual hierarchy.

---

# 5.6 Every Effect Needs a Reason

Before spawning any effect, ask:

> Why does this exist?

Examples

Smoke

Because something burned.

Dust

Because something struck the ground.

Sparks

Because hard materials collided.

Mist

Because water condensed.

Lightning

Because energy discharged.

Never add effects without physical or magical justification.

---

# 5.7 Respect the Player's Eyes

The brightest object should always be the most important object.

Do not allow background particles to outshine the attack.

Avoid excessive bloom.

Avoid permanent flashing.

Avoid visual fatigue.

Beauty should invite attention,

not demand it.

---

# 5.8 Control Visual Noise

Noise is detail.

Noise is **not** information.

Examples of excessive noise

* Random particles everywhere
* Constant screen shake
* Continuous distortion
* Excessive sparks
* Too many colors
* Too many gradients

When everything moves,

nothing feels alive.

---

# 5.9 Build Large Effects From Small Systems

Never create giant monolithic effects.

Instead compose.

Example

Dragon Breath

```text
Dragon Head

+

Flame Core

+

Smoke

+

Embers

+

Heat Distortion

+

Ground Burn

+

Light

+

Shockwave
```

Every layer remains simple.

The combination becomes spectacular.

---

# 5.10 Motion Always Has Three Levels

Every moving object should contain

Primary Motion

The actual gameplay movement.

Secondary Motion

Supporting movement.

Micro Motion

Tiny procedural movement.

Without micro motion,

everything appears robotic.

---

# 5.11 Timing Is More Important Than Quantity

Never increase particle count before improving timing.

A perfectly timed explosion with

20 particles

is more beautiful than

500 poorly timed particles.

Timing always wins.

---

# 5.12 Preserve Silhouettes

The player should recognize the skill instantly.

Never bury the main object beneath particles.

The silhouette should remain readable.

Examples

Sword

Dragon

Meteor

Tree

Shield

Portal

These should always remain visually identifiable.

---

# 5.13 Respect Gameplay

Art never overrides gameplay.

Do not hide

* enemies
* projectiles
* danger zones
* collision direction

Visual beauty should enhance clarity,

not reduce it.

---

# 5.14 Escalation

Power should increase naturally.

Example

```text
Small Skill

↓

Medium Skill

↓

Ultimate
```

The player should immediately recognize increasing power.

Do not make basic attacks look like ultimate abilities.

Leave room for escalation.

---

# 5.15 Every Ultimate Must Feel Different

Ultimate skills deserve unique pacing.

Characteristics

Longer anticipation.

Larger environmental reaction.

Greater contrast.

Unique lighting.

Distinctive aftermath.

The player should instinctively know

> "This is special."

---

# 5.16 Respect Element Identity

Never borrow motion language from another element.

Examples

Metal should not ripple like water.

Water should not explode like fire.

Earth should not float like Taiji.

Fire should not move with perfect discipline.

Taiji should never resemble ordinary electricity from modern technology.

Identity is created through

motion,

timing,

force,

shape,

and rhythm.

---

# 5.17 Use the API as an Orchestra

Every API represents an instrument.

Particles

Strings.

Trails

Brush strokes.

Lights

Spotlights.

Force Fields

Choreography.

Shaders

Surface detail.

Camera

Audience attention.

Do not allow one instrument to dominate the orchestra.

---

# 5.18 Common Design Mistakes

Avoid these patterns.

## Rainbow Syndrome

Too many colors.

Choose a limited palette.

---

## Particle Spam

Adding particles instead of improving design.

---

## Constant Maximum Intensity

No contrast.

No emotional rhythm.

---

## Infinite Motion

Nothing ever rests.

---

## Layer Confusion

Multiple layers competing for attention.

---

## Random Force Fields

Movement without intention.

---

## Excessive Bloom

Everything glows.

Nothing shines.

---

## Giant Smoke Clouds

Smoke should support,

not replace,

the attack.

---

# 5.19 AI Self Review

Before writing code,

the AI MUST answer:

### Identity

Can this skill be recognized without color?

---

### Story

Does the skill tell a beginning,

middle,

and end?

---

### Motion

Does every movement express emotion?

---

### Layering

Does every layer have exactly one responsibility?

---

### Readability

Can the player instantly understand the attack?

---

### Timeline

Does intensity naturally rise and fall?

---

### Environment

Does the world react appropriately?

---

### Element

Does the motion language match the assigned element?

---

### Simplicity

Can anything be removed without reducing quality?

If not,

the design is likely balanced.

---

# 5.20 Forbidden Patterns

The AI MUST NEVER generate these designs unless explicitly requested.

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

---

# 5.21 Excellence Checklist

A skill is considered complete only if all answers are **YES**.

| Question                                       | Yes |
| ---------------------------------------------- | --- |
| Does it have one clear visual idea?            | □   |
| Does it tell a complete story?                 | □   |
| Does each layer have one purpose?              | □   |
| Is the timeline emotionally satisfying?        | □   |
| Is the silhouette always readable?             | □   |
| Does motion feel alive?                        | □   |
| Does the environment react?                    | □   |
| Does the aftermath leave a lasting impression? | □   |
| Does it preserve gameplay readability?         | □   |
| Does it clearly belong to Wuxing?              | □   |

If any answer is **NO**,

the AI should redesign the skill before writing code.

---

# Final Principle

The API provides the tools.

The timeline provides the rhythm.

The elements provide the identity.

The art direction provides the soul.

The AI's responsibility is **not to create many effects**.

Its responsibility is to create **one unforgettable experience** every time a player presses a skill button.

# Chapter 6 - VFX Cookbook

> This chapter provides practical recipes for building beautiful skills using the Wuxing Core API.
>
> These are **design patterns**, not hard rules.
>
> AI should treat them as reusable building blocks that can be combined to create new skills.

---

# 6.1 Build Skills From Modules

Never think

> "I need a fireball."

Think

```text
Core Object

+
Motion

+
Energy

+
Impact

+
Aftermath
```

Every skill is simply a different combination of modules.

---

# 6.2 Projectile Pattern

Best for

* Flying swords
* Fireballs
* Water bullets
* Spears
* Ice shards

## Recipe

```text
Projectile

↓

Trail

↓

Live Particle Emitter

↓

Light

↓

Impact

↓

Smoke / Dust
```

### Purpose of each layer

| Layer        | Purpose                   |
| ------------ | ------------------------- |
| Projectile   | Main gameplay object      |
| Trail        | Shows speed               |
| Live Emitter | Makes movement feel alive |
| Light        | Draws attention           |
| Impact       | Delivers power            |
| Aftermath    | Gives weight              |

---

# 6.3 Beam Pattern

Examples

* Laser
* Lightning beam
* Spirit ray

## Recipe

```text
Beam Core

+

Flow Map

+

Glow

+

Edge Sparks

+

Impact Burst
```

Guidelines

* Beam should remain readable.
* Effects should wrap around the beam.
* Never bury the beam beneath particles.

---

# 6.4 Sword Slash Pattern

Examples

* Katana
* Energy blade
* Metal slash

## Recipe

```text
Sword Motion

↓

Trail

↓

Ribbon

↓

Spark

↓

Tiny Flash

↓

Ground Decal
```

Avoid

Large smoke.

Large explosions.

A slash should feel clean.

---

# 6.5 Explosion Pattern

Examples

* Fire explosion
* Earth burst
* Magical detonation

## Recipe

```text
Flash

↓

Shockwave

↓

Main Explosion

↓

Debris

↓

Smoke

↓

Residual Light
```

The flash always comes first.

Smoke always comes last.

---

# 6.6 Portal Pattern

Examples

* Teleport
* Summoning
* Dimensional gate

## Recipe

```text
Circle

↓

Flow Map

↓

Ribbon

↓

Orbiting Particles

↓

Distortion

↓

Glow
```

Portal energy should rotate.

Not explode.

---

# 6.7 Aura Pattern

Examples

* Buff
* Charge
* Meditation

## Recipe

```text
Emitter

↓

Orbit

↓

Curl Noise

↓

Ribbon

↓

Soft Glow
```

Aura should feel alive,

not noisy.

---

# 6.8 Dragon Pattern

Examples

* Fire Dragon
* Water Dragon
* Spirit Dragon

## Recipe

```text
Dragon Head

↓

Body Motion

↓

Ribbon Spine

↓

Emitter

↓

Glow

↓

Trail

↓

Impact
```

Important

The dragon itself should remain visible.

Particles should enhance,

not hide,

the dragon.

---

# 6.9 Meteor Pattern

Examples

* Fire meteor
* Earth meteor

## Recipe

```text
Meteor

↓

Long Trail

↓

Smoke

↓

Light

↓

Ground Impact

↓

Debris

↓

Shockwave

↓

Dust
```

The travel phase is as important as the impact.

---

# 6.10 Tornado Pattern

## Recipe

```text
Vortex Motion

↓

Ribbon Spiral

↓

Particles

↓

Debris

↓

Mist

↓

Leaves
```

The tornado should rotate because of forces,

not because particles are manually placed in circles.

---

# 6.11 Summon Pattern

Examples

* Sword summon
* Spirit beast
* Stone guardian

## Recipe

```text
Portal

↓

Materialization

↓

Glow

↓

Shape Reveal

↓

Idle Aura
```

Do not instantly spawn the object.

Reveal it.

---

# 6.12 Shield Pattern

## Recipe

```text
Core Shape

↓

Flow Map

↓

Energy Edge

↓

Ripple On Hit

↓

Light Pulse
```

A shield should feel stable,

not explosive.

---

# 6.13 Chain Lightning Pattern

## Recipe

```text
Charge

↓

Main Arc

↓

Secondary Arcs

↓

Ground Sparks

↓

Residual Electricity
```

Every arc should seek a destination.

Never draw perfectly straight electrical lines.

---

# 6.14 Healing Pattern

## Recipe

```text
Soft Glow

↓

Particles Rise

↓

Leaves / Light

↓

Pulse

↓

Fade
```

Healing should feel calm.

Never explosive.

---

# 6.15 Charge Pattern

Charge appears before many ultimate skills.

## Recipe

```text
Weak Glow

↓

Particles Gather

↓

Rotation

↓

Compression

↓

Flash

↓

Skill Release
```

The player should feel increasing pressure.

---

# 6.16 Dash Pattern

## Recipe

```text
Character Motion

↓

Afterimage

↓

Trail

↓

Dust

↓

Arrival Flash
```

Movement should remain readable.

Do not completely hide the character.

---

# 6.17 Impact Pattern

Every powerful hit should include several layers.

```text
Hit

↓

Flash

↓

Light

↓

Sparks

↓

Camera Shake

↓

Distortion

↓

Smoke
```

The exact layers depend on the skill.

Not every impact needs all of them.

---

# 6.18 Layer Combinations

These combinations generally work well.

## Fast Metal Skill

```text
Trail

+

Ribbon

+

Spark

+

Tiny Flash
```

---

## Water Skill

```text
Trail

+

Mist

+

Ribbon

+

Droplets
```

---

## Fire Skill

```text
Flames

+

Smoke

+

Embers

+

Light
```

---

## Earth Skill

```text
Dust

+

Debris

+

Shockwave

+

Ground Crack
```

---

## Wood Skill

```text
Leaves

+

Roots

+

Spores

+

Soft Glow
```

---

## Taiji Skill

```text
Energy Filaments

+

Electric Arcs

+

Orbiting Particles

+

Bloom Pulse

+

Distortion
```

---

# 6.19 API Selection Guide

Choose the API based on artistic intent.

| Artistic Goal         | Preferred API               |
| --------------------- | --------------------------- |
| Continuous movement   | Trail                       |
| Long elegant energy   | Ribbon                      |
| Persistent atmosphere | Emitter                     |
| Random detail         | Particle                    |
| Internal motion       | Flow Map                    |
| Organic movement      | ForceField                  |
| Strong impact         | Camera + Distortion + Light |
| Ground reaction       | Decal                       |
| Color transition      | Gradient                    |
| Animated texture      | Sprite Animation            |

Do not solve every problem with `SpawnParticle()`.

Use the most appropriate system.

---

# 6.20 Layer Priority

Always build in this order.

```text
Gameplay

↓

Core Shape

↓

Motion

↓

Energy

↓

Lighting

↓

Environment

↓

Atmosphere

↓

Fine Detail
```

If time is limited,

remove the lowest layers first.

Never remove

* Core Shape
* Motion

---

# 6.21 Common Recipes to Avoid

Avoid these combinations.

❌ Ribbon without motion.

❌ Smoke without impact.

❌ Light without a source.

❌ Distortion on weak attacks.

❌ Camera shake every hit.

❌ Huge bloom on every skill.

❌ Ten emitters doing the same thing.

❌ Trails longer than the projectile's readable path.

❌ Sparks floating slowly.

❌ Fire behaving like liquid.

---

# 6.22 Design Formula

Every new skill can be designed using the following formula.

```text
Fantasy

↓

Timeline

↓

Element Identity

↓

Core Shape

↓

Motion Pattern

↓

Force Pattern

↓

Layer Recipe

↓

API Mapping

↓

Code
```

The AI should never start by asking:

> "Which API should I call?"

Instead, ask:

> "What fantasy am I trying to make the player believe?"

The API is only the implementation.

The fantasy is the design.

---

# Final Principle

A cookbook is not a collection of finished dishes.

It is a collection of proven recipes.

Master these recipes first.

Then combine them creatively.

The best Wuxing skills should feel familiar in craftsmanship, yet unique in imagination.

# Chapter 7 - AI Skill Creation Workflow

> This chapter defines the complete workflow that every AI must follow when creating a new skill.
>
> **Never start from code.**
>
> Every skill should be designed from the gameplay fantasy downward until it naturally maps to the Wuxing Core API.

---

# 7.1 The Design Pyramid

Every skill is created from the top down.

```text
Fantasy
    ↓
Gameplay
    ↓
Element
    ↓
Timeline
    ↓
Visual Language
    ↓
Layer Composition
    ↓
Force Design
    ↓
API Mapping
    ↓
Implementation
```

Never skip a level.

If a higher level is weak,

the lower levels cannot save the design.

---

# 7.2 Step 1 — Define the Fantasy

The first question is never

> "How do I implement this?"

The first question is

> "What fantasy is the player experiencing?"

Examples

Good

* Summon celestial swords.
* Become a flowing river.
* Split the earth open.
* Command a dragon.
* Trap enemies inside roots.
* Compress lightning before releasing it.

Bad

* Spawn particles.
* Draw ribbon.
* Create explosion.

Technology is never the fantasy.

---

# 7.3 Step 2 — Define Gameplay

Every skill has one gameplay purpose.

Choose only one primary purpose.

Examples

Projectile

AOE

Dash

Trap

Shield

Buff

Summon

Beam

Orbit

Rain

Once gameplay is fixed,

visual decisions become much easier.

---

# 7.4 Step 3 — Choose the Element

Determine which visual language to use.

Do **not** choose based only on color.

Choose based on

* Motion
* Force
* Rhythm
* Shape
* Emotion

Example

A fast precise projectile

→ Metal

A flowing projectile

→ Water

A violent projectile

→ Fire

---

# 7.5 Step 4 — Build the Timeline

Determine the major phases.

Example

```text
Birth

↓

Charge

↓

Release

↓

Travel

↓

Impact

↓

Aftermath

↓

Fade
```

This becomes the backbone of the entire skill.

---

# 7.6 Step 5 — Define the Core Shape

Ask

> "If every particle disappears,
> what remains?"

Examples

Sword

Dragon

Fireball

Tree

Meteor

Portal

The Core Shape is the identity.

Everything else supports it.

---

# 7.7 Step 6 — Build the Layers

Build from large to small.

```text
Core Shape

↓

Motion

↓

Energy

↓

Lighting

↓

Environment

↓

Atmosphere

↓

Details
```

Never begin with details.

---

# 7.8 Step 7 — Design Motion

Motion has three levels.

Primary

Gameplay movement.

Secondary

Natural support.

Micro

Procedural life.

Example

Projectile

Primary

Forward.

Secondary

Gentle spiral.

Micro

Noise.

---

# 7.9 Step 8 — Design Forces

Every force should have a reason.

Example

Fire

Radial

*

Noise

*

Wind

Example

Water

Vortex

*

Viscosity

*

Curl

Example

Metal

Drag

*

Tiny directional gravity

Never randomly combine forces.

---

# 7.10 Step 9 — Select API

Only now should API be selected.

Example

Need continuous motion

→ Trail

Need persistent aura

→ Emitter

Need flowing texture

→ Flow Map

Need impact

→ Light

*

Camera

*

Distortion

API is chosen because of artistic needs,

never the opposite.

---

# 7.11 Step 10 — Write Code

Implementation should now be straightforward.

The code simply executes the design.

Good code follows the artistic blueprint.

Bad code invents the design while programming.

---

# 7.12 Example Workflow

Example

Metal Flying Sword

## Fantasy

The player commands a divine sword.

---

## Gameplay

Projectile.

---

## Element

Metal.

---

## Timeline

```text
Charge

↓

Launch

↓

Flight

↓

Impact

↓

Fade
```

---

## Core Shape

Sword.

---

## Motion

Fast.

Straight.

Tiny vibration.

---

## Forces

Directional gravity.

Light drag.

---

## Layers

Sword

*

Trail

*

Ribbon

*

Spark

*

Light

---

## API

Trail

Particle

Light

Gradient

Trail Update

---

## Code

Implementation only.

No artistic decisions remain.

---

# 7.13 Another Example

Fire Meteor

Fantasy

A burning star falls from heaven.

Gameplay

AOE Projectile.

Timeline

```text
Sky

↓

Meteor

↓

Long Fall

↓

Ground Impact

↓

Explosion

↓

Smoke
```

Core

Meteor.

Layers

Rock

*

Fire

*

Smoke

*

Trail

*

Light

*

Shockwave

Forces

Radial.

Noise.

Wind.

Only after this should the implementation begin.

---

# 7.14 AI Self-Correction

After generating a skill,

review it.

Ask

Does every layer have a reason?

Does every force support the fantasy?

Is every particle contributing?

Can something be removed?

Can timing improve?

Can readability improve?

If yes,

improve before finalizing.

---

# 7.15 Continuous Improvement

When modifying an existing skill,

do **not** immediately change code.

Instead evaluate

```text
Fantasy

↓

Timeline

↓

Identity

↓

Layers

↓

Motion

↓

API

↓

Implementation
```

Many problems originate from design,

not implementation.

---

# 7.16 Golden Workflow

Every skill should ultimately follow this sequence.

```text
Fantasy
      ↓
Gameplay
      ↓
Element
      ↓
Timeline
      ↓
Core Shape
      ↓
Motion
      ↓
Force Design
      ↓
Layer Composition
      ↓
Lighting
      ↓
Environment
      ↓
API Mapping
      ↓
Implementation
      ↓
Review
      ↓
Polish
```

Skipping steps usually produces generic effects.

Following the workflow consistently produces skills that are cohesive, readable, and visually memorable.

---

# 7.17 AI Final Checklist

Before returning any C99 implementation, verify all of the following:

## Design

* □ The fantasy is clear.
* □ The gameplay purpose is obvious.
* □ The element's visual language is respected.

## Timeline

* □ The skill has anticipation.
* □ The impact is the visual climax.
* □ The aftermath exists.

## Composition

* □ The Core Shape is always readable.
* □ Every layer has exactly one purpose.
* □ Motion includes primary, secondary and micro movement.

## Technical

* □ The selected APIs match the artistic intent.
* □ No API is used without purpose.
* □ The implementation follows the Core API architecture.

## Quality

* □ The skill could belong anywhere else? **No.**
* □ The skill is unmistakably Wuxing? **Yes.**

Only when every item passes should the AI generate the final implementation.

---

# Final Principle

The Wuxing Core API provides the **tools**.

The Art Direction provides the **vision**.

This workflow is the **bridge** between them.

An AI that follows this process will not merely generate effects—it will design complete skills with clear identity, coherent structure, strong gameplay readability, and a consistent artistic language worthy of the Wuxing universe.
