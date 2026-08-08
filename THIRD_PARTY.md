# Third-party policy

This repository must record the origin, version or revision, license, local
changes, and redistribution obligations of third-party material it contains or
distributes.

Third-party notices and license texts must accompany the relevant source or
distribution artifacts. Project-owned changes must not obscure upstream
provenance, and generated or vendored dependencies must not be added merely for
developer convenience.

## libCEC

The optional production DeviceSession adapter targets Pulse-Eight libCEC
7.1.1, upstream commit `3519af0715e654c43382423d843ad658f2e01cb4`, with the
project transaction ABI patch recorded under `third_party/libcec/`. libCEC is
GPL-2.0-or-later; linking and redistribution must satisfy its license and
retain the upstream notices. The dependency is external and is not downloaded
or vendored by the ordinary build.

The patch is project-owned integration work derived from and applicable to
libCEC. Its presence does not relicense libCEC or the resulting linked work
under the repository's Apache-2.0 license.
