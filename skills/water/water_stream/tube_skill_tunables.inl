// Included inside InitTubeSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.
// defaultValue uses the variable itself (equals C initializer before SkillTunables_LoadPersisted).

  // --- active phase (mist) ---
  s_tunables[tn++] = (SkillTunableEntry){"tube_base_radius",    &s_tubeBaseRadius,  0.01f, 0.5f,  s_tubeBaseRadius,  "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_gravity",        &s_mistGravity,     0.0f,  19.62f, s_mistGravity,    "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_noise",          &s_mistNoise,       0.0f,  2.0f,  s_mistNoise,       "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_drag",           &s_mistDrag,        0.0f,  10.0f, s_mistDrag,        "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_vel_xz",         &s_mistVelXZ,       0.0f,  3.0f,  s_mistVelXZ,       "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_vel_y_max",      &s_mistVelYMax,     0.0f,  3.0f,  s_mistVelYMax,     "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_radius_min",     &s_mistRadiusMin,   0.0f,  0.2f,  s_mistRadiusMin,   "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_radius_max",     &s_mistRadiusMax,   0.0f,  0.2f,  s_mistRadiusMax,   "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_life_min",       &s_mistLifeMin,     0.05f, 3.0f,  s_mistLifeMin,     "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_life_max",       &s_mistLifeMax,     0.05f, 3.0f,  s_mistLifeMax,     "active"};
  s_tunables[tn++] = (SkillTunableEntry){"mist_radius_curve",   NULL, 0.0f, 3.0f, 1.0f, "active", &s_mistRadiusCurve};
  s_tunables[tn++] = (SkillTunableEntry){"mist_speed_curve",    NULL, 0.0f, 3.0f, 1.0f, "active", &s_mistSpeedCurve};
  s_tunables[tn++] = (SkillTunableEntry){"mist_alpha_curve",    NULL, 0.0f, 1.0f, 1.0f, "active", &s_mistAlphaCurve};
  s_tunables[tn++] = (SkillTunableEntry){"mist_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "active", &s_mistEmissiveCurve};
  tn += SkillForceMix_MakeTunables(&s_mistForce, "mist_force_", "active", &s_tunables[tn]);

  // --- splash phase ---
  s_tunables[tn++] = (SkillTunableEntry){"splash_gravity",      &s_splashGravity,   0.0f,  19.62f, s_splashGravity,  "splash"};
  s_tunables[tn++] = (SkillTunableEntry){"splash_noise",        &s_splashNoise,     0.0f,  2.0f,  s_splashNoise,     "splash"};
  s_tunables[tn++] = (SkillTunableEntry){"splash_drag",         &s_splashDrag,      0.0f,  10.0f, s_splashDrag,      "splash"};

  // --- impact phase ---
  s_tunables[tn++] = (SkillTunableEntry){"impact_distort_radius",   &s_impactDistortRadius,   0.1f, 5.0f,  s_impactDistortRadius,   "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"impact_distort_strength", &s_impactDistortStrength, 0.0f, 1.0f,  s_impactDistortStrength, "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"impact_distort_life",     &s_impactDistortLife,     0.1f, 3.0f,  s_impactDistortLife,     "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"impact_distort_speed",    &s_impactDistortSpeed,    0.1f, 10.0f, s_impactDistortSpeed,    "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"impact_decal_scale",      &s_impactDecalScale,      0.0f, 0.5f,  s_impactDecalScale,      "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"impact_decal_life",       &s_impactDecalLife,       0.1f, 20.0f, s_impactDecalLife,       "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"impact_light_radius",     &s_impactLightRadius,     0.1f, 3.0f,  s_impactLightRadius,     "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"impact_light_life",       &s_impactLightLife,       0.1f, 3.0f,  s_impactLightLife,       "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"burst_speed_min",         &s_burstSpeedMin,         0.0f, 10.0f, s_burstSpeedMin,         "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"burst_speed_max",         &s_burstSpeedMax,         0.0f, 10.0f, s_burstSpeedMax,         "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"burst_radius_min",        &s_burstRadiusMin,        0.0f, 0.3f,  s_burstRadiusMin,        "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"burst_radius_max",        &s_burstRadiusMax,        0.0f, 0.3f,  s_burstRadiusMax,        "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"burst_life_min",          &s_burstLifeMin,          0.05f, 5.0f, s_burstLifeMin,          "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"burst_life_max",          &s_burstLifeMax,          0.05f, 5.0f, s_burstLifeMax,          "impact"};
  s_tunables[tn++] = (SkillTunableEntry){"burst_upward_bias",       &s_burstUpwardBias,       0.0f, 5.0f,  s_burstUpwardBias,       "impact"};
