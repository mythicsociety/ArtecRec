# ArtecRec by Mythic Society — Space Spider scanning on the laptop (Pervonasetti)

Artec Studio's scanning screen rebuilt on the Artec 3D Scanning SDK 2.0, scanning only. Built 1–2 Nov 2026 because
Studio's recording fps slides below 5 within ~15 minutes on this laptop; the SDK pipeline holds 5.7 fps indefinitely.
Processing stays in Artec Studio 17. Behaviour follows the Artec Studio 17 manual; where Studio and ArtecRec differ,
Studio is the spec.

## Studio behaviours reproduced (checked against the manual, 2 Nov 2026)
- Scanner-body buttons: Record button = Preview, then Record, then toggles Record/Pause; Stop button = Stop.
- Preview hides the project's scans; Record shows the marked ones. Space toggles Preview/Record.
- Lost tracking: with "Scan using auto-alignment" (Settings menu, on by default) the view turns to the green
  "Searching for position…" and recording resumes in a new scan, aligned and grouped with the previous one; with it
  off, the red "Tracking lost: repeat scan using slower motions…". Audio warning (Settings menu). Background changes
  colour while misaligned.
- "Auto-align new scans with those marked in Workspace": Preview, aim at a captured textured region, Record →
  "Searching for position" → recording starts once found, already aligned.
- Range meter: grey = registered key frames, dark green = last registered frame, red = registration error.
- View: rendering solid / wireframe / points / wireframe over solid / points and solid (default for Spider: scans as
  points), smooth/flat shading, backface show/cull/black/transparent, scan colour / surface colour, rotation centre
  marker (double-click; Edit → Cursor → mass centre / origin), PNG screenshots with white/black background.
- Workspace: columns eye / Name / Color / Type / Error / Frames / Scanner / Texture frames; header click sorts;
  Ctrl+A, Ctrl+D, Space, Ctrl+Alt+A, Ctrl+Alt+click; F2, Del, Ctrl+G; Duplicate, Show, Hide, Change color.
- Texture frames: "Automatic" records every 8th frame (Studio: "not every frame"); "One per N" as set.
- Scans render as lit, textured surfaces (View → Color → Texture, the default); textured frames are drawn over the
  untextured ones. Scan colour / surface colour modes as in Studio.
- Lost tracking as in Studio: unregistered frames are never kept; a real loss ends the scan at that frame and,
  with "Scan using auto-alignment" on, recording resumes in a new scan via "Searching for position".
- Full-screen scanning (owner's request, on by default — "Hide panels during scanning"): when Record starts, both side
  panels, the log strip and the status bar hide; the view fills the window with an fps/surfaces/time bar and
  Pause / Stop buttons at the bottom. Everything returns when the scan stops. Space and the scanner buttons still work.

## Where
- Laptop: `D:\ArtecRec\artecrec.exe` (desktop shortcut "ArtecRec"). Log: `D:\ArtecRec\artecrec.log`.
- Source: `C:\Artec\sdk-fps\artecrec.cpp` + `rec_sdk.h`, `rec_view.h`, `rec_panel.h`, `rec_ws.h`, `flyhook.h`; build with `build-rec.bat`.

## Screen (as in Studio 17)
- **Scan panel (left, F7):** Preview / Record (becomes Pause while recording) / Stop; Space toggles Preview–Record.
  Features to track: Geometry + Texture, Geometry. Show distance color. Auto-align new scans with those marked in
  Workspace. Scanning speed 1–8 fps. Advanced: hide panel during scanning, depth of field near/far, scan name,
  sensitivity, texture brightness, auto-adjust brightness, scanner in use, don't record texture, disable flash bulb,
  frequency of texture frames (automatic / one per N), delay before recording, limit frames per scan.
  Greyed out because the SDK has no such function: Targets, automatic base removal, real-time fusion, motion tolerance.
- **3D view:** left drag = rotate, middle drag or left+right = pan, right drag = zoom, wheel = zoom (option: to cursor),
  double-click = set rotation centre. F fit, H home, G grid, L lighting, 5 orthogonal. Standard views
  Ctrl+Shift+1/3/7 (front/left/top), Ctrl+1/3/7 (back/right/bottom) or the buttons at the top. Render points / solid /
  both (View menu or right-click). Range meter at the left (far at the top, near at the bottom, as in Studio).
  Live frame coloured by distance (red too close, green good, blue too far); grey = key frames already captured;
  red frame + banner + beep = tracking lost. Ctrl+Shift+S saves a screenshot.
- **Workspace (right, F11):** scans and groups with eye ticks, frames, max error, "tex" flag. F2 rename, Del delete,
  Ctrl+G groups the ticked ungrouped scans, right-click for show/hide/invert/ungroup/colour. Properties below.
- **File:** New / Open / Save / Save as (Ctrl+N / O / S). Status bar: free RAM, memory in use, state. Log strip (Ctrl+Alt+L).

## Field routine
1. Plug the Space Spider in (power + USB), start ArtecRec. Status says "Click Preview or Record to begin".
2. Record (or Space). Keep the histogram in the green band. Pause/Resume as needed (each pause starts a new scan).
3. Stop when the side is done. Repeat Record for the next side; tick "Auto-align…" to align sides automatically.
4. Ctrl+S → name the project, e.g. `D:\Scans\stone42.sproj`; ArtecRec creates `D:\Scans\stone42\` with `stone42.sproj`,
   `Scans\` and `artecrec-groups.txt`. Then File → New for the next stone (or keep adding scans).
5. Artec Studio 17: File → Open project → the `.sproj`; accept the one-click conversion to .a3d; process as usual.

## Limits
- 5.7–5.8 fps on this laptop (USB controller limit, packet 960 B); it does not fall over time.
- Memory grows ~100–150 MB per recorded minute; the laptop has 128 GB. Save and start a new project between stones.
- Groups and names live in `artecrec-groups.txt` beside the project (the .sproj format has no groups). Studio 17
  ignores the scan names in a converted project and shows "Scan 0, Scan 1…" in the saved order; the group prefix is
  written into the .sproj for other readers, but Studio does not display it.
- The viewer draws textures unlit at full brightness (mild gamma 1/1.3), frames as the SDK reconstructed them, later
  frames on top of earlier ones, untextured frames underneath in the scan's own tone — all as Studio's scan view does;
  the saved data is untouched. While scanning the view follows the scanner (press H to re-attach after rotating).
- After Stop every scan gets Studio's "surface optimization": the SDK's fine registration runs in the background
  ("Optimising surfaces…" in the status bar, a few seconds); save/align wait for it.
- Saving over an existing project writes a temporary `<name>__saving` folder, then swaps it in.
- If the scanner sends no frames for 6 s, ArtecRec restarts the capture (twice), then asks you to reconnect the scanner.
- Scan-panel settings are remembered between sessions (artecrec.ini next to the exe). The mouse wheel over the
  panel scrolls it. The full-screen bar shows fps in large type, with Pause/Stop at the bottom right.
- Lost tracking: recording ends, a new scan starts at once, the beep continues until the position is found, and
  the new scan is grouped with the previous one. Pause/Resume scans are grouped too.
- Big stones: scan in sections of 10–15 minutes each (one scan per section), save every few scans, one project per
  stone. Memory ~0.6 GB and disk ~0.3 GB per recorded minute; a one-hour stone is ~20 GB and saves in ~20 minutes.
  While recording, the other scans are drawn with fewer frames so the capture rate stays at 5.7 fps.
- Big projects: closing a 15-minute scan takes ~30 s ("Stopping…"), its optimisation ~2 min (background), saving
  ~5 min for 5 GB and opening ~1 min — the status bar shows the percentage and the window stays alive.
- SDK in use: Artec 3D Scanning SDK 2.0.0.56 (Dec 2017), the current public release.
- No scanner temperature readout (the SDK does not expose it). Never force-kill ArtecRec while recording — press Stop
  first (a killed capture can wedge the cameras until the scanner is power-cycled).
