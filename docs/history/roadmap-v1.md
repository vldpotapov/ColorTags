# ColorTags for Windows Explorer — Codex Roadmap

## 1. Project goal

Build a small Windows utility that adds Finder-like color tags to the **default Windows 11 File Explorer** without replacing Explorer.

Primary UX goal:

```text
Right click
└── Tags
    ├── 🔴 Red
    ├── 🟠 Orange
    ├── 🟡 Yellow
    ├── 🟢 Green
    ├── 🔵 Blue
    ├── 🟣 Purple
    ├── ⚪ Gray
    └── Remove tag
```

Desired Explorer view:

```text
Name                 Tag       Type            Date modified
Design               🔴        File folder     21.09.2026
References           🟢        File folder     20.09.2026
brief.pdf            🟡        PDF             18.09.2026
archive.zip          ⚪        ZIP             01.09.2026
```

Ideal future enhancement:
- tint the whole Explorer row based on the tag color;
- keep the default Explorer UI;
- preserve compatibility with normal file operations;
- avoid turning the project into a full custom file manager.

---

# 2. Product principles

1. Use the default Windows Explorer.
2. Keep the core tagging system independent from Explorer styling hacks.
3. Prefer official Windows Shell APIs where practical.
4. Treat per-row coloring as an optional enhancement, not as a core dependency.
5. Keep the architecture modular so the tag storage backend can be changed later.
6. Prioritize reliability over visual tricks.
7. Support both files and folders.
8. Support multi-selection where possible.
9. The tool must uninstall cleanly and remove Shell registrations.
10. Windows updates must not break the core tag data layer.

---

# 3. Desired feature set

## Core

- Assign one color tag to a file.
- Assign one color tag to a folder.
- Remove a tag.
- Read the current tag.
- Apply a tag to multiple selected items.
- Seven default tag colors:
  - Red
  - Orange
  - Yellow
  - Green
  - Blue
  - Purple
  - Gray

## Explorer integration

- Context menu entry:
  - `Tags`
- Optional Explorer column:
  - `Tag`
  - or `Color Tag`
- Optional text or visual value:
  - `Red`
  - `● Red`
  - `🔴`
- Optional icon overlay.
- Optional full-row tint.

## Future customization

Allow users to rename semantic meaning while retaining the underlying color:

```text
Red      → Urgent
Orange   → Review
Yellow   → Waiting
Green    → Done
Blue     → Reference
Purple   → Personal
Gray     → Archive
```

The internal tag should remain stable:

```text
red
orange
yellow
green
blue
purple
gray
```

---

# 4. Proposed architecture

```text
ColorTags/
│
├── src/
│   ├── Core/
│   │   ├── TagColor
│   │   ├── ITagStore
│   │   ├── TagService
│   │   └── TagValidation
│   │
│   ├── Storage/
│   │   ├── AdsTagStore
│   │   ├── SqliteTagStore
│   │   └── PropertySystemTagStore
│   │
│   ├── ShellExtension/
│   │   ├── ContextMenu
│   │   ├── PropertyHandler
│   │   ├── PropertySchema
│   │   └── Registration
│   │
│   ├── Visual/
│   │   ├── IconOverlay
│   │   └── ExplorerStyling
│   │
│   ├── Cli/
│   └── Settings/
│
├── tests/
│
├── docs/
│   ├── FEASIBILITY.md
│   ├── ARCHITECTURE.md
│   ├── EXPLORER_RESEARCH.md
│   └── STORAGE_RESEARCH.md
│
└── README.md
```

Important:

```text
ColorTags Core
     │
     ├── official Shell integration
     │
     └── optional Explorer visual enhancement
```

The core project must not depend on Windhawk or another injection framework.

---

# 5. Technical questions that must be answered first

Before implementing the full product, Codex must investigate the current Windows 11 Explorer environment.

Questions:

1. Can a custom property be registered and exposed in Explorer Details view?
2. Can that property work for:
   - normal files;
   - folders;
   - multiple file types?
3. Can the property be displayed in a custom column?
4. Can the column render:
   - plain text;
   - Unicode dot;
   - emoji;
   - a genuinely colored custom value?
5. Can a modern Windows 11 context-menu command be added reliably?
6. Can one command work on multiple selected files?
7. What Explorer UI technology is used for the relevant file-list rows?
8. Can a specific visible Explorer row be mapped back to:
   - Shell item;
   - PIDL;
   - filesystem path?
9. Can the row background be changed individually?
10. Can UWPSpy or Windhawk access the relevant row/control reliably?
11. How fragile would row-coloring be across Windows updates?
12. Can icon overlays provide a more stable visual fallback?

---

# 6. Phase 0 — Feasibility research

## Goal

Prove what Windows Explorer allows before committing to architecture.

Do not build the full application during this phase.

## Research areas

Investigate:

- Windows Shell Extensions
- Windows Property System
- property handlers
- property schemas
- Explorer Details view
- Windows 11 context menu extensions
- icon overlay handlers
- Explorer Win32 / WinUI architecture
- UWPSpy
- Windhawk
- File Explorer Styler

## Required experiments

### Experiment A — custom property

Create the smallest possible proof-of-concept that exposes a custom property for one test file.

Expected result:

```text
ColorTag = Red
```

### Experiment B — custom Explorer column

Determine whether that property can appear in Details view.

Expected target:

```text
Name          Color Tag        Type
test.txt      Red              Text Document
```

### Experiment C — folder support

Repeat the property experiment for a normal folder.

### Experiment D — context menu

Add a minimal command:

```text
Right click
└── Set test tag
```

The command should work for at least one file.

### Experiment E — per-row Explorer styling

Using UWPSpy / Windhawk or equivalent research tools:

- identify the file row control;
- inspect its accessible data;
- determine whether the corresponding file path or Shell item can be obtained;
- test whether the row background can be modified.

## Deliverable

Create:

```text
docs/FEASIBILITY.md
```

Use a table such as:

| Feature | Supported | Method | Risk | Notes |
|---|---|---|---|---|
| Tag files | YES/NO | ... | Low/Medium/High | ... |
| Tag folders | YES/NO | ... | ... | ... |
| Context menu | YES/NO | ... | ... | ... |
| Explorer column | YES/NO | ... | ... | ... |
| Colored dot in column | YES/NO/Partial | ... | ... | ... |
| Icon overlay | YES/NO | ... | ... | ... |
| Per-item row background | YES/NO/Experimental | ... | ... | ... |

## Stop condition

Do not start the full implementation until Phase 0 is complete and documented.

---

# 7. Phase 1 — Core tag model

## Goal

Create a platform-independent tagging core.

Suggested enum:

```cpp
enum class TagColor {
    None,
    Red,
    Orange,
    Yellow,
    Green,
    Blue,
    Purple,
    Gray
};
```

Equivalent implementation is acceptable in the final chosen language.

## Required API

```text
GetTag(path)
SetTag(path, color)
RemoveTag(path)
```

Also:

```text
IsTagSupported(path)
GetAllSupportedTags()
```

## Storage abstraction

Create:

```text
ITagStore
```

Example interface:

```text
GetTag(path)
SetTag(path, tag)
RemoveTag(path)
```

Possible implementations:

```text
ITagStore
 ├── AdsTagStore
 ├── SqliteTagStore
 └── PropertySystemTagStore
```

Do not hardcode the whole application to one storage method.

## Tests

Cover:

- file tagging;
- folder tagging;
- replacing a tag;
- removing a tag;
- invalid tag values;
- missing paths;
- Unicode paths;
- long paths;
- read-only items where relevant.

---

# 8. Phase 2 — Storage research and implementation

## Goal

Choose the primary persistence strategy based on actual Windows behavior.

Investigate at least these options.

---

## Option A — NTFS Alternate Data Streams

Example:

```text
presentation.pdf:ColorTag
```

Contents:

```text
red
```

### Advantages

- metadata travels with the filesystem object on NTFS in many local operations;
- no central database required;
- filenames stay unchanged;
- conceptually simple.

### Risks

- NTFS-specific;
- data may be lost when copying to FAT/exFAT;
- ZIP/archive handling can remove ADS;
- cloud synchronization may not preserve it;
- network shares may behave differently.

---

## Option B — SQLite database

Example conceptual record:

```text
path -> tag
```

### Advantages

- predictable storage;
- works beyond NTFS;
- easy to inspect and migrate.

### Risks

- file rename tracking;
- move tracking;
- external changes;
- stale entries;
- synchronization problems.

If SQLite is used, investigate whether a better identity than raw path is available.

---

## Option C — Windows Property System

Investigate whether a custom property can be the actual persisted tag source.

### Advantages

- native conceptual fit with Explorer;
- potentially clean Details-view integration.

### Risks

- complexity;
- file-type restrictions;
- folder support;
- write behavior;
- registration complexity.

---

## Deliverable

Create:

```text
docs/STORAGE_RESEARCH.md
```

Include:

- tested behavior;
- failure cases;
- compatibility;
- recommendation;
- fallback strategy.

---

# 9. Phase 3 — CLI prototype

## Goal

Validate the tag system without Explorer UI.

Expected usage:

```powershell
colortag set "C:\Work\project.psd" red
colortag get "C:\Work\project.psd"
colortag remove "C:\Work\project.psd"
```

Optional:

```powershell
colortag list
colortag inspect "C:\Work\project.psd"
```

Expected output:

```text
Tag: red
```

or:

```text
No tag
```

## Requirements

- sensible exit codes;
- clean errors;
- Unicode path support;
- no admin rights unless truly necessary;
- unit and integration tests.

---

# 10. Phase 4 — Explorer context menu

## Goal

Provide the first useful Explorer UX.

Desired menu:

```text
Tags >
   ● Red
   ● Orange
   ● Yellow
   ● Green
   ● Blue
   ● Purple
   ● Gray
   ─────────
   Remove tag
```

## Requirements

Support:

- files;
- folders;
- multiple selection;
- setting the same tag on all selected items;
- removing tag from all selected items.

## Important

Prefer the modern Windows 11 context-menu mechanism where practical.

Avoid unnecessary legacy hacks if an officially supported mechanism can provide the required UX.

## Validation

Test:

- one file;
- one folder;
- mixed files/folders;
- 10+ selected items;
- Explorer restart;
- system reboot.

---

# 11. Phase 5 — Explorer Tag column

## Goal

Show the tag in Explorer Details view.

Initial acceptable output:

```text
Name             Color Tag       Type
project.psd      Red             PSD
references       Green           File folder
```

## Experiments

Try in this order:

### Level 1

Plain text:

```text
Red
Green
Yellow
```

### Level 2

Unicode dot:

```text
● Red
● Green
● Yellow
```

### Level 3

Emoji:

```text
🔴 Red
🟢 Green
🟡 Yellow
```

### Level 4

Actual custom colored visual

Only attempt if the Explorer rendering architecture makes this feasible without excessive fragility.

## Important

Do not block the product on Level 4.

Plain text in a working Explorer column is already a successful milestone.

---

# 12. Phase 6 — Visual fallback: icon overlays

## Goal

Provide a stable visual tag even if custom row rendering is impossible.

Possible appearance:

```text
🔴 📁 Project
🟢 📄 brief.pdf
🟡 📄 contract.docx
```

The actual implementation should use proper Explorer icon overlay mechanisms rather than emoji in filenames.

## Research

Determine:

- available overlay handler limits;
- conflicts with OneDrive/Dropbox/Git clients;
- ordering and registration behavior;
- whether seven overlays are practical.

## Fallback strategy

If Windows overlay limits make seven colors unreliable, define a reduced mode:

```text
Red
Yellow
Green
```

or another compact set.

Important:

The internal data model should still support all seven colors even if the overlay view is reduced.

---

# 13. Phase 7 — Finder-like row coloring experiment

## Goal

Try to tint the whole Explorer row according to the tag.

Desired behavior:

```text
Tag = Red
↓
Explorer row background = subtle red tint
```

Example conceptual opacity:

```text
rgba(tagColor, 0.08–0.15)
```

Do not use aggressive full-saturation fills.

## Architecture requirement

Keep this as an optional layer:

```text
ColorTags Core
        ↓
Explorer visual adapter
        ↓
optional Windhawk / hook implementation
```

## Research target

Find a reliable mapping:

```text
Explorer row
      ↓
Shell item / PIDL / filesystem path
      ↓
GetTag(path)
      ↓
apply row background
```

## Important

A global Explorer style is not enough.

The system must distinguish individual items based on their assigned tag.

## Success criteria

At minimum:

- different files in the same folder can have different row colors;
- scrolling does not mix colors due to virtualized row reuse;
- selected/hovered states remain readable;
- dark mode works;
- light mode works.

## Failure condition

If this requires unstable deep hooks that are likely to break every Windows update, mark the feature experimental and do not make it required for release.

---

# 14. Phase 8 — Settings application

## Goal

Provide a minimal configuration UI.

Suggested screen:

```text
Color Tags

● Red        Urgent
● Orange     Review
● Yellow     Waiting
● Green      Done
● Blue       Reference
● Purple     Personal
● Gray       Archive
```

## Settings

Allow:

- enable/disable tag colors;
- rename display labels;
- reset labels;
- choose visual mode if multiple are available:
  - text column;
  - icon overlay;
  - row tint;
- enable/disable Explorer integration.

Do not allow changing the internal tag identifiers casually.

Internal stable IDs:

```text
red
orange
yellow
green
blue
purple
gray
```

---

# 15. Phase 9 — Reliability and edge cases

Test all important filesystem behavior.

## File operations

Test:

- rename;
- move within same folder;
- move to another folder;
- move to another drive;
- copy;
- duplicate;
- delete;
- restore from Recycle Bin.

## Filesystems

Test where available:

- NTFS;
- exFAT;
- FAT32;
- network share;
- SMB;
- OneDrive folder;
- Dropbox folder.

## File types

Test:

- folders;
- `.txt`;
- `.pdf`;
- `.docx`;
- `.psd`;
- `.blend`;
- images;
- archives;
- unknown extensions;
- files without extensions.

## Path cases

Test:

- spaces;
- Cyrillic;
- Czech characters;
- emoji;
- very long paths;
- deeply nested directories.

## Explorer behavior

Test:

- Explorer restart;
- Windows restart;
- multiple Explorer windows;
- tabs;
- dark mode;
- light mode;
- details view;
- list view;
- icon view where relevant.

## Performance

Test folders with:

```text
1,000 files
10,000 files
50,000 files
```

The tag system must not noticeably freeze Explorer.

Shell extensions must avoid expensive synchronous disk/database operations on Explorer's UI thread.

---

# 16. Phase 10 — Installer and uninstall

## Goal

Provide clean installation and removal.

Installer must handle:

- binaries;
- required runtime dependencies;
- Shell extension registration;
- property schema registration;
- optional visual module;
- settings app.

Uninstall must remove:

- Shell registrations;
- context menu registration;
- property schema registration where appropriate;
- visual hooks;
- application files.

User tag data should not be deleted silently unless explicitly requested.

---

# 17. Phase 11 — Release readiness

Before v1.0, verify:

- no Explorer crashes;
- clean install;
- clean uninstall;
- no admin requirement for normal usage if avoidable;
- no unexpected background service unless technically justified;
- low memory use;
- no network access unless explicitly introduced later;
- code signing strategy documented;
- Windows Defender false-positive risk investigated.

Create:

```text
docs/ARCHITECTURE.md
docs/TROUBLESHOOTING.md
docs/COMPATIBILITY.md
```

Update:

```text
README.md
```

with installation, usage and screenshots.

---

# 18. MVP definition

The MVP does not require Finder-like row tinting.

MVP is complete when:

- a user can assign one of seven tags to a file;
- a user can assign one of seven tags to a folder;
- a user can remove a tag;
- this can be done from Explorer context menu;
- tags persist reliably;
- tags can be read back;
- an Explorer-visible indication exists:
  - preferably custom column;
  - alternatively stable icon overlay;
- multi-selection works;
- uninstall is clean.

---

# 19. Ideal v1.0 definition

Ideal v1.0:

```text
Right click → Tags → Red
```

immediately produces:

```text
Name              Tag       Type
Project            🔴        File folder
```

and optionally:

```text
Project row → subtle red tint
```

No Explorer replacement is introduced.

---

# 20. Risk assessment

| Feature | Expected feasibility | Risk |
|---|---|---|
| Tag data model | Very high | Low |
| Seven colors | Very high | Low |
| Files | Very high | Low |
| Folders | High | Low/Medium |
| Remove tag | Very high | Low |
| Multi-selection | High | Low/Medium |
| Context menu | Very high | Low/Medium |
| Explorer custom property | High | Medium |
| Explorer Tag column | High | Medium |
| Colored emoji/dot in column | Medium | Medium |
| Icon overlay | High | Medium |
| Seven simultaneous overlay colors | Medium | Medium/High |
| Full row tint | Experimental | High |
| Windows Update resilience of core | High | Low/Medium |
| Windows Update resilience of row hooks | Low/Medium | High |

---

# 21. Recommended implementation priority

Implement in this exact order:

```text
1. Feasibility research
2. Tag core
3. Storage prototype
4. CLI
5. Context menu
6. Explorer property / Tag column
7. Icon overlay fallback
8. Row tint experiment
9. Settings UI
10. Hardening
11. Installer
```

Do not begin with row coloring.

---

# 22. First task for Codex

Start with **Phase 0 only**.

Codex instruction:

```text
Read this roadmap fully.

Do not implement the complete ColorTags application yet.

Begin with Phase 0 — Feasibility research.

The objective is to determine exactly what is technically possible in the
current Windows 11 File Explorer using official Shell APIs and, separately,
what would require unsupported UI injection or Windhawk-style hooking.

Perform minimal proof-of-concept experiments where necessary.

Document all findings in:

docs/FEASIBILITY.md

Pay special attention to:

1. custom properties for files;
2. custom properties for folders;
3. custom Details-view columns;
4. Windows 11 context-menu integration;
5. multi-selection;
6. icon overlays;
7. whether a visible Explorer row can be mapped to its Shell item/path;
8. whether individual rows can be tinted independently;
9. Windows update fragility.

Do not move to Phase 1 until FEASIBILITY.md is complete and the architecture
decision is explicit.
```

---

# 23. Decision gates

## Gate A — after Phase 0

Choose:

- primary storage;
- Explorer property strategy;
- context-menu mechanism;
- whether row tint is feasible enough to continue researching.

## Gate B — after Explorer column prototype

Choose main visual strategy:

```text
A. Tag column
B. icon overlay
C. Tag column + icon overlay
D. Tag column + experimental row tint
```

## Gate C — before v1.0

Decide whether full-row tint is:

```text
Stable
Experimental
Dropped
```

Core release must not depend on it.

---

# 24. Non-goals

Do not build:

- a custom file manager;
- a replacement Explorer;
- a cloud sync service;
- a complex tagging database with search/indexing in the first version;
- multiple tags per item in MVP;
- automatic AI categorization;
- a heavy background daemon unless unavoidable.

Keep the first version narrow:

> Finder-like color tags integrated into the default Windows Explorer.

