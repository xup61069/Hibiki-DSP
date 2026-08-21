# HibikiSetup

The planned open-source bootstrapper will install the user-space app and
virtual-driver package transactionally, preserve Scene/Calibration data, and
rollback to the last-known-good package. Official builds are Authenticode-signed
by Hibiki and contain a Microsoft-signed driver; the source is public and the
binary is delivered through Gumroad only.

`HibikiSetup.ps1` is the public bootstrapper source. It accepts an externally
delivered package plus `ReleaseManifest v1`, verifies source/toolchain/dependency/
SBOM metadata and package hashes, defaults to a non-mutating dry-run, and
requires explicit `-Apply` plus administrator rights before staging an INF with
`pnputil.exe`. The manifest also records the Microsoft driver signature and
Hibiki installer signer/RFC3161 timestamp; no package, certificate or Gumroad
credential is stored here.
