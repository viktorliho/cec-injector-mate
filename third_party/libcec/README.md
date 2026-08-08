# Patched libCEC dependency

The production adapter uses Pulse-Eight libCEC 7.1.1 at upstream commit
`3519af0715e654c43382423d843ad658f2e01cb4`. Apply
`libcec-7.1.1-project-adapter-v1.patch` to that exact tree. The patch SHA-256
is `151e75f3d81578f90aca3bc343259eed381cc5e1a3f92c302c59fee12bb1980c`.

The patch exports `cec_project_adapter_transaction_v1` (ABI version 1), so
project extension transactions share libCEC's existing adapter connection and
reader. The CMake opt-in build rejects a library without that symbol.

libCEC is GPL-2.0-or-later. Obtain its complete corresponding source and
license/notices from the pinned upstream project; distributors of linked
artifacts are responsible for satisfying those terms. This repository records
the maintained patch but does not vendor libCEC or its build output.
