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
| Autosave covers finished passes only | each finished pass autosaved in the background; recovery offered at start | still save after every 2–3 passes |

## Release candidate (6 Nov 09:17)

Tag `field-rc-2026-11-06` = commit `123e0e1`. Installed on the field laptop: D:\ArtecRec\artecrec.exe,
MD5 89612F7B5FB600F276760BBC78C81E2F (previous field build kept as D:\ArtecDiag\artecrec-prev-1211.exe).
Review rounds applied after the first status above: cloud review (10), ChatGPT R1-R4, Gemini (7), DeepSeek 1-7
(24 + 3). Tests: 84 passed, 0 failed, 0 skipped.

## Installed 6 Nov ~12:40 (owner: "you can kill the app")

Commit `122c782`: per-pass background autosave (D:\ArtecRec\Autosave, recovery offered at start),
collapsible/resizable panels, scanner temperature state in the status bar. artecrec.exe MD5
C5C0E5A2016DFF5916300E74C229F4E2, artecrec-fs.exe 3564E79AE6C38951B05879761AA6EEEA. Previous build kept as
D:\ArtecDiag\artecrec-prev-0911.exe. Tests 89/0/0. The force-close dropped 3 unsaved passes (11:11-11:18),
owner-approved. Field check: after the next crash or kill, the recovery question appears.

Field finding 6 Nov: global registration hit its 120 s limit on 6 long passes (250-480 frames); measure the
real time on the laptop and set the limit from it.

## No time limits on processing (6 Nov, commit 73049b5, installed ~14:45: artecrec.exe MD5 776F47494C89D6C14CB6A20695C2FE56, artecrec-fs.exe 602117478E6905D2512400F8FD7D6FDF; previous build kept as D:\ArtecDiag\artecrec-prev-122c782.exe)

Owner: "why should there be a limit at all?" Processing runs until it ends; Edit > Cancel processing stops it; a step
that reports progress and stops reporting for 5 min is treated as hung. Save no longer waits for processing. Global
registration is run from the Edit menu (owner option A). Tests 100/0/0.

Measured on the field laptop, 15-minute pass (5,269 frames, D:\ArtecDiag\t_auto_big):

| Step | Time | Old fixed limit |
|---|---|---|
| Open the project | 154 s | - |
| Surface optimisation | 104 s | 90 s (would have been cut) |
| Global registration | 25 % after 30 min (about 2 h) | 120 s |
| Auto-align, 2 scans | 89 s | 60 s (would have been cut) |

## Installed 6 Nov ~17:55 (owner approved in advance; ArtecRec was closed)

Commit `5cecaf3`: a pass joined live anchors the pass it joined (16:38 scan 1 moved 5.1 mm); stubs between a
tracking loss and Stop are discarded (16:06/16:37 13- and 16-frame scans). artecrec.exe MD5
9DEA2E7F95119D274C01C8A4FBF9F56B, artecrec-fs.exe F309515DB13B129A88E514B5D863542F. Previous build kept as
D:\ArtecDiag\artecrec-prev-73049b5.exe. Tests 102/0/0.

## Installed 7 Nov ~05:25 (owner approved in advance; ArtecRec was closed)

Commit `51bcf56`: UI scale 1.25 (21 px body text, 42 px buttons, darker banners for contrast; owner: "the font
sizes are too small"), plus the replay source and benchmark (no effect in normal use). artecrec.exe MD5
1090C9C35081A92FCBBB160BC7F2B5C6, artecrec-fs.exe 7ED43F6E5F37AC7CE49836F665835D06. Previous build kept as
D:\ArtecDiag\artecrec-prev-5cecaf3.exe. Tests 103/0/0.

## Installed 7 Nov 07:42 (owner: "if there is a new build by 830 AM you can install it")

Commit `2b02a16`: adds a diagnostic log line in the live texture join ("hybrid resume textures: reference WxH, probe
WxH") to confirm the texture-scale hypothesis; the rest of the commit is replay/benchmark code, inactive in normal use.
artecrec.exe MD5 C48F8D465E93DA829896C2E51CBCB00B, artecrec-fs.exe 8BE8EE8B2EC344297F0DA3314F155E56. Previous build
kept as D:\ArtecDiag\artecrec-prev-51bcf56.exe. Suite 102/1: the one failure is a 2.5 s window stall
while cancelling 7 optimisations at once (responsiveness check), measured with a 40 GB copy running on the PC; the same
code is in the previous field build.

## Still needing a field check (verified by code reading only)

| Item | How the acceptance session or the trip shows it |
|---|---|
| Live joining after the 6 Nov search changes | log: "FOUND" per pass, "vs pass N", gate reasons |
| Abandoned search cannot touch the live view / place a pass | only if a search ignores its cancel (log: "did not stop in 10 s") |
| A save given up on (after 15 min) | only if a save hangs; message names the folders |
| Graphics memory freed while minimised | minimise during a long pass; memory in the status bar stays flat |
| Redraw and frame-lock times | "rec fps" lines: longest draw / longest frame-lock wait |
| Reconnect on the window thread | USB pull during the session |
| A long project (thousands of frames) under low disk space | one long stone on the laptop before the trip if time allows |

## Field procedure (prices in the remaining risk)

1. One project per stone. Save after every 2-3 passes.
2. At the end of each day: File > Save project as... to a NEW folder name with the date (for example
   Belur_pillar3_12Nov). This keeps every day's work in its own folder, whatever happens to later saves.
3. Copy the day's folders to the second drive or the other laptop in the evening.
4. Full scanner battery each morning; charger and adapter in the bag. Repeated USB drops = check the battery first.

## Recovery card (print and keep with the laptop)

- **ArtecRec crashed or was closed by Task Manager:** everything up to the last save is on disk in its project
  folder. Reopen ArtecRec from the desktop shortcut and open that project. Passes recorded after the last save are lost.
- **"The last save was interrupted":** ArtecRec names two folders. The new copy is `<name>__saving`, the previous one
  `<name>__previous`. In Explorer, rename `<name>__saving` to `<name>`, then open it in ArtecRec. Do not delete either
  folder until the project opens correctly.
- **"The scanner stopped answering":** save the project now, close ArtecRec, unplug the scanner's power and USB for
  10 s, plug in power then USB, reopen ArtecRec.
- **Save refused ("already holds other files"):** nothing was changed. Save under a new name.
- **Anything else:** save under a new name, note the time, and send the support channel the time and a photo of the screen.

## After the trip (agreed priorities)

1. Worker lifecycle and SDK cancellation architecture. 2. Asynchronous scanner reconnection. 3. Rendering/memory
optimisation from field measurements. 4. Fault-injection tests. 5. Build reproducibility / portable tests.

### 7 Nov 09:50 — build 1b15788 (not installed; museum day in progress)
Trail29 (Madhusudhan, lab, 09:34-): the live search spent its whole time on the texture join, which the SDK refuses
(reference photos 960x1280, live 480x640 — confirmed by the diagnostic line at 08:46 and 09:39). Three tries of ~6 s
each, so the geometry search was cut off at Stop (0x00010105 in 0.0 s) on every short pass; passes were placed only by
the after-Stop auto-align (31 s). Fix: logic::textureJoinPossible — different sizes -> skip the texture join, geometry
at once. Unit 64/0; field-day replay 9/9 (same-size frames, texture join unaffected). MD5 4848ca4545ea6b20c058f9e57b7933b0.

### 7 Nov 10:38 — INSTALLED on the laptop: build 10:33 (commit after a1cbe02 + loss handling), MD5 4faf3e69736a2cc7463cbc08ed0d84a7
Owner ("can you install a new version with all the fixes" / "go"), between stone 2 and stone 3 at the museum. In it:
texture-size skip (1b15788), Field mode (a1cbe02, switched ON in the laptop's artecrec.ini), and a tracking loss no
longer ends the scan (logic::lossEndsScan 120 s; the SDK stores nothing while lost and registers again over the last
scanned surface). Tests: unit 66/0; integration 17/0 (a1cbe02); system 20/1 (the known 2.0 s stall check, 2.0014 s);
replay field-day 9/9, field-mode-check 7/7, loss-check 4/4 (+1 script mistake fixed). Previous exe kept as
D:\ArtecRecrtecrec.exe.prev. Madhusudhan had set autoAlign=0 and scanUsingAutoAlign=0 himself; left as they were.
Stone timings (build 06:55): stone 1 (low-medium) 16 min scanning, 19 min to placed, 7 passes; stone 2 (small-low)
~21 min, 13 scans, losses every 10-15 s at the stone edge over a blue tarp (photo), after-Stop auto-align 15-184 s.
Reruns alone on the installed build: loss-check 5/5; field-mode-check 7/7 after the field-day script was given a real
overlap for pass 2 (150 frames; the old 50-frame resume never joined in replay, its one FOUND came from the loss restart).
First field proof 10:44:51: tracking lost, back after 2.1 s, same scan continued (stone 3, Field mode, 4 passes kept as recorded).
