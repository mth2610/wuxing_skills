// Included inside InitMagmaFissureSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.

s_tunables[tn++] = (SkillTunableEntry){"Spacing", &s_instanceSpacing, 0.1f, 2.0f, s_instanceSpacing, "timing"};
s_tunables[tn++] = (SkillTunableEntry){"Stagger Dur", &s_staggerDuration, 0.1f, 2.0f, s_staggerDuration, "timing"};
s_tunables[tn++] = (SkillTunableEntry){"Rising Dur", &s_risingDuration, 0.05f, 1.0f, s_risingDuration, "timing"};
s_tunables[tn++] = (SkillTunableEntry){"Active Dur", &s_activeDuration, 0.1f, 5.0f, s_activeDuration, "timing"};
s_tunables[tn++] = (SkillTunableEntry){"Dissolve Dur", &s_dissolveDuration, 0.05f, 2.0f, s_dissolveDuration, "timing"};

s_tunables[tn++] = (SkillTunableEntry){"Base Radius", &s_baseRadius, 0.05f, 1.0f, s_baseRadius, "visual"};
s_tunables[tn++] = (SkillTunableEntry){"Max Height", &s_maxHeight, 0.1f, 3.0f, s_maxHeight, "visual"};
s_tunables[tn++] = (SkillTunableEntry){"Particle Speed", &s_particleSpeedMult, 0.1f, 5.0f, s_particleSpeedMult, "visual"};
s_tunables[tn++] = (SkillTunableEntry){"Light Radius", &s_lightRadiusMult, 1.0f, 15.0f, s_lightRadiusMult, "visual"};
s_tunables[tn++] = (SkillTunableEntry){"Camera Shake", &s_cameraShake, 0.0f, 1.0f, s_cameraShake, "visual"};

s_tunables[tn++] = (SkillTunableEntry){"Updraft Str", &s_updraftStrength, 0.0f, 10.0f, s_updraftStrength, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Curl Str", &s_noiseStrength, 0.0f, 15.0f, s_noiseStrength, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Curl Scale", &s_noiseScale, 0.1f, 10.0f, s_noiseScale, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Curl Speed", &s_noiseSpeed, 0.1f, 10.0f, s_noiseSpeed, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Perlin Str", &s_perlinStrength, 0.0f, 15.0f, s_perlinStrength, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Vortex Str", &s_vortexStrength, -20.0f, 20.0f, s_vortexStrength, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Drag Str", &s_dragStrength, 0.0f, 10.0f, s_dragStrength, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Wind Str", &s_windStrength, -10.0f, 10.0f, s_windStrength, "force"};
s_tunables[tn++] = (SkillTunableEntry){"Visc Str", &s_viscosityStrength, 0.0f, 10.0f, s_viscosityStrength, "force"};
