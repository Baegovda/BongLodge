# BongLodge - status and limitations (2026-10-30 22:40 KST)

Live tests: BLTest, UE 5.7.4, 125% DPI main monitor, window rects polled every 15 ms (per-monitor DPI aware).
Scripts: tests/rectpoll.ps1, tests/bulkmove.ps1 on the PC (C:\Users\Revaptor_FX\BongLodge_src\tests, raw logs r61_*, r63_*, r65_*).

| Item | State |
|---|---|
| 6.1 ~2px oversize at startup | PASS live. Main window first visible frame exactly 1920,0 1920x2100 in 6/6 launches (3 on 85d809b, 3 on this build); floating CB snapped from 1922x1292 to exact 1920x1290 (log "Creation rect snapped"). |
| 6.2 maximized flip | PASS live (run4): after maximizing, 2/2 launches first visible frame already max=1 (-9,-9 3858x2118), no normal-state flip (tests/r4_b_max1.txt, r4_c_max2.txt). |
| 6.3 minimized restore flicker | PASS live (1 run on 85d809b, 1 on this build): main window never visible in normal state; first visible frame is minimized (-32000,-32000). |
| 6.4 normal CB shrinks ~6px/launch | PASS: 8 runs earlier + 7 launches in run4, floating CB always -1920,810 1920x1290. |
| 6.5 floating CB not restoring | Cause found and fixed (live). DisplayFusion window position profile "SuckBong" has an UnrealEditor.exe entry matched by process path only (1124,685 1353x784, maximized, dpi 1.25). Applying it moves the main window AND every floating tab window onto that rect and maximizes them; BongLodge recorded that, so the saved CB placement was overwritten. The earlier theory (BongLodge copying main state to the tab) is refuted: the OS rects really moved. Fix: windows sharing an identical rect with another editor window are not recorded; a move is recorded only after it stays unchanged 1.5 s; a window closed while bulk-moved keeps its previous placement; on exit the floating areas in EditorLayout.ini are rewritten from BongLodge's placements. Live (simulated with SetWindowPos+maximize on both windows): before = json Root 1124,685 / CB -9,-9 3858x2118; after = Root 1920,0 1920x2100 / CB -1920,810 1920x1290 kept, next launches restore correctly. DisplayFusion itself was not touched. |
| 6.5 residual | The first launch right after a bulk-moved session showed the floating CB for ~120 ms at 0,0 3842x3196 before it jumped to its place; the next 2 launches were clean. Source not found. | Not seen in 8 launches in run4 (no frame at 0,0). |
| 6.6 undock / multi-window | PASS live (run4, real mouse drag with input blocked): World Settings tab dragged out of the maximized main window became a normal floating window 1978,1475 902x1012 (not maximized). Relaunch 1: restored at the same x/size, y fitted to 1088 because the window hung 387 px below the work area (by design), with a ~0.7 s 2px-oversize first frame; relaunch 2: first frame exact. Moved to the left monitor (-3000,300 1000x900): relaunch first frame exact. Docking back by drag: not tested. |
| Monitor add/remove | Simulated only (preset tools not touched): saved rect on a non-existent monitor (x=-9000) -> window moved to 1420,600 1000x900 on the primary monitor. Real unplug/replug not tested. |
| 6.7 identity | Key = sorted set of opened tab ids. |

## Limitations (by design)
- Two floating windows holding exactly the same tab set share one saved placement.
- Moving tabs between windows gives the window a new key; the old key keeps its last rect.
- If you deliberately put two editor windows on exactly the same rect, that placement is not saved.
- A move is saved only after the window stays put 1.5 s (a move followed by closing within 1.5 s is still saved at close).
- The editor has no start-minimized option; if the minimized-first-show suppression fails, a short normal flash is possible.
- To stop DisplayFusion moving every editor window, the UnrealEditor entry in its profile would have to match a window title instead of only the process (user's choice; BongLodge does not change it).

