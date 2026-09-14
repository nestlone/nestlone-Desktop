# Desktop icon hosting feasibility — 2026-09-14

Baseline: commit 0a0f535, tag snapshot/pre-desktop-hosting.
Branch: codex/desktop-icon-hosting.

Experiment uses the documented Shell desktop IFolderView2 interface and
SelectAndPositionItems, not file moves, file attributes or Explorer memory writes.
Only a newly created empty Desktop/DeskBox-hosting-test-20260914 folder was positioned.
No auto-arrange or other global desktop options were changed.

Observed desktop flags: 0x40200224; auto-arrange OFF.

| Requested position | Read-back position | HRESULT | Outcome |
|---|---|---|---|
| -20000, -20000 | 18, 2 | S_OK | Clamped onto desktop; not hidden |
| 2432, 256 (beyond 1920px desktop) | 1842, 275 | S_OK | Clamped onto desktop; not hidden |

Restoring the first original position (94,2) snapped to (94,93).
The second experiment restored (94,93) exactly. The temporary empty folder
remains on the desktop: automatic approval rejected cleanup with "blocked by
policy". No real user file was moved or removed.

## Decision

Do not integrate off-screen positioning as a hiding mechanism. An API success
result is insufficient; actual coordinates remain visible. Existing release
and application source are unchanged by this experiment.

This rejects the tested off-screen-position approach on this machine, not every
possible desktop integration. It does not establish an official per-item hiding
API. Bilateral drag-and-drop remains unimplemented and must not be advertised
as working.

For path-preserving hosting, a different architecture can take over the whole
desktop icon presentation (render unboxed items and boxed items together),
temporarily suppressing Explorer's icon view and restoring it on exit/recovery.
This is a substantially larger feature requiring Explorer refresh, recovery,
keyboard, selection, accessibility and Shell drag/drop coverage.

Alternatively, explicit user-approved physical file collection is simpler,
but changes file paths and is not part of this experiment.

## Reproduce

Create an expendable empty folder on the desktop. Run:

    tests\hosting\run.bat "ABSOLUTE_TEST_FOLDER_PATH" negative
    tests\hosting\run.bat "ABSOLUTE_TEST_FOLDER_PATH" positive

The probe positions that exact item, reads back coordinates and attempts to
restore them. It aborts if auto-arrange is enabled. Exit status is nonzero if
positioning was clamped, restoration differs, or the Shell interface is unavailable.
Use only a disposable test item, because grid snapping may alter its position.

References:
- https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview-selectandpositionitems
- https://devblogs.microsoft.com/oldnewthing/20211122-00/?p=105948
