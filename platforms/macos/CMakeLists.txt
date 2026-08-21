if(NOT APPLE)
    message(FATAL_ERROR "CEC_INJECTOR_MATE_BUILD_MACOS requires macOS")
endif()

add_library(cec_injector_mate_macos_discovery_model
    src/mac_device_discovery_model.cpp
    src/mac_device_inventory.cpp
)
add_library(CECInjector::MateMacDiscoveryModel ALIAS
    cec_injector_mate_macos_discovery_model
)
target_include_directories(cec_injector_mate_macos_discovery_model
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_macos_discovery_model
    PUBLIC
        CECInjector::MateCore
)
target_compile_features(cec_injector_mate_macos_discovery_model PUBLIC cxx_std_20)
target_compile_options(cec_injector_mate_macos_discovery_model PRIVATE
    -Wall -Wextra -Wpedantic
)

add_library(cec_injector_mate_macos_discovery
    src/mac_device_discovery.cpp
)
add_library(CECInjector::MateMacDiscovery ALIAS
    cec_injector_mate_macos_discovery
)
target_include_directories(cec_injector_mate_macos_discovery
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_macos_discovery
    PUBLIC
        CECInjector::MateMacDiscoveryModel
    PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
)
target_compile_features(cec_injector_mate_macos_discovery PUBLIC cxx_std_20)
target_compile_options(cec_injector_mate_macos_discovery PRIVATE
    -Wall -Wextra -Wpedantic
)

add_library(cec_injector_mate_macos_physical_address
    src/mac_physical_address.cpp
)
add_library(CECInjector::MateMacPhysicalAddress ALIAS
    cec_injector_mate_macos_physical_address
)
target_include_directories(cec_injector_mate_macos_physical_address
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_macos_physical_address
    PUBLIC
        CECInjector::MateCore
        CECInjector::MateEdid
    PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
)
target_compile_features(cec_injector_mate_macos_physical_address PUBLIC cxx_std_20)
target_compile_options(cec_injector_mate_macos_physical_address PRIVATE
    -Wall -Wextra -Wpedantic
)

add_library(cec_injector_mate_macos_session_factory
    src/mac_device_session_factory.cpp
)
add_library(CECInjector::MateMacSessionFactory ALIAS
    cec_injector_mate_macos_session_factory
)
target_include_directories(cec_injector_mate_macos_session_factory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cec_injector_mate_macos_session_factory
    PUBLIC
        CECInjector::MateMacDiscoveryModel
        CECInjector::LibCecAdapter
        CECInjector::MateComposition
)
target_compile_features(cec_injector_mate_macos_session_factory PUBLIC cxx_std_20)
target_compile_options(cec_injector_mate_macos_session_factory PRIVATE
    -Wall -Wextra -Wpedantic
)

if(BUILD_TESTING)
    add_executable(mate-macos-discovery-model-tests
        tests/mac_device_discovery_model_test.cpp
    )
    target_link_libraries(mate-macos-discovery-model-tests PRIVATE
        CECInjector::MateMacDiscoveryModel
    )
    target_compile_features(mate-macos-discovery-model-tests PRIVATE cxx_std_20)
    target_compile_options(mate-macos-discovery-model-tests PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME mate-macos-discovery-model COMMAND
        mate-macos-discovery-model-tests)

    add_executable(mate-macos-session-factory-tests
        tests/mac_device_session_factory_test.cpp
    )
    target_link_libraries(mate-macos-session-factory-tests PRIVATE
        CECInjector::MateMacSessionFactory
    )
    target_compile_features(mate-macos-session-factory-tests PRIVATE cxx_std_20)
    target_compile_options(mate-macos-session-factory-tests PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME mate-macos-session-factory COMMAND
        mate-macos-session-factory-tests)

    add_executable(mate-macos-physical-address-tests
        tests/mac_physical_address_test.cpp
    )
    target_link_libraries(mate-macos-physical-address-tests PRIVATE
        CECInjector::MateMacPhysicalAddress
    )
    target_compile_features(mate-macos-physical-address-tests PRIVATE cxx_std_20)
    target_compile_options(mate-macos-physical-address-tests PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME mate-macos-physical-address COMMAND
        mate-macos-physical-address-tests)

endif()

add_executable(cec-injector-mate-macos-discovery-probe
    tests/mac_discovery_probe.cpp
)
target_link_libraries(cec-injector-mate-macos-discovery-probe PRIVATE
    CECInjector::MateMacDiscovery
)
target_compile_features(cec-injector-mate-macos-discovery-probe PRIVATE cxx_std_20)
target_compile_options(cec-injector-mate-macos-discovery-probe PRIVATE
    -Wall -Wextra -Wpedantic
)

add_executable(cec-injector-mate-macos-pa-probe
    tests/mac_pa_probe.cpp
)
target_link_libraries(cec-injector-mate-macos-pa-probe PRIVATE
    CECInjector::MateMacPhysicalAddress
)
target_compile_features(cec-injector-mate-macos-pa-probe PRIVATE cxx_std_20)
target_compile_options(cec-injector-mate-macos-pa-probe PRIVATE
    -Wall -Wextra -Wpedantic
)
