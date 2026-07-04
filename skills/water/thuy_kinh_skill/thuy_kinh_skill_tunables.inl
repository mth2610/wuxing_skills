// Included inside InitThuyKinhSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.
// defaultValue for value-kind entries uses the variable itself (equals C initializer
// at this point, before SkillTunables_LoadPersisted runs) — single source of truth.

    s_tunables[tn++] = (SkillTunableEntry){"shake_enable", &s_shakeEnable, 0.0f, 1.0f, s_shakeEnable, "global"};

    // --- dome ---
    s_tunables[tn++] = (SkillTunableEntry){"shield_radius",         &s_shieldRadius,        0.1f,  5.0f, s_shieldRadius,        "dome"};

    // --- gather phase ---
    s_tunables[tn++] = (SkillTunableEntry){"gather_particle_y_min", &s_gatherParticleYMin,  0.0f,  1.0f, s_gatherParticleYMin,  "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_particle_y_max", &s_gatherParticleYMax,  0.0f,  2.0f, s_gatherParticleYMax,  "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_target_y",       &s_gatherTargetY,       0.0f,  2.0f, s_gatherTargetY,       "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_speed",          &s_gatherSpeed,         0.1f,  5.0f, s_gatherSpeed,         "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_radius_min",     &s_gatherRadiusMin,     0.001f,0.2f, s_gatherRadiusMin,     "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_radius_max",     &s_gatherRadiusMax,     0.001f,0.2f, s_gatherRadiusMax,     "gather"};
    s_tunables[tn++] = (SkillTunableEntry){"gather_radius_curve",   NULL, 0.0f, 3.0f, 1.0f, "gather", &s_gatherRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"gather_speed_curve",    NULL, 0.0f, 3.0f, 1.0f, "gather", &s_gatherSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"gather_alpha_curve",    NULL, 0.0f, 1.0f, 1.0f, "gather", &s_gatherAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"gather_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "gather", &s_gatherEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_gatherForce, "gather_force_", "gather", &s_tunables[tn]);

    // --- cast VFX ---
    s_tunables[tn++] = (SkillTunableEntry){"cast_light_y",          &s_castLightY,          0.0f,  2.0f, s_castLightY,          "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"cast_light_radius",     &s_castLightRadius,     0.1f,  5.0f, s_castLightRadius,     "cast"};

    // --- ribbon ---
    s_tunables[tn++] = (SkillTunableEntry){"ribbon_len",            &s_ribbonLen,           0.001f,0.5f, s_ribbonLen,           "ribbon"};
    s_tunables[tn++] = (SkillTunableEntry){"ribbon_thick",          &s_ribbonThick,         0.001f,0.2f, s_ribbonThick,         "ribbon"};
    s_tunables[tn++] = (SkillTunableEntry){"ribbon_floor_y",        &s_ribbonFloorY,        0.0f,  0.5f, s_ribbonFloorY,        "ribbon"};

    // --- drip ---
    s_tunables[tn++] = (SkillTunableEntry){"drip_vel_xz",           &s_dripVelXZ,           0.0f,  2.0f, s_dripVelXZ,           "drip"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_vel_y",            &s_dripVelY,            -5.0f, 0.0f, s_dripVelY,            "drip"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_radius_min",       &s_dripRadiusMin,       0.001f,0.1f, s_dripRadiusMin,       "drip"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_radius_max",       &s_dripRadiusMax,       0.001f,0.1f, s_dripRadiusMax,       "drip"};

    // --- bloom burst ---
    s_tunables[tn++] = (SkillTunableEntry){"bloom_crown_y",         &s_bloomCrownY,         0.0f,  0.5f, s_bloomCrownY,         "bloom"};
    s_tunables[tn++] = (SkillTunableEntry){"bloom_vel_outward",     &s_bloomVelOutward,     0.0f,  5.0f, s_bloomVelOutward,     "bloom"};
    s_tunables[tn++] = (SkillTunableEntry){"bloom_vel_up_min",      &s_bloomVelUpMin,       0.0f,  5.0f, s_bloomVelUpMin,       "bloom"};
    s_tunables[tn++] = (SkillTunableEntry){"bloom_vel_up_max",      &s_bloomVelUpMax,       0.0f,  5.0f, s_bloomVelUpMax,       "bloom"};
    s_tunables[tn++] = (SkillTunableEntry){"bloom_crown_radius_min",&s_bloomCrownRadiusMin, 0.001f,0.2f, s_bloomCrownRadiusMin, "bloom"};
    s_tunables[tn++] = (SkillTunableEntry){"bloom_crown_radius_max",&s_bloomCrownRadiusMax, 0.001f,0.2f, s_bloomCrownRadiusMax, "bloom"};
    s_tunables[tn++] = (SkillTunableEntry){"bloom_light_radius",    &s_bloomLightRadius,    0.1f,  5.0f, s_bloomLightRadius,    "bloom"};
    s_tunables[tn++] = (SkillTunableEntry){"bloom_surge_knockback", &s_bloomSurgeKnockback, 0.0f, 10.0f, s_bloomSurgeKnockback, "bloom"};

    // --- mist (active) ---
    s_tunables[tn++] = (SkillTunableEntry){"mist_y_min",            &s_mistYMin,            0.0f,  1.0f, s_mistYMin,            "mist"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_y_max",            &s_mistYMax,            0.0f,  2.0f, s_mistYMax,            "mist"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_vel_xz",           &s_mistVelXZ,           0.0f,  2.0f, s_mistVelXZ,           "mist"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_vel_y_min",        &s_mistVelYMin,         0.0f,  5.0f, s_mistVelYMin,         "mist"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_vel_y_max",        &s_mistVelYMax,         0.0f,  5.0f, s_mistVelYMax,         "mist"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_radius_min",       &s_mistRadiusMin,       0.001f,0.2f, s_mistRadiusMin,       "mist"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_radius_max",       &s_mistRadiusMax,       0.001f,0.2f, s_mistRadiusMax,       "mist"};
    s_tunables[tn++] = (SkillTunableEntry){"mist_radius_curve",     NULL, 0.0f, 3.0f, 1.0f, "mist", &s_mistRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"mist_speed_curve",      NULL, 0.0f, 3.0f, 1.0f, "mist", &s_mistSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"mist_alpha_curve",      NULL, 0.0f, 1.0f, 1.0f, "mist", &s_mistAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"mist_emissive_curve",   NULL, 0.0f, 3.0f, 1.0f, "mist", &s_mistEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_mistForce, "mist_force_", "mist", &s_tunables[tn]);

    // --- swirl force field ---
    s_tunables[tn++] = (SkillTunableEntry){"swirl_vortex",          &s_swirlVortex,         0.0f,  10.0f,s_swirlVortex,         "swirl"};
    s_tunables[tn++] = (SkillTunableEntry){"swirl_pull",            &s_swirlPull,           0.0f,  5.0f, s_swirlPull,           "swirl"};
    s_tunables[tn++] = (SkillTunableEntry){"swirl_drag",            &s_swirlDrag,           0.0f,  5.0f, s_swirlDrag,           "swirl"};

    // --- dissolve rain ---
    s_tunables[tn++] = (SkillTunableEntry){"rain_vel_outward",      &s_rainVelOutward,      0.0f,  5.0f, s_rainVelOutward,      "dissolve"};
    s_tunables[tn++] = (SkillTunableEntry){"rain_vel_up",           &s_rainVelUp,           0.0f,  5.0f, s_rainVelUp,           "dissolve"};
    s_tunables[tn++] = (SkillTunableEntry){"rain_radius_min",       &s_rainRadiusMin,       0.001f,0.1f, s_rainRadiusMin,       "dissolve"};
    s_tunables[tn++] = (SkillTunableEntry){"rain_radius_max",       &s_rainRadiusMax,       0.001f,0.1f, s_rainRadiusMax,       "dissolve"};
    s_tunables[tn++] = (SkillTunableEntry){"rain_gravity",          &s_rainGravity,         0.1f,  20.0f,s_rainGravity,         "dissolve"};
    s_tunables[tn++] = (SkillTunableEntry){"dissolve_radius_curve",   NULL, 0.0f, 3.0f, 1.0f, "dissolve", &s_dissolveRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"dissolve_speed_curve",    NULL, 0.0f, 3.0f, 1.0f, "dissolve", &s_dissolveSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"dissolve_alpha_curve",    NULL, 0.0f, 1.0f, 1.0f, "dissolve", &s_dissolveAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"dissolve_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "dissolve", &s_dissolveEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_dissolveForce, "dissolve_force_", "dissolve", &s_tunables[tn]);
