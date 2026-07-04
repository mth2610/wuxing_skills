// Included inside InitStonePrisonSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.
// defaultValue for value-kind entries uses the variable itself (equals C initializer
// at this point, before SkillTunables_LoadPersisted runs) — single source of truth.

    s_tunables[tn++] = (SkillTunableEntry){"shake_enable", &s_shakeEnable, 0.0f, 1.0f, s_shakeEnable, "global"};

    // --- pillar geometry ---
    s_tunables[tn++] = (SkillTunableEntry){"pillar_height",              &s_pillarHeight,             0.05f,  5.0f,  s_pillarHeight,             "pillar"};
    s_tunables[tn++] = (SkillTunableEntry){"pillar_radius",              &s_pillarRadius,             0.01f,  1.0f,  s_pillarRadius,             "pillar"};
    s_tunables[tn++] = (SkillTunableEntry){"rise_pillar_y_sink",         &s_risePillarYSink,          0.0f,   1.0f,  s_risePillarYSink,          "pillar"};

    // --- cast phase ---
    s_tunables[tn++] = (SkillTunableEntry){"cast_light_radius",          &s_castLightRadius,          0.1f,   5.0f,  s_castLightRadius,          "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_dust_vel_outward",      &s_castDustVelOutward,       0.0f,   5.0f,  s_castDustVelOutward,       "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_dust_vel_y_min",        &s_castDustVelYMin,          0.0f,   5.0f,  s_castDustVelYMin,          "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_dust_vel_y_max",        &s_castDustVelYMax,          0.0f,   5.0f,  s_castDustVelYMax,          "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_dust_radius_min",       &s_castDustRadiusMin,        0.001f, 0.2f,  s_castDustRadiusMin,        "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_dust_radius_max",       &s_castDustRadiusMax,        0.001f, 0.2f,  s_castDustRadiusMax,        "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_distort_radius",        &s_castDistortRadius,        0.1f,   5.0f,  s_castDistortRadius,        "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_radius_curve",          NULL, 0.0f, 3.0f, 1.0f, "cast",    &s_castRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_speed_curve",           NULL, 0.0f, 3.0f, 1.0f, "cast",    &s_castSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_alpha_curve",           NULL, 0.0f, 1.0f, 1.0f, "cast",    &s_castAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_emissive_curve",        NULL, 0.0f, 3.0f, 1.0f, "cast",    &s_castEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_castForce, "cast_force_", "cast", &s_tunables[tn]);

    // --- rise phase ---
    s_tunables[tn++] = (SkillTunableEntry){"rise_light_radius",          &s_riseLightRadius,          0.1f,   5.0f,  s_riseLightRadius,          "rise"};
    s_tunables[tn++] = (SkillTunableEntry){"rise_vel_outward",           &s_riseVelOutward,           0.0f,   5.0f,  s_riseVelOutward,           "rise"};
    s_tunables[tn++] = (SkillTunableEntry){"rise_vel_y_min",             &s_riseVelYMin,              0.0f,   5.0f,  s_riseVelYMin,              "rise"};
    s_tunables[tn++] = (SkillTunableEntry){"rise_vel_y_max",             &s_riseVelYMax,              0.0f,   5.0f,  s_riseVelYMax,              "rise"};
    s_tunables[tn++] = (SkillTunableEntry){"rise_particle_radius_min",   &s_riseParticleRadiusMin,    0.001f, 0.2f,  s_riseParticleRadiusMin,    "rise"};
    s_tunables[tn++] = (SkillTunableEntry){"rise_particle_radius_max",   &s_riseParticleRadiusMax,    0.001f, 0.2f,  s_riseParticleRadiusMax,    "rise"};
    s_tunables[tn++] = (SkillTunableEntry){"rise_radius_curve",          NULL, 0.0f, 3.0f, 1.0f, "rise",    &s_riseRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"rise_speed_curve",           NULL, 0.0f, 3.0f, 1.0f, "rise",    &s_riseSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"rise_alpha_curve",           NULL, 0.0f, 1.0f, 1.0f, "rise",    &s_riseAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"rise_emissive_curve",        NULL, 0.0f, 3.0f, 1.0f, "rise",    &s_riseEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_riseForce, "rise_force_", "rise", &s_tunables[tn]);

    // --- hold phase ---
    s_tunables[tn++] = (SkillTunableEntry){"spark_y_offset",             &s_sparkYOffset,             0.0f,   0.5f,  s_sparkYOffset,             "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"spark_vel_y_min",            &s_sparkVelYMin,             0.0f,   5.0f,  s_sparkVelYMin,             "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"spark_vel_y_max",            &s_sparkVelYMax,             0.0f,   5.0f,  s_sparkVelYMax,             "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"spark_radius_min",           &s_sparkRadiusMin,           0.001f, 0.1f,  s_sparkRadiusMin,           "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"spark_radius_max",           &s_sparkRadiusMax,           0.001f, 0.1f,  s_sparkRadiusMax,           "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"hold_radius_curve",          NULL, 0.0f, 3.0f, 1.0f, "hold",    &s_holdRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"hold_speed_curve",           NULL, 0.0f, 3.0f, 1.0f, "hold",    &s_holdSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"hold_alpha_curve",           NULL, 0.0f, 1.0f, 1.0f, "hold",    &s_holdAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"hold_emissive_curve",        NULL, 0.0f, 3.0f, 1.0f, "hold",    &s_holdEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_holdForce, "hold_force_", "hold", &s_tunables[tn]);

    // --- explode phase ---
    s_tunables[tn++] = (SkillTunableEntry){"explode_light_radius",       &s_explodeLightRadius,       0.1f,   5.0f,  s_explodeLightRadius,       "explode"};
    s_tunables[tn++] = (SkillTunableEntry){"explode_vel_outward",        &s_explodeVelOutward,        0.0f,   5.0f,  s_explodeVelOutward,        "explode"};
    s_tunables[tn++] = (SkillTunableEntry){"explode_vel_y_min",          &s_explodeVelYMin,           0.0f,   5.0f,  s_explodeVelYMin,           "explode"};
    s_tunables[tn++] = (SkillTunableEntry){"explode_vel_y_max",          &s_explodeVelYMax,           0.0f,   5.0f,  s_explodeVelYMax,           "explode"};
    s_tunables[tn++] = (SkillTunableEntry){"explode_particle_radius_min",&s_explodeParticleRadiusMin, 0.001f, 0.2f,  s_explodeParticleRadiusMin, "explode"};
    s_tunables[tn++] = (SkillTunableEntry){"explode_particle_radius_max",&s_explodeParticleRadiusMax, 0.001f, 0.2f,  s_explodeParticleRadiusMax, "explode"};
    s_tunables[tn++] = (SkillTunableEntry){"explode_distort_radius",     &s_explodeDistortRadius,     0.1f,   5.0f,  s_explodeDistortRadius,     "explode"};
    s_tunables[tn++] = (SkillTunableEntry){"explode_radius_curve",       NULL, 0.0f, 3.0f, 1.0f, "explode", &s_explodeRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"explode_speed_curve",        NULL, 0.0f, 3.0f, 1.0f, "explode", &s_explodeSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"explode_alpha_curve",        NULL, 0.0f, 1.0f, 1.0f, "explode", &s_explodeAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"explode_emissive_curve",     NULL, 0.0f, 3.0f, 1.0f, "explode", &s_explodeEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_explodeForce, "explode_force_", "explode", &s_tunables[tn]);
