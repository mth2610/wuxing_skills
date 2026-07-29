# HANDOFF — superseded 28/07/2026

This file was the entry point for the remaining Đợt E/F work. That work is done
or re-scoped, and keeping a second, drifting copy of the project state is how a
handoff becomes a lie.

Start here instead:

1. `core/CLAUDE.md` — module rules (mandatory).
2. `core/docs/VFX_PLAN.md` — **the plan** (Đợt H), including an honest
   what-is-left table for E/F in §0.2.
3. `core/docs/PROGRESS.md` — current state + open items. Short on purpose.
4. `ENGINE_LANDMINES.md` and `core/docs/LANDMINES.md` — read before touching any
   shader, particle population, or ribbon/mesh draw.

The one thing worth repeating here, because it shapes how an agent can work:
`./build/wuxing` fails with `FATAL: RLVK: instance creation failed` outside the
owner's graphics session, while the **headless** rlvk tests run fine. Anything
headless is testable by an agent; anything needing a surface is not, so runtime
questions go through a log line, a tunable, or arithmetic.
