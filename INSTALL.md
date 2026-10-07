# Installation

Windows 11, x64. Download from the [official releases](https://github.com/vldpotapov/ColorTags/releases).

## Recommended: installer

Run `ColorTags-Setup-1.0.1.exe`. It installs the **Tags** context menu, Tags column,
colored indicators and tray settings in `%LOCALAPPDATA%\Programs\ColorTags`.
Python and Windows SDK are not required: setup contains an isolated Python
runtime and a signed menu package.

Approve the administrator prompts for the menu certificate and column handler.
Run setup normally as the user who will use ColorTags; do not start it with a
different administrator account. Setup is English-only and startup is optional.
The EXE has no commercial code-signing certificate; Windows may show SmartScreen.

Right-click a file or folder and open **Tags**. In Details view, enable the
**Tags** column using the column header's **More...** dialog. Left-click the tray
icon to change tag names and display settings. If Explorer still shows its cached
menu, reopen Explorer or sign out and back in.

## Upgrade from 1.0.0

Run the new installer over the existing installation. It stops the old overlay
and adds the missing menu and column registration. The 1.0.0 EXE installed only
the overlay. File tags and custom labels are preserved.

Registration errors are reported by setup and logged in
`%LOCALAPPDATA%\Colortags\setup-integration.log`. Resolve the cause (for example,
declined administrator access) and rerun setup. Managed computers may forbid
self-signed MSIX packages; their administrator must allow the package.

## Manual ZIP installation

Extract the entire ZIP to a permanent folder. Open PowerShell in that folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ShellExtension\register.ps1 -RestartExplorer
```

Save Explorer work first: `-RestartExplorer` restarts Explorer. Approve the
administrator prompts. The release includes Python and the public MSIX
certificate. Do not move the extracted folder after registration: the menu runs
Python modules from it. Private signing keys are never distributed.

Start `Overlay\Start ColorTags.cmd`; stop with `Overlay\Stop ColorTags.cmd`.
The overlay itself does not require Python. For automatic startup, add a shortcut
to `Overlay\ColorTagsOverlay.exe` in `shell:startup`.

## Removal

For the EXE, uninstall through Windows Settings. Approve the prompts to remove
column registration and certificates added by setup. For the ZIP, stop the
overlay and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\ShellExtension\unregister.ps1
```

Uninstall preserves file tags, custom labels and overlay preferences. Remove
individual tags through the menu before uninstalling if desired.

## Developer registration

Source builds require Python 3.9+, Windows SDK signing tools and administrator
approval. `register.ps1` discovers an installed SDK; use `-SdkTools` for a custom
location. `tools/prepare-installer.ps1` prepares the embedded runtime and signed
MSIX on the build machine, so development tools are not needed on user computers.

Optional icon overlays are registered separately with `register_icon_overlays.ps1`.
Windows permits only 15 overlay slots system-wide. Use
`unregister_icon_overlays.ps1` to remove this optional feature.
