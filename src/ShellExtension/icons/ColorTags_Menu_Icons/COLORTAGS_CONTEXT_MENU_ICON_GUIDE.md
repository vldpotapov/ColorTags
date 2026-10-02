# ColorTags — Windows 11 Context Menu Icon Guide

## Purpose

This note describes how to use the supplied `Tags` icons in the **Windows 11 compact context menu** for the ColorTags project.

The icon package contains separate assets for light and dark UI themes.

---

## Included assets

### Multi-resolution ICO files

```text
tags_dark.ico
tags_light.ico
```

Each `.ico` contains these sizes:

```text
16×16
20×20
24×24
32×32
40×40
48×48
64×64
```

These are the preferred files for Windows Shell / Explorer integration.

### PNG exports

For each theme:

```text
tags_dark_16x16.png
tags_dark_20x20.png
tags_dark_24x24.png
tags_dark_32x32.png
tags_dark_40x40.png
tags_dark_48x48.png
tags_dark_64x64.png

tags_light_16x16.png
tags_light_20x20.png
tags_light_24x24.png
tags_light_32x32.png
tags_light_40x40.png
tags_light_48x48.png
tags_light_64x64.png
```

### Original vector sources

```text
tags_dark.svg
tags_light.svg
```

Keep the SVG files as the design source of truth.

---

## Intended usage

The icon is intended for the `Tags` command in the Windows 11 File Explorer compact context menu:

```text
Open
Share
Copy as path
Properties

[icon] Tags

Show more options
```

The icon should visually match the scale and weight of native Windows 11 context-menu icons.

---

## Theme mapping

Use:

```text
Windows dark UI  → tags_dark.ico
Windows light UI → tags_light.ico
```

Important:

The names describe the UI theme for which the icon was designed.

Do not recolor the assets at runtime unless necessary.

If Explorer or the chosen Shell API automatically applies monochrome theming to command icons, test the actual rendered result before adding manual theme switching.

---

## Preferred integration

For an `IExplorerCommand` implementation, expose the icon through:

```text
IExplorerCommand::GetIcon
```

Prefer referencing the icon as an application/DLL resource rather than relying on an arbitrary external file path.

Example conceptual structure:

```text
ColorTagsShellExtension.dll
├── ICON_TAGS_LIGHT
└── ICON_TAGS_DARK
```

The implementation may choose the appropriate resource based on the current Windows theme.

Do not hardcode assumptions about the current theme at install time. Theme selection should happen when the command icon is requested, if the chosen implementation supports it reliably.

---

## DPI and sizing

The design source already includes internal padding so the circular shape is not clipped.

Do not manually remove this padding.

The package contains multiple raster sizes because Explorer may render the menu at different effective pixel sizes depending on Windows display scaling.

Typical available assets:

| Asset | Intended use |
|---|---|
| 16×16 | Primary context-menu size / 100% scale |
| 20×20 | Higher DPI / intermediate scaling |
| 24×24 | Higher DPI |
| 32×32 | 200% and fallback |
| 40×40 | High DPI |
| 48×48 | High DPI |
| 64×64 | Large fallback / very high DPI |

Prefer the multi-resolution `.ico` and allow Windows to select the closest embedded size.

Avoid resizing a single 16×16 PNG upward at runtime.

---

## Transparency

The background must remain fully transparent.

Do not:

- add a background rectangle;
- add a white plate for dark mode;
- add a black plate for light mode;
- flatten the icon onto the menu background.

The shape itself should be the only visible artwork.

---

## Do not modify

Unless a visual problem is proven in Explorer, do not:

- crop the 1 px design padding;
- change stroke weight independently at different sizes;
- add shadows;
- add blur;
- add gradients;
- add extra outlines;
- add a colored background tile;
- scale the artwork beyond its intended safe area.

The goal is to keep the icon visually compatible with Windows 11 Fluent-style menu icons.

---

## Validation checklist

Before considering the integration complete, verify the actual icon inside the real Windows 11 File Explorer menu.

Test:

- [ ] Windows light theme
- [ ] Windows dark theme
- [ ] 100% display scaling
- [ ] 125% display scaling
- [ ] 150% display scaling
- [ ] 200% display scaling
- [ ] icon is not clipped
- [ ] circle remains visually centered
- [ ] stroke is readable at 16×16
- [ ] icon is not noticeably larger than native menu icons
- [ ] icon is not noticeably smaller than native menu icons
- [ ] transparency is correct
- [ ] no visible bitmap halo
- [ ] icon switches correctly after Windows theme change
- [ ] Explorer restart does not break icon loading

---

## Fallback rule

If dynamic light/dark switching through `IExplorerCommand::GetIcon` proves unreliable, keep the integration simple and document the limitation before introducing Explorer UI hooks.

Do **not** use WinUI visual-tree injection solely to switch the menu icon theme.

The icon feature should remain production-safe and independent from the experimental Explorer rendering work.

---

## File organization recommendation

Suggested repository structure:

```text
assets/
└── shell/
    └── tags/
        ├── tags_dark.ico
        ├── tags_light.ico
        ├── tags_dark.svg
        ├── tags_light.svg
        └── png/
            ├── tags_dark_16x16.png
            ├── tags_dark_20x20.png
            ├── ...
            ├── tags_light_16x16.png
            ├── tags_light_20x20.png
            └── ...
```

The application should use the `.ico` resources for Shell integration.

PNG files are primarily useful for:

- visual inspection;
- debugging;
- UI previews;
- future non-Shell UI.

SVG files are the master design assets.

---

## Implementation priority

1. Integrate the multi-resolution ICO into the shell extension.
2. Verify rendering at 100% scaling.
3. Verify dark/light theme behavior.
4. Verify higher DPI.
5. Only introduce explicit runtime theme switching if the default behavior is insufficient.
6. Do not change the icon artwork until a problem is reproduced in the real Explorer UI.

