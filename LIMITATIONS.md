# BongLodge - status and limitations (2026-10-30)

Live verification status: the changes in this commit are BUILT (RunUAT BuildPlugin, success) but NOT
live-tested and NOT installed, because another UnrealEditor (UE_VFX_57) was running. Nothing below is "fixed"
until a live test confirms it. v1.0.0's commit message over-claims; treat 6.1-6.3 there as unverified.

| Item | State |
|---|---|
| 6.1 ~2px oversize at startup | New: first-show snap of the window rect to the saved exact rect (WM_WINDOWPOSCHANGING, tolerance 6px, log "First show snapped"). Untested live. |
| 6.2 maximized flip | Code from v1.0.0 (state only when on right monitor). Untested live. |
| 6.3 minimized restore flicker | Code from v1.0.0 (suppress first show, ~2s safety re-show). Untested live. |
| 6.4 normal CB shrinks ~6px/launch | Same first-show snap as 6.1 stops the drift being saved. Untested live. |
| 6.5 floating CB not restoring | Logs read: in the current layout CB is docked (nothing to restore). Earlier sessions show the guard blocking an engine move and the CB ending elsewhere, then the user snapping it to x=-8 (DWM invisible border, looks clipped). New log line "Layout area [...] has no saved placement" shows identity mismatches. Root cause not proven. |
| 6.6 undock / multi-window | New: windows created by a drag-undock after startup are left under the cursor; window key refreshes when tabs move between windows ("Window identity changed"); enforcement stops when the user starts a title-bar drag (WM_ENTERSIZEMOVE). Untested live. |
| 6.7 identity | Key = sorted set of opened tab ids. |

## Limitations (by design)
- Two floating windows holding exactly the same tab set share one saved placement.
- Moving tabs between windows gives the window a new key; the old key keeps its last rect.
- Layout rewrite is in-memory only; EditorLayout.ini is never rewritten by BongLodge.
- The editor has no start-minimized option; if the minimized-first-show suppression fails, a short normal flash is possible.
