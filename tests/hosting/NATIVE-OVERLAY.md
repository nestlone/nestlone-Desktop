# Native-desktop-preserving correction

Supersedes the whole-desktop replacement in f755572 after the user reported a
black screen, no tray icon and a nonresponsive application.

## Evidence and diagnosis

The previous executable did synchronous Shell enumeration and icon extraction
on the UI/paint thread, installed the tray only after creating its entire
replacement canvas, and hid the native icon window. These are confirmed code
defects matching the failure symptoms; no crash dump proves a specific Shell
handler caused the reported hang.

## Current behavior

- Install the tray before desktop discovery; abort safely if it cannot be installed.
- Start with the native desktop fully visible and its region unchanged.
- Only render boxes, never repaint unboxed desktop icons or wallpaper.
- Keep all pixels outside boxes fully transparent. Render a successful layered
  frame before showing the overlay.
- Read Shell metadata and extract icons on a detached worker. The paint thread
  only consumes cached pixels. A slow Shell handler cannot block tray/painting.
- On an explicit desktop-to-box drop, exclude the managed native tile rectangle
  from the ListView's window region. Other native icons remain visible, in their
  original positions, and retain Windows rendering and interaction.
- Only paths explicitly dropped during this run participate in native masking.
  Previously saved box items are NOT automatically masked on launch.
- Dragging out removes the mask and requests the native Shell to place the
  item at the drop point. Windows grid snapping may adjust that point.
- Restore the original unrestricted region on pause, exit or process death.
  Refuse masking if another application already assigned a custom region.
- Do not move files, set hidden attributes, modify the wallpaper, or change
  desktop global visibility/auto-arrange preferences.

## Verification

tests/hosting/session.bat tests real native window regions and the overlay
WM_DROPFILES/internal mouse handlers with an existing disposable test folder.
It uses build/hosting-test-layout.json, not the user's layout.

Passed: untouched native region at launch; accepted desktop drop; excluded
target region; native window still visible; other regions and all other icon
positions unchanged; reverse drag removes the mask; original file attributes;
normal exit restores the original native region.

Forced termination via --crash-test followed by --verify-native passed recovery.
Render regression checks confirm outside pixels are alpha zero, title opacity
is independent of background, and Shell/legacy icon alpha is preserved.

## Limitations

The Windows UI inspection tool did not expose the desktop as a capture target.
These checks validate live window regions, coordinates and render pixels, not
an end-to-end visual comparison of the entire desktop.

Tile bounds use the Shell icon origin, spacing and icon size. This is experimental
for uncommon desktop view modes, custom icon spacing and mixed-DPI monitors.
The native Shell still knows about masked items for keyboard selection/search.
Shell changes are refreshed asynchronously; there can be a brief interval
before masks follow a native refresh or re-sort.

The older desktop replacement session report is historical and is not the
acceptance specification for the corrected build.
