# Source and distribution policy

GitHub is the canonical source repository. Tags and releases may contain
source archives, text manifests, SBOM and notices, but never compiled
executables, drivers, installers, packages or opaque prebuilt dependencies.

Public CI may compile and test in an ephemeral workspace. It must not upload
binary artifacts, publish packages, persist build outputs in caches or expose
production signing secrets. Official signed installers are canonical builds
delivered through Gumroad; the application has no activation or runtime DRM.

Every official build maps to one immutable source tag and records toolchain,
dependency lock, unsigned hashes, signed hashes and test evidence. Customers
must be able to obtain the corresponding source at no additional charge.
