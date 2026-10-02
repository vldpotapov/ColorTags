# Explorer row identity via supported accessibility APIs

Date: 2026-09-23

## Result

The earlier XAML-tree experiments did not expose file identity through
`DataContext` or `Tag`. That does not mean the visible Explorer rows have no
supported identity surface.

A read-only spike matched the current Windows 11 Explorer window through two
documented out-of-process APIs:

1. `Shell.Application` supplied the folder's filesystem items and paths.
2. UI Automation supplied the realized `DataItem` / `ListItem` rows, their
   accessible names, visibility, and screen rectangles.
3. Matching the UI Automation row name to the Shell item name associated every
   realized row with a filesystem path.

Observed result in the test window:

| Metric | Count |
|---|---:|
| Shell filesystem items | 337 |
| Realized UI Automation rows | 28 |
| Named rows | 28 |
| Exact row-name matches | 28 |
| Visible rows | 27 |

This disproves the previous claim that row-to-file identity is unavailable at
every stable layer. The mapping is available without injecting a DLL into
Explorer.

## What this proves

- A companion process can identify the currently realized rows.
- It can obtain a stable filesystem path for each matched row.
- It can obtain a screen rectangle for a row and track whether it is offscreen.
- The same approach can be tested across scrolling, sorting, navigation, DPI,
  view modes, tabs, and duplicate display-name edge cases.

## What this does not prove yet

UI Automation is an inspection and accessibility surface; it does not provide
an API for changing Explorer's row brush. Coloring still needs a rendering
strategy. The next bounded spike should use a click-through companion overlay,
kept outside `explorer.exe`, and verify that it follows row rectangles without
breaking pointer input, menus, scrolling, multiple windows, or DPI changes.

The helper script is `src/Visual/inspect_explorer_rows.ps1`. By default it
prints counts only. `-IncludePaths` adds the matched paths and rectangles for
development diagnostics.
