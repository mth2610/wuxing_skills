#ifndef SKILL_BOILERPLATE_H
#define SKILL_BOILERPLATE_H

/*
 * SKILL_EMPTY_PROJECTILE_API(Name)
 *
 * Provides no-op definitions of the three projectile-API lifecycle functions
 * for skills that have no flyable projectile (ground-rising, path-anchored,
 * entity-attached archetypes).  Use in the skill's .c file only — the .h
 * file must still contain the explicit prototypes so generate_registry.py
 * can see them through its text scan.
 *
 * Usage (in <name>_skill.c):
 *   #include "core/skill_boilerplate.h"
 *   ...
 *   SKILL_EMPTY_PROJECTILE_API(ThuyKinh)
 *
 * This expands to:
 *   bool IsThuyKinhSkillCoiling(void) { return false; }
 *   int  GetThuyKinhSkillProjectiles(...) { return 0; }
 *   void DeactivateThuyKinhProjectile(int index) { (void)index; }
 */
#define SKILL_EMPTY_PROJECTILE_API(Name)                                        \
    bool Is##Name##SkillCoiling(void) { return false; }                         \
    int  Get##Name##SkillProjectiles(SkillProjectile *out, int max) {           \
        (void)out; (void)max; return 0;                                         \
    }                                                                           \
    void Deactivate##Name##Projectile(int index) { (void)index; }

#endif // SKILL_BOILERPLATE_H
