# Hibiki DSP v1.0.0 — source-only release

## What this source tag contains

This release is a public source snapshot for reproducible inspection and
rebuild. It includes the repository source, build and test material, an SPDX
SBOM, third-party notices, and a text-only provenance manifest added in the
metadata commit that the annotated `v1.0.0` tag targets.

No GitHub Release asset, executable, driver package, installer, catalog,
signing material, activation service, or opaque prebuilt dependency is
distributed with this tag.

## How to verify it

The manifest records the source commit, the public distribution profile, and
raw Git-blob SHA-256 values for the toolchain lock, dependency inventory, this
SBOM, these release notes, the notices, and declared source files. The
repository's source-tag provenance checker validates that record against the
annotated tag topology.

Run the repository gates with PowerShell 7. `tools/verify.ps1` provides
unsigned source-build and test evidence; `tools/release-provenance-check.ps1`
validates the annotated tag and text provenance.

## Evidence boundary

Passing source and build checks does not establish driver installation, WaveRT
streaming, endpoint control, signing, or physical-audio behavior. Those are
outside this source-only release claim.

## Licenses and notices

The source tree is component-licensed. `LICENSES/README.md` maps source areas
to their SPDX licenses, and `THIRD_PARTY.yml` records third-party source,
license, purpose, and redistribution terms.
