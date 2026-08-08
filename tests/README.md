# Tests

Current native tests cover the Experimental portable value types, Device
selection state, and fake implementations of the DeviceSession and
HostPlatform contracts. Adapter tests exercise the libCEC-facing seam, Wake
codec, callback copying, and repeated session ownership without requiring
libCEC or hardware. Policy tests use a fake executor to verify lifecycle
plans, conditional ordering, and session recovery. The semantic event queue is
tested independently. These tests require no platform or libCEC dependency.

Future Service, Linux platform, and external libCEC integration tests remain
separate. Hardware validation remains distinct from native CTest results.
