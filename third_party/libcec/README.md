# Patched libCEC dependency

The production adapter uses Pulse-Eight libCEC 7.1.1 at upstream commit
`3519af0715e654c43382423d843ad658f2e01cb4`. Apply
`libcec-7.1.1-project-adapter-v1.patch` to that exact tree. The patch SHA-256
is `2f7eb96e157735be3ea9a64cb933a61ad48e95887cdf5d3a701a98ee73fc4c2f`.

The patch exports `cec_project_adapter_transaction_v1` (ABI version 1), so
project extension transactions share libCEC's existing adapter connection and
reader. The CMake opt-in build rejects a library without that symbol.

The patch also limits Set OSD Name operands to the 14 octets available after
the CEC header and opcode, preserving the 16-octet CEC frame maximum.

libCEC is GPL-2.0-or-later. Obtain its complete corresponding source and
license/notices from the pinned upstream project; distributors of linked
artifacts are responsible for satisfying those terms. This repository records
the maintained patch but does not vendor libCEC or its build output.
