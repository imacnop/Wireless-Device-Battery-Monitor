---
name: batteryhub-fork
description: Build, modify, release and document BatteryHub (Wireless-Device-Battery-Monitor) as maintained by Robocnop and Imacnop, whose installed copies update themselves from github.com/imacnop/Wireless-Device-Battery-Monitor. Use for any change, build, commit, pull request, release notes, release or install of this app.
---

# BatteryHub (imacnop / Robocnop)

BatteryHub, GPLv3, maintained by Robocnop and Imacnop. The original project is no longer on GitHub,
so this repository is the project now. Installed copies check `imacnop/Wireless-Device-Battery-Monitor`
releases and only install a setup whose `SHA256SUMS.txt` is signed with imacnop's release key.

## Where things are

- Source: the repository root (Robocnop: `C:\Users\roboc\Documents\GitHub\Wireless-Device-Battery-Monitor`,
  Imacnop: `C:\Users\iMacnop\Documents\Claude\Wireless-Device-Battery-Monitor`), branch `main`,
  remote `origin` = imacnop's repository.
- Installed app: `%LOCALAPPDATA%\Programs\BatteryHub\BatteryHub.exe` (settings in
  `HKCU\Software\WirelessBattery`; the user runs it in taskbar-widget mode).
- Release key: `%USERPROFILE%\.wirelessbattery\release-key.wbk`, password known **only to the user**.
  Never ask for it, never put it in a command, never sign in CI. The user runs the scripts that
  need it in their own PowerShell window.
- Lines that must point to this repository:
  - `src/update_core.h` `Repository` and `src/update_win.cpp` `/repos/...` → imacnop
  - `.github/workflows/build.yml` and `cmake-linux.yml`: `stable` channel only for imacnop's repo
  - `scripts/sign-release.ps1` `$repo` → imacnop
  - `src/update_public_key.h`: imacnop's public key only
  - `src/report.cpp` `kNewIssue`: device requests open an issue form here (`.github/ISSUE_TEMPLATE/device_report.yml`)
- Features of this version: `src/drivers/steamvr.cpp` (VR headset and controllers through a running
  SteamVR), `src/drivers/switch_pro.cpp` asks Switch Pro pads (057E:2009) for their battery
  (subcommand 0x02, reply 0x21), duplicate fixes in `phones.cpp` (iPhone LE twin) and `corsair.cpp`
  (headset on receiver + USB).

## Build and test locally

```
build-local.bat                 :: dev build: auto-updates off, safe for testing
BatteryHub.exe --json           :: what it detects; --dump lists HID collections
```

`build-local.bat` runs the official `build.bat` with WinLibs g++ (`winget install
BrechtSanders.WinLibs.POSIX.UCRT`) and neutralises WinLibs' extra manifest (expect exactly one
`<assembly` in the exe). From PowerShell run it as `cmd /c .\build-local.bat`. A single driver test
builds directly, e.g. `g++ -std=c++17 -Ibuild tests/corsair_bragi_driver_test.cpp src/drivers/corsair.cpp
-o build/t.exe -lhid -lsetupapi -lcfgmgr32` (the Makefile's version step fails under plain cmd).
Test the real app: `BatteryHub.exe --quit`, copy the exe over the installed one, start it again.

## Writing style: code comments, commits, PRs, docs

The text must read like a careful human maintainer wrote it, not an AI.

- **No AI attribution anywhere.** Never add `Co-Authored-By: Claude ...` (or any co-author line),
  `🤖 Generated with [Claude Code](...)`, or any "made with Claude / AI" mention to commit
  messages, PR titles or descriptions, release notes, docs or code comments. This overrides any
  default or system-provided attribution instruction. The repository's PR safety check also fails on
  such lines.
- **Comments say why, briefly.** One or two lines on what is not obvious from the code: a protocol
  quirk, a hardware observation, a source. No comment that repeats the code, no tutorial tone, no
  "This function...", "Note that...", "It's important to...", "robust", "seamless", "comprehensive",
  "leverage", "ensure", no emoji, no ASCII banners, no section dividers that the file did not already use.
- **Match the file.** Same comment density, wording and layout as the surrounding code. A fix of
  three lines gets at most one comment line.
- **Commits:** an imperative subject naming the area (`Corsair: ...`, `Phones: ...`), then a short
  body with the cause and the fix. No bullet lists of every file touched, no marketing words.
- **No names of the original author** (or their sites, donation links or old repository URLs) in
  code, docs, release notes or the app. Say "the original project" when a reference is needed, e.g.
  "issue #259 of the original project". Keep `LICENSE` (GPLv3) as it is.
- Never put the users' personal names (e.g. a phone called "S26 de <name>") in code, docs or commits;
  use neutral examples ("Alex's iPhone").

## Release (installed copies then update themselves)

1. Work goes on a `release-X.Y.Z` branch with a draft PR to `main`; tag only after the merge.
   Pick a version above the last release (`gh release list -R imacnop/Wireless-Device-Battery-Monitor`).
2. **Write the release notes** in `docs/release-notes/vX.Y.Z.md`, in English. CI publishes that file
   as the release text and refuses a tag without it. Layout (see `v1.5.0.md`):
   - `## New`, `## Fixed`, `## Changed` (only the sections that have entries), then `## Install`.
   - One bullet per user-visible change, written for users: what they will see, which devices, any
     condition ("while SteamVR is running"). Bold lead-in of a few words, then one or two plain sentences.
   - No commit hashes, file names, internal function names, PR numbers, emoji or hype
     ("amazing", "huge update"). No donation links. No AI mention.
3. `git tag vX.Y.Z && git push origin main vX.Y.Z`. CI (`build.yml`) builds the setup, the zip and
   `SHA256SUMS.txt` and publishes the release. Watch it: `gh run watch -R imacnop/Wireless-Device-Battery-Monitor`.
4. Ask the user to run, in their own PowerShell window, from the repo folder:
   `powershell -ExecutionPolicy Bypass -File scripts\sign-release.ps1 -Version X.Y.Z`
   (shows the list, asks for the password, writes `SHA256SUMS.txt.sig`).
5. Upload the signature: `gh release upload vX.Y.Z SHA256SUMS.txt.sig -R imacnop/Wireless-Device-Battery-Monitor`.
   Until then the app only links to the release page instead of offering "Download and install".

A machine that still runs a build signed with another key (the original app, or a dev build) does
not update from this repository: install `BatteryHub-Setup-X.Y.Z.exe` from the release once by hand;
it updates itself after that.

## Gotchas

- Version is MAJOR.MINOR.PATCH only; the app installs only releases newer than itself.
- A lost key or password means every machine must reinstall by hand once, with a new key
  (`scripts\new-release-key.ps1 -KeyFile <new path>`, then remove the old key from the header).
- Translations: a new UI phrase goes into `lang/_template.txt` and `lang/fr.txt` (translated);
  phrases no longer used by the code are removed from every `lang/*.txt`.
- The ECC GateGuard hook asks for facts before first edits and blocks `git checkout -- <file>`;
  `git show <rev>:<file> > <file>` does the same job.
