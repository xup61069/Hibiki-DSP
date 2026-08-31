# Source and distribution policy

GitHub is the canonical source repository. Source tags and release notes are
the release provenance; text manifests, SBOM and notices may accompany that
source metadata, but no GitHub release asset is uploaded and no compiled
executables, drivers, installers, packages or opaque prebuilt dependencies are
published.

Public CI may compile and test in an ephemeral workspace. It must not upload
artifacts, publish packages, persist build outputs in caches or use signing
permissions. The project does not use paid delivery, signing, activation or
runtime DRM, and HLK/WHCP is not a release prerequisite.

Every official source tag records toolchain, dependency lock, content hashes,
the non-empty `distribution_id` and test evidence in a text-only
`ReleaseManifest v1`; the release notes, SBOM and notices remain text
provenance. Anyone can obtain the corresponding source at no additional charge
and rebuild it.
