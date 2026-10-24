function(add_mate_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE CECInjector::MateCore)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_mate_test(mate-types-tests types_test.cpp)
add_mate_test(mate-device-registry-tests device_registry_test.cpp)
add_mate_test(mate-boundary-contract-tests boundary_contract_test.cpp)
target_link_libraries(mate-boundary-contract-tests PRIVATE
    CECInjector::MateComposition
)
add_mate_test(mate-policy-tests policy_test.cpp)
add_mate_test(mate-semantic-event-queue-tests semantic_event_queue_test.cpp)

add_test(NAME mate-core-boundary-scan
    COMMAND ${CMAKE_COMMAND}
        -DCORE_DIR=${PROJECT_SOURCE_DIR}/mate-core
        -P ${CMAKE_CURRENT_SOURCE_DIR}/core_boundary_scan.cmake
)

add_test(NAME mate-qt-boundary-scan
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_ROOT=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/qt_boundary_scan.cmake
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CEC_INJECTOR_MATE_BUILD_LINUX)
    add_test(NAME mate-linux-remote-wake-rule
        COMMAND sh
            ${PROJECT_SOURCE_DIR}/packaging/linux/tests/remote_wake_rule_test.sh
            ${PROJECT_SOURCE_DIR}/packaging/linux/generate-remote-wake-rule.sh
            ${PROJECT_SOURCE_DIR})
    add_test(NAME mate-linux-production-installation
        COMMAND sh
            ${PROJECT_SOURCE_DIR}/packaging/linux/tests/production_installation_test.sh
            ${PROJECT_SOURCE_DIR})
    add_test(NAME mate-steamos-update-survivability
        COMMAND sh
            ${PROJECT_SOURCE_DIR}/packaging/steamos/tests/update_survivability_test.sh
            ${PROJECT_SOURCE_DIR})
    add_test(NAME mate-linux-qt-platform-check
        COMMAND sh
            ${PROJECT_SOURCE_DIR}/packaging/linux/tests/qt_platform_check_test.sh
            ${PROJECT_SOURCE_DIR})
endif()

add_executable(mate-service-tests service_test.cpp)
target_link_libraries(mate-service-tests PRIVATE CECInjector::MateService)
target_include_directories(mate-service-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mate-service-tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(mate-service-tests PRIVATE /W4)
else()
    target_compile_options(mate-service-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME mate-service COMMAND mate-service-tests)

add_executable(mate-runtime-configuration-tests runtime_configuration_test.cpp)
target_link_libraries(mate-runtime-configuration-tests PRIVATE
    CECInjector::MateService)
add_test(NAME mate-runtime-configuration
    COMMAND mate-runtime-configuration-tests)
