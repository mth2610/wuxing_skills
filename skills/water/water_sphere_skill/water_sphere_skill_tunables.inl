// Included inside InitWaterSphereSkill — sees all file-statics and locals
// (s_tunables, tn).  Do not #include at file scope.
// defaultValue for value-kind entries uses the variable itself — single source
// of truth, no duplication.

    s_tunables[tn++] = (SkillTunableEntry){"shake_enable", &s_shakeEnable, 0.0f, 1.0f, s_shakeEnable, "global"};

    // ── flight phase ──────────────────────────────────────────────────────────
    s_tunables[tn++] = (SkillTunableEntry){"sphere_base_radius",       &s_sphereBaseRadius,    0.05f,  0.5f,  s_sphereBaseRadius,    "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"sphere_speed",             &s_sphereSpeed,         0.5f,   8.0f,  s_sphereSpeed,         "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"flight_light_radius_mult", &s_flightLightRadiusMult, 1.0f, 10.0f, s_flightLightRadiusMult, "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_vel_xz",              &s_dripVelXZ,           0.0f,   2.0f,  s_dripVelXZ,           "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_vel_y",               &s_dripVelY,            -5.0f,  0.0f,  s_dripVelY,            "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_radius_min",          &s_dripRadiusMin,       0.0f,   0.2f,  s_dripRadiusMin,       "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_radius_max",          &s_dripRadiusMax,       0.0f,   0.2f,  s_dripRadiusMax,       "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"drip_lifetime",            &s_dripLifetime,        0.05f,  2.0f,  s_dripLifetime,        "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"arrival_radius",           &s_arrivalRadius,       0.05f,  1.0f,  s_arrivalRadius,       "flight"};
    s_tunables[tn++] = (SkillTunableEntry){"flight_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightRadiusCurve};
    s_tunables[tn++] = (SkillTunableEntry){"flight_speed_curve",  NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightSpeedCurve};
    s_tunables[tn++] = (SkillTunableEntry){"flight_alpha_curve",    NULL, 0.0f, 1.0f, 1.0f, "flight", &s_flightAlphaCurve};
    s_tunables[tn++] = (SkillTunableEntry){"flight_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_flightForce, "flight_force_", "flight", &s_tunables[tn]);

    // ── splash / force field ──────────────────────────────────────────────────
    s_tunables[tn++] = (SkillTunableEntry){"splash_gravity",    &s_splashGravity,  0.0f, 20.0f, s_splashGravity,  "splash"};
    s_tunables[tn++] = (SkillTunableEntry){"splash_noise",      &s_splashNoise,    0.0f,  5.0f, s_splashNoise,    "splash"};
    s_tunables[tn++] = (SkillTunableEntry){"splash_drag",       &s_splashDrag,     0.0f, 10.0f, s_splashDrag,     "splash"};

    // ── impact phase ──────────────────────────────────────────────────────────
    s_tunables[tn++] = (SkillTunableEntry){"impact_distort_radius",   &s_impactDistortRadius,   0.1f,  5.0f, s_impactDistortRadius,   "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_distort_strength", &s_impactDistortStrength, 0.0f,  2.0f, s_impactDistortStrength, "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_distort_life",     &s_impactDistortLife,     0.1f,  3.0f, s_impactDistortLife,     "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_distort_speed",    &s_impactDistortSpeed,    0.1f, 10.0f, s_impactDistortSpeed,    "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_decal_scale",      &s_impactDecalScale,      0.0f,  1.0f, s_impactDecalScale,      "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_decal_life",       &s_impactDecalLife,       0.1f, 20.0f, s_impactDecalLife,       "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_light_radius",     &s_impactLightRadius,     0.1f,  5.0f, s_impactLightRadius,     "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"impact_light_life",       &s_impactLightLife,       0.1f,  5.0f, s_impactLightLife,       "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_speed_min",         &s_burstSpeedMin,         0.0f, 10.0f, s_burstSpeedMin,         "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_speed_max",         &s_burstSpeedMax,         0.0f, 10.0f, s_burstSpeedMax,         "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_radius_min",        &s_burstRadiusMin,        0.0f,  0.5f, s_burstRadiusMin,        "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_radius_max",        &s_burstRadiusMax,        0.0f,  0.5f, s_burstRadiusMax,        "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_life_min",          &s_burstLifeMin,          0.1f,  5.0f, s_burstLifeMin,          "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_life_max",          &s_burstLifeMax,          0.1f,  5.0f, s_burstLifeMax,          "impact"};
    s_tunables[tn++] = (SkillTunableEntry){"burst_upward_bias",       &s_burstUpwardBias,       0.0f,  8.0f, s_burstUpwardBias,       "impact"};
