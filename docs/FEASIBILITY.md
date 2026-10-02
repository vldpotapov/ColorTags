# ColorTags — Phase 0 Feasibility Report

**Date:** 2026-09-22
**Environment tested:** Windows 11 25H2 (build 26200.9457), Explorer 10.0.26100.8117 (WinUI 3), NTFS, no administrator rights, no Windhawk/UWPSpy installed.

---

## 1. Executive summary

| Feature | Supported | Method | Risk | Notes |
|---|---|---|---|---|
| Tag files | YES | NTFS ADS (`file:ColorTag`) — verified locally | Low | Works for any file type, no admin, no per-type handler needed |
| Tag folders | YES | NTFS ADS (`folder:ColorTag`) — verified locally | Low | ADS works on folders on NTFS |
| Context menu | YES | Modern: `IExplorerCommand` + sparse package / MSIX; Legacy: `HKCR\*\shell` verb | Medium | Legacy verbs land in "Show more options" on Win11; modern path needs packaged identity |
| Multi-selection | YES | `IShellItemArray` in `IExplorerCommand::Invoke` | Low/Medium | Documented contract; also works for legacy verbs (`%*` / `%V`) |
| Explorer column | YES | Built-in `System.Keywords` ("Tags") column — verified present in column set | Low | No custom schema needed for the column itself |
| Colored dot in column | Partial | Text value in the Tags column; emoji/dot render as text | Medium | Property system renders plain text only; no per-cell color |
| Icon overlay | YES (limited) | `IShellIconOverlayIdentifier` COM handler | Medium/High | 15-slot hard limit; OneDrive consumes 5; conflicts common |
| Per-item row background | Experimental | Custom Windhawk mod hooking WinUI 3 `ListViewItem` | High | Static styling mods cannot do data-driven per-row tint; fragile across updates |

**Bottom line:** The MVP is feasible without any Explorer UI hacks. Tag storage via NTFS ADS works for files and folders today. The Explorer-visible indication should be the built-in **Tags column** (text), with **icon overlays** as a reduced fallback. Per-row tinting is possible only via Windhawk-style injection and must stay experimental.

---

## 2. Environment facts (this machine)

- Windows 11 25H2, build 26200.9457, Explorer `10.0.26100.8117` — the new WinUI 3 File Explorer.
- Current user has **no administrator rights** → all experiments below are HKCU / user-scope only.
- System drive is **NTFS** (verified via `Get-Volume`).
- Windhawk, UWPSpy, ExplorerPatcher: **not installed** (research only).
- `PackagedCom` registration infrastructure present (`HKLM\SOFTWARE\Classes\PackagedCom`, 7 entries) → modern context-menu path is available on this system.

---

## 3. Experiment A — custom property

**Question:** Can a custom property be registered and exposed for a file?

**Findings:**

- Custom properties require a **property schema** (`.propdesc` XML) registered via `PSRegisterPropertySchema` (writes to `HKLM`, needs admin) plus a **property handler** (COM DLL, C++ only — managed code is not allowed in Explorer's in-process load path).
- Property handlers are **always associated with specific file types** (`HKLM\...\PropertySystem\PropertyHandlers\.ext`). There is **no handler for `.txt`** on this system (verified: the registered list contains only media/Office formats — `.jpg`, `.mp3`, `.doc`, `.xls`, `.msg`, etc.).
- **`System.Keywords` ("Tags") already exists** as a system property (multivalue string, `PKEY_Keywords`, `F29F85E0-4FF9-1068-AB91-08002B27B3D9` / 5). No custom schema is required to display it.
- Writing `System.Keywords` works only for file types that have a property handler that supports writing (media/Office). For `.txt`, `.pdf`, `.psd`, `.blend`, archives — **no write path** via the property system.

**Verdict:** Custom property = **YES, but per-file-type and admin-gated**. For the tag column, the built-in `System.Keywords` is the pragmatic choice — but it cannot be the universal storage backend because most file types have no writable handler.

---

## 4. Experiment B — custom Explorer column

**Question:** Can the property appear in Details view, and how does it render?

**Findings (verified via Shell COM `GetDetailsOf`):**

- The **"Tags" column (index 18)** is present in the Explorer column set for a plain `.txt` file. It is the built-in `System.Keywords` column — users can add it via "Choose columns" without any registration.
- Rendering levels:
  - **Level 1 (plain text)** — YES. The column shows the string value.
  - **Level 2 (Unicode dot `● Red`)** — YES as text; the dot is a normal Unicode character in the string.
  - **Level 3 (emoji `🔴`)** — YES as text; emoji render depends on the font (Segoe UI Emoji), which Explorer handles.
  - **Level 4 (genuinely colored custom value)** — **NO**. The property system renders column values as plain text; there is no per-cell color mechanism in the Details view.

**Verdict:** Column = **YES** (built-in Tags column). Colored cell = **NO** (text only). Plain text / dot / emoji in the column is a realistic milestone; colored cells are not.

---

## 5. Experiment C — folder support

**Question:** Can the property work for folders?

**Findings:**

- **Property handlers cannot be registered for folders.** Microsoft's model binds handlers to file extensions; community attempts with `Folder`, `Directory`, `AllFileSystemObjects`, `*` all fail (StackOverflow 41912219). The "Tags" column exists for folders in the column set, but there is no writable property store for folder keywords.
- **NTFS ADS works for folders** — verified locally: `folder:ColorTag` stream written and read back successfully.

**Verdict:** Property system for folders = **NO**. ADS for folders = **YES**. → Storage must not depend on the property system if folders must be taggable.

---

## 6. Experiment D — context menu

**Question:** Can a modern Windows 11 context-menu command be added reliably, with multi-selection?

**Findings:**

- **Legacy verbs** (`HKCR\*\shell\...` / `HKCU\Software\Classes\*\shell\...`): verified registration works **without admin** (HKCU). On Windows 11 these appear only under **"Show more options"** (Shift+F10), not in the primary menu.
- **Modern path** (`IExplorerCommand`): appears in the primary Win11 menu. Registration requires **app identity** — MSIX package or **sparse package** for unpackaged apps — via `windows.fileExplorerContextMenus` manifest extension + `PackagedCom`. Infrastructure confirmed present on this machine.
- **Multi-selection:** `IExplorerCommand::Invoke` receives an `IShellItemArray` — enumerate to get all selected items. Documented and reliable.
- Submenu ("Tags ▸ Red/Orange/...") is supported via `IExplorerCommand::EnumSubCommands` (app-attributed flyout).
- Note: `IExplorerCommand` methods run on the UI thread — no network/disk-heavy work allowed.

**Verdict:** Context menu = **YES**. Modern menu requires a compiled COM DLL + sparse package (medium effort, no admin needed for user-scope sparse package). Multi-selection = **YES**.

---

## 7. Experiment E — per-row Explorer styling

**Question:** Can a visible row be mapped to its path and tinted individually?

**Findings:**

- Explorer's file list is **WinUI 3** (`DetailsViewControl`); rows are `ListViewItem` containers in a virtualized list.
- **UWPSpy** can inspect the live WinUI 3 tree of `explorer.exe` (choose "WinUI 3" mode; may require disabling "Launch folder windows in a separate process"). It uses the XAML Diagnostic APIs. It is an inspection tool, not a runtime styling mechanism.
- **Windhawk "Windows 11 File Explorer Styler"** applies **static** XAML styles by selector (`Grid#DetailsViewControlRootGrid`, etc.). It cannot do **data-driven per-item** coloring: there is no per-row selector keyed to the file's tag. It also cannot map a row to its Shell item/path by itself.
- Per-row tint therefore requires a **custom Windhawk mod** (C++ hooking) that:
  1. hooks the `ListViewItem` container generation,
  2. reads the item's data context (Shell item → path),
  3. calls the tag store,
  4. sets the row `Background` brush.
- Fragility: WinUI internals change across Windows updates; the roadmap's own risk table rates full row tint **High** risk. The Styler mod itself is version-sensitive (targets `explorer.exe` 26100+).

**Verdict:** Row→path mapping = **possible but undocumented, requires injection**. Per-item background = **Experimental, High risk**. Not MVP material.

---

## 8. Icon overlays (fallback visual)

**Findings:**

- Hard limit: **15 overlay slots** in the system image list; **4 reserved** by the system → 11 for third parties.
- OneDrive registers **5** → typically **6 slots left**. Dropbox (5), TortoiseSVN/Git (9) make conflicts the norm.
- Handlers are loaded **alphabetically by registry key name**; apps use leading spaces to win the sort. Silent drop beyond slot 15.
- **Overlays are not shown under cloud-synced folders** (OneDrive) — handlers aren't even invoked there.
- One overlay per icon; if several apply, one wins arbitrarily.
- Microsoft's official position: overlays are a limited shared resource; prefer status columns.

**Verdict:** Icon overlay = **YES but unreliable for 7 colors**. A reduced set (Red/Yellow/Green) or a single "tagged" overlay is realistic; 7 simultaneous color overlays will collide with OneDrive/Dropbox/Git clients on real machines.

---

## 9. Storage experiments (input for Gate A)

| Method | Files | Folders | No admin | Verified | Risks |
|---|---|---|---|---|---|
| NTFS ADS (`:ColorTag`) | YES | YES | YES | ✅ locally | NTFS-only; lost on FAT/exFAT copy, ZIP, some sync; travels with rename/move on NTFS |
| SQLite (path→tag) | YES | YES | YES | not run (Phase 2) | rename/move tracking, stale entries, sync |
| Property System | per-type only | NO | NO (schema) | research | handler per file type; folders impossible |

ADS is the only method verified to work for **both files and folders without admin** on this machine.

---

## 10. Answers to the roadmap's technical questions

1. **Custom property in Details view?** Yes — but per-file-type handler + admin schema. Built-in `System.Keywords` column already exists.
2. **Works for files / folders / multiple types?** Files: only types with handlers. Folders: **no**. Multiple types: no universal handler.
3. **Custom column?** Yes — the Tags column is available out of the box.
4. **Column rendering:** plain text / dot / emoji = yes (text); genuinely colored value = **no**.
5. **Modern context-menu command?** Yes — `IExplorerCommand` + sparse package/MSIX.
6. **Multiple selected files?** Yes — `IShellItemArray`.
7. **Explorer row UI technology?** WinUI 3 (`ListViewItem` in `DetailsViewControl`).
8. **Row → Shell item / PIDL / path?** Possible via data-context inspection (UWPSpy), undocumented; requires injection to act on it.
9. **Row background changeable individually?** Only via Windhawk-style hooking; experimental.
10. **UWPSpy / Windhawk access?** UWPSpy: yes (WinUI 3 mode). Windhawk: yes, but static styling only; per-row needs a custom mod.
11. **Fragility across updates?** High for row coloring; low for ADS storage and context menu.
12. **Icon overlays as fallback?** Yes, but slot-limited and conflict-prone; reduced set recommended.

---

## 11. Recommendations (input for Gate A)

1. **Primary storage: NTFS ADS** (`:ColorTag` stream, value = stable tag id `red|orange|yellow|green|blue|purple|gray`). Verified for files and folders, no admin, survives rename/move on NTFS. SQLite remains the documented fallback for non-NTFS volumes (Phase 2 decides).
2. **Explorer property strategy:** do **not** build a custom property handler in v1. Use the built-in **Tags column** (`System.Keywords`) only where the file type supports writing; the primary visual indicator should not depend on it.
3. **Context menu:** `IExplorerCommand` + sparse package (modern Win11 menu, multi-select, submenu via `EnumSubCommands`). Legacy verb as a no-admin fallback.
4. **Row tint:** mark **experimental**, do not gate the release on it. If pursued: custom Windhawk mod, reduced color set, explicit update-fragility warning.
5. **Icon overlay:** optional reduced mode (e.g. Red/Yellow/Green) as a visual fallback; never rely on 7 simultaneous overlays.

---

## 12. Open items for later phases

- Phase 2: verify ADS behavior on exFAT/FAT32/network shares/OneDrive folders; decide SQLite fallback and identity strategy.
- Phase 4: prototype sparse-package `IExplorerCommand` DLL; verify primary-menu placement and multi-select on this build.
- Phase 5: confirm Tags-column text rendering (Level 1–3) visually in Details view.
- Phase 7: UWPSpy inspection of the row container; feasibility of a custom Windhawk mod for per-row tint.