# Security

## Reporting a vulnerability

Please **do not open a public issue**. Use GitHub's private report instead:
[Report a vulnerability](https://github.com/imacnop/Wireless-Device-Battery-Monitor/security/advisories/new).
Say what is affected, how to reproduce it, and which version you run (menu > *About*, or the setup file name).

## How updates are protected

BatteryHub updates itself from this repository's releases, but it only installs a setup whose SHA-256
is listed in a `SHA256SUMS.txt` signed with this project's release key. The key and its password never
go to GitHub: someone who took over the account or the build workflow could publish files, but not a
signature the app accepts. See [docs/releasing.md](docs/releasing.md).

## What the app does not do

Drivers only send read-only battery queries to devices they recognise. The app does not send device or
battery data anywhere. It goes online only for the update check (GitHub's public API) and, when you
click *Send*, for a device support request.
