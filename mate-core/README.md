# Mate Core

This directory now provides the Experimental portable C++20 value types,
DeviceSession and HostPlatform contracts, and the initial Device selection
state model. Operating-system APIs, libCEC, IPC, Device paths, and
platform-native input codes do not belong here.

The portable policy foundation represents lifecycle decisions as typed plans
for a Service executor. A bounded, thread-free semantic queue provides FIFO
ordering and explicit duplicate/overflow results.

The C++ API is not yet stable.
