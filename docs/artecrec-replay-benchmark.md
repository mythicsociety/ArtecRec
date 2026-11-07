# ArtecRec replay benchmark — 7 Nov 2026

Owner, 6 Nov: "is there a way to simulate scanning without a human?" / "we have hundreds of scans! can they be
used?" / "I am not sure all passes in those projects are already placed right. How are you ensuring they were?" /
"the finished models for these projects exist under finished models folder".

## What it is

`artecrec.exe --replay-bench <out.csv> [--replay-from-mid] <project.sproj>` replays every scan of a project as a new
pass against the other scans (the scan hidden meanwhile). The frames go into the same observer the scanner calls, at
5.7 fps, starting at the origin as the SDK does. Everything after the camera is the real code: live position search,
Stop, tidy-up, anchors, auto-align, autosave. `--replay-script <file>` runs a scripted session (record / lose
tracking / stop / expect); `tests/e2e/replay-field-bugs.txt` reproduces the 6 Nov anchor and stub bugs.

Per run (one CSV line): live join (yes/no, tries, seconds), error at Stop and at the end against the saved position,
whether the other scans moved, the saved position's own consistency with the other passes (truthMovedMm, truthShare),
and the share of the pass's points within 2 mm of the project's finished model (modelFit, truthModelFit).

Verdicts: JOINED_OK / JOINED_WRONG / ALIGNED_OK / ALIGNED_WRONG / NOT_PLACED (within 5 mm of the saved position),
`?TRUTH` when the saved position itself is doubtful (would move > 2 mm or < 50 % on the other passes),
NO_OVERLAP_SAVED when the pass overlaps nothing as saved, `+MODEL_OK/OFF` from the finished model, `+ANCHOR_MOVED`.

## Sources

- RawData projects (`\\192.168.0.18\AllProjectData\3D_data\RawData`, 540 projects, 4.2 TB): the field saves. 33 small
  ones were tried first: of 29 passes, 24 had unreliable saved positions and 1 no overlap. Not usable as the answer.
- FinishedModels projects (`...\3D_data\FinishedModels\<district>\<hobli>\<place>\<project>`): the Studio project
  (registered, aligned scans in `Scans`) plus the fused model in `Exports`. These are the corpus. Copied with
  `tests/replay/finished-copy.ps1` to `D:\ArtecReplay\finished\<name>\` (project file, Scans, Model\<smallest stl/ply>).

## Results so far (mid-pass resume, 7 Nov 05:40-06:25, 5 finished projects)

| Project | Pass | Frames | Live join | Final error | On the model | Saved pos. check | Verdict |
|---|---|---|---|---|---|---|---|
| Chikkajala Garudakamba | 1 | 726 | no (1 try) | 1.3 mm | 96 % | ok | ALIGNED_OK + MODEL_OK |
| Chikkajala Garudakamba | 2 | 322 | yes, try 1, 3.3 s | 3.1 mm | 99 % | ok | JOINED_OK + MODEL_OK |
| Chikkajala temple basement | 1 | 458 | yes, try 1, 21 s | 4.4 mm | 74 % (saved: 95 %) | overlap 11 % | JOINED_OK?TRUTH + MODEL_OFF |
| Chikkajala temple basement | 2 | 543 | no | 7.9 mm | 67 % (saved: 98 %) | overlap 15 % | ALIGNED_WRONG?TRUTH + MODEL_OFF |
| Aradeshamahalli Dv41 | 1 | 724 | - | - | - | no overlap as saved | NO_OVERLAP_SAVED |
| Maranayakanahalli 2 | 1 | 214 | no | 2.3 mm | 98 % | ok | ALIGNED_OK + MODEL_OK |
| Maranayakanahalli 2 | 2 | 608 | no | 1.8 mm | 100 % | ok | ALIGNED_OK + MODEL_OK |
| Maranayakanahalli 2 | 3 | 112 | no | 1.1 mm | 100 % | ok | ALIGNED_OK + MODEL_OK |
| Aradeshanahalli Dv43 | 3 | 1120 | - | - | - | no overlap as saved | NO_OVERLAP_SAVED |

Counts: 5 placed correctly (1 live join, 4 auto-align), 2 a few mm off where the pass overlaps the others by only
11-15 %, 2 passes that overlap nothing as saved. No anchor moved. Model frame check: 92 % (Garudakamba).

Reading: with little overlap the join or auto-align lands 4-8 mm off while still looking plausible on screen. Field
rule stays: resume over surface already scanned, with generous overlap. Live joins from a mid-pass start are rare
(1 of 7 tried): the first frames of a resume are often over fresh surface.

## A slid join, and a second opinion that catches it (7 Nov 08:30)

Kalya Byrava, pass 1 (102 frames, overlap start, reference sound: 95 % on the other passes, 0.01 mm): the live
geometry join reported 100 % within 6 mm, median 0.6 mm; the refinement 100 % within 2 mm, residual 0.08 mm. The pass
is 10 mm from its true place (placement fit on the model 50 % vs 91 % saved). Reproduced 3 of 3 runs. A repeating
relief lets a geometry join slide while every internal check reads "perfect".

Second opinion = the SDK's auto-align of the new pass against the others from scratch, after Stop: 0.3 mm from the
saved position, 9.9 mm from the live placement. A "live join vs auto-align disagree by > 3 mm" check would have caught
this one. Columns `secondErrMm`, `secondVsLiveMm` now record it for every live-joined pass; the corpus decides the rule.

## Two things the replay cannot judge

- **Re-optimisation of the frames.** The replayed frames are already registered by Studio. ArtecRec's surface
  optimisation then re-registers them, and in replay that goes either way (Garudakamba pass 2: 86 % -> 95 % on the
  model; temple basement pass 1: 95 % -> 74 %), with SDK warnings about texture quality. The verdict therefore uses
  `modelFitRigid` (the source frames under the new pass's placement). Whether the optimisation helps raw field frames
  is a field question.
- **The texture join.** Replayed reference and probe frames share one texture size, so the field failure "can't
  register surfaces with different texture image scales" (113 of 201 searches on 6 Nov) never shows in replay. The
  `hybrid resume textures:` log line (reference vs probe image size) must come from a field session.

## How to run

1. Copy projects: `tests/replay/finished-copy.ps1 -List <list of relative .sproj paths>` (NAS read-only; needs the
   NAS open in Explorer on the hub; the lab PCs get "Access denied").
2. Benchmark one: `tests/replay/bench-one.ps1 -Folder D:\ArtecReplay\finished\<name>` (appends to
   `D:\ArtecReplay\results\bench-finished.csv`, copies the log to `log-fin-<name>.txt`).
3. Compare the verdict counts with the previous run before installing a search/align change on the laptop.
