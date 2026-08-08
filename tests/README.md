# Tests

Current native tests cover the Experimental portable value types, Device
selection state, and fake implementations of the DeviceSession and
HostPlatform contracts. The boundary harness also covers opaque-locator
session construction, ordered host lifecycle delivery, and explicit or
destructor-driven transition-hold completion. A source scan rejects Linux,
systemd, libCEC, Device-path, and sysfs dependencies in Mate Core. Adapter
tests exercise the libCEC-facing seam, Wake
codec, callback copying, and repeated session ownership without requiring
libCEC or hardware. Policy tests use a fake executor to verify lifecycle
plans, conditional ordering, and session recovery. The semantic event queue is
tested independently. These tests require no platform or libCEC dependency.

Future Service, Linux platform, and external libCEC integration tests remain
separate. Hardware validation remains distinct from native CTest results.
