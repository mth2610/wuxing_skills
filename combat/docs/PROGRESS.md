# combat — Progress

## Done
- Projectile-registry convention (see `docs/API.md` §5) migrated so far (Step 0, 07/2026): `FIRE` (dragon head collider), `GLACIAL_CANNON` (wavefront collider; final burst via team-aware `Entity_ApplyAoEDamage`; the channel fizzles when its caster dies), `TUBE` (stream head collider).

## Backlog
- Remaining VFX-only skills adopt the `Combat_SubmitProjectile` / `ClashEvent` pattern as they gain gameplay.
