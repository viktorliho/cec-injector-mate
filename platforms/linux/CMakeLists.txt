add_library(cec_injector_mate_linux_discovery_model
    src/device_discovery.cpp
)
add_library(CECInjector::MateLinuxDiscoveryModel ALIAS
    cec_injector_mate_linux_discovery_model
)

target_include_directories(cec_injector_mate_linux_discovery_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_linux_discovery_model
    PUBLIC
        CECInjector::MateCore
)
target_compile_features(cec_injector_mate_linux_discovery_model PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(cec_injector_mate_linux_discovery_model PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_linux_discovery_model PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

if(CEC_INJECTOR_MATE_BUILD_LINUX)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "CEC_INJECTOR_MATE_BUILD_LINUX requires Linux")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
    find_package(Threads REQUIRED)

    add_library(cec_injector_mate_linux_discovery
        src/udev_device_discovery.cpp
    )
    add_library(CECInjector::MateLinuxDiscovery ALIAS
        cec_injector_mate_linux_discovery
    )
    target_link_libraries(cec_injector_mate_linux_discovery
        PUBLIC
            CECInjector::MateLinuxDiscoveryModel
        PRIVATE
            PkgConfig::LIBUDEV
            Threads::Threads
    )
    target_compile_features(cec_injector_mate_linux_discovery PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(cec_injector_mate_linux_discovery PRIVATE /W4)
    else()
        target_compile_options(cec_injector_mate_linux_discovery PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
endif()

if(BUILD_TESTING)
    add_executable(mate-linux-device-discovery-tests
        tests/device_discovery_test.cpp
    )
    target_link_libraries(mate-linux-device-discovery-tests PRIVATE
        CECInjector::MateLinuxDiscoveryModel
    )
    target_include_directories(mate-linux-device-discovery-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_features(mate-linux-device-discovery-tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(mate-linux-device-discovery-tests PRIVATE /W4)
    else()
        target_compile_options(mate-linux-device-discovery-tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME mate-linux-device-discovery
        COMMAND mate-linux-device-discovery-tests
    )
endif()
