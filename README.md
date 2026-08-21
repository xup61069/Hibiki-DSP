# Hibiki DSP

Hibiki DSP is a Windows Audio Scene OS: a system-level, multi-lane DSP hub
for games, films, DAWs, browsers, headphones, speakers and multichannel
setups. The long-term product combines virtual endpoints, Hibiki ASIO,
full-duplex routing, calibration, ISO 226-derived equal-loudness control,
VST3 isolation and safe device switching.

This repository is the public source of truth. It is currently a development
foundation, not a ready-to-install consumer release.

## Quick start

1. Clone this repository on Windows 11 24H2 or newer, x64.
2. Read `AGENTS.md` and `docs/START_HERE.md`.
3. Run `pwsh -File tools/doctor.ps1 -CheckOnly`.
4. Run `pwsh -File tools/verify.ps1`.

The verification command builds only an unsigned local development tree. The
public GitHub repository never publishes compiled binaries. Official signed
installers are distributed separately through the Hibiki DSP Gumroad product;
the application itself has no activation or runtime license check.

## Current state

The repository now includes testable user-space graph/routing, volume/ISO formula
compensation, IPC, ASIO stream, VST supervisor exchange/lane quarantine, bounded
Scene state binding/preflight, per-application routing rules, output sink and browser-capture prototypes. Public
CI also enforces the source-only publication rule and refuses tracked binaries or
release/package upload steps. The WaveRT-backed driver, physical endpoint soak,
third-party VST3 certification, calibrated ISO data, WinUI SDK validation and
signed distribution pipeline remain staged behind their corresponding specs and
ADR gates.

## Architecture

Read `docs/PROJECT_MAP.md` for the subsystem map. Product behavior belongs in
accepted specs; architectural rationale belongs in `docs/adr/`; merged
capabilities are recorded in `docs/state/BASELINE.md`.

## Licensing

Hibiki user-space is GPL-3.0-only. The SYSVAD-derived driver is MS-PL, public
schemas/SDK are Apache-2.0, and documentation is CC-BY-4.0. See
`LICENSES/README.md` and `THIRD_PARTY.yml`.

## Contributing

See `CONTRIBUTING.md`, `SECURITY.md` and `SOURCE_POLICY.md`. Never commit
private calibration data, endpoint identifiers, signing keys, customer data,
ISO standard content or compiled artifacts.
