// Included inside InitLeafWhirlwindSkill — sees all file-statics and locals (s_tunables, tn).
// Do not #include at file scope.

s_tunables[tn++] = (SkillTunableEntry){"Base Radius", &s_baseRadius, 0.1f, 10.0f, s_baseRadius, "general"};
s_tunables[tn++] = (SkillTunableEntry){"Effect Scale", &s_effectScale, 0.1f, 10.0f, s_effectScale, "general"};
s_tunables[tn++] = (SkillTunableEntry){"Pull Speed", &s_pullSpeed, 0.5f, 7.0f, s_pullSpeed, "general"};
