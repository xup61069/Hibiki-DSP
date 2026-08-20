# Hibiki DSP license map

The repository is intentionally component-licensed. Do not replace this map
with a blanket "everything is GPL" statement.

| Component | SPDX | Notes |
| --- | --- | --- |
| `src/`, `apps/`, `asio/`, `vst-host/`, `extensions/`, `installer/` | GPL-3.0-only | Hibiki user-space source |
| `driver/` | MS-PL | SYSVAD-derived driver boundary |
| `sdk/`, `schemas/` | Apache-2.0 | Public contracts and SDK material |
| `docs/` | CC-BY-4.0 | Human and AI documentation |
| `tests/fixtures/` | CC0-1.0 | Original synthetic test vectors |

The full texts for the project component licenses are stored in this directory.
External dependencies are listed with their exact license and source in
`THIRD_PARTY.yml`; their notices must be preserved when bundled.
