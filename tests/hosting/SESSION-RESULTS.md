# Whole-desktop hosting experiment

Branch: codex/desktop-icon-hosting.
Original snapshot: 0a0f535 / snapshot/pre-desktop-hosting.

## Implemented

- Read the live desktop Shell view, including public desktop shortcuts and
  virtual items. Preserve real file paths and attributes.
- Render unboxed desktop items and boxes on one layered canvas. Box ownership
  excludes the item from the unboxed view, so it appears only once.
- Internal pointer dragging: desktop to box, box to desktop, and box to box.
  Escape/capture cancellation does not transfer ownership.
- Double-click opens an item. Desktop selection, basic Open context action,
  and pause/resume commands are available.
- Persist desktop drop locations in version 4 layout data. Keep existing box
  configuration compatible. First experimental launch copies existing layout
  to layout.json.pre-hosting without overwriting an earlier backup.
- Pause/normal exit restores the original Explorer icon window.
- Before hiding it, arm an independent recovery process that waits for the
  owner process to end. Validate the native window class, Explorer process id,
  and per-session ownership property before restoring visibility.
- Refresh desktop enumeration every two seconds. If the native icon window
  disappears or becomes visible unexpectedly, stop hosting and preserve the
  native desktop. Resume is available from the tray.
- Cover virtual-screen bounds. Multi-monitor behavior is coded but has not
  been exercised on a physical multi-monitor setup.

## Verification on this machine

tests/hosting/session.bat creates a real canvas and suppresses/restores the
real native icon view. Pointer messages exercise internal movement using an
existing disposable test folder. Builds use DESKBOX_TESTING to write only
build/hosting-test-layout.json; normal user layout is not overwritten.

Passed:
1. Read visible native desktop and arm recovery.
2. Hide native icons.
3. Desktop-to-box changes ownership once; unboxed hit-test excludes it.
4. Box-to-desktop removes ownership and exposes the item at the drop point.
5. Escape cancels a transfer.
6. Box-to-box transfer leaves exactly one owner.
7. File attributes remain unchanged.
8. New position persists through isolated save/load.
9. Pause restores native icons; resume suppresses them again.
10. Normal shutdown restores native icons.
11. Forced process termination followed by --verify-native confirms the
    recovery process restores native icons.

tests/render/run.bat also passes background-alpha, opaque-title, collapsed-body,
legacy icon mask and native Shell icon transparency regressions.

## Limits of the experimental build

This is not a complete Explorer replacement: multi-selection, marquee
selection, full native context menus, OLE drag/drop to other applications,
file copy/move from outside the desktop, sorting, and scrolling long box
contents are not implemented. Internal mouse-message tests do not substitute
for a full manual usability pass on multiple monitors and DPI settings.

It does not clear hidden attributes left by older versions because their
original values were not recorded. It does not change the user's Show Hidden
Items preference. Pausing restores Explorer's previous layout; custom drop
positions are used again on the next hosting session.

## Build / rollback

    build.bat "C:\Users\bilib\Downloads\desktop\DeskBox\release" nestlone-D-hosting.exe

Keep release/assets beside the experimental executable for its application
icon. The old release/nestlone-D.exe is left in place. Exit the experimental
program before running it; both editions share a single-instance mutex.

For code rollback, use the snapshot/pre-desktop-hosting tag in a separate
worktree/check-out. Do not reset a working tree with uncommitted user changes.
