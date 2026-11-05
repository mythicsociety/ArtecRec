# ArtecRec field test (laptop + Space Spider)

Do this test before the trip, on the laptop, with the scanner. It takes about 45 minutes.
Do it on a real stone or a carved test slab. Two people is best: one scans, one reads and ticks.

Write down the time (from the laptop clock) at step 1. You need it at the end.

For every step: do what it says, look at the screen, and tick **OK** if you see what is written under "You should see".
If you see something else, tick **PROBLEM** and write one line: what you saw, and the time.
Take a photo of the screen with your phone when there is a problem.

Never close ArtecRec with Task Manager during the test unless a step tells you to.
If the window freezes for more than 2 minutes, write the time, then close it with Task Manager, and tell the team.

---

## Part A - Start

**1. Start ArtecRec with the scanner NOT plugged in.**
Double-click the ArtecRec icon.
You should see: the window opens in a few seconds. The title bar shows "build" and a date. At the bottom it says
"No scanner - looking for it...". No error box.
[ ] OK  [ ] PROBLEM: ______________________

**2. Plug in the scanner.** First the power cable, then the USB cable. Wait 10 seconds.
You should see: the message at the bottom changes to "Ready". The Scan panel on the left shows "Artec Scanner" and a number.
(If it stays "No scanner" for 2 minutes: close ArtecRec, then start it again with the scanner plugged in. Write PROBLEM.)
[ ] OK  [ ] PROBLEM: ______________________

**3. Click "Preview".** Point the scanner at the stone, about 20-30 cm away.
You should see: the live surface of the stone in the middle of the screen, mostly green. The number "fps" on the left
is 7 or more.
[ ] OK  [ ] PROBLEM: ______________________

## Part B - Record

**4. Click "Record" (or press the Space bar). Move slowly along the stone for about 1 minute. Then click "Stop".**
You should see: while recording, "Recording in progress" and the "Surfaces recorded" number going up. After Stop, within
about 30 seconds, a new line "Spider Scan 1" in the Workspace on the right. The window does not freeze.
[ ] OK  [ ] PROBLEM: ______________________

**5. Record a second pass that overlaps the first one by about half. Stop after 1 minute.**
You should see: "Spider Scan 2" in the Workspace. The two passes sit together on the screen (no step or gap at the join).
[ ] OK  [ ] PROBLEM: ______________________

**6. Press Ctrl+S straight after Stop** (do not wait). If it asks for a name, go to step 10 first and come back.
You should see: "Saving project..." with a percent number, then "Ready". No error box.
[ ] OK  [ ] PROBLEM: ______________________

## Part C - Lose tracking on purpose

**7. Click "Record". After 20 seconds, turn the scanner quickly away from the stone (to the sky or the floor) for 5 seconds.
Then point it back at the place you were scanning and keep scanning slowly.**
You should see: a beep, "Searching for position..." or "Tracking lost". When you point back at the stone, recording goes on
by itself in a new scan, placed in the right spot. Then click "Stop".
[ ] OK  [ ] PROBLEM: ______________________

**8. Look at the Workspace.**
You should see: the scans from step 7 are in one group (for example "Group 1"). Nothing on the screen is far away from the
stone (no piece floating in the air).
[ ] OK  [ ] PROBLEM: ______________________

## Part D - Pull the USB cable while recording

**9. Click "Record". After 20 seconds, pull the scanner's USB cable out of the laptop. Wait 30 seconds. Do not click anything.**
You should see: within about 10 seconds the recording stops by itself. The bottom says "Scanner disconnected -
reconnecting...". The window still works (you can turn the 3D view with the mouse). The scans recorded before stay in the Workspace.
Now plug the USB cable back in and wait 15 seconds.
You should see: "Ready" again. Click "Preview": the live surface comes back.
(If "Preview" shows nothing after 1 minute: unplug power and USB, wait 10 seconds, plug power, then USB. Write PROBLEM.)
Click "Stop".
[ ] OK  [ ] PROBLEM: ______________________

## Part E - Save into a new folder

**10. Click File > "Save project as...". Go to the Desktop. Type the name `Field Test 10.11` and click Save.**
You should see: "Saving project..." with a percent, then "Ready". The title or status shows the project is saved
(no "project modified").
[ ] OK  [ ] PROBLEM: ______________________

**11. Open the Desktop in Windows Explorer.**
You should see: a NEW folder called `Field Test 10.11` (full name, with the ".11" at the end). Inside: `Field Test 10.11.sproj`
and a folder `Scans`.
[ ] OK  [ ] PROBLEM: ______________________

**12. Safety check: saving must never delete other files.** On the Desktop make a folder `Belur`. Put any photo inside it.
In ArtecRec click File > "Save project as...", go INTO the folder `Belur`, type `Belur` and click Save.
You should see: either the project is saved in a new folder `Belur\Belur`, or a message says it cannot save there.
Then open the `Belur` folder in Explorer: **the photo must still be there.**
[ ] OK  [ ] PROBLEM: ______________________

**13. Give one scan a Kannada name.** In the Workspace click a scan, press F2, type a Kannada name (for example ಬೇಲೂರು 1)
and press Enter. Press Ctrl+S.
You should see: the name stays in Kannada in the Workspace. Saving ends with "Ready".
[ ] OK  [ ] PROBLEM: ______________________

## Part F - Close and open again

**14. Click File > Exit.**
You should see: ArtecRec closes within 30 seconds. It does not ask "Save the project first?" (you saved in step 13).
Open Task Manager: there is no "artecrec" left in the list.
[ ] OK  [ ] PROBLEM: ______________________

**15. Start ArtecRec again. Click File > "Open project...". Open `Desktop\Field Test 10.11\Field Test 10.11.sproj`.**
You should see: "Opening project..." then all your scans in the Workspace, with the same names and groups as before
(the Kannada name too). The stone looks the same as before.
[ ] OK  [ ] PROBLEM: ______________________

**16. Click File > Exit.**
You should see: it closes within 30 seconds.
[ ] OK  [ ] PROBLEM: ______________________

## Part G - Check in Artec Studio 17

**17. Start Artec Studio 17. Open `Desktop\Field Test 10.11\Field Test 10.11.sproj`.**
You should see: the project opens with no error. The Workspace shows all the scans, with the same names (and the Kannada
name readable, not boxes or question marks).
[ ] OK  [ ] PROBLEM: ______________________

**18. In Studio, select all scans and run Fusion (any type).**
You should see: a fused model of the stone with no doubled letters and no steps at the joins between passes.
Close Studio WITHOUT saving.
[ ] OK  [ ] PROBLEM: ______________________

## Part H - Check the log (for the team member with the computer)

**19. Copy the log file `artecrec.log` from the ArtecRec folder on the laptop (the folder of `artecrec.exe`) to a USB stick.**
Then run, on any Windows computer (replace the time with the time from step 1):

    powershell -ExecutionPolicy Bypass -File C:\Artec\tests\e2e\verify-log.ps1 -Log E:\artecrec.log -Since "10:30:00"

You should see: the last line says `0 failed`. WARN lines are allowed but read them.
Send the log file and the result to the team even if everything is OK.
[ ] OK  [ ] PROBLEM: ______________________

---

Tester names: __________________  Date: __________  Laptop: __________  ArtecRec build (title bar): __________________

Delete the test folders `Field Test 10.11` and `Belur` from the Desktop when the team has the results.
