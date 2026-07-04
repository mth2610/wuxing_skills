// Included inside InitDiaLongSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.
// defaultValue uses the variable itself (equals C initializer before SkillTunables_LoadPersisted).

    s_tunables[tn++] = (SkillTunableEntry){"shake_enable", &s_shakeEnable, 0.0f, 1.0f, s_shakeEnable, "global"};

    // --- cast phase ---
    s_tunables[tn++] = (SkillTunableEntry){"vert_spacing",       &s_vertSpacing,       0.1f,  2.0f,  s_vertSpacing,       "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_light_radius",  &s_castLightRadius,   0.1f,  3.0f,  s_castLightRadius,   "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"rune_decal_radius",  &s_runeDecalRadius,   0.1f,  3.0f,  s_runeDecalRadius,   "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_radius_curve",  NULL, 0.0f, 3.0f, 1.0f, "cast",   &s_castRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_speed_curve",   NULL, 0.0f, 3.0f, 1.0f, "cast",   &s_castSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_alpha_curve",    NULL, 0.0f, 1.0f, 1.0f, "cast",   &s_castAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"cast_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "cast",   &s_castEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_castForce,   "cast_force_",   "cast",   &s_tunables[tn]);

    // --- erupt phase ---
    s_tunables[tn++] = (SkillTunableEntry){"vert_height_base",      &s_vertHeightBase,      0.05f, 2.0f,  s_vertHeightBase,      "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"vert_height_arc",       &s_vertHeightArc,       0.0f,  1.0f,  s_vertHeightArc,       "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"fissure_width",         &s_fissureWidth,        0.01f, 1.0f,  s_fissureWidth,        "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"fissure_depth",         &s_fissureDepth,        0.01f, 1.0f,  s_fissureDepth,        "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"erupt_light_radius",    &s_eruptLightRadius,    0.1f,  3.0f,  s_eruptLightRadius,    "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"crack_decal_radius",    &s_crackDecalRadius,    0.1f,  2.0f,  s_crackDecalRadius,    "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"shatter_decal_radius_v",&s_shatterDecalRadiusV, 0.1f,  2.0f,  s_shatterDecalRadiusV, "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"vert_aoe_radius",       &s_vertAoERadius,       0.1f,  2.0f,  s_vertAoERadius,       "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"projectile_radius",     &s_projectileRadius,    0.1f,  2.0f,  s_projectileRadius,    "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"distort_radius",        &s_distortRadius,       0.1f,  5.0f,  s_distortRadius,       "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_speed_out",       &s_burstSpeedOut,       0.0f,  5.0f,  s_burstSpeedOut,       "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_speed_up_min",    &s_burstSpeedUpMin,     0.0f,  5.0f,  s_burstSpeedUpMin,     "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_speed_up_max",    &s_burstSpeedUpMax,     0.0f,  5.0f,  s_burstSpeedUpMax,     "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_radius_min",      &s_burstRadiusMin,      0.0f,  0.2f,  s_burstRadiusMin,      "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_radius_max",      &s_burstRadiusMax,      0.0f,  0.2f,  s_burstRadiusMax,      "erupt"};
    s_tunables[tn++] = (SkillTunableEntry){"erupt_radius_curve",    NULL, 0.0f, 3.0f, 1.0f, "erupt", &s_eruptRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"erupt_speed_curve",     NULL, 0.0f, 3.0f, 1.0f, "erupt", &s_eruptSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"erupt_alpha_curve",     NULL, 0.0f, 1.0f, 1.0f, "erupt", &s_eruptAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"erupt_emissive_curve",  NULL, 0.0f, 3.0f, 1.0f, "erupt", &s_eruptEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_eruptForce,  "erupt_force_",  "erupt",  &s_tunables[tn]);

    // --- active phase ---
    s_tunables[tn++] = (SkillTunableEntry){"head_height",            &s_headHeight,            0.1f,  3.0f,   s_headHeight,            "active"};
    s_tunables[tn++] = (SkillTunableEntry){"head_light_radius",      &s_headLightRadius,       0.1f,  5.0f,   s_headLightRadius,       "active"};
    s_tunables[tn++] = (SkillTunableEntry){"shatter_decal_radius_h", &s_shatterDecalRadiusH,   0.1f,  2.0f,   s_shatterDecalRadiusH,   "active"};
    s_tunables[tn++] = (SkillTunableEntry){"lava_decal_radius",      &s_lavaDecalRadius,       0.1f,  2.0f,   s_lavaDecalRadius,       "active"};
    s_tunables[tn++] = (SkillTunableEntry){"head_aoe_radius",        &s_headAoERadius,         0.1f,  3.0f,   s_headAoERadius,         "active"};
    s_tunables[tn++] = (SkillTunableEntry){"dot_aoe_radius",         &s_dotAoERadius,          0.1f,  3.0f,   s_dotAoERadius,          "active"};
    s_tunables[tn++] = (SkillTunableEntry){"head_emitter_rate",      &s_headEmitterRate,       0.0f,  100.0f, s_headEmitterRate,       "active"};
    s_tunables[tn++] = (SkillTunableEntry){"ember_speed_xz",         &s_emberSpeedXZ,          0.0f,  2.0f,   s_emberSpeedXZ,          "active"};
    s_tunables[tn++] = (SkillTunableEntry){"ember_speed_up_min",     &s_emberSpeedUpMin,       0.0f,  2.0f,   s_emberSpeedUpMin,       "active"};
    s_tunables[tn++] = (SkillTunableEntry){"ember_speed_up_max",     &s_emberSpeedUpMax,       0.0f,  2.0f,   s_emberSpeedUpMax,       "active"};
    s_tunables[tn++] = (SkillTunableEntry){"ember_radius_min",       &s_emberRadiusMin,        0.0f,  0.1f,   s_emberRadiusMin,        "active"};
    s_tunables[tn++] = (SkillTunableEntry){"ember_radius_max",       &s_emberRadiusMax,        0.0f,  0.1f,   s_emberRadiusMax,        "active"};
    s_tunables[tn++] = (SkillTunableEntry){"active_radius_curve",    NULL, 0.0f, 3.0f, 1.0f, "active", &s_activeRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"active_speed_curve",     NULL, 0.0f, 3.0f, 1.0f, "active", &s_activeSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"active_alpha_curve",     NULL, 0.0f, 1.0f, 1.0f, "active", &s_activeAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"active_emissive_curve",  NULL, 0.0f, 3.0f, 1.0f, "active", &s_activeEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_activeForce, "active_force_", "active", &s_tunables[tn]);
