> Kept for the evidence it records. Its conclusion has not been re-tested;
> the row-tint branch was abandoned for other reasons. Note that this file
> was named INLINE_PALETTE_RESEARCH.md in the repository root, next to an
> unrelated document of the same name in docs/.

# Phase 7 — Row Tint (per-row color tint in the NEW WinUI3 Explorer) — Research / Verdict

**Session:** Phase-7 diagnostic cycle (POST-CENSUS)
**Date:** 2026-09-23
**Status:** `BLOCKED` — not production-safe
**Related roadmap:** `COLORTAGS_WINDOWS_EXPLORER_CODEX_ROADMAP_v2.md` / Phase 7
**Scope note:** this file documents **only the row-tint question** for the new Explorer. It does not
describe Core / Storage / TagService (untouched this phase). Replaces/precedes any earlier
`inline-palette` sketch docs that claimed a working row mechanism.

---

## 1. TL;DR

Per-row tint in the **new WinUI3 Explorer** is **BLOCKED** and frozen as **not production-safe**.

The one thing Phase 7 had to prove was the **row ↔ file identity mapping** (which visual row is
which file, to tint it by the file's ColorTags tag). Across **6 full census captures and 33 607
realized-row visual mutations**, that mapping is **never reachable** through any observable,
stable layer:

- `DataContext` — **0 / 33 607** non-null on realized rows;
- `Tag` — **0 / 33 607** non-null;
- realized row text — only `Loading...` placeholder ×4; **a real file name was never realized** as
  a TextBlock in the visual tree, in any capture (foreground, maximize, PGDN/END/PGUP scroll,
  click attempts on rows, foreground refocus);
- no PIDL / Shell item / stable file object surfaced on any realized container.

Per the user's Phase-7 gating ("если identity по-прежнему недоступна либо требует хрупкого
version-specific reverse engineering внутренних структур Explorer без стабильной точки перехвата
— останавливай Phase 7 и фиксируй как High Risk / not production-safe") — this is exactly the
condition that was reachedched. Work **stops** here by decision (option **C**).

---

## 2. What was actually built and verified (instrumentation, not theory)

### 2.1 Tooling (all working, proven end-to-end)

| Component | Status |
|-----------|--------|
| Windhawk mod `colortags-explorer-log` injected into live new Explorer | ✅ **working** (loaded: True, PID 7580 → 9332 in later windows) |
| Visual-tree census (ItemsRepeater / ItemsStackPanel / realized rows) | ✅ **working** — 33 607 Add-mutations across 6 captures |
| DBWIN/`OutputDebugString` listener + `colortags-explorer-log.wh.cpp` capture pipeline | ✅ **working** (log1: 1 051 159 B, log2: 1 277 258 B, log3: 3 407 147 B) |
| Foreground refocus + maximize + PGDN/END/PGUP scroll + row-click attempt | ✅ executed; census confirms **rows keep only placeholders** |

Mod source: `%TEMP%\opencode\colortags-explorer-log.wh.cpp` (likely translocated to the workspace
as `colortags-explorer-log.wh.cpp`); DLL deployed to
`C:\ProgramData\Windhawk\Engine\Mods\64\colortags-explorer-log.dll`.

Capture logs: `%TEMP%\opencode\wh-log1.txt … wh-log7.txt` (log7 ~144 B stub) + DBWIN listener
runs.

### 2.2 Row-realization findings (census facts)

- The new Explorer uses **`ItemsRepeater` + `ItemsStackPanel`** (WinUI3), rows are realized as
  virtualized containers.
- **Visual rows exist** (they appear/disappear on scroll — recycling works at the layout level).
- But **no row ever carries file identity** in any property the mod can read in-process:
  `DataContext` null, `Tag` null, `DataContext` of any descendant null, realized text = only the
  placeholder (`Loading…`), **file name never present** in the visual tree.

### 2.3 Why "Loading..." proves the block

The realized rows carry the **loading/placeholder template** (`TextBlock[0..3]: Loading...` ×4).
When the Explorer loads a real row for an actual file, the file name is not materialized into
any accessible FrameworkElement — so even the presence of a row does not tell us *which file* it
is. There is no `Loading`→`fileName` transition in any capture, including foreground refocus,
maximize, scroll to end, and click-on-row attempts.

---

## 3. Attempted mappings → result summary

| Identity layer | Attempt | Result |
|----------------|---------|--------|
| `FrameworkElement.DataContext` | read on every realized row + descendant | **all null** (0/33 607) |
| `FrameworkElement.Tag` | read on every realized row + descendant | **all null** (0/33 607) |
| Row text (`TextBlock[*]`) | full poem census of TextBlock texts | only placeholder `Loading...`; no real file name ever |
| `ItemsRepeater` element factory / `ElementPrepared` / `ElementFactory` | gated behind rebuild (option A) | **not attempted** — would require version-specific reverse engineering of internal Explorer structures without a stable hook point, which Phase-7 gating marks as blocked |
| PIDL / Shell item via visual container | — | not reachable (no identity carried by container) |

---

## 4. Production-safety verdict

```
Status:                 BLOCKED
Production safety:      NOT SAFE / HIGH RISK
Mechanism:              not production-safe — requires fragile version-specific reverse
                        engineering of internal Explorer structures with no stable hook point
Final decision (user):  option C — stop here; do not continue row-tint reverse engineering
```

### 4.1 What is intentionally NOT done (per decision)

- No further `ItemsRepeater.ElementFactory` / `ElementPrepared` rebuilds (option A).
- No further click/scroll heroics to force identity (option B).
- No changes to Core / Storage / TagService.
- No new DLL injections for row tint.
- Row tint reclassified as **deferred / experimental-only** (production block).

---

## 5. Next priorities (project roadmap after this block)

1. **Color indicator in the Tag column** — in the new Explorer: a colored dot/chip showing the
   file's tag color *inside the Tag column cell(s)*, where file identity is carried by the column
   data (not by row tints). *(Recommended next.)*
2. **Inline compact context-menu palette** — quick tag-color assignment from the right-click menu.
3. **Stable native context-menu fallback** — robust right-click path for the new Explorer.
4. **Row tint — deferred experimental**, only if a future stable Explorer row↔file mapping
   mechanism becomes available.

---

## 6. Artifacts

- `COLORTAGS_WINDOWS_EXPLORER_CODEX_ROADMAP_v2.md` — master phase roadmap (Phase 7 status
  **BLOCKED**).
- `colortags-explorer-log.wh.cpp` → `colortags-explorer-log.dll` (deployed to Windhawk Mods).
- Capture logs `wh-log1..7.txt` (census evidence) in `%TEMP%\opencode\`.
- This file (`INLINE_PALETTE_RESEARCH.md`) — the consolidated Phase-7 research/verdict record.
