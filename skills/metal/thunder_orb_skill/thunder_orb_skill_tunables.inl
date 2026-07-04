// Included inside InitThunderOrbSkill — sees all file-statics and locals (s_thunderOrbTunables, tn).
// Do not #include at file scope.
// defaultValue for value-kind entries uses the variable itself (equals C initializer at this point,
// before SkillTunables_LoadPersisted runs) — single source of truth, no duplication.
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_max_duration", &s_flightMaxDuration, 0.3f, 8.0f, s_flightMaxDuration, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_max_range", &s_flightMaxRange, 1.0f, 30.0f, s_flightMaxRange, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_speed", NULL, 0.5f, 15.0f, 3.8f, "flight", &s_flightSpeedCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_radius", &s_orbRadius, 0.01f, 0.5f, s_orbRadius, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_len_min", &s_rayLenMin, 0.02f, 2.0f, s_rayLenMin, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_len_max", &s_rayLenMax, 0.05f, 2.0f, s_rayLenMax, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_scale_min", &s_rayScaleMin, 0.0f, 2.0f, s_rayScaleMin, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"ray_scale_max", &s_rayScaleMax, 0.0f, 2.0f, s_rayScaleMax, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"surf_arc_life_min", &s_surfArcLifeMin, 0.01f, 1.0f, s_surfArcLifeMin, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"surf_arc_life_max", &s_surfArcLifeMax, 0.01f, 1.0f, s_surfArcLifeMax, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_radius", &s_orbParticleRadius, 0.0f, 0.3f, s_orbParticleRadius, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_lifetime", &s_orbParticleLifetime, 0.02f, 1.0f, s_orbParticleLifetime, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_speed_min", &s_orbParticleSpeedMin, 0.0f, 5.0f, s_orbParticleSpeedMin, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_particle_speed_max", &s_orbParticleSpeedMax, 0.0f, 5.0f, s_orbParticleSpeedMax, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core1_radius_mult", &s_orbCore1RadiusMult, 0.0f, 5.0f, s_orbCore1RadiusMult, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core1_lifetime", &s_orbCore1Lifetime, 0.02f, 1.0f, s_orbCore1Lifetime, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core2_radius_mult", &s_orbCore2RadiusMult, 0.0f, 5.0f, s_orbCore2RadiusMult, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"orb_core2_lifetime", &s_orbCore2Lifetime, 0.02f, 1.0f, s_orbCore2Lifetime, "flight"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightRadiusCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightSpeedParticleCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "flight", &s_flightAlphaCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"flight_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "flight", &s_flightEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_flightForce, "flight_force_", "flight", &s_thunderOrbTunables[tn]);

    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_knockback", &s_impactKnockback, 0.0f, 10.0f, s_impactKnockback, "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_flash_radius", &s_impactFlashRadius, 0.1f, 5.0f, s_impactFlashRadius, "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_flash_lifetime", &s_impactFlashLifetime, 0.02f, 2.0f, s_impactFlashLifetime, "impact"};

    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_duration", &s_rainDuration, 0.5f, 15.0f, s_rainDuration, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_y_origin", &s_rainYOrigin, 0.5f, 10.0f, s_rainYOrigin, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_bolt_scale", &s_rainBoltScale, 0.1f, 5.0f, s_rainBoltScale, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_bolt_lifetime_min", &s_rainBoltLifetimeMin, 0.02f, 2.0f, s_rainBoltLifetimeMin, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_bolt_lifetime_max", &s_rainBoltLifetimeMax, 0.02f, 2.0f, s_rainBoltLifetimeMax, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_strike_knockback", &s_rainStrikeKnockback, 0.0f, 10.0f, s_rainStrikeKnockback, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_radius", &s_rainRadius, 0.1f, 5.0f, s_rainRadius, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_light_radius", &s_rainLightRadius, 0.05f, 3.0f, s_rainLightRadius, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_radius", &s_sparkRadius, 0.0f, 0.2f, s_sparkRadius, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_count", &s_sparkCount, 0.0f, 40.0f, s_sparkCount, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_out_speed_min", &s_sparkOutSpeedMin, 0.0f, 5.0f, s_sparkOutSpeedMin, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_out_speed_max", &s_sparkOutSpeedMax, 0.0f, 5.0f, s_sparkOutSpeedMax, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_up_speed_min", &s_sparkUpSpeedMin, 0.0f, 8.0f, s_sparkUpSpeedMin, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_up_speed_max", &s_sparkUpSpeedMax, 0.0f, 8.0f, s_sparkUpSpeedMax, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_lifetime_min", &s_sparkLifetimeMin, 0.02f, 2.0f, s_sparkLifetimeMin, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"spark_lifetime_max", &s_sparkLifetimeMax, 0.02f, 2.0f, s_sparkLifetimeMax, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"arc_life", &s_arcLife, 0.02f, 2.0f, s_arcLife, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"arc_radius_min", &s_arcRadiusMin, 0.0f, 2.0f, s_arcRadiusMin, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"arc_radius_max", &s_arcRadiusMax, 0.0f, 2.0f, s_arcRadiusMax, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "rain", &s_rainRadiusCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "rain", &s_rainSpeedCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "rain", &s_rainAlphaCurve};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_emissive_curve", NULL, 0.0f, 3.0f, 1.0f, "rain", &s_rainEmissiveCurve};
    tn += SkillForceMix_MakeTunables(&s_rainForce, "rain_force_", "rain", &s_thunderOrbTunables[tn]);

    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_decal_scale_start", &s_impactDecalScaleStart, 0.0f, 5.0f, s_impactDecalScaleStart, "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_decal_scale_end",   &s_impactDecalScaleEnd,   0.0f, 5.0f, s_impactDecalScaleEnd,   "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_decal_lifetime",    &s_impactDecalLifetime,   0.1f, 20.0f, s_impactDecalLifetime,  "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_decal_rot_speed",   &s_impactDecalRotSpeed,   -360.0f, 360.0f, s_impactDecalRotSpeed, "impact"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"impact_decal_y_offset",    &s_impactDecalYOffset,    0.0f, 0.5f, s_impactDecalYOffset,    "impact"};

    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_decal_scale_start",   &s_rainDecalScaleStart,   0.0f, 5.0f, s_rainDecalScaleStart,   "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_decal_scale_end",     &s_rainDecalScaleEnd,     0.0f, 5.0f, s_rainDecalScaleEnd,     "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_decal_lifetime",      &s_rainDecalLifetime,     0.1f, 20.0f, s_rainDecalLifetime,    "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_decal_rot_speed",     &s_rainDecalRotSpeed,     -360.0f, 360.0f, s_rainDecalRotSpeed, "rain"};
    s_thunderOrbTunables[tn++] = (SkillTunableEntry){"rain_decal_y_offset",      &s_rainDecalYOffset,      0.0f, 0.5f, s_rainDecalYOffset,      "rain"};
