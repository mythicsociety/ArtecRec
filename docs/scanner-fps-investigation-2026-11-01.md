# Artec Space Spider – slow scanning on the laptop (Pervonasetti) – investigation record, 1 Nov 2026

Owner's problem: since ~Mar 2025 Artec Studio on the HP ZBook 17 G6 runs at 5–6 fps instead of 7.5 (Space Spider spec).
The HP Thunderbolt Dock G2 did not help.

## Conclusion (14:00, 1 Nov 2026)
**Cause: the laptop's USB host controllers (Intel xHCI 1.10, "USB 3.1 eXtensible Host Controller - 1.10") corrupt the
scanner's camera images above a certain USB packet size.** Artec's own support article names USB 3.1 chipsets as a cause of
reduced FPS with Eva/Spider. Measured on the laptop with `ForcePacketSizeSpider` fixed and `AdaptivePacketSize=0`:

| packet factor | transfer errors (40 s) | fps |
|---|---|---|
| 2 | 0 | 3.0 |
| 4 | 0 | 5.8 |
| 5, 6, 7, 8 | ~300, no frames | – |

Every port on the laptop (USB-A, both USB-C/Thunderbolt) and the HP dock's internal controller are Intel xHCI 1.10 and
behave the same. The desktop DIGITALSTUDIO (Intel xHCI 1.0, Windows 10) carries the default (large) packet size cleanly:
7.2–7.3 fps in Studio 17.1.2.15 and in the SDK probe.

Studio's default `AdaptivePacketSize=1` hunts for a working size: on cold cameras it settles at the factor-4 level (5.9)
and, on this laptop, slides further over minutes. With adaptive OFF and factor 4 forced the laptop gives a steady 5.8.
On the desktop, cold cameras + adaptive ON also gave 5.9; adaptive OFF (factor 0 = default) gave 7.3 → adaptive hunting
is harmful on both machines; it is now OFF on both.

## Endurance tests, afternoon (fixed packet 4, adaptive off, 0 USB errors throughout)
- Studio 17.1.5.2: 5.8 → 5.6 (5 min) → 5.3 (10) → 5.1 (15) → 4.4 (27 min). Memory grew to 6.9 GB. The "falling fps" is
  inside Studio 17.1.5.2, not USB.
- Studio 15.1.2.60: 5.8 → 5.7 (5) → 5.6 (15) → 5.6 (25) → 5.4 (27 min). Memory 8.2 GB. Far flatter.
- Stop → Preview after 27 min in Studio 15: 5.4 → 5.5 only; memory not released. A full Studio restart resets it.
- Owner's note: the laptop has always shown 5.8–5.9 (2021 photos); the complaint was the slide. Owner's plan: scan in
  Studio 15, process in 17; restart Studio between stones. Spider II (30 fps) considered for the future; Eva rejected
  (accuracy insufficient for inscriptions). Main use of the scanner: 3D inscriptions.
- Studio 15 lost its activation after the BIOS graphics switches; owner re-activated via Installation Center.

## Recording tests, late afternoon (Studio 15, fixed packet 4, texture on)
- Run 1 (16:24): 5.8 → 5.4 (7 min) → 4.0 (17 min). Stop → Preview → Record again: 3.9 (no reset).
- Studio restart at 16:45 with the CPU zone at 86 °C before AND after: 5.8 again at once → heat is not the cause;
  the slide is per Studio session.
- Run 2 (16:46): 5.8 → 5.6 (5) → 5.3 (10) → 4.7 (14 min). Same curve. Recording slides ~2× faster than preview.
- Field rule that follows: about 12–15 min of recording per Studio session before fps < 5; restart Studio between
  sides/stones. Studio 15 and 17 behave alike here; 15 is no cure.

## SDK route (owner's "go", 17:00)
- The SDK scanning procedure (IScanningProcedure, Artec's own tracking/registration) runs on the laptop once its
  USB packet size is reduced the way Studio's ForcePacketSizeSpider does. The SDK has no public knob for it; the
  flyCamera layer programs `Camera::SetFormat7Configuration(settings, 1200)` per camera through its import table.
  Camera reports unit 240 B, max 2880 B → Studio's "factor" = packet/240 (factor 4 = 960 B, factor 5 = 1200 B = SDK
  default = the value this laptop's controller corrupts; factor 12 = max, crashed Studio).
- `C:\Artec\sdk-fps\flyhook.h` patches the flyCamera IAT (and the Camera vtable) in-process; env `FLYPKT=960`
  clamps the packet. `sdkscan.exe <secs> [texture] [icp|hybrid|texture]` records with the SDK and logs
  fps/failures/memory every 10 s. With FLYPKT=960, texture on, hybrid: 5.7 fps, 0 failures (30 s).
- 30-min SDK endurance run 17:10–17:40 (log `D:\ArtecDiag\sdk\sdkscan-1800.txt`): **5.7 fps flat for the whole
  30 min**, 10,271 frames, 0 registration/capture/texture failures, CPU zone 86 °C throughout, memory 36 GB (raw frames
  with textures kept in RAM; laptop has 128 GB). Studio was below 5 fps at 14 min under the same conditions.
  → The slide is in Studio's application layer, not in capture, tracking or registration. An SDK-based recorder
  (record → save .sproj → process in Studio 17) would hold 5.7–5.8 on this laptop indefinitely.

## ArtecRec — the SDK recorder (built and installed 1 Nov evening, owner's "go for all")
- `C:\Artec\sdk-fps\artecrec.cpp` (+ flyhook.h, build-rec.bat). Installed on the laptop as `D:\ArtecRec\artecrec.exe`
  with the SDK DLLs, README.md and a desktop shortcut. User guide: `C:\Artec\docs\artecrec-README.md`.
- Win32 + OpenGL app: Preview / Record / Pause / Stop (F9/F10/F11), live point view (green = current frame,
  grey = key frames, red = tracking lost), fps, frame/scan counts, memory with a >80 GB warning, Save project…
  (creates `<dir>\<name>\<name>.sproj` + `Scans\`), Discard scans. Packet clamp 960 B built in (env FLYPKT overrides).
- Verified on the laptop: records at 5.7 fps; pause/resume/stop; each pause starts a new scan; project saved via the
  file dialog in 7 s (2 scans, .dscan/.tscan/.mscan); the SDK project opened in Studio 17.1.5.2 ("Scan 0, 116 frames",
  after Studio's one-click conversion to .a3d). Texture is captured on SDK texture key frames (as Studio does).

- v2 (2 Nov 01:50, owner: "same functionality as Studio… no processing"): tracking geometry+texture / geometry,
  capture-texture toggle, auto-align new scans on Stop (SDK createAutoalignAlgorithm, 2.6 s for 2 scans, verified),
  distance colour in the live view (camera looks down -z; depth = -z, SDK default range 170..330 mm, sens 0.5),
  live sliders: speed 1–8 fps, range near/far, sensitivity. Not in the SDK: targets, base removal, real-time fusion.

- v3 (2 Nov 03:57, owner: "study Studio 17 carefully… a user should not feel anything is missing from a scanning
  perspective"): full rebuild of Studio 17's scanning screen — Scan panel with every Studio control (Targets, base
  removal, real-time fusion, motion tolerance greyed as "not in the SDK"), Advanced section (hide panel, depth of
  field, scan name, sensitivity, texture brightness, auto-adjust, scanner in use, don't record texture, flash,
  texture-frame frequency, delay, frame limit), Studio mouse navigation (LMB orbit, MMB/LMB+RMB pan, RMB zoom, wheel,
  double-click rotation centre), fit/home/standard views/ortho/grid/lighting/render modes/range meter/gizmo/screenshot,
  Workspace tree with eye ticks, rename, delete, groups (sidecar artecrec-groups.txt, mapped by index because the SDK
  project writer renames scans "Scan 0, 1…"), Properties, File New/Open/Save/Save-as, status bar, log strip, audio
  warning on tracking lost. Display meshes filter edges > 2.5 mm and grazing triangles (cos < 0.22) to avoid the
  "curtain" streaks. Source split: artecrec.cpp + rec_sdk.h / rec_view.h / rec_panel.h / rec_ws.h.
  Verified on the laptop: record, pause, stop, group, save, new, reopen (names + groups restored), view buttons.

- v4 (2 Nov 04:52, renamed "ArtecRec by Mythic Society"; owner: "every behaviour must mimic Studio", "check against
  the manual for all features"): audited against the Studio 17 manual chapters Scanning, Viewing, Workspace, Settings,
  Hot Keys, Hardware (full text pulled with curl → scratchpad/manual/*.txt) and the SDK 2.0 guide pages. Added:
  scanner-body buttons + overheat/disconnect callbacks (IScannerObserver), Preview hides scans, Studio's lost-tracking
  texts (green "Searching for position" with "Scan using auto-alignment" on, red "Tracking lost" off), resume in a new
  grouped aligned scan, live position search at scan start (probe grows to 240 frames; frames before the find are
  discarded), range meter grey/dark-green/red, dropped-colour background, render modes incl. wireframe and "points and
  solid" (Spider default: scans as points), flat/smooth, backface modes, scan/surface colour, rotation-centre marker,
  PNG screenshots, Workspace ListView with Studio's columns/sorting/selection keys/context menu/Duplicate, hot-key
  fixes (Right = Ctrl+Shift+3, Ctrl+5), "Enable real-time processing" row, default name "Spider Scan N", texture
  "Automatic" = every 8th frame (SDK key-frame mode gave 1 per 70). Verified: position search FOUND in 1.5 s on scan 2.

- v4 field test (2 Nov 05:00–05:50, owner scanning a carved figure and a wood-panel wall): decisive check = open the
  ArtecRec project in Studio 17 and compare. Capture was fine; the viewer was wrong: (1) texture not applied (pixels
  freed after upload broke the "has texture" test); (2) unregistered first frames treated as tracking loss; (3) "Normal"
  sensitivity/gain sent 0/min instead of the scanner defaults (holes, dark texture); (4) 200 overlapping frames
  z-fighting, grazing/long triangles (shattered look); (5) textures far darker than Studio's view. Fixes: texture mode
  draws textured frames only, later frames on top via per-frame polygon offset, display gamma 1/1.7, edge 1.5 mm /
  cos 0.3 filter, defaults = scanner defaults, loss = SDK failure or 3 unregistered in a row, scans < 3 frames
  discarded, full-screen scanning (both panels hidden) with on-screen Pause/Stop and panel state restored after a
  resumed scan. Studio 17 ignores scan names on .sproj import (shows "Scan 0, 1…").

- SDK samples read (06:00; scanning-and-process, simple-capture, parallel-capture, project): applied point normals
  before auto-align (fixes RANSAC sampling), ignoreRegistrationErrors=false (SDK keeps only registered frames; my
  manual frame dropping removed), Resume = new scan (ContinueRecord unsupported), maxFrameCount 0 = unlimited.
  Verified: position search FOUND in 2.4 s with normals; recording unaffected by the ignore flag.

- SDK documentation read in full (06:15–06:40, crawled docs.artec3d.com/sdk/2.0 — 491 pages — into the session
  scratchpad): guide pages (index, Basics, Beyond Basics, Tweaking Scan Settings, General Pipeline, Base API,
  Limitations, Bundling), all samples, namespace references (capturing, scanning, base, algorithms, project, io,
  errors) and the class/struct references used by ArtecRec. Tried and reverted the same morning: (a) deep-copying
  callback frames for the position-search probe (RegistrationInfo note) — the copies lack the SDK's internal
  feature data, auto-align then fails with "cannot take sample for RANSAC"; the probe keeps the reference-counted
  callback frames (IScan::add holds its own reference). (b) "Automatic" texture = OnTextureKeyFrame — gave 1
  texture frame per scan on a slow-moving scanner; back to every 8th frame. Kept: warm-up frames before the first
  registered frame no longer count as tracking loss (the 06:27 run lost tracking at frame 2 and split into 3
  scans); shutter speed / illumination coefficient logged at connect (0.20 ms, 10.57; auto exposure and auto
  white balance are OFF by default, ArtecRec turns white balance on). Verified 06:32: two wall scans, position
  found live in 2.5 s, 25 search frames dropped, scans contiguous, errors 0.31 / 0.18.
- 06:49 texture tone fix: measured the raw texture frames at ~199/255 mean luminance — they were never dark; the
  display gamma 1/1.7 added on 1 Nov washed them to cream. Now: per-frame exposure normalisation to a mean of
  150/255 (scale clamped 0.5–5, so dark frames are lifted and bright ones held back) plus a mild gamma 1/1.15.
  Wall now renders as a solid yellow like Studio's view of the same data. Installed on the laptop; owner to judge.
- 07:00–07:11 owner feedback on wall_sdk_5 ("patchy", "small holes", and earlier "view stuck"): (a) the view now
  follows the scanner pose while scanning (inverse frame rotation multiplied into the modelview; mouse offsets on
  top; H re-attaches) — Studio attaches the camera to the scanner. (b) Per-frame exposure normalisation reverted:
  it gave every frame its own scale and made the scan patchy; fixed scale 1.0 + gamma 1/1.3. (c) Textured frames
  are drawn UNLIT, as in the Studio 17 reference screenshot (studio-wall4.png: full-brightness texture, no shading),
  so frames at different angles keep one tone. (d) Untextured frames are drawn underneath in the scan's average
  texture colour, so the gaps between texture frames no longer read as holes. Studio's own view of this data also
  shows the frame boundaries and the speckled patch at the right, so that is the data, not the viewer.
- 07:30–08:10 "Studio's scan has no holes and is smoother": (a) Studio's real-time processing includes "surface
  optimization" = fine registration after each scan; added the SDK serial fine registration (FineTextured when
  every frame has texture, else Fine) run in the job thread after Stop, status "Optimising surfaces…" (181 frames
  in 2.6 s, 229 in 3.6 s). (b) The small holes were ArtecRec's own display filter (grazing-angle cull cos<0.3 and
  1.5 mm edge limit) — removed; frames draw as the SDK reconstructed them. (c) Live view accumulates every 2nd
  registered frame (was key frames only) so a tilted re-scan fills holes on screen. (d) Reviewed the Robot Raconteur
  Artec driver (github robotraconteur-contrib) at the owner's request: thin pass-through of the same SDK calls, no
  extra frame-quality steps. (e) 08:00 freeze: 240 display frames + 240 live frames redrawn every 40 ms stalled the
  GPU; the window froze for minutes and Stop clicks were lost (scanner kept flashing; killed the process). Fix:
  redraw only on new data / input / every 400 ms; display 120 frames, live accumulation 150. Stop verified 08:10.
- 08:12–08:30 lost-tracking path: fine registration + display build moved to a background thread per scan
  (WM_POSTDONE swaps the result in; save/align/duplicate wait for it), so the restart after loss is immediate.
  Bug found in the 08:19 log: no position search after loss because beginRecordNow re-reads the panel's
  "Auto-align" checkbox — now a g_forceAlign flag makes the resume search run regardless, as Studio's
  "Scan using auto-alignment" does. Continuous beep (every 0.7 s) while tracking is lost or the position is
  being searched, as in Studio.
- 08:25–09:10: (a) second save into an existing project folder failed (0x80010104: the SDK saver will not write
  into a folder that already holds a project) — save now goes to `<dir>__saving`, then the folders are swapped and
  the old one removed. (b) Position search grew with the project (6 s → 11 s) and held g_scanMx during the normals
  pass (view froze) — references are now cloned (cloneScan keeps the per-frame attributes; re-adding frames with
  add() lost them and RANSAC failed every time) and thinned to 80 frames per scan outside the lock. (c) 09:05: a
  Preview delivered no frames for 17 s (capture stall after a forced close); relaunch fixed it. Added a capture
  watchdog: no frame in 6 s → restart the capture (twice), then ask the user to reconnect the scanner. Build
  09:10 installed on the laptop (D:\ArtecRec) but NOT yet field-tested: thinned-clone position search, watchdog.
  Owner stopped testing at 09:10. Saved owner projects today: wall_sdk … wall_sdk_11, 17_wall (Studio reference).

## Autonomous test pass (2 Nov 09:10–, owner away, laptop + scanner left connected)
Driver: scratchpad `runtest.sh` → step script on the laptop (clicks/keys/drags/screenshots via an interactive
scheduled task), log tail + screenshots back. Lesson 09:21: a run whose keystrokes went to File Explorer instead of
ArtecRec MOVED `D:\ArtecDiag` into `D:\working_folder` (restored intact at 09:26). The driver now aborts unless the
foreground window is ArtecRec, and a single-item click list must not be flattened by PowerShell (trailing dummy item).
Found and fixed (installed as found):
- Hotkeys dead after a scan (Ctrl+S ignored): hiding the focused panel button left no focus window → focus set to
  the 3D view when panels hide/restore.
- Scan-panel settings not remembered: added artecrec.ini (next to the exe), loaded at start, saved on every change;
  Windows' ini cache had to be flushed (WritePrivateProfileString(NULL,…)) or a killed process lost the values.
- Position search time grew with the project (3 → 7 s with five small scans): references capped at ~60 frames in
  total (clone + remove, never add() — add() loses the per-frame attributes and RANSAC fails). Now 1.5–1.8 s.
- No position search / no grouping after lost tracking: jobDone read g_resumeGroupWith AFTER onJobDone had cleared
  it. Loss now → new scan, search (found in 1.8 s in the slider-induced loss test), grouped with the previous scan.
- Pause/Resume scans grouped automatically (Studio behaviour).
- Near > far possible via the sliders (range 396..330 was sent to the SDK): near/far now kept 20 mm apart.
- Save over an existing project verified ("replacing the existing project"); Ctrl+S 3 s after Stop verified.
- Rename (F2) and Delete (with confirmation) verified; project open verified (3 scans, textures).
Later in the pass (10:00–12:15):
- 15-min recording: 5.7 fps flat, 5269 frames, 0 failures; close took 33 s, fine registration 126 s, save 287 s
  (4.9 GB), open 71 s. Save/open now run in a worker thread with a pumped UI and "Saving project… NN%" / "Opening
  project… NN%" in the status bar; the full-screen bar shows "Stopping — finishing surfaces…" / "Optimising surfaces…".
- Mouse wheel over a panel slider moved the slider (and nudged Sensitivity to 35% during a test — reset): the wheel
  is now routed to the Scan panel so it scrolls, as in Studio.
- "Don't record texture" forces Geometry tracking (Studio) and now restores Geometry+Texture when re-enabled.
- Frame limit: the SDK's maxFrameCount ended the recording for good after the 2nd scan — now 0; ArtecRec's timer
  starts the next scan itself (verified: 40-frame limit → 3 full scans + remainder, recording continuous).
- Delay before recording verified (3 s countdown). Geometry-only tracking verified. New project verified.
- Full-screen readout enlarged at the owner's request (54 px fps, 30 px details/buttons; Stop at bottom right).
- Final build confirmation run 12:12–12:32: 13.5 min, 5.7–5.8 fps flat, 4530 frames, 0 failures; stop 30 s,
  optimisation 96 s, save 242 s with the window alive ("Saving project… 9% … 67%" seen on screen). ArtecRec closed
  at 12:33; owner projects wall_sdk..wall_sdk_11 and 17_wall verified in D:\ArtecDiag.
- 12:40 owner: "we scan really big stones – 7 ft × 4 ft" → an hour+ of frames per stone over many scans. Position
  search redesigned for that scale: each scan gets a prebuilt search reference (cloneScan + thin to 40 frames +
  normals, built in the background; 0.1 s even for 5269 frames since cloneScan shares the frame objects), and the
  search is tiered — tries 1–2 use the 3 most recent visible scans, later tries all scans. Loaded projects build
  their references on open (background). Hour-long run (5 × 10-min scans with auto-align, then save) started 12:57
  in the background to measure memory and save time at that scale.
- Hour run result (12:56–14:03, 5 × 10-min scans into one project, no auto-align because the ini test edit had not
  taken): the capture rate FELL with project size — 5.0→3.5 fps in scan 1 (5269-frame project loaded), 1.5–3.5 fps
  in scans 4–5 (≈15 000 frames in the project). Cause: the viewer. Every redraw (one per new frame) pushed the whole
  display set of every scan (up to 120 frames each, ~10 M vertices) through the Intel UHD 630 driver from client
  memory, starving the SDK's capture/registration threads. Fixes: (1) while recording, at most 30 display frames per
  stored scan are drawn — verified 14:13–14:17: 5.6–5.8 fps with the 5269-frame project loaded; (2) vertex buffer
  objects (GL 1.5 entry points via wglGetProcAddress; "OpenGL: Intel UHD 630, vertex buffers on" in the log) so stored
  frames are uploaded to the GPU once; the live frame stays in client memory. The retrieveFrame error 0x80070141
  appears exactly at every Stop click → SDK noise on stopping, not a cable fault.
- Settings persistence root cause (14:20): a control notification during start-up called saveIni BEFORE loadIni and
  wrote the defaults over the file; saves are now ignored until the ini has been loaded. Verified: autoAlign and
  speed survive a relaunch.
- Vertex-buffer build verified 14:23–14:34 with the 5269-frame project loaded and auto-align on: position found in
  3.9 s (two references), 5.7 fps flat for 10 min, stored scans render correctly with textures. Final build
  (459 776 bytes) installed 14:36; ArtecRec closed.
## Owner session 3 Nov 04:05–04:40
- Lost tracking did not realign (one search, "not found"): the probe held the FIRST frames of the new scan, taken
  while the scanner pointed away. Probe is now a sliding window of the latest 12 registered frames. Verified by the
  owner 04:38: position found in 2.1 s after a real loss, scan placed.
- A real USB drop (04:16:31, 5 s after a scan ended; the owner saw a loose connection) left the cameras wedged:
  the device re-enumerates "OK", the SDK reconnects, but retrieveFrame fails with 0x80070141 (device hardware error)
  until the scanner is power-cycled. A software device restart (pnputil /restart-device) did not help. Added:
  automatic reconnection (no modal dialog, status "Scanner disconnected - reconnecting..."), the capture watchdog
  escalates to an on-screen instruction to power-cycle (unplug power + USB, 10 s, power, USB, Preview). The owner's
  power cycle at 04:27 restored 5.7 fps.
- Tilted re-scan (hole filling): owner reports PASS.
## Field session 3 Nov 15:00– (Malurpatna herostone, heat sink refixed, laptop on Tailscale 100.71.58.90)
PNBC relayed the owner's findings (inbox 2026-11-03-pnbc-artecrec-position-search-and-autoalign-findings.md).
- Pass 2 of trail2: 11 live position searches failed (RANSAC outlier limit 82–97 %) — flat slab, 12-frame probe,
  40-frame reference; the operator had started pass 2 on new area (no overlap). Post-Stop auto-align DID align
  (one scan moved) but ArtecRec's verdict assumed the newest scan must be the moved one → could show "could not
  align". Fixes (build 463 360 B, installed 15:39 with the owner's go): verdict = any scan moved, per-scan
  moved/kept + translation logged; the live search expresses the probe pose relative to the first reference's
  original pose (same anchoring issue); probe window 12→24 frames (every frame); from the 5th try references are
  rebuilt with 120 frames per scan. SDK limit: AutoAlignSettings has only scannerType — no texture/hybrid option,
  Studio's own tracker is not exposed. Field advice given: start pass 2 over already-captured area.
- 16:00–17:50 (owner chose "option 2"): hybrid resume built — SDK serial registration RoughTextured, then
  FineTextured, over [last 10 textured frames of the most recent scan + latest 8 textured probe frames]; the probe
  pose is taken relative to the last reference frame. On the herostone it found the position in 0.5–1.7 s every
  time (16:29, 16:36, 16:52, 17:14) where RANSAC had failed 17 times. Live-placed scans are no longer re-aligned
  after Stop; stubs < 10 frames dropped; a placed scan joins the group of the scan it was placed against.
  Gowtham (field tester) saw a line between the passes in ArtecRec; the same project (Trail6) in Studio 17 shows
  one smooth surface → the join is good, the line is ArtecRec's per-scan fill tone → one project-wide tone (built,
  staged, not installed). Auto-reconnect worked in the field (17:04:50 drop → reconnected 17:04:57). createScanner
  0xC0050000 at start after a power cycle → the app now starts without a scanner and keeps looking (staged).
  Process rules learned: install only after the field team's yes on the support channel (owner: their go = my go;
  Gowtham: "always ask me"); plain language with the team; the permission guard blocks commands that kill ArtecRec
  — when the team has closed it themselves, copy + launch works.
- 17:30–19:05: Gowtham: "it must continue from ANY part of scan 1" → hybrid windows over every visible scan (16
  textured frames, step 10, tail of the latest scan first, two windows per try) — installed 18:40 with his yes.
  Trail7 (passes 1–4): resumes found in ~3 s each; in Studio passes 1–3 sit well, one of 3/4 sits visibly low (the
  SDK's fine textured pass reported "geometric/texture quality is not sufficient"). Built + staged: 16-frame windows,
  12-frame textured probe, and a second Fine (geometry) serial pass after the textured one. Offline experiment with
  regtest.exe (C:\Artec\sdk-fps\regtest.cpp): SDK global registration over Trail7 (683 frames) = 205 s, geometry-only
  (GeometryAndTexture needs every frame textured), scan transforms moved < 1 mm → result D:\ArtecDiag\Trail7_glob for
  Gowtham to judge in Studio; too slow for an after-every-scan step anyway. The scanner was unplugged at 17:45;
  the new build sat in "No scanner - looking for it" as designed.
## 4 Nov — deadline set: Hoysala temple trip 10–17 Nov (Gowtham + Madhusudhana); owner: "at this time it is unusable"
- joinmeasure.exe (C:\Artec\sdk-fps\joinmeasure.cpp): nearest-point distance of each scan to the earlier scans
  (grid hash, 8 mm cap), median / 90th pct over the overlapping points. Calibration: static-scanner RANSAC joins
  0.05–0.19 mm; owner's wall with RANSAC position search 0.5 mm median. Herostone hybrid joins: Trail6 6.5 mm,
  Trail7 scans 3/4 4.3 / 3.7 mm (57 % overlap) — ~10× worse; Trail7_glob (geometry global registration) unchanged.
  So the serial rough+fine chain is the precision problem, not the pose search. Change: a GeometryAndTexture global
  registration over the window + probe set (all textured, ~30 frames) as the third step of the hybrid resume
  (built 06:05, staged). Asked Gowtham for a Studio-aligned Trail7_studio as the target number.
- Owner 06:00: "humans also use texture to identify places" → "build another version with image feature search and have
  the team test both". Built rec_feat.h (own ORB-style: FAST-9 on a 320-px grey copy, 5x5 box blur, 256-bit rotated
  BRIEF, popcnt Hamming, ratio test; each keypoint carries the 3D point of the nearest textured vertex via a UV grid;
  3-point RANSAC rigid fit + polar-decomposition refine). Single frames failed on the stone (repeated motifs → finds
  on the wrong repeat); WINDOWS fixed it: bank = windows of 5 consecutive textured frames (step 3, ≤40/scan, scan-
  local coords + 2 mm point grid), probe = latest 3 textured frames at 2 image scales (320/240 px), find accepted
  only if ≥20 inliers AND ≥25 % of the probe surface within 6 mm of the window with median ≤2.5 mm.
- feattest.exe (offline, D:\ArtecDiag) on Trail7: 5 of 18 probes found, 0.1–1.8 s each; scan 4 → scan 3 window with
  166 inliers / 92 % surface / 0.5 mm median, and the project's STORED pose of the same frames has 0 agreeing
  texture matches, 60 % / 2.1 mm. Finds differ from the stored poses by a consistent 14 cm (scan 4) and 33 cm
  (scans 2, 3). ⇒ the hybrid (geometry+texture serial registration) resume has been placing passes on the WRONG
  REPEAT of the carving, sliding along the flat slab; joinmeasure cannot see an in-plane slide on a flat stone, so
  the "3.7 mm" precision figure above measured relief mismatch, not alignment. Gowtham's "pass sat slightly low"
  was the symptom. Saved D:\ArtecDiag\Trail7_feat (scans 2–4 moved to the feature poses, chained) and asked Gowtham
  to compare it with Trail7 in Studio, pass by pass — the decisive check, no scanner or install needed.
- feattest on stone.sproj (owner's 2-pass, 3 Nov): 3 of 6 probes found with 318–592 inliers, 73–93 % surface,
  0.5–0.7 mm median, all ~60–70 mm from the stored pose; the stored pose has 0 inliers and 0 % surface on those
  windows. Same verdict as Trail7. Saved D:\ArtecDiag\stone_feat for a Studio comparison.
- Two builds staged in D:\ArtecDiag (SDK DLLs copied there too): artecrec.exe (precision: textured global
  registration as third hybrid step) and artecrec-fs.exe (image-feature search first, then the SDK textured global
  registration over window+probe; exe name selects it; log artecrec-fs.log). FS smoke test 06:25: opens Trail7 in
  8.6 s, feature banks 0.2–0.9 s per scan, responsive. Install only on Gowtham's yes.
## 4 Nov, field session with owner + Gowtham (10:20–14:30) — single-pass quality, the real defect found
- Owner: "perfect for a single pass first". Builds installed on their go through the day (FS build, launched by
  scheduled task; the desktop shortcut still opens artecrec.exe): global registration after every Stop (15 s/180
  frames, as Studio), lit textured relief, shared-photo projection for frames without a photo (camera = exact pinhole
  fit, 0 px error; best-coverage photo of the 6 nearest; outside-photo triangles kept in fill colour), trimming OFF
  ("I don't want any part of the scan dropped"). Trail8/9/10 recorded. Team's Studio export of the same stone:
  Trail10\Studio\Studio Trail10\*.ply (one PLY per frame, 189, registered coords) — the comparison data.
- Tools (D:\ArtecDiag, sources C:\Artec\sdk-fps): framecheck (nearest-point per frame — BLIND to this defect),
  covmap (top-view coverage), zspread / zspread_ply (height spread per 1 mm cell, per-frame offsets, within-frame
  statistic — THE metric), pairfit (pairwise rigid ICP + scale), chainfix (frame-to-model ICP, diverges on flat
  stone), regtest2 (SDK rough/fine/global sequences), noisepts (stray classification + filters), fpdesc (scanner's
  default FrameProcessorDesc: minObj 1, step 6, edge 10 mm, interpolate off, maxAngle 0 = no filters).
- Findings: frames from both programs have the same pair-fit quality (0.5 mm RMS) and scale; ArtecRec frames carry
  STRAY POINTS at a second level inside single frames (7–11 % of cells >2 mm within one frame vs Studio 0.5 %) —
  long bridging triangles + small floating patches the SDK keeps and Studio filters at capture. Registration (SDK
  rough/fine/global, frame-to-model) cannot fix it and left zspread unchanged. Frame cleaning (edge >2 mm dropped,
  patches <300 triangles dropped, unused points dropped; 5–7 % of points) takes pass 2 from 20.2 % to 1.2 % cells
  >2 mm (Studio 3.0 %), within-frame 0.5 % = Studio. Ported as cleanFrame/cleanScan in rec_sdk.h, run in
  postProcessScan before registration (build 14:30, staged, not yet installed).
- 14:30 installed (owner's go). Trail11 (330 frames, one pass, cleaning removed 6.9 %): zspread 3.1 % cells >2 mm
  (Studio 3.0 %), within-frame 1.1 % (Studio 0.5 %); only frames 316–330 flagged (4–9 mm off: the scanner pulled
  away before Stop). Single-pass data quality now equals Studio's. Gowtham fused it in Studio (small-object filter,
  Fast fusion): zspread_ply on the fused PLY 0.9 % cells >2 mm = the letter edges only; texture mapping on the fusion
  (Trail11_OriginalTexture.jpg, 8192², 42 photo frames) is sharp and even, no seams. Owner + Gowtham: "seems ok".
  meshcheck.exe (C:\Artec\sdk-fps) on the fused PLY (939 k vertices, 1.87 M faces): 0 holes, 1 component, 0 non-manifold
  edges, 0 inconsistent windings, 85 duplicate vertices + 19 degenerate faces (negligible), dihedral median 4°, 0.019 %
  edges >60° (letter edges), edge length median 0.42 mm. Owner 15:15: "if all tests pass install the new build and ask
  Gowtham to do the next test" → installed 15:20 (near-empty frame drop + early image memory); multi-pass test posted.
- Trail12 (3 passes, recorded with the STANDARD build via the desktop shortcut — hybrid texture resume FOUND each pass
  in 2–3 s and placed it live): each pass alone clean (zspread 0.2–0.7 % >2 mm, 0 flagged), all together 35.5 % >2 mm:
  pass 2 +4.0 mm, pass 3 −6.8 mm in depth over the whole overlap (red L in zs12_0). SDK global registration over all
  scans: unchanged. passalign (rigid point-to-plane ICP of a pass onto the earlier ones): 1059 s (nearest search far
  too slow), scan 2 moved 11 mm with residual 0.92 mm, scan 3 failed, spread unchanged (33.5 %) → the passes do not
  fit each other as rigid bodies although each is internally consistent (scale/shape difference between passes?
  pairfit showed a distance-dependent scale of ~0.5 %). Asked Gowtham for Studio's global registration + fusion of
  Trail12 as the decisive test (can Studio join them?).
- Studio's fused Trail12 (Trail12\Export\Trail_12_fusion.ply, 1.96 M vertices): zspread_ply 0.5 % >2 mm, meshcheck 0
  holes, 1 component, consistent → Studio JOINS the passes; the data is good, ArtecRec's placement is the fault.
  passalign v3 (3 mm grid cells, 15→6→2 mm caps): scan 2 moved 10 mm, scan 3 moved 21 mm (≈ one letter row — wrong
  repeat), residual 0.95 mm, joint zspread 35.5 → 19.4 % (not yet Studio's 0.5 %), scale 0.9998 (no scale issue).
  Ported as pa::refine in rec_sdk.h: after a PLACED scan's registration, rigid point-to-plane ICP of the scan against
  the other visible scans' 40-frame references, applied before the display build (build 16:20, staged). OPEN: verify
  in the field with the FS build; the desktop shortcut still launches artecrec.exe (team used it for Trail12).
- feattest on Trail12: image features find passes 2/3 at 113 / 193 mm from the stored poses (other repeats of the same
  letters) — the feature search is fooled by the repeating inscription as well. Owner 16:40: "go, install it and use
  option 1" = Studio's way: a new pass starts over the END of the previous pass; g_localResume (rec_sdk.h): only the
  3 tail windows of the latest visible pass are tried, a find > 150 mm from that tail is rejected, no feature search,
  no RANSAC tiers. Also: earlier passes drawn WHOLE while recording (cap 600 frames total) so gaps can be seen, as
  in Studio (owner's request). Built 16:45, staged; the install waits for the team to close the running copy (the
  auto-mode classifier refuses my kill of a running ArtecRec, which is right).
- 16:35–16:50, THE ROOT CAUSE OF THE JOIN FAILURES: the SDK's textured serial registration drops the probe frames
  ("[ALGO] Filtering frame #13 due mismatched image sizes with frame #12" / "MVRAlgorithm: Can't register surfaces
  with different texture image scales" — in the FS log since 10:46, 12× in the Trail12 session) and returns OK with
  the probe UNCHANGED, so hybridResume's "FOUND" was a no-op: the new pass stayed at the project origin (= start of
  pass 1). It "worked" only when the next pass began near the start of pass 1; the correct joins (Trail12, Trail13
  on screen) were made by the geometry auto-align after Stop (moves of 370–390 mm = origin → end of pass 1). My
  surface gate (0 % on the window) was right; I wrongly made it log-only at 16:3x. Fix (build 16:55): local resume =
  textured windows with the surface gate as a HARD check, then the SDK geometry auto-align of the probe against the
  latest pass's 40-frame reference, gated the same way; live view starts at the end of the previous pass (owner's
  request); pass refinement after Stop (58 s — too slow, to be sped up). Trail13 as saved holds only pass 1.
  OPEN: why the texture scales differ (probe frames vs stored frames — ConvertTextures half-size?) → would restore
  the textured join.
- 17:40–17:55 Trail14 (5 passes, FS build 16:55 install): every join by the local geometry auto-align in 1.3–2.0 s,
  surface 44–95 % within 6 mm, median 0.5–1.3 mm; pass refinement 2–28 s, moves 0.4–8 mm, residuals 0.15–0.24 mm.
  zspread of all 5 passes together: 0.8 % cells >2 mm (= letter edges), 0 of 444 frames flagged, within-frame 0.4 %.
  MULTI-PASS JOIN SOLVED on this stone with the "start over the end of the previous pass" rule. Owner was told the
  tracking-loss behaviour (discussion only): scan closes, new scan searches the end of the closed one, search
  frames discarded at the join (improvement: keep them).
- 18:00–18:40: Trail14 copied laptop → C:\Artec\stage (home LAN) → Arulalperumal (Tailscale 100.69.162.63, user Admin,
  lab4 key, per SBC) as D:\Trail14, verified 23 files / 1,137,002,617 bytes. Gowtham fused it there
  (D:\Trail14\Exports\Trail14_Fusion.ply, 3.1 M vertices): zspread_ply 0.4 % >2 mm, winding consistent, dihedral median
  4.3°; 391 holes (one real unscanned patch ~4×3 cm upper left, perimeter 413 mm; the rest tiny) and 393 small
  fragments — no small-object filter / hole filling applied yet. Staging copy C:\Artec\stage still to delete.
- 17:17 the team relaunched from the desktop shortcut (standard build, same joining code). 17:19 USB drop, scanner
  not back after 90 s → power-cycle instructions posted. Trail15 saved 17:20. The after-Stop auto-align moved a
  PLACED scan (scan 6, 24 cm) at 16:57 → fixed in rec_sdk.h runAutoAlign (placed scans are anchors; only unplaced
  scans move, expressed relative to the first placed scan's returned pose) — built 17:0x. 18:20 team left ("please
  close it yourself", Gowtham): closed via WM_CLOSE from an interactive scheduled task (CloseMainWindow from the ssh
  session cannot reach the desktop's windows), then both builds (579,584 bytes) copied into D:\ArtecRec, NOT launched.
  Tomorrow's first launch (desktop shortcut = artecrec.exe) runs the auto-align fix. Trail15 (3 scans) was saved.
- Second, separate defect: pass 1's first ~28 frames sit 2.5 mm off (a block in the zspread map) — a tracking error at
  the start of the pass; SDK fine+global on the cleaned frames made it WORSE (54 %), chainfix too (8 mm moves). Open.
- Also: Artec lists HP ZBook 17 G6 under its USB 3.1 issue (owner's post) — matches the scanner drops; saving over a
  folder Studio has open fails (error 5) and lands in <name>__saving — use a new name per save.
Test projects left in D:\ArtecDiag: t_auto3, t_auto7, t_auto_big (4.9 GB), t_auto_final (4.4 GB), t_auto_hour (partial, the save was interrupted), Trail7_glob, Trail7_feat, stone_feat, Trail8_glob, Trail10_glob, T10_* — mine, deletable.
Note for anyone editing artecrec.ini by hand: write it as ASCII/ANSI; a UTF-8/UTF-16 BOM makes Windows' ini reader
miss the [scan] section and every value falls back to its default.
Known behaviour: the first Record after launch sometimes stores only 1 frame (69 frames arrive, none registered)
and the scan is discarded; the next Record is fine. Memory: ~0.6 GB per recorded minute with textures (SDK frames
+ full-size textures; the live-view copies are capped at 150 frames).

## Open items (2 Nov 09:10)
- Verify the cloned/thinned position search finds the position in ~1–2 s and that the live view keeps moving.
- Verify the capture watchdog (and whether the 08:24 "scanner disconnected" event recurs; USB status was OK).
- Studio comparison still pending from the owner on wall_sdk_8+ (holes/smoothness after the frame-filter removal
  and fine registration); the owner's last verdict was "better, workable, not the same as Studio 17".
- Remaining known gaps vs Studio: live "smooth green" look (Studio shades the live frame differently), texture
  saturation (Studio's yellow is more saturated), 2.5–3 GB working set after several scans (live accumulation).
  Noted: capture failures normal up to ~10%; 0xC0030206 = LibInternalFailure (trigger-wait spam, transient);
  texture brightness in Studio terms = gain + shutter speed; Studio renders the same ArtecRec data as flat bright
  patches (verified on wall_sdk_4: 208 frames, error 0.3, texture 26 frames).

## Settings left in place
- Laptop `%APPDATA%\Artec\Artec Studio 17 Professional\studio.conf`: `AdaptivePacketSize=0`, `ForcePacketSizeSpider=4`,
  `FileLogLevel=1` (backup `studio.conf.bak-artecdiag`). Studio 15 (installed today by owner) untouched.
- Desktop `studio.conf`: `AdaptivePacketSize=0` (backup `studio.conf.bak-artecdiag`), `ForcePacketSizeSpider=0`.
- Laptop restored: HVCI Enabled=1, BIOS graphics Hybrid (owner), NVIDIA profile knobs reset to default (profile "Artec Studio"
  still exists with defaults; backup `D:\ArtecDiag\nvdrs-backup-114237`), Windows graphics preference entries removed/left for
  Studio 15 (High performance), calibration files restored (hashes verified).

## What is NOT the cause (all measured)
Scanner/cable, USB bandwidth itself (SDK probe 7.4 fps with reduced packets), CPU/GPU speed, thermal (laptop runs hot
80–94 °C but that only affects the slow slide), Memory integrity/VBS, BIOS graphics mode, Studio build (15.1.2.60 and
17.1.5.2 equal), NVIDIA driver/threaded optimization/vsync, Windows swap-effect, power states, affinity, AV/HP agents,
Fault Tolerant Heap, calibration files (2025 .acd/.corr), fps slider, GPU test.
False leads I reported during the day and withdrew: NVIDIA driver spin (it is the normal vblank wait), page-fault cost,
memory leak (Studio grows ~170 MB/min on both machines; harmless for fps).

## Options for the owner
1. Accept 5.8 fps steady on the laptop (current setting). No hunting, no slide.
2. A different USB host controller between laptop and scanner: Artec's "USB Kit" (the solution Artec sells for this
   exact case), or a Thunderbolt enclosure with a USB 3.0 card whose controller is not Intel xHCI 1.10. Verify with the
   probe/`ForcePacketSizeSpider=8` test before relying on it.
3. Use the desktop (or any PC with an older xHCI 1.0 controller) for full-rate scanning.
4. Separately: laptop cooling service (CPU zone 80–94 °C at light load; all-core load drops the clock to 2.7–3.3 GHz).

## Tooling
Laptop D:\ArtecDiag (probes, loggers, traces, scripts; nothing on C:). Desktop C:\Artec\sdk-fps (fpstest/fpspipe probes,
nvprof NVAPI tool, gltest, syscall/pagefault/st benchmarks, etlhist/hot/stacks.py trace analysers). Access via
ssh -i ~/.ssh/id_ed25519_lab4 shasa@192.168.1.41. Phone 192.168.1.39 paired for adb screen reads.
Test automation on the laptop: one-shot interactive scheduled tasks (ArtecLaunchOnce/ArtecStepOnce), all removed.
