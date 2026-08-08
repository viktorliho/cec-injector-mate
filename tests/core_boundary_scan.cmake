file(GLOB_RECURSE core_sources
    "${CORE_DIR}/include/*.h"
    "${CORE_DIR}/src/*.cpp"
)

set(forbidden_pattern
    "(/dev/|/sys/|libudev|sd_bus|systemd|logind|linux/uinput|linux/input|libcec)")

foreach(source IN LISTS core_sources)
    file(READ "${source}" contents)
    string(TOLOWER "${contents}" normalized)
    if(normalized MATCHES "${forbidden_pattern}")
        message(FATAL_ERROR "platform dependency found in Mate Core: ${source}")
    endif()
endforeach()
