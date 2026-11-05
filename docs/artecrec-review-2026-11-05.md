# ArtecRec code review, 5 Nov 2026 (afternoon)

FULL LIST (48 items, with status and notes, editable): https://claude.ai/artifact/DSWy46XH37LgxR6Zanmdas
This file holds the first 28; the page adds the remaining reviewer findings (A6-A9, B7-B10, C7-C11, D6, E3) and my
settings/screens review (F1-F9). The page is the record from now on.

Owner, 14:50: "this is not the right way to find out issues. Perform a review and surface issues", then "after that I want
you do a deep code review as well".

Method: three independent read-only reviews (concurrency and lifetimes; stuck states and SDK errors; data loss and
memory), then my own check of the top findings against the code and my own pass over the joining logic. Code at commit
"Time limits on the after-scan steps" (build staged, NOT installed; laptop runs build 12:11).

Key SDK facts (headers): `executeJob` has no timeout. A cancellation token only works if the job polls it; it cannot
interrupt a job stuck in a driver call. So every time limit added today (8 s Stop cancel, 60 s auto-align,
`execLimited`) is best-effort, with no second level of escape.

## A. Can destroy data outright (fix first)

A1. **Save can delete a whole folder.** `doSave` (artecrec.cpp:57-58) + `saveProject` (rec_sdk.h:918-936). If the folder
chosen in Save As ends with the project name (case-insensitive, no separator check), that folder becomes the project
folder. An existing folder is moved to `__old` and deleted with SHFileOperation, no Recycle Bin. Example: saving
"Belur" into `D:\Hoysala\Belur\` that already holds other projects and photos deletes all of them. VERIFIED.

A2. **Re-saving a project opened from Studio deletes everything that is not a scan.** `openProject` loads only
EntryType_Scan (rec_sdk.h:941); the folder swap then replaces the whole project folder. Fusions, textured models and
any other files in that folder are gone. VERIFIED (by reading; not run).

A3. **Save right after Stop can crash or save the wrong data.** `postProcessScan` posts WM_POSTDONE, then decrements
`g_postBusy` (rec_sdk.h:792); `waitPostProcess` exits without draining the queue. The pumped save (`runJobPumped`)
then runs `onPostDone`, which releases the old scan and clears `g_master` while the saver reads it. Outcomes: crash
(all unsaved scans lost), or the unregistered scan saved and `g_dirty=false`, so the refined pose is never saved and
no "save?" prompt on close. Ctrl+S right after Stop is the normal habit. VERIFIED.

A4. **No autosave.** Any crash or kill loses every scan since the last manual save. VERIFIED (none in code).

A5. **Non-English scan names write garbage into the .sproj.** `xmlEsc` (rec_sdk.h:898) uses `wcstombs` in the C
locale; it fails on Kannada and leaves an uninitialised buffer, written into the XML. The .sproj is also rewritten in
place ("wb"), so a crash there leaves a broken file. VERIFIED.

## B. Locks the window or traps the user (Task Manager needed, unsaved scans lost)

B1. **After Stop, the UI thread waits for the position search with no limit.** `onJobDone` joins `g_searchThread`
(rec_sdk.h:806). The search runs rough, fine and global registration (hybridResume) and a geometry auto-align, none
with a time limit or token. Global registration is the job that hung at 14:21. VERIFIED.

B2. **Every Stop with auto-align on freezes the window** while the scan is tidied (10-180 s): `runAutoAlign` ->
`waitPostProcess`, which handles only WM_POSTDONE (no paint, input, timer). Windows then offers "close the program".
VERIFIED. Same wait (unlimited) in Save, New/Clear and Duplicate.

B3. **Stuck Stop / stuck Aligning has no way out.** After the single cancel, if the SDK ignores it, the mode stays
Stopping/Aligning forever; Save needs Idle (artecrec.cpp:114); closing says "Stop the scan first" (artecrec.cpp:229).
Scans already in memory cannot be saved. VERIFIED (no fallback).

B4. **A scanner that stops sending frames after the first frame is never detected.** The watchdog only checks
`g_cbFrames == 0` since start (artecrec.cpp:169). VERIFIED.

B5. **Auto-restart after tracking loss restarts on a scanner that just disconnected**, and the reconnect only runs in
Idle; Stop pressed while an auto-restart is stopping is ignored and recording starts again; the 3-second restart has
no cap. (artecrec.cpp:41, 164-165, 192-194.) Reviewer-confirmed; I checked the restart path.

B6. **Exit can leave an invisible process holding the scanner** (joins without limit after the window closes,
artecrec.cpp:265). A relaunch then shows "No scanner".

## C. Crashes from two threads touching the same data

C1. **Workspace actions work in every mode**: Delete, Duplicate, Group, column sort, Del key (rec_ws.h:49-76, 99).
During auto-align the align thread holds indexes into the scan list (rec_sdk.h:844, 862-869): deleting crashes, sorting
moves the wrong scan. During recording the search keeps scan/frame indexes (hybridWindows) and reads them later
without a range check (rec_sdk.h:506, 509, 533). During save, Delete clears the model the saver reads. VERIFIED.

C2. **Sorting moves the scan records before taking the lock** (rec_ws.h:76) while post threads write into them.
VERIFIED.

C3. **The busy counter is raised inside the new post thread** (rec_sdk.h:753), not when it is started (794). Auto-align
or save started in that gap runs while `cleanScan` rewrites the same scan in place (the 10:55 crash class). VERIFIED.

C4. **The search keeps using the SDK's live frames after Stop** (g_tempScan/g_tempTex hold the procedure's own frames)
while the SDK post-processes textures. Plausible cause of crash at address 0. Not verifiable without SDK source.

C5. **`g_alignMsg` (a string) is written by the align thread while the UI reads it every 40 ms** (rec_sdk.h:876,
artecrec.cpp:75). `g_groups` is changed without the lock while background threads read it. Rare crashes. VERIFIED.

C6. **Pause/Resume places the wrong segment.** At Stop, the live-found pose (of the LAST segment) is applied to segment
0 (`i == 0`, rec_sdk.h:809, 814). VERIFIED.

## D. Joining logic (my own pass)

D1. **The join gate cannot see a sideways slide.** Both the texture join and the geometry fallback accept a join if the
probe surface lies within 6 mm of the previous pass (nearest point in 3D, `feat::Grid::check`). On a flat or gently
curved stone a pass slid along the surface by a letter width still lies on the surface and passes. The median is taken
over inliers only, so it is biased low. The texture registration itself guards against slides; the geometry fallback
(SDK auto-align, geometry only) does not.

D2. **Pass refinement has no sanity limit.** `pa::refine` (rec_sdk.h:735-740) applies whatever ICP returns: no limit on
the move (pass 7 moved 6.6 mm today) and no minimum share of matched points. Point-to-plane ICP can also slide along a
flat surface. The "residual" shown is over points already within 2 mm, so it always looks good (0.01 mm on a 10-frame
pass today) and is not a quality measure.

D3. **There is no Auto-align command.** IDM_AUTOALIGN only toggles the "Scan using auto-alignment" setting
(artecrec.cpp:122). My 14:4x message "press Auto-align later" points to nothing. VERIFIED. (The team's "Auto-align
after Stop" is the automatic one, gated by the Scan-panel checkbox.)

D4. **Already-aligned scans are not protected after reopening.** Only `placed` scans are anchors, and `placed` is not
saved. After reopening, an auto-align can move good scans.

D5. **Scan names in Studio are matched by order** (writeStudioNames and the groups sidecar). If Studio's or the SDK's
order differs, names and groups land on the wrong scans.

## E. Resource use over a field day

E1. About 3 MB per frame in the SDK, plus display copies (about 0.4-0.5 GB per scan in RAM and the same in GPU memory).
A 40-pass project is about 60 GB of scan data plus 15-20 GB of display; one project per stone is fine, one project per
day is not. No low-memory warning.
E2. Opening a project starts one post thread per scan at once (rec_sdk.h:951): about 2x the project in RAM at once.

## Checked, safe (by the reviewers and spot-checked)

Reference counting balanced in job/post/open/delete/duplicate; hybridResume and featureSearch hold their own scan
reference (11:55 fix holds); no lock-order inversion; no std::thread destroyed while joinable; g_live/g_liveAcc always
under g_mx; g_liveAcc bounded; the old project folder is kept until the new copy is complete (except A1/A2); Trail16
saved by ArtecRec opened and fused in Studio 17.

## Proposed fix order before 9 Nov

1. Save safety: A1 (always save into `<dir>\<name>\`, never swap a folder that holds anything but this project), A2
   (keep non-scan entries or refuse to overwrite a Studio project; save as a new name), A3 (drain WM_POSTDONE, save a
   snapshot), A5, then autosave after every pass (A4).
2. No UI-thread waits: B1, B2 (pump messages, cap the waits), B3 (after cancel + N s, abandon the job, go to Idle and
   allow Save), B4.
3. Mode guards on all Workspace actions (C1, C2), counter raised before the thread starts (C3), stop the search before
   the procedure finishes (C4), lock g_alignMsg/g_groups (C5), Pause/Resume placement (C6).
4. Join safety: refinement limits (D2), a texture or relief check in the gate (D1), add a real Auto-align command (D3),
   save `placed` (D4).
5. Restart rules: B5, B6.

Each step: build, bench test on the laptop when the team allows, install only on the team's or owner's go.
