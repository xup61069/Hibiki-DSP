# Source and distribution policy

GitHub is the canonical source repository. Tags and releases may contain
source archives, text manifests, SBOM and notices, but never compiled
executables, drivers, installers, packages or opaque prebuilt dependencies.

Public CI may compile and test in an ephemeral workspace. It must not upload
binary artifacts, publish packages, persist build outputs in caches or use
signing permissions. The project does not use paid delivery, signing,
activation or runtime DRM.

Every official source tag records toolchain, dependency lock, content hashes
and test evidence. Anyone can obtain the corresponding source at no additional
charge and rebuild it.
