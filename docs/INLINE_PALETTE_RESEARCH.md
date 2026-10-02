# Inline Palette Experiment — Research & Backlog

Status: **BACKLOG** (not started)
Source: Roadmap v2, Phase 0 Experiment F (§6) + §8.1 mid-project insertion.
Created: 2026-09-22

---

## 1. What this experiment is

A horizontal, clickable color palette rendered **directly inside the Windows 11
compact context menu** (Finder-style), instead of (or in addition to) the
standard `Tags >` submenu:

```text
Right click file/folder

Open
Share
...
🔴 🟠 🟡 🟢 🔵 🟣 ⚪
Customize / Remove tag
```

Each circle maps to `SetTag(path, color)`; a "remove" affordance maps to
`RemoveTag(path)`.

This is **experimental UI only**. It must never change the tag data model or
storage, and the official `Tags >` submenu remains the mandatory fallback.

---

## 2. Placement decision (relative to Phases 3–5)

### Recommendation: run inside Phase 4 as an experimental parallel track

Phase 4 (Explorer context menu) is the natural host. The palette is not a
separate blocking phase and does not touch Phase 3 (CLI) or Phase 5 (Tag
column).

| Phase | Relationship | Why |
|---|---|---|
| Phase 3 (CLI) | untouched | CLI is the validation tool; palette is UI-only |
| Phase 4 (context menu) | **host** — palette is a track inside it | palette reuses Phase 4's entire plumbing: `IExplorerCommand` registration, `IShellItemArray` → paths, multi-select, menu refresh |
| Phase 5 (Tag column) | independent, not gated | column work is property-system/registration; the palette must not block the MVP's primary visual indicator |

### Rationale

1. **Dependency direction (§8.1):** `InlinePaletteExperiment → Explorer
   integration → TagService → ITagStore`. The palette needs exactly the
   Explorer-integration layer Phase 4 builds. Building it earlier means
   building that plumbing twice.
2. **Mandatory fallback first (§6):** "The official compact-menu integration
   remains the mandatory fallback." Phase 4's `Tags >` submenu *is* that
   fallback; it must exist before the palette can be judged.
3. **Parallel prototype (§8.1):** Phase 4 has not started, so per the roadmap
   we prototype the official submenu and the palette in parallel — the
   submenu as the deliverable, the palette as the experiment.
4. **De-risks Phase 7:** the palette validates the same injection territory
   (UWPSpy / Windhawk on Explorer's WinUI flyout) that Phase 7 (row tint)
   needs. A low-stakes answer here informs the high-stakes decision there.
5. **Risk profile (§20):** "Inline palette in compact menu — Experimental /
   High". Consistent with a gated experiment: stable → optional adapter;
   unstable → keep the submenu only.

### Technical reality (from Phase 0 findings)

- `IExplorerCommand` is a command model (title / icon / state / subcommands).
  It **cannot host custom controls** → a native horizontal palette is **NO**.
- The compact menu is WinUI 3 (XAML); its flyout tree is not reachable via
  official APIs → a palette requires **UI injection** (UWPSpy / Windhawk
  XAML injection) → **PARTIAL / experimental**.
- Realistic outcomes:
  - injection proves stable → optional palette adapter over Phase 4's
    integration layer;
  - injection proves fragile → keep `Tags >` submenu, document the finding.

---

## 3. Backlog entry

```text
[BACKLOG] Phase 4 track B — Inline palette experiment
  Parent: Phase 4 (Explorer context menu)
  Depends on: Phase 4 track A (official Tags > submenu) — the fallback
  Blocks: nothing (must not gate Phase 5)
  Deliverable: docs/INLINE_PALETTE_RESEARCH.md findings + optional adapter
  Exit criteria: stable → adapter; unstable → documented rejection
```

---

## 4. Research questions (from Experiment F)

1. Can `IExplorerCommand` support anything beyond standard command rows and
   subcommands?
2. Does the Win11 Explorer context menu expose a usable WinUI/XAML visual
   tree?
3. Can UWPSpy identify the relevant flyout controls?
4. Can Windhawk (or another injection layer) insert a custom horizontal
   control?
5. Can individual circles be clickable?
6. Can each click map directly to `SetTag(path, color)`?
7. Mouse interaction; keyboard accessibility; touch behavior.
8. High-DPI behavior; dark/light mode.
9. Multiple file selection.
10. Menu reopening and state refresh.
11. Windows update fragility.

---

## 5. Architecture constraints (do not break)

```text
InlinePaletteExperiment
        ↓
Explorer integration (Phase 4)
        ↓
TagService            ← already exposes SetTag / GetTag / RemoveTag
        ↓
ITagStore             ← AdsTagStore / SqliteTagStore / CompositeTagStore
```

- No storage migration for the palette.
- No changes to `TagService`, `ITagStore`, or existing stores.
- No changes to existing tests or storage contracts.
- The only new surface is the minimal Explorer-integration interface Phase 4
  adds (path resolution from shell items → `TagService` calls), which the
  palette reuses as an adapter.

---

## 6. Findings

(empty — filled during Phase 4 track B)

| Question | Result | Evidence | Date |
|---|---|---|---|
| — | — | — | — |