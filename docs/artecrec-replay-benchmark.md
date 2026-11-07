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

## Results so far (mid-pass resume)

| Project | Pass | Frames | Live join | Final error | On the model | Anchors moved | Verdict |
|---|---|---|---|---|---|---|---|
| Chikkajala Garudakamba | 1 | 726 | no (1 try) | 1.3 mm | 96 % | 0 | ALIGNED_OK + MODEL_OK |
| Chikkajala Garudakamba | 2 | 322 | yes, try 1, 3.3 s | 3.1 mm | 99 % | 0 | JOINED_OK + MODEL_OK |

Model frame check: 92 % of the saved passes within 2 mm of the model.

## How to run

1. Copy projects: `tests/replay/finished-copy.ps1 -List <list of relative .sproj paths>` (NAS read-only; needs the
   NAS open in Explorer on the hub; the lab PCs get "Access denied").
2. Benchmark one: `tests/replay/bench-one.ps1 -Folder D:\ArtecReplay\finished\<name>` (appends to
   `D:\ArtecReplay\results\bench-finished.csv`, copies the log to `log-fin-<name>.txt`).
3. Compare the verdict counts with the previous run before installing a search/align change on the laptop.
