// Included inside InitWoodThornsSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.
// defaultValue for value-kind entries uses the variable itself (equals C initializer at this
// point, before SkillTunables_LoadPersisted runs) — single source of truth, no duplication.

    s_tunables[tn++] = (SkillTunableEntry){"shake_enable", &s_shakeEnable, 0.0f, 1.0f, s_shakeEnable, "global"};

    // Spatial shape
    s_tunables[tn++] = (SkillTunableEntry){"thorn_spacing",      &s_thornSpacing,     0.05f, 2.0f,   s_thornSpacing,     "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"thorn_max_height",   &s_thornMaxHeight,   0.05f, 2.0f,   s_thornMaxHeight,   "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"thorn_base_radius",  &s_thornBaseRadius,  0.01f, 0.5f,   s_thornBaseRadius,  "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"aoe_radius",         &s_aoeRadius,        0.05f, 2.0f,   s_aoeRadius,        "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"decal_scale",        &s_decalScale,       0.0f,  5.0f,   s_decalScale,       "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"light_radius",       &s_lightRadius,      0.05f, 3.0f,   s_lightRadius,      "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"shake_trauma",       &s_shakeTrauma,      0.0f,  1.0f,   s_shakeTrauma,      "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"distort_radius",     &s_distortRadius,    0.05f, 3.0f,   s_distortRadius,    "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"side_jitter_max",    &s_sideJitterMax,    0.0f,  1.0f,   s_sideJitterMax,    "cast"};

    // Dust burst (cast phase)
    s_tunables[tn++] = (SkillTunableEntry){"dust_speed_out_min", &s_dustSpeedOutMin,  0.0f,  5.0f,   s_dustSpeedOutMin,  "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"dust_speed_out_max", &s_dustSpeedOutMax,  0.0f,  5.0f,   s_dustSpeedOutMax,  "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"dust_speed_up_min",  &s_dustSpeedUpMin,   0.0f,  5.0f,   s_dustSpeedUpMin,   "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"dust_speed_up_max",  &s_dustSpeedUpMax,   0.0f,  5.0f,   s_dustSpeedUpMax,   "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"dust_radius_min",    &s_dustRadiusMin,    0.0f,  0.3f,   s_dustRadiusMin,    "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"dust_radius_max",    &s_dustRadiusMax,    0.0f,  0.3f,   s_dustRadiusMax,    "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"dust_life_min",      &s_dustLifeMin,      0.05f, 3.0f,   s_dustLifeMin,      "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"dust_life_max",      &s_dustLifeMax,      0.05f, 3.0f,   s_dustLifeMax,      "cast"};

    // Curves — cast phase
    s_tunables[tn++] = (SkillTunableEntry){"cast_radius_curve",  NULL, 0.0f, 3.0f, 1.0f, "cast", &s_castRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_speed_curve",   NULL, 0.0f, 3.0f, 1.0f, "cast", &s_castSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_alpha_curve",   NULL, 0.0f, 1.0f, 1.0f, "cast", &s_castAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "cast", &s_castEmissiveCurve};

    // Force mix — cast phase
    tn += SkillForceMix_MakeTunables(&s_castForce, "cast_force_", "cast", &s_tunables[tn]);

    // Holding mist
    s_tunables[tn++] = (SkillTunableEntry){"mist_speed_out_min", &s_mistSpeedOutMin,  0.0f,  3.0f,   s_mistSpeedOutMin,  "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_speed_out_max", &s_mistSpeedOutMax,  0.0f,  3.0f,   s_mistSpeedOutMax,  "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_speed_up_min",  &s_mistSpeedUpMin,   0.0f,  3.0f,   s_mistSpeedUpMin,   "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_speed_up_max",  &s_mistSpeedUpMax,   0.0f,  3.0f,   s_mistSpeedUpMax,   "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_radius_min",    &s_mistRadiusMin,    0.0f,  0.5f,   s_mistRadiusMin,    "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_radius_max",    &s_mistRadiusMax,    0.0f,  0.5f,   s_mistRadiusMax,    "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_life_min",      &s_mistLifeMin,      0.05f, 5.0f,   s_mistLifeMin,      "hold"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_life_max",      &s_mistLifeMax,      0.05f, 5.0f,   s_mistLifeMax,      "hold"};

    // Curves — hold phase
    s_tunables[tn++] = (SkillTunableEntry){"hold_radius_curve",  NULL, 0.0f, 3.0f, 1.0f, "hold", &s_holdRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"hold_speed_curve",   NULL, 0.0f, 3.0f, 1.0f, "hold", &s_holdSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"hold_alpha_curve",   NULL, 0.0f, 1.0f, 1.0f, "hold", &s_holdAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"hold_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "hold", &s_holdEmissiveCurve};

    // Force mix — hold phase
    tn += SkillForceMix_MakeTunables(&s_holdForce, "hold_force_", "hold", &s_tunables[tn]);
