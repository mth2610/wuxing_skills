// metal_wire_orb_skill_tunables.inl
// Included inside InitMetalWireOrbSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.

    s_tunables[tn++] = (SkillTunableEntry){"Orb Radius", &tp_orbRadius, 0.1f, 5.0f, 1.0f, "orb"};
    s_tunables[tn++] = (SkillTunableEntry){"Orb Speed", &tp_orbSpeed, 1.0f, 50.0f, 15.0f, "orb"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Spawn Interval", &tp_wireSpawnInterval, 0.01f, 0.5f, 0.02f, "wire"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Orbit Radius Min", &tp_wireOrbitRadiusMin, 0.1f, 10.0f, 1.2f, "orbit"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Orbit Radius Max", &tp_wireOrbitRadiusMax, 0.1f, 10.0f, 3.0f, "orbit"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Orbit Speed Min", &tp_wireOrbitSpeedMin, 1.0f, 50.0f, 5.0f, "orbit"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Orbit Speed Max", &tp_wireOrbitSpeedMax, 1.0f, 50.0f, 15.0f, "orbit"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Length Min", &tp_wireLengthMin, 1.0f, 100.0f, 30.0f, "wire"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Length Max", &tp_wireLengthMax, 1.0f, 100.0f, 60.0f, "wire"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Life Min", &tp_wireLifeMin, 0.05f, 5.0f, 0.5f, "wire"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Life Max", &tp_wireLifeMax, 0.05f, 5.0f, 1.2f, "wire"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Width Min", &tp_wireThickMin, 0.01f, 1.0f, 0.02f, "wire"};
    s_tunables[tn++] = (SkillTunableEntry){"Wire Width Max", &tp_wireThickMax, 0.01f, 1.0f, 0.08f, "wire"};

    tn += SkillForceMix_MakeTunables(&s_wireForceMix, "wire_force_", "wire", &s_tunables[tn]);
