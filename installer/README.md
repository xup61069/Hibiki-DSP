# HibikiSetup

The planned open-source bootstrapper will install the user-space app and
virtual-driver package transactionally, preserve Scene/Calibration data, and
rollback to the last-known-good package. The project does not require HLK or any
code signing; source remains public and users rebuild from source.

`HibikiSetup.ps1` is the public bootstrapper source. It accepts an externally
delivered package plus `ReleaseManifest v1`, verifies source/toolchain/dependency/
SBOM metadata and package hashes, defaults to a non-mutating dry-run, and
requires explicit `-Apply` plus administrator rights before staging an INF with
`pnputil.exe`. The manifest records content hashes only; no package, certificate
or credential is stored here.

The same bootstrapper supports `-Uninstall` for the verified user-space payload.
It validates the package manifest and destination before touching anything, removes
only manifest-declared files inside the destination, backs up every removal to a
temporary directory, restores prior state on failure, and preserves existing
`%LocalAppData%/Hibiki DSP` Scene and route-rule files. Dry-run reports the planned
removals and preserved-data boundaries without deleting anything. This is a
source-level capability only; it does not claim install, load, runtime audio,
driver, or physical-audio evidence.

## User-space payload staging (source-level capability)

After full manifest verification, `-Apply` copies each manifest-declared
`unsigned_files` entry into an explicit destination directory (default:
`%ProgramFiles%\Hibiki DSP`). The destination path is validated fail-closed
before any mutation: it must be absolute, canonical, not blank, not a drive root,
must not contain `.` or `..` segments, and must not be inside the Windows
directory.

Each copy is staged to a temporary file inside the target directory, verified
by SHA-256 against the manifest hash, then atomically moved into place.
Existing files that would be overwritten are first moved into a per-run backup
directory inside the destination. If any step fails, completed copies are
removed in reverse order, backed-up originals are restored, newly created
empty directories are cleaned up, and the error is re-thrown. On success the
backup directory is removed. Files under `%LocalAppData%/Hibiki DSP`
(including `session-route-rules-v1.json` and `scene-cards-v1.json`) are never
touched by this staging path.

A dry run reports the resolved destination, planned payload files and preserved
user-data boundaries without creating directories or files.

## Evidence scope

This document describes source-level design and offline verification only.
It does not claim install, load, runtime audio, driver, or physical-audio
evidence. Running `-Apply` changes machine state and
requires administrator privileges on a locked test machine.

## Source-tag manifests vs Package manifests

Official Git source tags carry text-only SourceReleaseManifest v1 metadata
(see SPEC-0025 / ADR-0009) that verifies source blob content directly from Git.
HibikiSetup.ps1 processes externally supplied ReleaseManifest v1 packages
when an installer or driver package is delivered outside GitHub.
