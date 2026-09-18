# Native desktop decoration and position rules

Explorer's `SysListView32` owns every icon, label, selection, context menu,
keyboard gesture, rename editor, accessibility object and drag/drop target.
nestlone-D neither paints nor masks desktop items and registers no drop target
or mouse hook for the desktop body.

## Windows and input

The background is a `WS_CHILD | WS_EX_LAYERED | WS_EX_TRANSPARENT |
WS_EX_NOACTIVATE` window sharing a parent with `SHELLDLL_DefView`. Its sibling
Z order is below that complete view. Only two small windows per box sit above
the native view: the title bar and the bottom-right resize grip. File input
always remains with Explorer, including input on empty space inside a box.

Title rename uses a focused native edit positioned over the title. This edits
box metadata only; Explorer handles file rename independently.

The embedded compatibility manifest declares Windows 8+ layered-child support
and per-monitor V2 DPI awareness. Stored positions use physical pixels relative
to the virtual-screen origin, converted to ListView client coordinates before
positioning. Mixed-DPI hardware still requires manual testing.

## Position rules

A COM worker reads the desktop `IFolderView2` and maps Shell parsing names to
current item indices and physical positions. It does not extract icon bitmaps.
After a native drag finishes, position differences update box membership.
Dragging outside the background releases membership. Items already under a new
background can be explicitly adopted from the title-bar menu.

Dragging a title queues translated positions for its members. The arrange
button queues grid positions using native icon spacing. A coalescing worker
rechecks each item's identity before sending `LVM_SETITEMPOSITION`, with scalar
packed coordinates rather than remote pointers. Read-back detects grid snapping
or rejection. Windows can still reorder items between verification and the
message; this is not a transaction with Explorer.

The worker refuses automatic-arrange / incompatible view modes, uses bounded
cross-process messages, and reports positioning errors in the title-bar menu.
It does not change global arrangement preferences or request elevation.
Packed positions outside 0..32767 are unsupported. An arrange request refuses
occupied slots; if a box is full, remaining items stay in place.

Starting or reconnecting does not automatically move icons. Membership is saved;
positions themselves are retained by Explorer. Renaming/deleting files can leave
obsolete metadata paths, which are skipped by positioning. A renamed item that
appears inside a box is adopted on the next snapshot.

## Recovery and boundaries

The manager rediscovers Explorer and recreates its own decorations after the
native windows disappear. It never hides the native view. Exit/termination only
removes decoration windows; real icon positions and Explorer state survive.
The only call that clears a native window region is migration cleanup of a
pre-existing nestlone-D mask whose recorded owner process has exited.

There is no per-box native list mode, icon clipping, private marquee selection,
scrolling viewport or icon-hiding collapse. Collapse removes the background
body only; native icons remain visible. Global desktop view settings apply to
all boxes. No replacement Shell context menus or file-operation handlers exist.

## Verification

`tests/render/run.bat` checks background alpha, absence of any painted icon
pixels, and collapsed backgrounds. `tests/hosting/session.bat` checks real
Explorer visibility/regions, decoration Z order, input-transparent style,
preserved desktop positions/selection, and adoption/release rules against
isolated layout data. `--position-test` explicitly enables moving and restoring
the dedicated disposable test folder; other icon positions are compared.

These tests passed on the development machine. They do not substitute for a
manual visual pass across Windows releases, wallpaper applications, Explorer
restart and mixed-DPI monitors. Screenshot automation was unavailable during
this migration because its runtime could not initialize.

References: [LVM_SETITEMPOSITION](https://learn.microsoft.com/en-us/windows/win32/controls/lvm-setitemposition),
[layered windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features).
