# Artec lane — moved here on 2026-08-12

**Work in `C:\Artec`. Never in `D:\Artec`.**

D: was wiped on 19 July 2026 and is frozen. No lane writes there. `D:\Artec` still exists but
holds one empty `samples` folder dated 19 July — the day of the wipe. **The work is gone. Only
the knowledge survived.**

## What was carried across

Four memory files, copied from the old project key and verified by SHA-256:

    artec-pottery-environment.md      the workstation setup, tools, versions, shell traps
    pottery-automation-domain.md      what this lane owns: sherd scan-to-drawing automation
    pottery-pipeline-architecture.md  Artec SDK coverage, the gaps, the Blender stage
    MEMORY.md                         the index

They now live under the `C--Artec` project key, so a Claude started **in this folder** loads them.

## What did NOT come across, and why

**The 29 July conversation.** Claude offers `--resume` only for sessions belonging to the folder
you start in. Keeping the conversation would have meant starting in `D:\Artec`, which is the one
thing you must not do.

**A readable copy is here anyway:** `OLD-SESSION-2026-07-29.jsonl`, 5.31 MB. It is the transcript,
not a resumable session. Read it if the memory files leave a gap.

## Starting the lane

    cd C:\Artec
    claude

**Do not use `--resume`.** There is no session under this key yet. The memory files are the
starting point.

## First thing the lane should do

Write its own `permissions.deny` rules and ask the owner to run them. Every prohibition kept as
prose gets forgotten; a deny rule stops the tool call. `C:\Users\Uday\ClaudeFleet\apply-deny-rules.ps1`
is the shape to copy — it blocks D: in both slash spellings, both drive-letter cases.
