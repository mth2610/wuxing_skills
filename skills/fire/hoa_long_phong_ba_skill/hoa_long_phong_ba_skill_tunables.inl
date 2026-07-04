// Included inside InitHoaLongPhongBaSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.
// defaultValue uses the variable itself (equals C initializer before SkillTunables_LoadPersisted).

    // --- gather phase ---
    s_tunables[tn++] = (SkillTunableEntry){"gather_r_dist_min",       &s_gatherRDistMin,       0.0f,  1.0f,  s_gatherRDistMin,       "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_r_dist_max",       &s_gatherRDistMax,       0.0f,  1.0f,  s_gatherRDistMax,       "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_height_range",     &s_gatherHeightRange,    0.0f,  1.0f,  s_gatherHeightRange,    "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_height_base",      &s_gatherHeightBase,     0.0f,  1.0f,  s_gatherHeightBase,     "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_speed",            &s_gatherSpeed,          0.0f,  5.0f,  s_gatherSpeed,          "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_particle_radius",  &s_gatherParticleRadius, 0.0f,  0.2f,  s_gatherParticleRadius, "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_light_radius",       &s_castLightRadius,      0.1f,  3.0f,  s_castLightRadius,      "gather"};

    // --- travel phase ---
    s_tunables[tn++] = (SkillTunableEntry){"base_radius",             &s_baseRadius,           0.01f, 0.5f,  s_baseRadius,           "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"travel_back_speed",       &s_travelBackSpeed,      0.0f,  5.0f,  s_travelBackSpeed,      "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"travel_wind_strength",    &s_travelWindStrength,   0.0f,  5.0f,  s_travelWindStrength,   "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"travel_jitter",           &s_travelJitter,         0.0f,  1.0f,  s_travelJitter,         "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"travel_particle_radius",  &s_travelParticleRadius, 0.0f,  0.2f,  s_travelParticleRadius, "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"spark_back_speed",        &s_sparkBackSpeed,       0.0f,  5.0f,  s_sparkBackSpeed,       "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"spark_jitter",            &s_sparkJitter,          0.0f,  1.0f,  s_sparkJitter,          "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"spark_up_bias",           &s_sparkUpBias,          -1.0f, 1.0f,  s_sparkUpBias,          "travel"};
    s_tunables[tn++] = (SkillTunableEntry){"travel_radius_curve",     NULL, 0.0f, 3.0f, 1.0f, "travel", &s_travelRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"travel_speed_curve",      NULL, 0.0f, 3.0f, 1.0f, "travel", &s_travelSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"travel_alpha_curve",      NULL, 0.0f, 1.0f, 1.0f, "travel", &s_travelAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"travel_emissive_curve",   NULL, 0.0f, 3.0f, 1.0f, "travel", &s_travelEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_travelForceMix, "travel_force_", "travel", &s_tunables[tn]);

    // --- impact phase ---
    s_tunables[tn++] = (SkillTunableEntry){"impact_light_radius",     &s_impactLightRadius,    0.1f,  3.0f,  s_impactLightRadius,    "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_distort_radius",   &s_impactDistortRadius,  0.1f,  3.0f,  s_impactDistortRadius,  "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"aoe_radius",              &s_aoeRadius,            0.1f,  3.0f,  s_aoeRadius,            "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_spark_speed_min",  &s_impactSparkSpeedMin,  0.0f,  5.0f,  s_impactSparkSpeedMin,  "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_spark_speed_max",  &s_impactSparkSpeedMax,  0.0f,  5.0f,  s_impactSparkSpeedMax,  "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_spark_up_base",    &s_impactSparkUpBase,    0.0f,  3.0f,  s_impactSparkUpBase,    "impact"};

    // --- geyser phase ---
    s_tunables[tn++] = (SkillTunableEntry){"geyser_spin_speed_min",   &s_geyserSpinSpeedMin,   0.0f,  5.0f,  s_geyserSpinSpeedMin,   "geyser"};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_spin_speed_max",   &s_geyserSpinSpeedMax,   0.0f,  5.0f,  s_geyserSpinSpeedMax,   "geyser"};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_up_speed_min",     &s_geyserUpSpeedMin,     0.0f,  5.0f,  s_geyserUpSpeedMin,     "geyser"};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_up_speed_max",     &s_geyserUpSpeedMax,     0.0f,  5.0f,  s_geyserUpSpeedMax,     "geyser"};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_pos_y_range",      &s_geyserPosYRange,      0.0f,  0.5f,  s_geyserPosYRange,      "geyser"};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_particle_radius",  &s_geyserParticleRadius, 0.0f,  0.3f,  s_geyserParticleRadius, "geyser"};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_radius_curve",     NULL, 0.0f, 3.0f, 1.0f, "geyser", &s_geyserRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_speed_curve",      NULL, 0.0f, 3.0f, 1.0f, "geyser", &s_geyserSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_alpha_curve",      NULL, 0.0f, 1.0f, 1.0f, "geyser", &s_geyserAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"geyser_emissive_curve",   NULL, 0.0f, 3.0f, 1.0f, "geyser", &s_geyserEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_geyserForceMix, "geyser_force_", "geyser", &s_tunables[tn]);

    // --- aftermath phase ---
    s_tunables[tn++] = (SkillTunableEntry){"aftermath_up_speed_min",    &s_aftermathUpSpeedMin,    0.0f, 5.0f,  s_aftermathUpSpeedMin,    "aftermath"};
    s_tunables[tn++] = (SkillTunableEntry){"aftermath_up_speed_max",    &s_aftermathUpSpeedMax,    0.0f, 5.0f,  s_aftermathUpSpeedMax,    "aftermath"};
    s_tunables[tn++] = (SkillTunableEntry){"aftermath_spin_min",        &s_aftermathSpinMin,       0.0f, 5.0f,  s_aftermathSpinMin,       "aftermath"};
    s_tunables[tn++] = (SkillTunableEntry){"aftermath_spin_max",        &s_aftermathSpinMax,       0.0f, 5.0f,  s_aftermathSpinMax,       "aftermath"};
    s_tunables[tn++] = (SkillTunableEntry){"aftermath_r_dist",          &s_aftermathRDist,         0.0f, 0.5f,  s_aftermathRDist,         "aftermath"};
    s_tunables[tn++] = (SkillTunableEntry){"aftermath_y_jitter",        &s_aftermathYJitter,       0.0f, 0.2f,  s_aftermathYJitter,       "aftermath"};
    s_tunables[tn++] = (SkillTunableEntry){"aftermath_particle_radius", &s_aftermathParticleRadius,0.0f, 0.3f,  s_aftermathParticleRadius,"aftermath"};
