# DeskBox reference notes

The upstream reference repository is cloned locally at:

`references/DeskBox/`

Source: [Tianyu199509/DeskBox](https://github.com/Tianyu199509/DeskBox)

The checkout is intentionally ignored by this repository. It is a read-only
implementation reference and is not part of the nestlone-D build.

## What it does

DeskBox uses C# / WinUI 3 for the widget surface and a Rust native layer for
selected Shell operations. A file widget is a real folder-backed file view,
not a second view of Explorer's desktop `SysListView32`.

Relevant areas:

- `src/DeskBox/Helpers/NativeDropTarget.cs` and
  `NativeDropTargetComInterop.cs`: COM `IDropTarget` registration, live
  `DragEnter` / `DragOver` / `Drop`, clipboard formats and native drop feedback.
- `src/DeskBox/Views/ContentWidgetWindow.NativeDragDrop.cs`: connects the drop
  bridge to file-widget import, shortcut launch and drag feedback.
- `src/DeskBox/Controls/WidgetContents/FileSurfaceContent.*`: WinUI list/grid
  rendering, selection, internal reorder, folder targets and inline UI rules.
- `src/DeskBox/Controls/WidgetShell.xaml` and `.xaml.cs`: title bar, resize,
  compact/capsule presentation and visibility transitions.
- `src/DeskBox/Services/WidgetLayerService.cs`: desktop-layer placement,
  foreground/raised state and z-order recovery.
- `src/DeskBox/Services/DesktopOrganization*.cs`: preview, planning,
  transaction, recovery journal and safe file-transfer workflow.

## What can be borrowed

The native drop lifecycle, drop-effect policy, visual feedback, inline rename
interaction, capsule transition state machine, z-order recovery, and recovery
journal are useful design references for future nestlone-D work.

## What must not be copied into the current native-desktop architecture

DeskBox owns and renders its own file items. Its interaction model therefore
does not preserve Explorer's exact desktop selection, marquee selection,
context menu, keyboard navigation, accessibility, or Shell namespace behavior.
The current nestlone-D architecture deliberately keeps those behaviors in
Explorer and uses only a background decoration plus position rules. Copying
DeskBox's widget item layer would violate that boundary.

## License note

The current upstream repository declares GPL-3.0-only. This checkout is used
for architectural study; no source code has been copied into nestlone-D. Any
future code reuse must be reviewed for license compatibility first.
