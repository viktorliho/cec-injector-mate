set(forbidden_roots
    mate-core
    service
    adapters
    platform-support
    platforms
    applications/frontend)

foreach(root IN LISTS forbidden_roots)
    file(GLOB_RECURSE candidates
        "${PROJECT_ROOT}/${root}/*.h"
        "${PROJECT_ROOT}/${root}/*.hpp"
        "${PROJECT_ROOT}/${root}/*.cpp")
    foreach(candidate IN LISTS candidates)
        file(READ "${candidate}" contents)
        if(contents MATCHES "#[ \t]*include[ \t]*[<\"]Q[A-Za-z0-9_/.-]+[>\"]")
            message(FATAL_ERROR "Qt include outside applications/gui: ${candidate}")
        endif()
    endforeach()
endforeach()

file(GLOB_RECURSE frontend_candidates
    "${PROJECT_ROOT}/applications/frontend/*.h"
    "${PROJECT_ROOT}/applications/frontend/*.hpp"
    "${PROJECT_ROOT}/applications/frontend/*.cpp")
foreach(candidate IN LISTS frontend_candidates)
    file(READ "${candidate}" contents)
    if(contents MATCHES "(LinuxHostPlatform|MacDeviceDiscovery|MacPhysicalAddressResolver|LibCecDeviceSession|RealLibCecApi|systemd|libudev|IOKit|DBus)")
        message(FATAL_ERROR
            "Platform/backend implementation leaked into frontend contract: ${candidate}")
    endif()
endforeach()
