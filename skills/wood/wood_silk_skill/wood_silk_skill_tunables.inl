// Included inside InitWoodSilkSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.

    s_tunables[tn++] = (SkillTunableEntry){"strand_length", &s_strandLength, 0.1f, 10.0f, s_strandLength, "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"strand_thick",  &s_strandThick,  0.01f, 1.0f,  s_strandThick,  "cast"};
    s_tunables[tn++] = (SkillTunableEntry){"strand_life",   &s_strandLife,   0.5f,  20.0f, s_strandLife,   "cast"};

    tn += SkillForceMix_MakeTunables(&s_windForceMix, "wind_force_", "cast", &s_tunables[tn]);
