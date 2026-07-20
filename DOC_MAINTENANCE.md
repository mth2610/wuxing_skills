# DOC_MAINTENANCE.md

> Mandatory rules when updating any API doc (`docs/API.md`) or any other documentation in the project. Applies to both humans and AI — but especially binding on AI, which tends to write every sentence in the same confident tone whether it is a verified fact or a plausible inference.
>
> Sibling doc: `DOC_ARCHITECTURE.md` governs **how docs are organized** (archetypes, placement). This file governs **how to write** them.

---

## 1. Classify the source of every claim — marking is mandatory

Every new technical claim added to a doc must fall into exactly one of these three tiers:

### Tier 1 — Ground-truth
Read directly from real source (`.h`, `.c`, `.glsl`) that was provided or read during the session.
→ Write it plainly as fact, **no marking needed**.

### Tier 2 — Inferred
Deduced from reading a sample/consumer (e.g. one specific skill) when the source that actually *defines* the behavior has **not** been read (e.g. inferring the UV convention from how `tube.vs` uses `vertexTexCoord`, without having read `procedural_mesh_utils.c`).
→ Must be wrapped in a warning block, spelling out **"(inferred — confirm against `<file>` source)"**:

```markdown
> [!NOTE]
> **(inferred — confirm against `procedural_mesh_utils.c`):** ...inferred content...
```

### Tier 3 — Convention / Decision
A convention chosen by a human or the team, not derivable from reading code (e.g. a hard-coded `lightDir`, a directory-naming rule).
→ Mark it clearly as a convention, not natural engine behavior:

```markdown
> [!NOTE]
> **(project convention):** ...the convention...
```

**Why mandatory:** without classification, an AI re-reading the doc in a later session treats "inferred" as equal to "ground-truth" and keeps propagating the error — exactly the failure that happened in earlier versions of the core API doc (the GLSL Shader Guidelines section), before the real `vs_header.glsl`/`fs_header.glsl`/`lighting.glsl` files existed to check against.

---

## 2. Every technical claim must point to exactly one source file

Do not write vague forms like "Provided by the engine" or "Handled automatically". Always name the **specific file** (`vs_header.glsl`, `lighting.glsl`, `force_field.c`...).

Reason: when that source file later changes, whoever updates the doc knows immediately which passage to re-check, by file name — instead of re-reading the whole doc to guess what was affected.

---

## 3. Never infer and then write as fact when the source is missing

This is the most important rule for AI.

When asked to update/extend a doc **without** the relevant source file in hand, the AI **must not** invent behavior and write it in an assertive tone. Pick one of three:

1. **Ask the user** for the relevant source file before writing.
2. If the user confirms they want a provisional write based on inference → write it in the exact Tier 2 format from section 1, never skipping the disclaimer.
3. If unsure and no immediate need to write → leave the old content as-is, attach a comment:
   ```html
   <!-- TODO: verify against force_field.c — source not read, this section is based on the header comment -->
   ```

**Do not:** silently "round up" an inference into an assertive sentence so the doc looks seamless and professional. Smooth prose is not more important than accuracy.

---

## 4. Patch Log — record every edit

Each doc (or its equivalent) should have a patch-log table at the end, updated on every change:

```markdown
## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-06-29 | Claude | §10 GLSL Shader Guidelines | vs_header.glsl, fs_header.glsl, lighting.glsl (read directly) | Ground-truth |
| 2026-06-29 | Claude | §9 Procedural Mesh — Tube UV convention | inferred from tube.vs/tube.fs | Inferred — not confirmed |
```

Purpose: when a doc is later found to be wrong somewhere, you can trace whether that passage was added from fact or assumption, and from which session, to judge its reliability without re-investigating from scratch.

---

## 5. When sources conflict — priority order

When you find a conflict (e.g. a skill's sample code does something different from the doc, or two sample skills disagree), apply this priority order — do **not** default to "the doc is always right" or "the sample is always right":

1. **Core engine source** (`core/*.h`, `core/*.c`, `core/shaders/common/*.glsl`) — closest to the engine, most trustworthy.
2. **A sample skill confirmed to be canonical** (e.g. one explicitly marked as the reference implementation).
3. **The `.md` doc** — should only ever be a summary of the two above, not an independent source of truth.

When fixing, decide whether the conflict is a **stale doc** or **sample code that is now wrong/outdated under a new rule** — don't automatically assume one side is right. If unsure, ask the user instead of picking a side.

---

## 6. Limit the edit scope — do not rewrite everything

When asked to update **one specific section** of a doc, the AI edits only that scope. Other sections — even if the AI notices they could be improved, have gaps, or could be "tightened up" — must be **left as-is**.

If you spot a problem outside the requested scope: raise it as a **separate suggestion** at the end of the reply, don't edit it into the file automatically.

Reason: edits sprawling beyond the request make diffs hard to review, blur the line between requested and self-decided changes, and raise the risk of slipping inferred (Tier 2) content into places the user hasn't reviewed yet.

---

## 7. Quick checklist before committing a doc edit

- [ ] Is every new sentence tagged with the right tier (Ground-truth / Inferred / Convention)?
- [ ] Does every claim point to a specific source file (if ground-truth)?
- [ ] Is any inference written as absolute fact?
- [ ] Is the Patch Log updated?
- [ ] Is the edit scope exactly what was requested, not sprawling?
- [ ] On a source conflict, did you apply the priority order in §5, or ask the user when unsure?
- [ ] Is the doc in English (only `nguhanhtyvo_kehoach.md` stays Vietnamese)?
