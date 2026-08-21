# Graph Report - core  (2026-08-21)

## Corpus Check
- 223 files · ~367,087 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 2108 nodes · 4426 edges · 145 communities (141 shown, 4 thin omitted)
- Extraction: 93% EXTRACTED · 7% INFERRED · 0% AMBIGUOUS · INFERRED: 322 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Decal System
- Uv Deform
- Post Fx
- Ribbon Strip
- Swept Trail Test
- Particle System
- Fluid Surface
- Silhouette Test
- Map Manager
- Force Field
- Particle Manager
- Trail System
- Trail Deform Test
- Skill Manager
- Particle System
- Trail System
- Trail System
- Particle Lighting Test
- Tube Frame Test
- Material System
- Status Vfx
- Uv Deform Test
- Particle Gpu Backend
- Shock Ring Test
- Fluid Impact
- Surface Material
- Particle System
- Sweep Slash Test
- Fluid Surface Noise Test
- Scene Targets
- Volume Trail Test
- Sprite Anim
- Audio System
- Fluid Pbd Gpu
- Skill Helper
- Ground Wave Test
- Skill Manager
- Atmosphere
- Portal Disc Test
- Material System
- Visual Composer
- Scene Targets
- Core Glow Test
- Mesh Deform Test
- Pm Tube Offset Clamp
- Vc Motion
- Vfx Sequence
- Time Fx
- Resource Manager
- Scene Targets
- Vfx Presets
- Skill Manager
- Converge Motes Test
- Pm Tube Envelope Anchor
- Flame Motion Test
- Skill Helper
- Procedural Mesh Utils
- Vfx Light Space Test
- Bright Vfx Isolation Test
- Light Shaft Test
- Shield Shell Test
- Volume Optical Depth Test
- Metaball Fx
- Skill Manager
- Debris Shards Test
- Smoke Column Test
- Spark Trail Test
- Afterimage
- Float Curve
- Mesh Adjacency
- Vfx Surface Registry
- Beam Geometry Test
- Pm Tube Fade Anchor
- Shader Permutation Test
- Skill Helper
- Utils Math
- Flow Map Test
- Fluid Dual Depth Test
- Gfx Tier Test
- Texture Packing Test
- Trail Cloth Test
- Flow Map
- Gfx Quality
- Motion Controller
- Fluid Depth Filter Test
- Shader Uniform Wiring Test
- Camera Fx
- Debug Draw
- Mesh Cache
- Fluid Specular Aa Test
- Pm Tube Envelope Coordinate
- Rune Circle Quality Test
- Soft Depth Region Test
- Trail Geom Segs Test
- Trail Noise Material Anchor
- Volume Space Contract Test
- Material System
- Vfx Sequence Test
- Shader Preprocessor
- Bloom Pyramid Contract Test
- Color Grade Lut Test
- Fluid Surface Optics Test
- Skill Helper
- Particle System
- Skill Manager
- Skill Manager
- Fluid Cost Gate Test
- Fluid Liquid Material Test
- Frame Delta Determinism Test
- Vc Material
- Render Target Probe
- Skill Helper
- Bloom Thin Emitter Contract
- Contract Path Test
- Ember Trail Bright Contract
- Energy Burst Semantic Layers
- Fluid Anisotropic Splat Test
- Fluid Capture Projection Test
- Fluid Filter 2d Test
- Fluid Pbd Grid Stamp
- Lightning Arc Contract Test
- Lightning Trail Contract Test
- Scene Target Alpha Contract
- Shader Stage Interface Test
- Skill Manager
- Particle System
- Composition Tu Test
- Decal System Test
- Fluid Refraction Source Test
- Fluid Silhouette Coverage Test
- Fxaa Pass Test
- Particle Appearance Adoption Test
- Particle Glow Recipe Test
- Smoke Fire Emitter Test
- Vfx Unified Render Contract
- Water Ring Coverage Test
- Skill Manager
- Particle Manager Contract Test
- Vfx Render Layers Contract
- Vfx Surface Registry Test
- Volume Shipping Gate Test

## God Nodes (most connected - your core abstractions)
1. `ParticleConfig` - 69 edges
2. `ParticleRadialBurstConfig` - 30 edges
3. `DrawTrailEntitiesLayer()` - 26 edges
4. `DrawTrailGeometry()` - 23 edges
5. `Render()` - 22 edges
6. `main()` - 22 edges
7. `DecalSystem_AddConformalMaterialEx()` - 20 edges
8. `GetRandomValue()` - 20 edges
9. `FluidImpact_SpawnWater()` - 19 edges
10. `ResourceManager_LoadShader()` - 19 edges

## Surprising Connections (you probably didn't know these)
- `Afterimage_Init()` --calls--> `Material_LoadCustom()`  [INFERRED]
  afterimage.c → material/material_system.c
- `Afterimage_Draw()` --calls--> `VFXRender_BeginDraw()`  [INFERRED]
  afterimage.c → scene_targets.c
- `Afterimage_Draw()` --calls--> `VFXRender_EndDraw()`  [INFERRED]
  afterimage.c → scene_targets.c
- `Atmosphere_Draw()` --calls--> `VFXRender_BeginDraw()`  [INFERRED]
  atmosphere.c → scene_targets.c
- `ResolveSfx()` --calls--> `ResourceManager_LoadSound()`  [INFERRED]
  audio_system.c → resource_manager.c

## Import Cycles
- None detected.

## Communities (145 total, 4 thin omitted)

### Community 0 - "Decal System"
Cohesion: 0.06
Nodes (88): DecalEntity, DecalHandle, DecalMaterialParams, DecalRenderStats, BlendMode, Camera3D, Color, Texture2D (+80 more)

### Community 1 - "Uv Deform"
Cohesion: 0.05
Nodes (66): Shader, Vector2, Vector3, MD_Hash(), MD_Smooth(), MeshDeform_AddLayer(), MeshDeform_Apply(), MeshDeform_CacheLocations() (+58 more)

### Community 2 - "Post Fx"
Cohesion: 0.05
Nodes (54): Texture2D, ColorGradeLut_Adopt(), ColorGradeLut_BuildNeutralImage(), ColorGradeLut_Init(), ColorGradeLut_IsNeutral(), ColorGradeLut_Load(), ColorGradeLut_Texture(), ColorGradeLut_Unload() (+46 more)

### Community 3 - "Ribbon Strip"
Cohesion: 0.09
Nodes (57): Camera3D, Vector3, LightningStroke_BuildPath(), LightningStroke_CopyPath(), LightningStroke_DefaultConfig(), LightningStroke_DeriveSeed(), LightningStroke_DrawLayer(), LightningStroke_DrawWarpedPath() (+49 more)

### Community 4 - "Swept Trail Test"
Cohesion: 0.09
Nodes (50): Stop2, Style, AspectK(), BenchPath(), BladeProfile(), Stop, CollapseWS(), CountTurns() (+42 more)

### Community 5 - "Particle System"
Cohesion: 0.04
Nodes (50): SkillCurve, SpriteAnim, ParticleConfig, alphaCurve, angularVelocity, animation, collisionElasticity, collisionEnabled (+42 more)

### Community 6 - "Fluid Surface"
Cohesion: 0.09
Nodes (39): FluidImpact_Draw(), FluidPBDGPU_GetMaterial(), FluidPBDGPU_IsActive(), Camera3D, Color, Matrix, ParticleRenderStream, RenderTexture2D (+31 more)

### Community 7 - "Silhouette Test"
Cohesion: 0.19
Nodes (32): EdgeMethod, add(), BuildCube(), BuildTube(), Mesh, CollapseWS(), cross(), dot() (+24 more)

### Community 8 - "Map Manager"
Cohesion: 0.08
Nodes (22): Vector3, FluidPBD_GetRenderParticles(), FluidPBD_Init(), FluidPBD_SpawnImpact(), FluidPBD_Update(), PBD_Basis(), PBD_CellIndex(), PBD_SafeNormal() (+14 more)

### Community 9 - "Force Field"
Cohesion: 0.14
Nodes (30): ForceField, ForceLayer, Vector3, FastFloor(), ForceField_AddLayer(), ForceField_Clear(), ForceField_Evaluate(), ForceField_GetViscosityDamping() (+22 more)

### Community 10 - "Particle Manager"
Cohesion: 0.13
Nodes (29): FluidImpact_EmitBackground(), GpuParticleConfig, ParticleEmitterDesc, ParticleEmitterStatus, ParticleManagerStats, GpuParticleSystem_Spawn(), Camera3D, ParticleEmitterHandle (+21 more)

### Community 11 - "Trail System"
Cohesion: 0.11
Nodes (27): Matrix, TrailConfig, Vector3, VFXPriority, CatmullRom(), ComputeWispStyleTaper(), ConstrainRibbonSegment(), EvictLowestPriorityTrail() (+19 more)

### Community 12 - "Trail Deform Test"
Cohesion: 0.17
Nodes (28): BranchForMode(), CollapseWS(), EnergyBodyCoverage(), EnergyStructure(), FileHas(), main(), MirrorEnergyBlade(), MirrorEnv() (+20 more)

### Community 13 - "Skill Manager"
Cohesion: 0.07
Nodes (4): AgentPosProviderFn, NearbyTargetsProviderFn, SkillManager_SetAgentPosProvider(), SkillManager_SetNearbyTargetsProvider()

### Community 14 - "Particle System"
Cohesion: 0.07
Nodes (28): Color, ColorGradient, ForceField, ParticleRadialBurstConfig, animation, colorEnd, colorStart, countMax (+20 more)

### Community 15 - "Trail System"
Cohesion: 0.17
Nodes (28): TimeFX_Elapsed(), TrailCameraBasis, TrailEntity, TrailLayer, ApplyAnchoredHelix(), ApplyDeformUniforms(), Color, Rectangle (+20 more)

### Community 16 - "Trail System"
Cohesion: 0.16
Nodes (27): DeformLocs, RenderGroup, BlendMode, Camera3D, Shader, CacheShaderLocs(), DrawTrailEntities(), DrawTrailEntitiesBody() (+19 more)

### Community 17 - "Particle Lighting Test"
Cohesion: 0.24
Nodes (26): FileContains(), LightFromAzimuth(), LitAt(), main(), ScreenNormal(), SoftFactor(), Test_AmbientGainFlattens(), Test_AzimuthSweepRotatesBrightSide() (+18 more)

### Community 18 - "Tube Frame Test"
Cohesion: 0.24
Nodes (26): AccumulatedRoll(), add(), CollapseWS(), crs(), dot(), FileHas(), FrameFromReference(), FrameParallelTransport() (+18 more)

### Community 19 - "Material System"
Cohesion: 0.14
Nodes (24): AuraShellMaterial, CrystalMaterialInstanced, EffectMaterialInstanced, AuraShellMaterial_Begin(), AuraShellMaterial_End(), Shader, CrystalMaterial_Begin(), CrystalMaterial_End() (+16 more)

### Community 21 - "Status Vfx"
Cohesion: 0.12
Nodes (15): Vector3, CreateEmitter(), StopEmitter(), UpdateEmitterTarget(), EmitterConfig, SkillManager_GetAgentPos(), Color, EffectPresetType (+7 more)

### Community 22 - "Uv Deform Test"
Cohesion: 0.20
Nodes (23): AcrossU(), AlongV(), CollapseWS(), Envelope(), FileHas(), FileHasCode(), FileHasImpl(), FoldAngle() (+15 more)

### Community 23 - "Particle Gpu Backend"
Cohesion: 0.13
Nodes (21): Camera3D, ForceField, Texture2D, Vector3, CompileComputeShader(), GpuParticleSystem_ActiveCount(), GpuParticleSystem_Draw(), GpuParticleSystem_DrawDebug() (+13 more)

### Community 24 - "Shock Ring Test"
Cohesion: 0.20
Nodes (22): Alpha01(), CanvasWidth(), CollapseWS(), CoreWidth(), Detail(), FileHas(), HalfThickness(), main() (+14 more)

### Community 25 - "Fluid Impact"
Cohesion: 0.19
Nodes (20): Color, ParticleEmitterHandle, Vector3, FluidImpact_AddResidue(), FluidImpact_Basis(), FluidImpact_ColorIsUnset(), FluidImpact_CreateSurfaceEmitter(), FluidImpact_DefaultGroundHit() (+12 more)

### Community 26 - "Surface Material"
Cohesion: 0.11
Nodes (14): Camera3D, Color, Model, Shader, Texture2D, Vector3, ColorToVec3(), SurfaceMaterial_Apply() (+6 more)

### Community 27 - "Particle System"
Cohesion: 0.17
Nodes (16): Camera3D, Color, Texture2D, Vector3, ColorToVec3_Particle(), DrawParticles(), DrawParticlesBody(), DrawParticlesEmission() (+8 more)

### Community 28 - "Sweep Slash Test"
Cohesion: 0.22
Nodes (20): Clamp01(), main(), MaskAlpha(), Profile(), Schedule(), SlurpFile(), SmoothStep01(), Test_BandExistsWhileTheSwingReads() (+12 more)

### Community 29 - "Fluid Surface Noise Test"
Cohesion: 0.19
Nodes (17): Color, ColorGradient, Texture2D, ColorGradient_AddStop(), ColorGradient_BakeLUT(), ColorGradient_MakeElectric(), ColorGradient_Sample(), ColorGradient_StandardFade() (+9 more)

### Community 30 - "Scene Targets"
Cohesion: 0.14
Nodes (13): ParticleLighting_End(), Rectangle, RenderTexture2D, LoadLinearDepthTarget(), LoadRenderTextureWithDepthTexture(), LoadSceneSnapshotTarget(), SceneTargets_EndVFXLayer(), SceneTargets_Init() (+5 more)

### Community 31 - "Volume Trail Test"
Cohesion: 0.23
Nodes (18): CollapseWS(), CountIn(), Emits(), FileHas(), LayerCount(), LoadFlat(), main(), MaxNodes() (+10 more)

### Community 32 - "Sprite Anim"
Cohesion: 0.23
Nodes (16): AnimPlayMode, Rectangle, SpriteAnim, SpriteAnim_CalculateFrameSampleBlend(), SpriteAnim_CalculateUV(), SpriteAnim_CalculateUVBlend(), SpriteAnim_FrameSample(), SpriteAnim_GetUVRect() (+8 more)

### Community 33 - "Audio System"
Cohesion: 0.18
Nodes (13): Audio_CastSfxForElement(), Audio_PlayMusic(), Audio_PlaySFX(), Audio_PlaySFXAt(), Audio_SetListener(), Audio_Shutdown(), Audio_StopMusic(), Sound (+5 more)

### Community 34 - "Fluid Pbd Gpu"
Cohesion: 0.16
Nodes (13): Camera3D, Shader, Vector3, FluidPBDGPU_ClearGrid(), FluidPBDGPU_Draw(), FluidPBDGPU_DrawSurfaceBackDepth(), FluidPBDGPU_DrawSurfaceDepth(), FluidPBDGPU_Init() (+5 more)

### Community 35 - "Skill Helper"
Cohesion: 0.17
Nodes (15): LayeredTimeline, DamageVolume_Init(), DamageVolume_Unload(), EmitterSystem_Init(), EmitterSystem_Unload(), InitHelperResources(), Timeline_AddLayer(), Timeline_Event() (+7 more)

### Community 36 - "Ground Wave Test"
Cohesion: 0.24
Nodes (17): Alpha01(), BandWidth(), FileHas(), LipHeight(), main(), Mix(), Profile(), Radius01() (+9 more)

### Community 37 - "Skill Manager"
Cohesion: 0.17
Nodes (17): Vector3, CastElectricWrapper(), CastFireWrapper(), CastMetalWrapper(), CastShieldWrapper(), CastTubeWrapper(), CastWaterWrapper(), CastWindWrapper() (+9 more)

### Community 38 - "Atmosphere"
Cohesion: 0.17
Nodes (13): Atmosphere_Configure(), Atmosphere_Draw(), Atmosphere_Init(), Atmosphere_Update(), Camera3D, Color, Vector3, Rand01() (+5 more)

### Community 39 - "Portal Disc Test"
Cohesion: 0.24
Nodes (15): Alpha01(), CollapseWS(), FileHas(), Interior(), main(), RimProfile(), Scale(), Slices() (+7 more)

### Community 40 - "Material System"
Cohesion: 0.22
Nodes (15): AuraShellMaterialParams, EffectMaterialParams, AuraShellMaterial_Load(), EffectMaterial, EffectMaterial_Bind(), EffectMaterialInstanced_Load(), Material_Get(), Material_LoadCustom() (+7 more)

### Community 41 - "Visual Composer"
Cohesion: 0.16
Nodes (5): Camera3D, VFX_Compose_Draw3D(), DecalMaterialDesc, DecalMaterialId, DecalMaterial_Get()

### Community 42 - "Scene Targets"
Cohesion: 0.18
Nodes (15): VFXAppearanceId, VFXContrastLayer, VFXResolvedAppearance, VFXSurfaceMode, SceneTargets_BeginVFXBody(), SceneTargets_BeginVFXEmission(), VFXRender_AppearanceDrawsPass(), VFXRender_BeginAppearance() (+7 more)

### Community 43 - "Core Glow Test"
Cohesion: 0.30
Nodes (14): CollapseWS(), CoreBoost(), CoreRadius(), FileHas(), HaloRadius(), main(), MidBoost(), MidRadius() (+6 more)

### Community 44 - "Mesh Deform Test"
Cohesion: 0.30
Nodes (14): BuildNoise(), CollapseWS(), FileHas(), main(), MD_Hash(), MD_Smooth(), SampleImage(), SampleLattice() (+6 more)

### Community 45 - "Pm Tube Offset Clamp"
Cohesion: 0.32
Nodes (14): ClampedScalar(), ClampOffset(), CollapseWS(), DeformFloor(), dot3(), FileHas(), main(), scl() (+6 more)

### Community 46 - "Vc Motion"
Cohesion: 0.27
Nodes (10): Vector3, VC_DirCone(), VC_MotionBob(), VC_MotionHelix(), VC_MotionJitter(), VC_MotionOrbit(), VC_MotionSpiralIn(), VC_MotionSpiralInAccrete() (+2 more)

### Community 47 - "Vfx Sequence"
Cohesion: 0.27
Nodes (10): VC_MaterialId, Vector3, VFX_SeqAt(), VFX_SeqBegin(), VFX_SeqPlay(), VFX_SeqPreset(), VFX_SeqReset(), VFX_SeqSetUnscaled() (+2 more)

### Community 48 - "Time Fx"
Cohesion: 0.15
Nodes (9): VFX_SeqFireBeat(), VFX_Sequence_Update(), VFX_Compose_Update(), Vector3, PostFX_RadialBurst(), Vector3, ScreenDistort_Add(), TimeFX_Hitstop() (+1 more)

### Community 49 - "Resource Manager"
Cohesion: 0.16
Nodes (11): Font, ModelAnimation, Model, Shader, Sound, LoadShaderProcessed(), ResourceManager_LoadFont(), ResourceManager_LoadModel() (+3 more)

### Community 50 - "Scene Targets"
Cohesion: 0.16
Nodes (14): ParticleLighting_Begin(), ParticleSystem_HasVolumeParticles(), Shader, Texture2D, GetSoftParticleLocs(), SceneTargets_BindDepthForSoftParticles(), SceneTargets_GetBackgroundLuma(), SceneTargets_GetDepthTexture() (+6 more)

### Community 51 - "Vfx Presets"
Cohesion: 0.22
Nodes (10): EffectPresetType, VC_MaterialId, VFX_MaterialFromPreset(), VFX_Preset_GetCast(), VFX_Preset_GetImpact(), VFX_Preset_GetProjectile(), VFX_Presets_Init(), VFX_CastPreset (+2 more)

### Community 52 - "Skill Manager"
Cohesion: 0.18
Nodes (13): Color, DrawCircleLines3D(), DrawSkillManagerOverlay(), DrawSkillManagerWorld3D(), EnsureBuiltInRegistered(), GetRegisteredSkillColor(), GetRegisteredSkillCount(), GetRegisteredSkillName() (+5 more)

### Community 53 - "Converge Motes Test"
Cohesion: 0.33
Nodes (13): Boost(), CollapseWS(), FileHas(), main(), Mix(), Pull(), RateMul(), Shell() (+5 more)

### Community 54 - "Pm Tube Envelope Anchor"
Cohesion: 0.31
Nodes (12): ChurnRoom, CapsuleCurve(), CollapseWS(), FileHas(), HeadWeld(), HeadWeldSq(), main(), Measure() (+4 more)

### Community 55 - "Flame Motion Test"
Cohesion: 0.29
Nodes (12): Curve, MotionResult, MotionSpec, CurveEval(), Integrate(), main(), Terminal(), Test_CoreStaysAtTheBase() (+4 more)

### Community 56 - "Skill Helper"
Cohesion: 0.17
Nodes (13): DecalPresetType, EffectMaterial, EffectPresetType, Material_LoadElement(), PlayCastSound(), PlayImpactSound(), SkillBuilder_AddCastEffect(), SkillBuilder_AddDamageVolume() (+5 more)

### Community 57 - "Procedural Mesh Utils"
Cohesion: 0.22
Nodes (10): Vector3, PMSweptSection_ClampOffset(), ProceduralMesh_BezierPoint(), ProceduralMesh_BezierTangent(), ProceduralMesh__Hash(), ProceduralMesh__Noise2(), Vector3, GetBezierPoint() (+2 more)

### Community 58 - "Vfx Light Space Test"
Cohesion: 0.42
Nodes (12): M4, attenuation(), cross(), dot(), len(), look_at(), main(), norm() (+4 more)

### Community 59 - "Bright Vfx Isolation Test"
Cohesion: 0.45
Nodes (12): aces(), aces1(), add(), additive(), Rgb, chroma(), layered(), main() (+4 more)

### Community 60 - "Light Shaft Test"
Cohesion: 0.31
Nodes (12): Clamp01(), main(), ShaftAlpha(), ShaftOffset(), ShaftProfile(), SlurpFile(), SmoothStep01(), Test_CrossProfileIsSymmetric() (+4 more)

### Community 61 - "Shield Shell Test"
Cohesion: 0.31
Nodes (12): CountIn(), EmissionBlock(), EmissionBlockHas(), GlassContactMirror(), GlassContactPlateau(), GlassFresnelPow4(), GlassRimBandHalfWidthFrac(), Has() (+4 more)

### Community 62 - "Volume Optical Depth Test"
Cohesion: 0.40
Nodes (12): ChordLength(), CollapseWS(), EdgeNew(), EdgeOld(), FileHas(), main(), NdotV(), SmoothStep() (+4 more)

### Community 63 - "Metaball Fx"
Cohesion: 0.23
Nodes (7): Camera3D, Color, Vector3, MetaballFX_Composite(), MetaballFX_DrawRegistered(), MetaballFX_Prepare(), MetaballFX_RegisterBlob()

### Community 64 - "Skill Manager"
Cohesion: 0.23
Nodes (12): DamageVolume_Update(), AddFloatingText(), AddKnockbackToEnemy(), ApplyAoEDamage(), SkillManager_GetEnemyAgentId(), UpdateElectricSkillWrapper(), UpdateFireSkillWrapper(), UpdateFluidSkillWrapper() (+4 more)

### Community 65 - "Debris Shards Test"
Cohesion: 0.33
Nodes (11): CollapseWS(), FileExists(), FileHas(), main(), Test_AuthoredShadingMakesTheTumbleVisible(), Test_ChipGeometry(), Test_CountVsTier(), Test_MirrorMatchesTheSource() (+3 more)

### Community 66 - "Smoke Column Test"
Cohesion: 0.33
Nodes (11): CollapseWS(), FileHas(), main(), ProfCapsule(), ProfDroplet(), ProfLegacy(), Test_FrozenPathIsExactOnFrameOne(), Test_MirrorStillMatchesSource() (+3 more)

### Community 67 - "Spark Trail Test"
Cohesion: 0.32
Nodes (11): Stop, CurveEval(), FileHas(), main(), SmoothStepC(), Test_Aspect(), Test_Budget(), Test_Lens() (+3 more)

### Community 68 - "Afterimage"
Cohesion: 0.18
Nodes (7): Afterimage_Draw(), Afterimage_Init(), Afterimage_Spawn(), Color, Matrix, Model, Material_SetFloat()

### Community 69 - "Float Curve"
Cohesion: 0.24
Nodes (8): FloatCurve_AddStop(), FloatCurve_Sample(), FloatCurve, SkillCurve, SkillCurve_Eval(), SkillCurve_SetConstant(), SkillCurve, SkillHelper_StepCurveFlight()

### Community 70 - "Mesh Adjacency"
Cohesion: 0.27
Nodes (9): Mesh, Vector3, MeshAdjacency_Build(), MeshAdjacency_GeneratePath(), MeshAdjacency_SampleEdge(), MeshAdjacency_SampleVertex(), Matrix, MeshAdjacency (+1 more)

### Community 71 - "Vfx Surface Registry"
Cohesion: 0.20
Nodes (9): Texture2D, ResourceManager_LoadTexture(), Texture2D, VFX_SurfaceRegistry_Get(), VFX_SurfaceRegistry_LoadTexture(), VFX_SurfaceFilter, VFX_SurfaceId, VFX_SurfaceProfile (+1 more)

### Community 72 - "Beam Geometry Test"
Cohesion: 0.38
Nodes (10): CollapseWS(), FileHas(), main(), SmoothStep(), Test_CoincidentEndpointsHideRatherThanDrawGarbage(), Test_EndOnKillsTheVolumeShadedBody(), Test_MirrorMatchesSource(), Test_ProbeStripsEverythingThatMovesTheEdge() (+2 more)

### Community 73 - "Pm Tube Fade Anchor"
Cohesion: 0.40
Nodes (10): CapsuleTrail(), CollapseWS(), FileHas(), main(), Mask(), SS(), Test_MirrorMatchesSource(), Test_OpacityAndWidthNoLongerFightEachOther() (+2 more)

### Community 74 - "Shader Permutation Test"
Cohesion: 0.40
Nodes (10): FileExists(), FileHas(), LineIsCommentedInclude(), main(), ReadAll(), ScanForCommentedIncludes(), Test_BothMaterialsUseTheSeam(), Test_InstanceAttributeIsExclusiveToTheInstancedHeader() (+2 more)

### Community 75 - "Skill Helper"
Cohesion: 0.29
Nodes (10): EmitterPreset, MeshPresetType, Color, Vector3, DrawCorePyramid(), DrawCoreTetrahedron(), DrawEffectMesh(), Emitter_AttachToPoint() (+2 more)

### Community 76 - "Utils Math"
Cohesion: 0.27
Nodes (8): ParticleRadialBurstConfig_Unify(), ParticleSystem_SpawnGlow(), ParticleSystem_SpawnRadialBurst(), SpawnParticle(), EmitterSystem_Update(), GetRandomValue(), Math_Mix(), Random01()

### Community 77 - "Flow Map Test"
Cohesion: 0.40
Nodes (9): CollapseWS(), Displacement(), FileHas(), main(), Test_EncodingNeverClips(), Test_MirrorStillMatchesSource(), Test_TheShownLayerIsTheLeastStretched(), Test_TrailFieldTilesOnBothAxes() (+1 more)

### Community 78 - "Fluid Dual Depth Test"
Cohesion: 0.40
Nodes (9): BackViewZ(), Distance(), FrontViewZ(), main(), ReadFile(), ReduceBack(), ReduceFront(), SphereZ() (+1 more)

### Community 79 - "Gfx Tier Test"
Cohesion: 0.44
Nodes (9): ApplyTier(), GfxQuality, main(), SlurpFile(), Test_SourcesStillGate(), Test_TheGateNeverEnables(), Test_TheSpecTable(), Test_TiersAreMonotonic() (+1 more)

### Community 80 - "Texture Packing Test"
Cohesion: 0.44
Nodes (9): CollapseWS(), FileHas(), main(), Slurp(), Test_EveryAssetDeclaresALayout(), Test_NoMipmapsIsStillTrue(), Test_ReferenceSheetsMatchTheSpecsClaim(), Test_SpecIsPresentAndComplete() (+1 more)

### Community 81 - "Trail Cloth Test"
Cohesion: 0.38
Nodes (9): CollapseWS(), FileHas(), main(), Test_ClothFloorProtectsTheTangent(), Test_LayerBudget(), Test_MaterialUVBeatsSegRatioUV(), Test_MirrorStillMatchesSource(), Test_OrderBoundIsRelativeToSpacing() (+1 more)

### Community 82 - "Flow Map"
Cohesion: 0.47
Nodes (8): FlowMap, Shader, Texture2D, FlowMap_Apply(), FlowMap_Create(), FlowMap_CreateWithTrailTexture(), FlowMap_CreateWithVortexTexture(), FlowMap_Unload()

### Community 83 - "Gfx Quality"
Cohesion: 0.28
Nodes (8): FluidImpact_BackgroundCount(), FluidImpact_HeroCount(), GfxQuality, GfxQuality_Default(), GfxQuality_Get(), GfxQuality_Set(), ParticleGPUCaps, ParticleSystem_GetGPUCaps()

### Community 84 - "Motion Controller"
Cohesion: 0.28
Nodes (6): Vector3, Motion_Arrived(), Motion_Init(), Motion_Step(), MotionParams, MotionState

### Community 85 - "Fluid Depth Filter Test"
Cohesion: 0.39
Nodes (7): AdaptiveRadius(), main(), ReachPixels(), ReadFile(), SigmaS(), SurvivingBump(), TapsAcrossGap()

### Community 86 - "Shader Uniform Wiring Test"
Cohesion: 0.44
Nodes (8): CheckPairMulti(), CollectUniforms(), IsEngineBound(), main(), MentionsQuoted(), SlurpFile(), SlurpShaderWithIncludes(), StripLineComments()

### Community 87 - "Camera Fx"
Cohesion: 0.29
Nodes (6): Camera3D, CameraFX_Shake(), CameraFX_Update(), Noise(), CameraImpulse, CameraFX_AddImpulse()

### Community 88 - "Debug Draw"
Cohesion: 0.32
Nodes (4): Color, Vector3, DebugDraw_Circle(), DebugDraw_Sphere()

### Community 89 - "Mesh Cache"
Cohesion: 0.32
Nodes (5): MeshCache_GetIce(), MeshCache_GetRock(), MeshCache_GetRockEx(), RockMeshData, ShardClusterMeshData

### Community 90 - "Fluid Specular Aa Test"
Cohesion: 0.46
Nodes (7): BlinnExponentScale(), Clamp01(), FilteredRoughness(), LobeEnergy(), main(), ReadFile(), WidenedAmplitude()

### Community 91 - "Pm Tube Envelope Coordinate"
Cohesion: 0.46
Nodes (7): CollapseWS(), FileHas(), HeadWeldSq(), main(), SmoothStep(), Test_MirrorMatchesSource(), Test_ScaledSurfCoordinateShrinksEnvelopeQuadratically()

### Community 92 - "Rune Circle Quality Test"
Cohesion: 0.50
Nodes (7): Additive(), AlphaOver(), Rgb, Chroma(), Has(), main(), ReadFile()

### Community 93 - "Soft Depth Region Test"
Cohesion: 0.46
Nodes (7): ExpectedSourceRow(), Has(), main(), NearlyEqual(), SampledSourceRow(), SoftDepthDestY(), SoftDepthDestY_Broken()

### Community 94 - "Trail Geom Segs Test"
Cohesion: 0.46
Nodes (7): CollapseWS(), FileHas(), main(), SlopeDegrees(), Test_MirrorMatchesSource(), Test_SliceCountAlsoSetsTheOffsetCeiling(), Test_TightRingsMakeTheSurfaceVertical()

### Community 95 - "Trail Noise Material Anchor"
Cohesion: 0.50
Nodes (7): CollapseWS(), CoordinateDriftInCells(), FileHas(), main(), Test_DriftScalesWithWindowNotWithTuning(), Test_MirrorMatchesSource(), Test_MovingAnchorDragsTheNoiseAcrossTheMaterial()

### Community 96 - "Volume Space Contract Test"
Cohesion: 0.54
Nodes (7): CollapseWS(), FileHas(), main(), StripComments(), Test_TheTwoStagesAgree(), Test_VertexStageDoesNotDoubleTransform(), Test_ViewVectorIsViewSpace()

### Community 97 - "Material System"
Cohesion: 0.43
Nodes (7): CrystalMaterial, CrystalMaterialParams, CrystalMaterial_Bind(), CrystalMaterial_Load(), CrystalMaterialInstanced_Load(), MatApplyTextureDefaults(), VfxTextureDefault

### Community 99 - "Vfx Sequence Test"
Cohesion: 0.57
Nodes (6): Seq, main(), seq_add(), seq_play(), seq_update(), slurp()

### Community 100 - "Shader Preprocessor"
Cohesion: 0.48
Nodes (5): InjectDefines(), ProcessIncludes(), RewriteVersionForGLES(), ShaderPreprocessor_Load(), ShaderPreprocessor_LoadWithDefines()

### Community 101 - "Bloom Pyramid Contract Test"
Cohesion: 0.52
Nodes (6): KarisWeightedAverage(), main(), ReadFile(), Reject(), Require(), SoftCeiling()

### Community 102 - "Color Grade Lut Test"
Cohesion: 0.52
Nodes (6): ApplyLut(), BuildNeutralStrip(), main(), ReadFile(), Require(), SampleStrip()

### Community 103 - "Fluid Surface Optics Test"
Cohesion: 0.52
Nodes (6): Dot3(), main(), PerturbNormal(), PickGradientZ(), ReadFile(), WaterColumnDepth()

### Community 104 - "Skill Helper"
Cohesion: 0.33
Nodes (6): DamageVolume, EffectPresetType, VC_MaterialId, SkillBuilder_Build(), SkillHelper_PresetMaterial(), SpawnDamageVolume()

### Community 105 - "Particle System"
Cohesion: 0.40
Nodes (6): ParticleManager_CopySurfaceSamples(), Vector3, ParticleSurfaceSample, position, radius, ParticleSystem_GetSurfaceSamples()

### Community 106 - "Skill Manager"
Cohesion: 0.47
Nodes (6): SkillTunableEntry, RegisterSkillTunables(), Skill_GetTunables(), SkillTunables_Flatten(), SkillTunables_LoadPersisted(), SkillTunables_Unflatten()

### Community 107 - "Skill Manager"
Cohesion: 0.33
Nodes (6): Skill_CalculateCooldown(), Skill_CalculateDamage(), Skill_CalculateKnockback(), Skill_CalculateManaCost(), Skill_GetCategoryName(), SkillCategory

### Community 108 - "Fluid Cost Gate Test"
Cohesion: 0.67
Nodes (5): Admit(), AdmitEx(), main(), ProjectedRadiusPx(), ReadFile()

### Community 109 - "Fluid Liquid Material Test"
Cohesion: 0.60
Nodes (5): BindSlot(), DecodeSlot(), EmissionDepth(), main(), ReadFile()

### Community 110 - "Frame Delta Determinism Test"
Cohesion: 0.80
Nodes (5): HasWallClock(), main(), ReadFile(), Require(), ScanTreeForWallClock()

### Community 111 - "Vc Material"
Cohesion: 0.50
Nodes (3): Color, VC_Premultiply(), VC_Whiten()

### Community 112 - "Render Target Probe"
Cohesion: 0.50
Nodes (4): RenderTexture2D, RenderTargetProbe_Dump(), RenderTargetProbe_MatchesFrame(), SceneTargets_End()

### Community 113 - "Skill Helper"
Cohesion: 0.40
Nodes (5): ForceField, SkillTunableEntry, SkillForceMix_AddLayers(), SkillForceMix_MakeTunables(), SkillForceMix

### Community 114 - "Bloom Thin Emitter Contract"
Cohesion: 0.70
Nodes (4): BrightestOfCell(), main(), ReadFile(), Require()

### Community 115 - "Contract Path Test"
Cohesion: 0.70
Nodes (4): CheckOneTest(), Exists(), LooksLikePath(), main()

### Community 116 - "Ember Trail Bright Contract"
Cohesion: 0.60
Nodes (3): EmberBodyMask(), EmberHaloMask(), main()

### Community 117 - "Energy Burst Semantic Layers"
Cohesion: 0.80
Nodes (4): Forbid(), Has(), main(), Require()

### Community 118 - "Fluid Anisotropic Splat Test"
Cohesion: 0.70
Nodes (4): Aspect(), CrossAxis(), main(), ReadFile()

### Community 119 - "Fluid Capture Projection Test"
Cohesion: 0.70
Nodes (4): DecodeViewDistance(), EncodeDeviceDepth(), main(), ReadFile()

### Community 120 - "Fluid Filter 2d Test"
Cohesion: 0.70
Nodes (4): main(), ReadFile(), TapCount1D(), TapCount2D()

### Community 121 - "Fluid Pbd Grid Stamp"
Cohesion: 0.70
Nodes (4): DecodeHead(), EncodeHead(), main(), ReadFile()

### Community 122 - "Lightning Arc Contract Test"
Cohesion: 0.70
Nodes (4): main(), ReadFile(), Require(), RequireNot()

### Community 123 - "Lightning Trail Contract Test"
Cohesion: 0.70
Nodes (4): main(), ReadFile(), Require(), RequireNot()

### Community 124 - "Scene Target Alpha Contract"
Cohesion: 0.70
Nodes (4): Check(), FileHas(), FlushesInsideDisabledWindow(), main()

### Community 125 - "Shader Stage Interface Test"
Cohesion: 0.90
Nodes (4): CheckPair(), CollectDeclarations(), main(), ReadFile()

### Community 126 - "Skill Manager"
Cohesion: 0.50
Nodes (4): CastPathType, AddCastPortal(), CastSkill(), Skill_GetManaCost()

### Community 127 - "Particle System"
Cohesion: 0.50
Nodes (4): Particle_AllocSlot(), ParticleConfig_Unify(), ParticleSystem_SpawnFromEmitter(), ParticleSystem_SpawnLegacy()

### Community 128 - "Composition Tu Test"
Cohesion: 0.83
Nodes (3): main(), ScanFile(), Walk()

### Community 129 - "Decal System Test"
Cohesion: 0.83
Nodes (3): Exists(), Has(), main()

### Community 130 - "Fluid Refraction Source Test"
Cohesion: 0.83
Nodes (3): FunctionBody(), main(), ReadFile()

### Community 131 - "Fluid Silhouette Coverage Test"
Cohesion: 0.83
Nodes (3): main(), MaskCoverage(), ReadFile()

### Community 132 - "Fxaa Pass Test"
Cohesion: 0.83
Nodes (3): FxaaWouldFilter(), Has(), main()

### Community 133 - "Particle Appearance Adoption Test"
Cohesion: 0.83
Nodes (3): main(), ScanFile(), Walk()

### Community 134 - "Particle Glow Recipe Test"
Cohesion: 0.83
Nodes (3): Check(), Has(), main()

### Community 135 - "Smoke Fire Emitter Test"
Cohesion: 0.83
Nodes (3): Check(), Has(), main()

### Community 136 - "Vfx Unified Render Contract"
Cohesion: 0.83
Nodes (3): Check(), Has(), main()

### Community 137 - "Water Ring Coverage Test"
Cohesion: 0.83
Nodes (3): CoverageRatio(), main(), ReadFile()

### Community 138 - "Skill Manager"
Cohesion: 0.67
Nodes (3): ProjectedPoint, Camera3D, ProjectPointCached()

## Knowledge Gaps
- **68 isolated node(s):** `position`, `velocity`, `colorStart`, `colorEnd`, `radius` (+63 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **4 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ParticleConfig` connect `Particle System` to `Mesh Adjacency`, `Particle System`, `Particle Manager`, `Utils Math`, `Particle System`, `Color Gradient`, `Particle System`?**
  _High betweenness centrality (0.035) - this node is a cross-community bridge._
- **Why does `ApplyDeformUniforms()` connect `Trail System` to `Trail System`, `Uv Deform`, `Trail System`, `Decal System`?**
  _High betweenness centrality (0.030) - this node is a cross-community bridge._
- **Why does `ParticleRadialBurstConfig` connect `Particle System` to `Color Gradient`, `Utils Math`?**
  _High betweenness centrality (0.018) - this node is a cross-community bridge._
- **Are the 3 inferred relationships involving `DrawTrailEntitiesLayer()` (e.g. with `TimeFX_Elapsed()` and `SurfaceFlow_Apply()`) actually correct?**
  _`DrawTrailEntitiesLayer()` has 3 INFERRED edges - model-reasoned connections that need verification._
- **What connects `position`, `velocity`, `colorStart` to the rest of the system?**
  _68 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Decal System` be split into smaller, more focused modules?**
  _Cohesion score 0.057195149851292613 - nodes in this community are weakly interconnected._
- **Should `Uv Deform` be split into smaller, more focused modules?**
  _Cohesion score 0.05191146881287726 - nodes in this community are weakly interconnected._