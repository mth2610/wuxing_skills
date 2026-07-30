# Core Engine API — Index

> **Index-only, generated from `core/**/*.h` by `scripts/gen_core_api_index.sh`** (ground-truth signatures — never hand-edit the Signature Index; edit the headers and regenerate). This file is the map; the headers are the territory.
>
> - **How to USE these APIs** (patterns, worked examples, contracts, the "why"): [`API_GUIDE.md`](API_GUIDE.md) — the prose companion to this index.
> - **Struct fields / enum semantics:** open the header named in each section (structs are listed by name).
> - **Composition layer** (`VFX_Compose*`, material/motion split): guidance in [`COMPOSITION_API.md`](COMPOSITION_API.md).
> - **Authoring a skill** (recipes, archetype skeletons): [`RECIPE.md`](../../skills/docs/RECIPE.md), [`SKELETONS.md`](../../skills/docs/SKELETONS.md).
> - **Traps:** [`LANDMINES.md`](LANDMINES.md) (core-local) + [`ENGINE_LANDMINES.md`](../../ENGINE_LANDMINES.md) (cross-cutting — read before touching GL/shaders).
> - **Code standards** (C99, memory, scale, shaders, auto-registry, aesthetic laws): [`AGENT_CODE_STANDARD.md`](../../AGENT_CODE_STANDARD.md). The rules below are the API-usage subset; that file is the full checklist.

## Critical usage rules (what bare signatures don't tell you)
- **No `malloc`/`free`** — static pools only. Load assets via `ResourceManager_Load*`; **never** call `UnloadShader`/`UnloadTexture` in skill code (the manager owns lifetimes).
- **Meter scale** (1 unit = 1 m): mesh radii ~0.10–0.20, force/gravity 3.0–7.0 (vs real 9.81), particle speed 1.0–3.0.
- **A system's `*_Init`/`*_Update`/`*_Draw`/`*_Unload` are the engine-loop lifecycle** — skill code does not call them; it calls the spawn/add entry points only. Exceptions are called out below.
- **Trails:** `SpawnTrailEntity` returns an id. `TRAIL_TYPE_PROJECTILE` and `SpawnLightningTrail` self-terminate; **`TRAIL_TYPE_FOLLOWER` and manually-driven trails: the caller MUST `KillTrail(id)`.** Drive electric followers with `Lightning_UpdateFollowerTip`, not raw `UpdateFollowerPosition`.
- **`VFXLight_Spawn` requires a `VFXPriority`** — a full pool evicts the lowest priority. Use `VFX_PRIORITY_HIGH_ULTIMATE` for casts that must not drop.
- **Metaballs:** call `MetaballFX_RegisterBlob` every frame per blob (1-frame lifetime); never call `MetaballFX_DrawRegistered` from skill code.
- **ScreenDistort:** skills only call `ScreenDistort_Add` (auto-expires after `lifetime`); the rest is engine lifecycle.
- **Depth-state changes** must flush the batch (`rlDrawRenderBatchActive()`) before AND after — see `ENGINE_LANDMINES.md` §1.
- **Custom shader textures:** bind via `SetShaderValueTexture`, not `rlActiveTextureSlot`/`rlEnableTexture` — see `LANDMINES.md`.
- **Cooldowns** are keyed `(skillIndex, agentId)`; call `SkillManager_TriggerCooldown` at cast, `SkillManager_CanCast` to gate.
- **Composition rule:** element colors/gradients/force-fields come from `VFX_Material(VC_MAT_*)`; motion math (orbit/ring/jitter/breathe) from `vc_motion.h`. Assemble new `VFX_Compose*` from material + motion + primitives — hard-coded colors only for deliberate identity breaks, with a comment.
- **`CameraFX_Shake` defaults to 0** — never add camera shake to a default skill; expose it as a tunable defaulting to 0.

## Element colors
`ELEMENT_COLOR_{WATER,WOOD,FIRE,EARTH,METAL,TAIJI}` (see `core/presets/vfx_presets.h`). Use these, not ad-hoc RGB, except for deliberate identity breaks.

---

## Signature Index (by header)

### `core/resource_manager.h`
```c
  void ResourceManager_Init(void);
  void ResourceManager_Unload(void);
  Texture2D ResourceManager_LoadTexture(const char *filePath);
  Shader ResourceManager_LoadShader(const char *vsFilePath, const char *fsFilePath);
  Sound ResourceManager_LoadSound(const char *filePath);
  Font ResourceManager_LoadFont(const char *filePath, int baseSize);
  Model ResourceManager_LoadModel(const char *filePath);
  ModelAnimation *ResourceManager_LoadModelAnimations(const char *filePath, int *outCount);
```

### `core/tuning.h`
```c
  void Tuning_Init(const char *configPath);
  bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue);
  void Tuning_Update(void);
  void Tuning_Reload(void);
  bool Tuning_LoadFloatsFromPath(const char *path, const char *const *keys, float *outValues, int count);
  bool Tuning_SaveFloats(const char *path, const char *const *keys, const float *values, int count);
```

### `core/skill_manager.h`
```c
  float Skill_CalculateDamage(SkillCategory cat, SkillParams params);
  float Skill_CalculateCooldown(SkillCategory cat, SkillParams params);
  float Skill_CalculateKnockback(SkillCategory cat, SkillParams params);
  float Skill_CalculateManaCost(SkillCategory cat, SkillParams params);
  const char* Skill_GetCategoryName(SkillCategory cat);
  void InitSkillManager(int screenWidth, int screenHeight);
  void UpdateSkillManager(float dt, Vector3 enemyPos, float enemyRadius);
  void DrawSkillManagerWorld3D(void);
  void DrawSkillManagerOverlay(void);
  void UnloadSkillManager(void);
  bool CastSkill(int skillIndex, int agentId, Vector3 startPos, Vector3 target, SkillParams params);
  int RegisterSkill(const char* name, Color color, void (*init)(int screenWidth, int screenHeight), void (*cast)(int agentId, Vector3 startPos, Vector3 target, SkillParams params), void (*update)(float dt, Vector3 enemyPos, float enemyRadius), void (*draw)(void), void (*unload)(void));
  void SetSkillOverrides(int skillIndex, int pathType, int anchorType, int quantity, float sizeScale);
  int GetRegisteredSkillCount(void);
  const char* GetRegisteredSkillName(int index);
  Color GetRegisteredSkillColor(int index);
  int Skill_GetIndexByName(const char *name);
  ProjectedPoint ProjectPointCached(Vector3 worldPos, Camera3D cam);
  void RegisterStaticOccluder(Vector3 center, float radius, float height);
  void ClearStaticOccluders(void);
  float GetLineOfSightVisibility(Vector3 viewPoint, Vector3 targetPoint);
  bool IsAnySkillCoiling(void);
  bool IsAnySkillShocking(void);
  bool IsEnemySlowed(void);
  bool IsEnemyBurning(void);
  bool IsEnemyRooted(void);
  void AddRootToEnemy(float duration);
  void AddSlowToEnemy(float duration);
  void AddFloatingText(Vector3 pos, const char *text, Color color, float size, float lifetime);
  Vector3 GetAccumulatedKnockback(void);
  void ClearAccumulatedKnockback(void);
  void AddKnockbackToEnemy(Vector3 force);
  void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
  void SkillManager_BeginShader(Shader shader);
  void SkillManager_EndShader(void);
  bool SkillManager_CanCast(int skillIndex, int agentId);
  void SkillManager_SetFreeCast(bool on);
  void SkillManager_TriggerCooldown(int skillIndex, int agentId, float cooldownSeconds);
  void SkillManager_ResetAgentCooldowns(int agentId);
  void RegisterSkillAbort(int skillIndex, void (*abort)(int agentId));
  void AbortSkill(int skillIndex, int agentId);
  void RegisterSkillLifecycleQuery(int skillIndex, bool (*hasActiveInstance)(int agentId));
  bool Skill_HasActiveInstance(int skillIndex, int agentId);
  void RegisterSkillTunables(int skillIndex, const SkillTunableEntry *entries, int count);
  int Skill_GetTunables(int skillIndex, SkillTunableEntry *outEntries, int maxEntries);
  int SkillTunables_Flatten(const SkillTunableEntry *entries, int count, char outKeys[][TUNING_MAX_KEY_LEN], float *outValues, int maxKeys);
  void SkillTunables_Unflatten(const SkillTunableEntry *entries, int count, const char *const *keys, const float *values, int keyCount);
  bool SkillTunables_LoadPersisted(const char *path, SkillTunableEntry *entries, int count);
  void SkillManager_SetAgentPosProvider(AgentPosProviderFn fn);
  bool SkillManager_GetAgentPos(int agentId, Vector3 *outPos);
  void SkillManager_SetNearbyTargetsProvider(NearbyTargetsProviderFn fn);
  int SkillManager_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds);
  void SkillManager_SetEnemyAgentId(int agentId);
  int SkillManager_GetEnemyAgentId(void);
  void SkillManager_RegisterWall(Vector3 position, int element, float radius, float refreshDuration);
  bool SkillManager_FindNearbyWall(Vector3 casterPos, float checkRadius, Vector3 *outWallPos, int *outElement);
  void RegisterSkillManaCost(int skillIndex, float cost);
  float Skill_GetManaCost(int skillIndex);
  void RegisterSkillCastAnimSeconds(int skillIndex, float seconds);
  float Skill_GetCastAnimSeconds(int skillIndex);
```
**Enums:** SkillType { SKILL_WATER,SKILL_METAL,SKILL_FIRE,SKILL_WOOD,SKILL_ELECTRIC };CastAnchorType { CAST_ANCHOR_CASTER,CAST_ANCHOR_TARGET } CastPathType { CAST_PATH_PROJECTILE,CAST_PATH_UNDERGROUND,CAST_PATH_SKY };SkillCategory { SKILL_CAT_PROJECTILE,SKILL_CAT_AOE_CONTROL,SKILL_CAT_MELEE,SKILL_CAT_TRAP_UTILITY,SKILL_CAT_BUFF_SUPPORT }
**Structs** (fields in header): SkillParams, ProjectedPoint, SkillTunableEntry

### `core/skill_helper.h`
```c
  void PlayCastSound(EffectPresetType preset);
  void PlayImpactSound(EffectPresetType preset);
  void DamageVolume_Init(void);
  void DamageVolume_Update(float dt);
  int SpawnDamageVolume(DamageVolume config);
  void DamageVolume_Unload(void);
  void Timeline_Start(SkillTimeline *t, float duration);
  bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt);
  bool Timeline_Finished(SkillTimeline *t);
  void Timeline_LayeredStart(LayeredTimeline *t);
  bool Timeline_AddLayer(LayeredTimeline *t, const char *tag, float start, float duration);
  bool Timeline_IsLayerActive(const LayeredTimeline *t, int layerIndex);
  float Timeline_LayerProgress(const LayeredTimeline *t, int layerIndex);
  bool Timeline_LayerEvent(const LayeredTimeline *t, int layerIndex, float dt);
  void SkillHelper_StepCurveFlight(const SkillCurve *speedCurve, float elapsed, float dt, float maxDuration, float maxRange, float targetDistance, float *traveled, bool *arrived);
  Vector3 SkillHelper_EvaluateForceLayer(const ForceLayer *layer, Vector3 pos, Vector3 vel, float time, Vector3 axisOrigin, Vector3 axisDir);
  void SkillForceMix_AddLayers(const SkillForceMix *mix, ForceField *ff);
  int SkillForceMix_MakeTunables(SkillForceMix *mix, const char *labelPrefix, const char *phase, SkillTunableEntry *outEntries);
  void EmitterSystem_Init(void);
  void EmitterSystem_Update(float dt);
  int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration);
  void Emitter_Stop(int emitterId);
  void EmitterSystem_Unload(void);
  void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color);
  void Material_LoadElement(EffectMaterial *outMat, EffectPresetType element);
  void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration);
  void SpawnGroundDecalEx(DecalPresetType type, Vector3 pos, float scaleStart, float scaleEnd, float lifetime, float rotSpeed, float yOffset);
  void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse);
  ForceField ForceField_CreatePreset(ForceFieldPreset preset);
  void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale);
  void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx);
  void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration);
  void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration);
  void SkillBuilder_Build(SkillBuildContext *ctx);
  void SkillBuilder_AddCastEffect(SkillBuildContext *ctx, EffectPresetType preset);
  int SkillHelper_ChainTargets(Vector3 origin, float jumpRadius, int maxJumps, Vector3 *outPoints, int maxOut);
  void DamageVolume_GetStats(int *active, int *max);
  void InitHelperResources(void);
```
**Enums:** EffectPresetType { EFFECT_PRESET_FIRE_EXPLOSION,EFFECT_PRESET_ICE_SHATTER,EFFECT_PRESET_WATER_SPLASH,EFFECT_PRESET_LIGHTNING_IMPACT,EFFECT_PRESET_EARTH_CRACK,EFFECT_PRESET_WOOD_BLOOM,EFFECT_PRESET_METAL_SHARD,EFFECT_PRESET_TAIJI_BURST };ShapeType { SHAPE_CIRCLE,SHAPE_BOX,SHAPE_CONE } EmitterPreset { EMITTER_FIRE,EMITTER_SNOW,EMITTER_WATER_SPURT,EMITTER_SHOCKED_SPARKS,EMITTER_WOOD_LEAVES,EMITTER_EARTH_DUST,EMITTER_METAL_SPARKS,EMITTER_TAIJI_MOTES };MeshPresetType { MESH_PRESET_DISC,MESH_PRESET_RING,MESH_PRESET_CONE,MESH_PRESET_TORNADO,MESH_PRESET_CYLINDER,MESH_PRESET_SPHERE,MESH_PRESET_SHOCKWAVE,MESH_PRESET_PYRAMID,MESH_PRESET_TETRAHEDRON } DecalPresetType { DECAL_PRESET_CRACK,DECAL_PRESET_EARTH_SHATTER,DECAL_PRESET_EARTH_RUNE,DECAL_PRESET_BURN,DECAL_PRESET_FIRE_LAVA,DECAL_PRESET_WATER,DECAL_PRESET_WATER_SPLASH,DECAL_PRESET_WATER_RIPPLE,DECAL_PRESET_ICE,DECAL_PRESET_WOOD_ROOT,DECAL_PRESET_WOOD_MOSS,DECAL_PRESET_METAL_SLASH,DECAL_PRESET_METAL_CRATER,DECAL_PRESET_METAL_RUNE,DECAL_PRESET_TAIJI_RING,DECAL_PRESET_TAIJI_LIGHTNING,DECAL_PRESET_TAIJI_WIND,DECAL_PRESET_GENERIC_IMPACT_RING,DECAL_PRESET_GENERIC_GLOW,DECAL_PRESET_GENERIC_SHADOW };ForceFieldPreset { FORCE_PRESET_FIRE_UPDRAFT,FORCE_PRESET_SNOW_BLIZZARD,FORCE_PRESET_WATER_VORTEX,FORCE_PRESET_EARTH_RUMBLE,FORCE_PRESET_WOOD_GROWTH,FORCE_PRESET_METAL_IMPLOSION,FORCE_PRESET_TAIJI_ORBIT }
**Structs** (fields in header): DamageVolume, SkillTimeline, TimelineLayer, LayeredTimeline, SkillForceMix, ParticleEmitter, CameraImpulse, SkillBuildContext

### `core/skill_curve.h`
```c
  void SkillCurve_SetConstant(SkillCurve *curve, float value);
  float SkillCurve_Eval(const SkillCurve *curve, float t01);
```

### `core/force_field.h`
```c
  float Noise_Perlin3D(float x, float y, float z);
  float Noise_Value3D(float x, float y, float z);
  Vector3 Noise_Curl3D(float x, float y, float z, float scale);
  void ForceField_Clear(ForceField *ff);
  bool ForceField_AddLayer(ForceField *ff, ForceLayer layer);
  Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel, float time, Vector3 axisOrigin, Vector3 axisDir);
  float ForceField_GetViscosityDamping(const ForceField *ff, float dt);
  void ForceField_PackGPU(const ForceField *ff, Vector3 axisOrigin, Vector3 axisDir, ForceFieldGPU *out);
  void WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq);
  void WindZone_Clear(void);
  bool WindZone_IsActive(void);
  Vector3 WindZone_Evaluate(Vector3 pos, Vector3 vel, float time);
```
**Enums:** ForceType { FORCE_GRAVITY_DIR,FORCE_GRAVITY_POINT,FORCE_VORTEX,FORCE_WIND,FORCE_NOISE_PERLIN,FORCE_NOISE_CURL,FORCE_DRAG,FORCE_VISCOSITY,FORCE_RADIAL_AXIS,FORCE_VORTEX_AXIS,FORCE_VECTOR_TEXTURE }
**Structs** (fields in header): ForceLayer, ForceField, ForceLayerGPU, ForceFieldGPU

### `core/particle_system.h`
```c
  void InitParticleSystem(void);
  void SpawnParticle(ParticleConfig config);
  void ParticleSystem_GetStats(int *active, int *max);
  void UpdateParticles(float dt);
  void DrawParticles(Camera3D camera, Texture2D texture);
  void UnloadParticleSystem(void);
  void ParticleSystem_SetLighting(float strength01, float scatter01);
  void ParticleSystem_GetLighting(float *outStrength, float *outScatter);
  bool IsParticleSystemActive(void);
  void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg);
  void SpawnParticleOnMesh(const struct MeshAdjacency *adj, Matrix transform, ParticleConfig config);
  void ParticleSystem_ResetForceFieldRegistry(void);
```
**Structs** (fields in header): ParticleRadialBurstConfig, ParticleConfig

### `core/mesh_adjacency.h`
```c
  void MeshAdjacency_Build(MeshAdjacency *out, Mesh mesh);
  Vector3 MeshAdjacency_SampleVertex(const MeshAdjacency *adj);
  Vector3 MeshAdjacency_SampleEdge(const MeshAdjacency *adj);
  int MeshAdjacency_GeneratePath(const MeshAdjacency *adj, int startVertex, int length, Vector3 *outPath);
```
**Structs** (fields in header): MeshAdjacency

### `core/trail_system.h`
```c
  void TrailSystem_SetGlobalTexture(Texture2D tex);
  void InitTrailSystem(Shader defaultShader);
  int SpawnTrailEntity(TrailConfig config);
  TrailEntity *GetTrail(int id);
  void KillTrail(int id);
  void UpdateTrailSystem(float dt);
  void DrawTrailEntities(Camera3D camera);
  void UnloadTrailSystem(void);
  int GetActiveTrailCount(void);
  void TrailSystem_GetStats(int *active, int *max);
  void UpdateFollowerPosition(int id, Vector3 newTipPos);
  void Trail_AttachToTransform(int id, const Matrix *targetTransform, Vector3 localOffset);
  void Trail_SetFollowerOrbit(int id, float radius, float speed, Vector3 axis, float phase);
  void SetFollowerAxis(int id, Vector3 axisOrigin, Vector3 axisDir);
  void Trail_SetLateralOffset(int id, Vector3 worldOffset);
  void Trail_SetFrozen(int id, bool frozen);
```
**Enums:** TrailType { TRAIL_TYPE_PROJECTILE,TRAIL_TYPE_WISP,TRAIL_TYPE_PORTAL,TRAIL_TYPE_FOLLOWER };TrailShape { TRAIL_SHAPE_RIBBON,TRAIL_SHAPE_TUBE } TrailWidthEnvelopeType { TRAIL_WIDTH_ENVELOPE_UNIFORM,TRAIL_WIDTH_ENVELOPE_TAPER_TAIL,TRAIL_WIDTH_ENVELOPE_TAPER_BOTH,TRAIL_WIDTH_ENVELOPE_PULSE }
**Structs** (fields in header): TrailLayer, TrailSectionPoint, TrailConfig, TrailEntity

### `core/ribbon_strip.h`
```c
  void DrawRibbonStripEx(const RibbonPoint *points, int count, Texture2D texture, Camera3D camera, RibbonMode mode, Vector3 fixedNormal);
  void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture, Camera3D camera);
  void Ribbon_ConstrainSegment(Vector3 *a, Vector3 *b, float restLen, bool pinnedA, RibbonConstrainMode mode);
  void Ribbon_ComputeArcLengthUV(RibbonPoint *points, int count);
  void Ribbon_ComputeCrossFrame(const Vector3 *points, int count, RibbonMode mode, Vector3 fixedNormal, Camera3D camera, Vector3 *outAxisA, Vector3 *outAxisB);
  void DrawRibbonEnergyField(const Vector3 *points, int count, float width, const float *widthEnvelope, const RibbonEnergyFieldLayer *layers, int layerCount, Texture2D texture, RibbonMode mode, Vector3 fixedNormal, Camera3D camera, float time);
```
**Enums:** RibbonMode { RIBBON_CAMERA_FACING,RIBBON_WORLD_UP,RIBBON_FIXED_NORMAL };RibbonConstrainMode { RIBBON_CONSTRAIN_EXACT,RIBBON_CONSTRAIN_MAX,RIBBON_CONSTRAIN_MIN }
**Structs** (fields in header): RibbonPoint, RibbonEnergyFieldLayer

### `core/decal_system.h`
```c
  void DecalSystem_Init(void);
  void DecalSystem_Add(Vector3 pos, float rotation, float scale, Texture2D texture, float lifetime, Color tint);
  void DecalSystem_AddEx(Vector3 pos, float rotation, float rotSpeed, float scaleStart, float scaleEnd, Texture2D texture, float lifetime, Color tint, BlendMode blendMode, float yOffset);
  void DecalSystem_AddFlowEx(Vector3 pos, float rotation, float rotSpeed, float scaleStart, float scaleEnd, Texture2D texture, float lifetime, Color tint, BlendMode blendMode, float yOffset, float flowSpeed, float flowStrength, float glowIntensity);
  void DecalSystem_AddStreak(const Vector3 *points, int count, float rotation, float scale, Texture2D texture, float lifetime, Color tint);
  void DecalSystem_Update(float dt);
  void DecalSystem_SetCamera(Camera3D camera);
  void DecalSystem_Draw(void);
  void DecalSystem_Unload(void);
  void DecalSystem_GetStats(int *active, int *max);
```
**Structs** (fields in header): DecalEntity

### `core/screen_distort.h`
```c
  void ScreenDistort_Init(int width, int height);
  bool ScreenDistort_IsHDR(void);
  void ScreenDistort_Unload(void);
  void ScreenDistort_Begin(void);
  void ScreenDistort_End(void);
  void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed);
  void ScreenDistort_Update(float dt);
  void ScreenDistort_Draw(Camera3D camera);
  void ScreenDistort_SnapshotDepth(void);
  Texture2D ScreenDistort_GetDepthTexture(void);
  void ScreenDistort_BindDepthForSoftParticles(Shader shader, int textureSlot);
  void ScreenDistort_UnbindSoftParticleDepth(int textureSlot);
```
**Structs** (fields in header): DistortionSource

### `core/metaball_fx.h`
```c
  void MetaballFX_Init(int width, int height);
  void MetaballFX_Unload(void);
  void MetaballFX_RegisterBlob(Vector3 worldPos, float radius);
  void MetaballFX_DrawRegistered(Camera3D camera, Color tint, float threshold, float smoothness);
```

### `core/color_gradient.h`
```c
  bool ColorGradient_AddStop(ColorGradient *g, float t, Color color);
  Color ColorGradient_Sample(const ColorGradient *g, float t);
  ColorGradient ColorGradient_MakeElectric(void);
  void ColorGradient_StandardFade(ColorGradient *grad, Color baseColor, float midT, float brightenAmount);
```
**Structs** (fields in header): GradientStop, ColorGradient

### `core/float_curve.h`
```c
  bool FloatCurve_AddStop(FloatCurve *c, float t, float value);
  float FloatCurve_Sample(const FloatCurve *c, float t);
```
**Structs** (fields in header): FloatCurveStop, FloatCurve

### `core/flow_map.h`
```c
  FlowMap FlowMap_Create(Shader shader, Texture2D flowTex, const char *timeUniformName);
  FlowMap FlowMap_CreateWithVortexTexture(Shader shader, int texSize, const char *timeUniformName);
  FlowMap FlowMap_CreateWithTrailTexture(Shader shader, int texSize, float swirl, const char *timeUniformName);
  void FlowMap_Apply(const FlowMap *fm, Shader shader, float time);
  void FlowMap_Unload(FlowMap *fm);
```
**Structs** (fields in header): FlowMapConfig, FlowMap

### `core/path_spline.h`
```c
  Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
  Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target, float t);
  int SamplePath(const Vector3 *path, int pathCount, float spacing, Vector3 *outSegments, int maxSegments);
```

### `core/sprite_anim.h`
```c
  void SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode);
  void SpriteAnim_Update(SpriteAnim *anim, float dt);
  Rectangle SpriteAnim_GetUVRect(const SpriteAnim *anim);
  bool SpriteAnim_IsFinished(const SpriteAnim *anim);
  void SpriteAnim_Reset(SpriteAnim *anim);
  Rectangle SpriteAnim_CalculateUV(const SpriteAnim *template, float age, int *outFrame);
  Rectangle SpriteAnim_CalculateUVBlend(const SpriteAnim *template, float age, Rectangle *outNext, float *outBlend);
```
**Enums:** AnimPlayMode { ANIM_ONCE,ANIM_LOOP,ANIM_RANDOM_START,ANIM_PING_PONG }
**Structs** (fields in header): SpriteAnim

### `core/vfx_light.h`
```c
  void VFXLight_Init(void);
  void VFXLight_Reset(void);
  void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime, VFXPriority priority);
  void VFXLight_Update(float dt);
  void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);
  void VFXLight_GetStats(int *active, int *max);
  void VFXLight_BindToShader(Shader shader, int maxLights);
  void VFXLight_RegisterShader(Shader shader);
  void VFXLight_BindAll(int maxLights);
  void VFXLight_DrawDebug(void);
  void VFXLight_DebugTestLight(Vector3 pos);
```
**Enums:** VFXPriority { VFX_PRIORITY_LOW,VFX_PRIORITY_HIGH_ULTIMATE }
**Structs** (fields in header): VFXLightData

### `core/post_fx.h`
```c
  void PostFX_Init(int width, int height);
  bool PostFX_IsHDR(void);
  void PostFX_Unload(void);
  void PostFX_Begin(void);
  void PostFX_End(void);
  void PostFX_Draw(const PostFXConfig *config);
  void PostFX_SetMonochrome(float intensity01);
  void PostFX_RadialBurst(Vector3 worldPos, float strength, float duration);
  void PostFX_UpdateTransient(Camera3D cam, float dt);
  bool PostFX_HasTransient(void);
  void PostFX_ApplyTransient(PostFXConfig *config);
```
**Structs** (fields in header): PostFXConfig

### `core/camera_fx.h`
```c
  void CameraFX_Shake(float trauma);
  void CameraFX_Update(Camera3D *camera, float dt);
```

### `core/debug_draw.h`
```c
  void DebugDraw_SetEnabled(bool enabled);
  bool DebugDraw_IsEnabled(void);
  void DebugDraw_Sphere(Vector3 pos, float radius, Color color);
  void DebugDraw_Circle(Vector3 center, float radius, Color color);
```

### `core/motion_controller.h`
```c
  void Motion_Init(MotionState *s, MotionParams params, Vector3 startPos, Vector3 target);
  void Motion_Step(MotionState *s, float dt);
  bool Motion_Arrived(const MotionState *s);
```
**Enums:** MotionType { MOTION_LINEAR,MOTION_HOMING,MOTION_BALLISTIC,MOTION_SPIRAL,MOTION_ORBIT,MOTION_BOOMERANG }
**Structs** (fields in header): MotionParams, MotionState

### `core/status_vfx.h`
```c
  int StatusVFX_Attach(int agentId, EffectPresetType element, float duration);
  void StatusVFX_Detach(int handle);
  void StatusVFX_Update(float dt);
  void StatusVFX_Draw(void);
  void StatusVFX_GetStats(int *active, int *max);
```

### `core/afterimage.h`
```c
  void Afterimage_Init(void);
  void Afterimage_Spawn(Model model, Matrix transform, Color tint, float life);
  void Afterimage_Update(float dt);
  void Afterimage_Draw(void);
  void Afterimage_GetStats(int *active, int *max);
```

### `core/surface_material.h`
```c
  void SurfaceMaterial_Init(void);
  Shader SurfaceMaterial_GetShader(void);
  void SurfaceMaterial_Apply(Model *model);
  void SurfaceMaterial_UpdateFrame(Camera3D camera);
  void SurfaceMaterial_SetMatcapActive(Texture2D matcap, float amount);
  void SurfaceMaterial_ClearMatcap(void);
  void SurfaceMaterial_SetNormalMapActive(Texture2D normalMap);
  void SurfaceMaterial_ClearNormalMap(void);
  void SurfaceMaterial_SetAniso(float anisoShininess);
  void SurfaceMaterial_ClearAniso(void);
  void SurfaceMaterial_SetSSS(float strength, float power);
  void SurfaceMaterial_ClearSSS(void);
  void SurfaceMaterial_BeginShadowCast(Model model, Shader depthShader);
  void SurfaceMaterial_EndShadowCast(Model model);
```

### `core/gfx_quality.h`
```c
  void GfxQuality_Set(GfxQuality q);
  GfxQuality GfxQuality_Get(void);
  GfxQuality GfxQuality_Default(void);
```
**Enums:** GfxQuality { GFX_UNLIT,GFX_LOW,GFX_MED,GFX_HIGH }

### `core/audio_system.h`
```c
  void Audio_Init(void);
  void Audio_Shutdown(void);
  void Audio_Update(float dt);
  void Audio_SetMasterVolume(float volume01);
  void Audio_SetListener(Vector3 pos);
  void Audio_PlaySFX(SfxId id);
  void Audio_PlaySFXAt(SfxId id, Vector3 worldPos);
  SfxId Audio_CastSfxForElement(int element);
  void Audio_PlayMusic(MusicId id);
  void Audio_StopMusic(void);
```
**Enums:** SfxId { SFX_UI_CLICK,SFX_CAST_WATER,SFX_CAST_WOOD,SFX_CAST_FIRE,SFX_CAST_EARTH,SFX_CAST_METAL,SFX_CAST_TAIJI,SFX_MELEE_HIT,SFX_SKILL_HIT,SFX_CLASH,SFX_EXPLOSION,SFX_TAIJI_ENTER,SFX_RINGOUT,SFX_VICTORY,SFX_DEFEAT,SFX_COUNT };MusicId { MUS_NONE,MUS_ARENA_NIGHT,MUS_COUNT }

### `core/atmosphere.h`
```c
  void Atmosphere_Init(void);
  void Atmosphere_Configure(Vector3 center, Vector3 extent, int count, Color tint);
  void Atmosphere_Update(float dt, Camera3D camera);
  void Atmosphere_Draw(Camera3D camera);
  void Atmosphere_Unload(void);
```

### `core/material/material_system.h`
```c
  void MaterialSystem_Init(void);
  void MaterialSystem_Unload(void);
  void Material_Get(EffectMaterial *outMat, MaterialPreset preset);
  void Material_LoadCustom(EffectMaterial *outMat, const EffectMaterialParams *params);
  void Material_LoadCustomShader(EffectMaterial *outMat, const EffectMaterialParams *params, const char* vsPath, const char* fsPath);
  void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
  void Material_Begin(EffectMaterial mat);
  void Material_End(void);
  void EffectMaterialInstanced_Load(EffectMaterialInstanced *outMat, const EffectMaterialParams *params);
  void EffectMaterialInstanced_Begin(EffectMaterialInstanced mat);
  void EffectMaterialInstanced_End(void);
  void CrystalMaterial_Load(CrystalMaterial *outMat, const CrystalMaterialParams *params);
  void CrystalMaterial_Begin(CrystalMaterial mat);
  void CrystalMaterial_End(void);
  void CrystalMaterial_SetGrowProgress(CrystalMaterial mat, float progress);
  void CrystalMaterialInstanced_Load(CrystalMaterialInstanced *outMat, const CrystalMaterialParams *params);
  void CrystalMaterialInstanced_Begin(CrystalMaterialInstanced mat);
  void CrystalMaterialInstanced_SetGrowProgress(CrystalMaterialInstanced mat, float progress);
  void CrystalMaterialInstanced_End(void);
  void PlasmaMaterial_Load(PlasmaMaterial *outMat, const PlasmaMaterialParams *params);
  void PlasmaMaterial_Begin(PlasmaMaterial mat);
  void PlasmaMaterial_End(void);
  void AuraShellMaterial_Load(AuraShellMaterial *outMat, const AuraShellMaterialParams *params);
  void AuraShellMaterial_Begin(AuraShellMaterial mat);
  void AuraShellMaterial_End(void);
```
**Enums:** MaterialPreset { MAT_FIRE,MAT_ICE,MAT_WATER,MAT_PORTAL,MAT_ROCK,MAT_METAL,MAT_GLASS,MAT_CUSTOM }
**Structs** (fields in header): EffectMaterialParams, EffectMaterial, EffectMaterialInstanced, CrystalMaterialParams, CrystalMaterial, CrystalMaterialInstanced, PlasmaMaterialParams, PlasmaMaterial, AuraShellMaterialParams, AuraShellMaterial

### `core/geometry/procedural_mesh_utils.h`
```c
  void DrawCoreSphere(Vector3 center, float radius, int rings, int slices, Color color);
  void DrawCoreBillboardQuad(Vector3 center, float halfSize, Camera3D cam, Color color);
  void DrawCoreOrientedQuad(Vector3 center, Vector3 normal, float halfSize, Color color);
  void DrawCoreCrossQuads(Vector3 base, float halfWidth, float height, int planeCount, Color color);
  void DrawCoreGroundPatch(Vector3 center, float halfSize, int subdiv, float yLift, GroundHeightSampleFn heightFn, void *userData, Color color);
  void DrawCoreCylinder(Vector3 bottom, Vector3 top, float radiusBottom, float radiusTop, int slices, Color color);
  void DrawCoreCone(Vector3 bottom, float radius, float height, int slices, Color color);
  void DrawCorePlaneRect(Vector3 center, Vector2 size, Color color);
  void DrawCorePlanePolygon(Vector3 center, float radius, int sides, Color color);
  void DrawCoreCube(Vector3 position, float width, float height, float length, Color color);
  void DrawCoreTorus(Vector3 center, float innerRadius, float outerRadius, int sides, int rings, Color color);
  void DrawCorePrism(Vector3 bottom, Vector3 top, float radius, int sides, Color color);
  Vector3 ProceduralMesh_BezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
  Vector3 ProceduralMesh_BezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
  TubeMeshConfig ProceduralMesh_DefaultTubeConfig(void);
  void ProceduralMesh_BuildTube(TubeMeshData *out, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float baseRadius, float flowProgress, float time, int segments, int radialSegs, const TubeMeshConfig *cfg);
  void ProceduralMesh_BuildTubeAlongPath(TubeMeshData *out, const Vector3 *pathPoints, int pathCount, float baseRadius, float startT, float endT, float time, int segments, int radialSegs, const TubeMeshConfig *cfg);
  void ProceduralMesh_DrawTube(const TubeMeshData *data, float uvLengthScale);
  void ProceduralMesh_DrawTubeEx(const TubeMeshData *data, float uvLengthScale, float uvOffset);
  WavePlaneConfig ProceduralMesh_DefaultWavePlaneConfig(void);
  void ProceduralMesh_BuildWavePlane(WavePlaneMeshData *out, Vector3 center, float width, float length, int segmentsX, int segmentsZ, float time, const WavePlaneConfig *cfg);
  void ProceduralMesh_DrawWavePlane(const WavePlaneMeshData *data, Color color);
  CurlingWaveConfig ProceduralMesh_DefaultCurlingWaveConfig(void);
  void ProceduralMesh_BuildCurlingWave(CurlingWaveMeshData *out, Vector3 baseCenter, Vector3 widthDirection, const CurlingWaveConfig *cfg, int profileSegs, int widthSegs);
  void ProceduralMesh_DrawCurlingWave(const CurlingWaveMeshData *data, Color color);
  void ProceduralMesh_BuildRock(RockMeshData *out, Vector3 center, float radius, float jitterAmount, int seed, int subdivisions);
  void ProceduralMesh_DrawRock(const RockMeshData *data, Color color);
  Mesh ProceduralMesh_BuildRockTemplateMesh(float radius, float jitterAmount, int seed, int subdivisions);
  ShardClusterConfig ProceduralMesh_DefaultShardClusterConfig(void);
  void ProceduralMesh_BuildShardCluster(ShardClusterMeshData *out, Vector3 origin, Vector3 mainDirection, int shardCount, float minLength, float maxLength, int seed, const ShardClusterConfig *cfg);
  void ProceduralMesh_DrawShardCluster(const ShardClusterMeshData *data, Color color);
  VortexFunnelConfig ProceduralMesh_DefaultVortexFunnelConfig(void);
  void ProceduralMesh_BuildVortexFunnel(VortexFunnelMeshData *out, Vector3 center, const VortexFunnelConfig *cfg, int heightSegs, int radialSegs, float time);
  void ProceduralMesh_DrawVortexFunnel(const VortexFunnelMeshData *data, Color color);
  void ProceduralMesh_BuildFissure(FissureMeshData *out, const Vector3 *pathPoints, int pathPointCount, float width, float depth, float jaggedness, int seed);
  void ProceduralMesh_DrawFissure(const FissureMeshData *data, Color color);
  void ProceduralMesh_DrawFissurePartial(const FissureMeshData *data, Color color, int maxSegments);
  void ProceduralMesh_DrawFissureShaded(const FissureMeshData *data, const Color crossColors[FISSURE_CROSS_VERTS], int maxSegments);
  Mesh ProceduralMesh_CreateBaseGrid(float width, float length, int segmentsX, int segmentsZ);
  Mesh ProceduralMesh_CreateBaseCylinder(int radialSegs, int heightSegs);
  MeshDisplacementParams ProceduralMesh_DefaultDisplacementParams(void);
  void ProceduralMesh_SetDisplacementUniforms(Shader shader, const MeshDisplacementParams *params);
  void ProceduralMesh_UnloadBase(Mesh *mesh);
  void ProceduralMesh_DrawOrganicStonePillar(Vector3 pillarPos, float currentHeight, float baseRad, float topRad);
  void ProceduralMesh_DrawOrganicPuddle(Vector3 pos, float radius);
  void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color);
  void ProceduralMesh_BuildCrystalCluster(CrystalClusterMeshData *out, Vector3 center, const CrystalDesc *desc, int count, int seed, float progress);
  void ProceduralMesh_DrawCrystalClusterMesh(const CrystalClusterMeshData *data, Color color);
  void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color);
  Mesh ProceduralMesh_BuildCrystalClusterMesh(const CrystalDesc *desc, int count, int seed);
  Mesh ProceduralMesh_BuildCrystalTemplateMesh(const CrystalDesc *desc);
  void ProceduralMesh_DrawBakedCrystalCluster(Mesh mesh, Material material, Matrix transform);
  Material ProceduralMesh_GetPassthroughMaterial(Shader shader);
```
**Structs** (fields in header): TubeMeshConfig, TubeMeshData, WavePlaneConfig, WavePlaneMeshData, CurlingWaveConfig, CurlingWaveMeshData, RockMeshData, ShardClusterConfig, ShardClusterMeshData, VortexFunnelConfig, VortexFunnelMeshData, FissureMeshData, MeshDisplacementParams, CrystalDesc, CrystalClusterMeshData

### `core/composition/visual_composer.h`
```c
  void VFX_Compose_Update(float dt);
  void VFX_Compose_Draw3D(Camera3D cam);
  void VFX_ComposeSmokePuff(Vector3 pos, VC_MaterialId matId, float scale, float density);
  void VFX_ComposeFlameVolume(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
  int VFX_ComposeCharacterAura(int agentId, VC_MaterialId matId, float intensity);
  void VFX_AuraSetIntensity(int handle, float intensity01);
  void VFX_KillCharacterAura(int handle);
  void VFX_ComposeGlintSparkle(Vector3 center, VC_MaterialId mat, float scale, float time);
  void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat, float radius, float t01, int ringCount);
  void VFX_ComposeCoreGlow(Vector3 center, VC_MaterialId mat, float radius, float intensity01);
  void VFX_ComposeEnergyOrb(Vector3 center, VC_MaterialId mat, float radius, float intensity01);
  void VFX_ComposeChargeConverge(Vector3 center, VC_MaterialId mat, float radius, float t01, int moteCount);
  void VFX_ComposeDissolveExit(Vector3 pos, VC_MaterialId mat, float scale, float t01);
  void VFX_ComposeSweepSlash(Vector3 origin, Vector3 dir, VC_MaterialId mat, float length, float arcRad, float t01);
  void VFX_ComposeEnergyBurst(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
  void VFX_ComposeImpactPackage(Vector3 pos, Vector3 normal, VC_MaterialId matId, float scale, float severity01);
  void VFX_ComposeImpactFlash(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
  void VFX_ComposeImpactDistort(Vector3 pos, float scale, float severity01);
  void VFX_ComposeImpactDecal(Vector3 pos, VC_MaterialId matId, float scale, float severity01);
  void VFX_ComposeLightShaft(Vector3 from, Vector3 to, VC_MaterialId mat, float width, float intensity);
  int VFX_ComposeSweptTrail(const Matrix *followTransform, VC_MaterialId mat, float width, float lifetime, VFX_TrailStyle style);
  void VFX_TrailSetWidth(int handle, float width01);
  void VFX_KillSweptTrail(int handle);
  void VFX_ComposeGroundWave(Vector3 center, VC_MaterialId mat, float radius, float t01, GroundHeightSampleFn heightFn, void *ud);
  float VFX_GroundHeightFromMap(float worldX, float worldZ, void *unused);
  int VFX_ComposeSparkTrail(Vector3 pos, Vector3 vel, VC_MaterialId matId, float length, float life);
  int VFX_ComposeProjectile(const Matrix *followTransform, VC_MaterialId mat, float radius);
  void VFX_KillProjectile(int handle);
  void VFX_BeginWaterStreams(float time);
  void VFX_EndWaterStreams(void);
  void VFX_ComposeBlackHole(VC_MaterialId matId, Vector3 pos, float radius, float time);
  void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width, float progress, float time);
  void VFX_ComposeIceCrystal(Vector3 basePos, int seed);
  void VFX_ComposeParticleUpgradesTest(Vector3 pos);
  void VFX_ComposeShardDebris(Vector3 pos, int count, float speed, VC_MaterialId matId);
  void VFX_ComposeStonePillar(Vector3 basePos, float progress);
  void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time);
  void VFX_ComposeWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time);
  void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress);
```
**Enums:** VFX_TrailStyle { VFX_TRAIL_BLADE,VFX_TRAIL_RIBBON,VFX_TRAIL_FILAMENT,VFX_TRAIL_HAZE,VFX_TRAIL_STYLE_COUNT }

### `core/composition/vfx_sequence.h`
```c
  VFX_Sequence *VFX_SeqBegin(Vector3 origin, VC_MaterialId mat, float scale);
  void VFX_SeqAt(VFX_Sequence *s, float t, VFX_Beat beat);
  void VFX_SeqSetUnscaled(VFX_Sequence *s, bool unscaled);
  int VFX_SeqPlay(VFX_Sequence *s);
  void VFX_SeqStop(int handle);
  VFX_Sequence *VFX_SeqPreset(Vector3 origin, VC_MaterialId mat, float scale, float anticipation, float burst, float sustain, float dissipate);
  void VFX_Sequence_Update(float scaledDt);
  void VFX_Sequence_GetStats(int *playing, int *max);
```
**Enums:** VFX_BeatKind { VFX_BEAT_COMPOSE,VFX_BEAT_LIGHT,VFX_BEAT_SHAKE,VFX_BEAT_HITSTOP,VFX_BEAT_DISTORT,VFX_BEAT_RADIAL,VFX_BEAT_DECAL,VFX_BEAT_CALLBACK }
**Structs** (fields in header): VFX_Beat, VFX_Sequence

### `core/presets/vfx_presets.h`
```c
  VC_MaterialId VFX_MaterialFromPreset(EffectPresetType preset);
  void VFX_Presets_Init(void);
  const VFX_ImpactPreset* VFX_Preset_GetImpact(EffectPresetType preset);
  const VFX_CastPreset* VFX_Preset_GetCast(EffectPresetType preset);
  const VFX_ProjectilePreset* VFX_Preset_GetProjectile(EffectPresetType preset);
```
**Structs** (fields in header): VFX_ImpactPreset, VFX_CastPreset, VFX_ProjectilePreset

### `core/utils_math.h`
```c
  float Math_Mix(float x, float y, float a);
  float SmoothStep01(float x);
  float Random01(void);
```
