# Owned desktop surface and Shell-backed paths

Explorer remains the source of desktop paths and Shell metadata, but the owned
view mode temporarily hides Explorer's `SysListView32` and renders the desktop
items in nestlone-D's own surface. Paths are never moved into an application
folder. The owned surface provides the box layout, collapse behavior, icon hit
testing, Shell opening and Shell context-menu delegation.

## Windows and input

The owned desktop surface is a layered child sharing a parent with
`SHELLDLL_DefView`, placed above the native view while the native list view is
hidden. It draws Shell-provided icons and labels from their original paths.
Only title/resize decorations and the owned icon surface receive input.

Title rename uses a focused native edit positioned over the title. This edits
box metadata only; Explorer handles file rename independently.

The embedded compatibility manifest declares Windows 8+ layered-child support
and per-monitor V2 DPI awareness. Stored positions use physical pixels relative
to the virtual-screen origin, converted to ListView client coordinates before
positioning. Mixed-DPI hardware still requires manual testing.

## Position rules

A COM worker reads the desktop `IFolderView2` and maps Shell parsing names to
current item indices and physical positions. It does not extract icon bitmaps.
After a native drag finishes, position differences update box membership and
reflow affected boxes, including native drags within the same box.
Dragging outside the background releases membership. Items already under a new
background can be explicitly adopted from the title-bar menu.

Dragging a title queues translated positions for its members. The arrange
button queues grid positions using native icon spacing. The grid starts at the
box interior, reserves header/footer space and complete icon/label cells, and
reflows after resizing. Narrow boxes retain a minimum height for their rows;
adoption can grow a box when the virtual-screen extent permits. A coalescing worker
rechecks each item's identity before sending `LVM_SETITEMPOSITION`, with scalar
packed coordinates rather than remote pointers. Read-back detects grid snapping
or rejection. Windows can still reorder items between verification and the
message; this is not a transaction with Explorer.

The worker refuses automatic-arrange / incompatible view modes, uses bounded
cross-process messages, and reports positioning errors in the title-bar menu.
It does not change global arrangement preferences or request elevation.
Packed positions outside 0..32767 are unsupported. An arrange request refuses
occupied slots or an oversized layout before submitting any moves; items then
stay in place, with the reason available in the title-bar menu.

Starting or reconnecting does not automatically move icons. Membership is saved;
positions themselves are retained by Explorer. Renaming/deleting files can leave
obsolete metadata paths, which are skipped by positioning. A renamed item that
appears inside a box is adopted on the next snapshot.

## Recovery and boundaries

The manager rediscovers Explorer and recreates its owned surface after the
native windows disappear. Exit/termination restores the native view; real file
paths and the saved layout survive.
The only call that clears a native window region is migration cleanup of a
pre-existing nestlone-D mask whose recorded owner process has exited.

The owned view has a real per-box collapse: collapsed items are removed from
the owned surface while their paths remain unchanged. The owned surface now
supports Ctrl toggle selection, Shift range selection, group icon dragging,
Ctrl multi-box selection and group box dragging. The current migration stage
does not yet reproduce every Explorer interaction (marquee selection, keyboard
navigation and accessibility) one-for-one; right-click menus are delegated to
Shell and opening uses the original path.
Explorer's native list view is restored on exit or failed attachment.

## Verification

`tests/render/run.bat` checks background alpha, absence of any painted icon
pixels, collapsed backgrounds, grid wrapping and full-cell padding.
`tests/hosting/session.bat` checks real
Explorer visibility/regions, owned-surface Z order, Shell icon creation,
preserved desktop positions/selection, and adoption/release rules against
isolated layout data. `--position-test` explicitly enables moving and restoring
the dedicated disposable test folder; other icon positions are compared.
`--diagnose [screen-x screen-y]` reports the actual hit window and owning process
without changing the desktop; `--capabilities` queries visibility interfaces.

These tests passed on the development machine. They do not substitute for a
manual visual pass across Windows releases, wallpaper applications, Explorer
restart and mixed-DPI monitors. Screenshot automation was unavailable during
this migration because its runtime could not initialize.

References: [LVM_SETITEMPOSITION](https://learn.microsoft.com/en-us/windows/win32/controls/lvm-setitemposition),
[layered windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features).
