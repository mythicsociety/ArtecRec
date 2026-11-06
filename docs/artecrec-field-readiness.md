# ArtecRec field readiness — Hoysala trip 10–17 Nov 2026

Status 6 Nov 2026 07:00. Code at main `4ee5b64` (all review rounds: own review 48 items, second review 17, test-suite
findings 8, cloud review 10, outside review R1–R4). Tests on the workstation: 84 passed, 0 failed, 0 skipped
(unit 54, SDK integration 13, system 15, e2e 2). Build staged on the laptop at D:\ArtecDiag\artecrec.exe,
NOT installed (laptop runs build 15:47 of 5 Nov).

## Acceptance session (with the team, real scanner) — must pass before the build is frozen

| # | Check | Pass when |
|---|---|---|
| 1 | Install, start from the desktop shortcut | title shows the staged build; log "build ..." line matches |
| 2 | 4–6 overlapping passes on one stone, each started over scanned surface | most passes "FOUND" live; failures say why (SDK "not found" vs gate reason) |
| 3 | Stop after each pass | auto-align runs only for unplaced new passes; no pass moves after it was placed |
| 4 | Save, close, reopen | same scans, names, groups, visibility; positions unchanged |
| 5 | Pull the USB during a recording | recording ends within ~15 s, earlier passes kept, window responsive, reconnects |
| 6 | Long pass (5+ min) | "rec fps" ~5.7; "longest draw" and "frame-lock wait" small (record values) |
| 7 | Saved project measured (zspread / meshcheck) | no doubled surfaces at joins; repeating letters not one row off |
| 8 | verify-log.ps1 on the session log | all checks PASS |

After a pass: tag the commit `field-2026-11-xx`, record the exe MD5, keep the previous build next to it, and make no
further changes before the trip except for a critical defect.

## Open risks (accepted for the trip, with mitigation)

| Risk | Mitigation in the build | What the team does |
|---|---|---|
| Live joining unproven on hardware after the 6 Nov search changes | rotation over 3 newest passes; strict gates; auto-align after Stop | start each pass over scanned surface; if a pass does not join, keep scanning; check after Stop |
| An SDK job that ignores cancel keeps running after being abandoned | generations, own references, results discarded; app returns to Idle | save, then close and reopen ArtecRec when told |
| Save/open hang | after 15 min: keep waiting / give up; scans stay in memory | if asked, give up and save to another folder |
| Reconnect runs on the window thread | field evidence: 26 min of retries without a freeze | if the window freezes after a USB drop, wait 1 min, then power-cycle the scanner |
| Scanner battery low shows as repeated USB drops | — | full battery each day; carry charger and adapter |
| Memory on very large projects | warning at 16 GB free, recording refused below 6 GB | one project per stone; save often |
| No timed autosave | status bar shows NOT SAVED; Save queues itself while processing | save after every 2–3 passes |

## After the trip (agreed priorities)

1. Worker lifecycle and SDK cancellation architecture. 2. Asynchronous scanner reconnection. 3. Rendering/memory
optimisation from field measurements. 4. Fault-injection tests. 5. Build reproducibility / portable tests.
