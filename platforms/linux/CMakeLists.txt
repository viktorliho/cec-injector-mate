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

add_library(cec_injector_mate_linux_physical_address
    src/drm_physical_address.cpp
    src/physical_address.cpp
)
add_library(CECInjector::MateLinuxPhysicalAddress ALIAS
    cec_injector_mate_linux_physical_address
)
target_include_directories(cec_injector_mate_linux_physical_address
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_linux_physical_address
    PUBLIC
        CECInjector::MateCore
)
target_compile_features(cec_injector_mate_linux_physical_address PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cec_injector_mate_linux_physical_address PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_linux_physical_address PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

add_library(cec_injector_mate_linux_remote_input_model
    src/remote_input.cpp
)
add_library(CECInjector::MateLinuxRemoteInputModel ALIAS
    cec_injector_mate_linux_remote_input_model
)
target_include_directories(cec_injector_mate_linux_remote_input_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_linux_remote_input_model
    PUBLIC
        CECInjector::MateCore
)
target_compile_features(cec_injector_mate_linux_remote_input_model PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cec_injector_mate_linux_remote_input_model PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_linux_remote_input_model PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

add_library(cec_injector_mate_linux_remote_wake
    src/remote_wake_environment.cpp
)
add_library(CECInjector::MateLinuxRemoteWake ALIAS
    cec_injector_mate_linux_remote_wake
)
target_include_directories(cec_injector_mate_linux_remote_wake
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_linux_remote_wake
    PUBLIC
        CECInjector::MateLinuxDiscoveryModel
)
target_compile_features(cec_injector_mate_linux_remote_wake PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cec_injector_mate_linux_remote_wake PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_linux_remote_wake PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

add_library(cec_injector_mate_linux_power_session_model
    src/power_session.cpp
)
add_library(CECInjector::MateLinuxPowerSessionModel ALIAS
    cec_injector_mate_linux_power_session_model
)
target_include_directories(cec_injector_mate_linux_power_session_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_linux_power_session_model
    PUBLIC
        CECInjector::MateCore
)
target_compile_features(cec_injector_mate_linux_power_session_model PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cec_injector_mate_linux_power_session_model PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_linux_power_session_model PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

if(CEC_INJECTOR_MATE_BUILD_LINUX)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "CEC_INJECTOR_MATE_BUILD_LINUX requires Linux")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)
    pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
    pkg_check_modules(LIBDRM REQUIRED IMPORTED_TARGET libdrm)
    find_package(Threads REQUIRED)

    target_sources(cec_injector_mate_linux_physical_address PRIVATE
        src/libdrm_edid_source.cpp)
    target_link_libraries(cec_injector_mate_linux_physical_address PRIVATE
        PkgConfig::LIBDRM)

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

    add_library(cec_injector_mate_linux_uinput
        src/uinput_sink.cpp
    )
    add_library(CECInjector::MateLinuxUinput ALIAS
        cec_injector_mate_linux_uinput
    )
    target_link_libraries(cec_injector_mate_linux_uinput
        PUBLIC
            CECInjector::MateLinuxRemoteInputModel
    )
    target_compile_features(cec_injector_mate_linux_uinput PUBLIC cxx_std_20)
    target_compile_options(cec_injector_mate_linux_uinput PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_library(cec_injector_mate_linux_power_session
        src/sd_bus_power_session.cpp
    )
    add_library(CECInjector::MateLinuxPowerSession ALIAS
        cec_injector_mate_linux_power_session
    )
    target_link_libraries(cec_injector_mate_linux_power_session
        PUBLIC
            CECInjector::MateLinuxPowerSessionModel
        PRIVATE
            PkgConfig::LIBSYSTEMD
            Threads::Threads
    )
    target_compile_features(cec_injector_mate_linux_power_session PUBLIC cxx_std_20)
    target_compile_options(cec_injector_mate_linux_power_session PRIVATE
        -Wall -Wextra -Wpedantic
    )

    add_library(cec_injector_mate_linux_host_platform
        src/linux_host_platform.cpp
    )
    add_library(CECInjector::MateLinuxHostPlatform ALIAS
        cec_injector_mate_linux_host_platform
    )
    target_link_libraries(cec_injector_mate_linux_host_platform PUBLIC
        CECInjector::MateLinuxDiscovery
        CECInjector::MateLinuxPhysicalAddress
        CECInjector::MateLinuxUinput
        CECInjector::MateLinuxRemoteWake
        CECInjector::MateLinuxPowerSession
        Threads::Threads
    )
    target_compile_features(cec_injector_mate_linux_host_platform PUBLIC cxx_std_20)
    target_compile_options(cec_injector_mate_linux_host_platform PRIVATE
        -Wall -Wextra -Wpedantic)

    if(CEC_INJECTOR_MATE_BUILD_LIBCEC)
        add_library(cec_injector_mate_linux_session_factory
            src/linux_device_session_factory.cpp
        )
        add_library(CECInjector::MateLinuxSessionFactory ALIAS
            cec_injector_mate_linux_session_factory
        )
        target_link_libraries(cec_injector_mate_linux_session_factory PUBLIC
            CECInjector::MateLinuxHostPlatform
            CECInjector::LibCecAdapter
            CECInjector::MateComposition
        )
        target_compile_features(cec_injector_mate_linux_session_factory PUBLIC cxx_std_20)
        target_compile_options(cec_injector_mate_linux_session_factory PRIVATE
            -Wall -Wextra -Wpedantic)
        if(BUILD_TESTING)
            add_executable(mate-linux-composition-tests
                tests/linux_composition_test.cpp)
            target_link_libraries(mate-linux-composition-tests PRIVATE
                CECInjector::MateLinuxSessionFactory)
            target_include_directories(mate-linux-composition-tests PRIVATE
                ${PROJECT_SOURCE_DIR}/tests)
            target_compile_features(mate-linux-composition-tests PRIVATE cxx_std_20)
            target_compile_options(mate-linux-composition-tests PRIVATE
                -Wall -Wextra -Wpedantic)
            add_test(NAME mate-linux-composition
                COMMAND mate-linux-composition-tests)
        endif()
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

    add_executable(mate-linux-physical-address-tests
        tests/physical_address_test.cpp
    )
    target_link_libraries(mate-linux-physical-address-tests PRIVATE
        CECInjector::MateLinuxPhysicalAddress
    )
    target_include_directories(mate-linux-physical-address-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_features(mate-linux-physical-address-tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(mate-linux-physical-address-tests PRIVATE /W4)
    else()
        target_compile_options(mate-linux-physical-address-tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME mate-linux-physical-address
        COMMAND mate-linux-physical-address-tests
    )

    add_executable(mate-linux-remote-input-tests
        tests/remote_input_test.cpp
    )
    target_link_libraries(mate-linux-remote-input-tests PRIVATE
        CECInjector::MateLinuxRemoteInputModel
    )
    target_include_directories(mate-linux-remote-input-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_features(mate-linux-remote-input-tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(mate-linux-remote-input-tests PRIVATE /W4)
    else()
        target_compile_options(mate-linux-remote-input-tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME mate-linux-remote-input
        COMMAND mate-linux-remote-input-tests
    )

    add_executable(mate-linux-remote-wake-tests
        tests/remote_wake_environment_test.cpp
    )
    target_link_libraries(mate-linux-remote-wake-tests PRIVATE
        CECInjector::MateLinuxRemoteWake
    )
    target_include_directories(mate-linux-remote-wake-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_features(mate-linux-remote-wake-tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(mate-linux-remote-wake-tests PRIVATE /W4)
    else()
        target_compile_options(mate-linux-remote-wake-tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME mate-linux-remote-wake
        COMMAND mate-linux-remote-wake-tests
    )

    add_executable(mate-linux-power-session-tests
        tests/power_session_test.cpp
    )
    target_link_libraries(mate-linux-power-session-tests PRIVATE
        CECInjector::MateLinuxPowerSessionModel
    )
    target_include_directories(mate-linux-power-session-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_features(mate-linux-power-session-tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(mate-linux-power-session-tests PRIVATE /W4)
    else()
        target_compile_options(mate-linux-power-session-tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()
    add_test(NAME mate-linux-power-session
        COMMAND mate-linux-power-session-tests
    )
endif()
