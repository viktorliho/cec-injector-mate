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
