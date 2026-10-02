# Native ColorTags overlay

This is the replacement for the experimental PowerShell/WinForms overlay.
It runs out of process and never injects code into Explorer.

## Safety model

- A dedicated UI thread owns every overlay HWND and performs every layered-window update.
- A separate STA worker reads Explorer through Shell COM and UI Automation.
- `SetWinEventHook` callbacks only post messages to the UI thread.
- Repeated location events are coalesced.
- Mouse-wheel and touchpad events hide the colored layer immediately while
  Explorer moves and virtualizes its rows. A 120 ms settle timer after the
  final wheel event requests a fresh UI Automation snapshot and restores the
  layer directly at Explorer's measured coordinates.
- A one-page buffer above and below the viewport is built from actual
  `IFolderView::GetItemPosition` coordinates. Shell positions preserve sorting
  and group gaps when the exact snapshot is rebuilt.
- The list's UI Automation scroll percentage and visible-page size distinguish
  a real wheel scroll from an event past the first or last row. A boundary
  event therefore does not unnecessarily hide the layer.
- Row-location, scrollbar-value and scrolling events trigger cached visual
  scans. The expensive Shell item map is refreshed separately and reused.
- Every scan carries the current content generation. A scan started before a
  scroll or keyboard navigation event is discarded instead of briefly drawing
  stale dots over the new rows. Only events that actually move rows bump that
  generation - scrolling, a scrollbar value change, keyboard navigation and the
  wheel. Explorer also emits accessibility events for hover, focus and an item
  redrawn after `SHChangeNotify`, and treating those as staleness discarded one
  finished scan after another.
- An overlay is removed when its window is gone, or when a successful scan found
  no tagged row in it. A window missing from a pass for any other reason keeps
  what it has, so a single failed scan does not blink the indicators out - but
  only for `kStaleOverlayMilliseconds`, after which the layer is hidden rather
  than left standing over rows it no longer describes.
- Explorer tabs all register their own `IShellWindows` entry under one shared
  top-level HWND, so the window is matched to the tab whose shell view window is
  visible. Matching on the HWND alone returns whichever tab was opened first.
- Periodic full scans have deadline priority over queued scroll events. This
  guarantees that the visible-row cache is rebuilt after scrolling away from
  tagged rows and back, even during a continuous event stream.
- Overlay windows stay visible when a context menu opens. Because they are
  owned by Explorer and are not topmost, the menu remains above them while
  dots outside the menu keep their normal appearance.
- Wheel handling does not start repeated UI Automation scans while Explorer is
  moving. The settle timer performs one exact refresh, avoiding the visible
  end snap and late appearance of newly realized rows caused by out-of-process
  motion prediction.
- The colored disk is slightly larger than Explorer's monochrome glyph and
  covers it without painting an opaque background patch. Row hover therefore
  needs no mouse-move hook or redraw loop.
- Overlay windows are owned popups, not cross-process child windows and not topmost windows.
- `SW_SHOWNOACTIVATE` runs only on the transition to visible. It raises a window
  to the top of its z-order band, so calling it on every scan pass lifted the
  layer back above whatever application the user had switched to.
- The layer follows the foreground window: shown while Explorer or one of its
  context menus is in front, hidden as soon as another application takes over.
  An `EVENT_SYSTEM_FOREGROUND` hook applies this on the switch itself rather
  than at the next scan pass.
- The stop script first signals a named event and uses force only as a verified-PID fallback.

## Build and run

```powershell
& .\src\VisualNative\build.ps1
& .\src\VisualNative\Start-ColorTagsOverlay.ps1
& .\src\VisualNative\Stop-ColorTagsOverlay.ps1
```

For a bounded test in one folder:

```powershell
& .\src\VisualNative\out\ColorTagsOverlay.exe `
    --target-folder 'C:\path\to\folder' `
    --duration-seconds 30
```

The native helper supports the `Tags` and `Color Tag` column names. It reads
the same `:ColorTag` ADS values and uses the same seven colors as the context
menu.

Focused smoke tests live in `tests/run_native_overlay_*_smoke.ps1` and cover
wheel settle behavior, UI Automation scrolling, PageDown/Home, context menus
and window movement.

## Display modes

Two modes, switched in `HKCU\Software\ColorTags\DisplayMode` (0 = dot,
1 = label) or with `--mode dot|label` for a bounded test:

- **Dot** — the colored disk over Explorer's own glyph, as before.
- **Label** — a colored badge carrying the tag's label, sized to the text and
  clipped to the `Tags` column. Below `kMinimumLabelWidth` the column cannot
  hold a readable badge and the dot is used instead.

Labels are read from `HKCU\Software\ColorTags\Labels` and are presentation
only: the `:ColorTag` stream keeps the stable ids. The tray icon's settings
window edits every label, the display mode and the scroll behaviour in one
place; `Set-ColorTagsSettings.ps1` does the same from a script. The overlay
reloads them on its next scan pass, so a rename needs no restart.

The tray icon is owned by a hidden top-level window rather than by the
message-only manager window: a message-only window cannot take the foreground,
and without that a tray popup menu never closes. The settings window is laid
out in code instead of a dialog resource so the helper stays a single `.cpp`
with no resource script, and the main message loop passes its messages through
`IsDialogMessageW` so Tab, Enter and Esc behave as expected.

Badge text is drawn with GDI into the same DIB section that
`UpdateLayeredWindow` presents. GDI writes nothing into the alpha channel, so
every painted badge rectangle is forced opaque in one pass after `GdiFlush`;
without that the badge is invisible in a layered window.

## Why the label is drawn twice

The property handler returns the same label as the value of `System.Keywords`,
so Explorer already draws it in the `Tags` column with its own text. The badge
paints over that text with the tag's color.

This is deliberate. An out-of-process overlay can never be frame-synchronized
with Explorer's own scrolling: it learns about movement through UI Automation
events, after Explorer has already drawn the frame. So the layer hides while
the content moves. With the native text underneath, a fast scroll now drops
only the color — the tag's name stays where Explorer put it and does not jump.
Before, everything disappeared.

The badge font therefore comes from the same system UI font Explorer uses, so
the transition between the two is as close to a color change as an external
layer can get.
