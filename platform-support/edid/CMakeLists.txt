add_library(cec_injector_mate_edid
    src/physical_address.cpp
)
add_library(CECInjector::MateEdid ALIAS cec_injector_mate_edid)

target_include_directories(cec_injector_mate_edid
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(cec_injector_mate_edid PUBLIC cxx_std_20)
target_compile_options(cec_injector_mate_edid PRIVATE
    -Wall -Wextra -Wpedantic
)

if(BUILD_TESTING)
    add_executable(mate-edid-tests
        tests/physical_address_test.cpp
    )
    target_link_libraries(mate-edid-tests PRIVATE
        CECInjector::MateEdid
    )
    target_include_directories(mate-edid-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
    )
    target_compile_features(mate-edid-tests PRIVATE cxx_std_20)
    target_compile_options(mate-edid-tests PRIVATE
        -Wall -Wextra -Wpedantic
    )
    add_test(NAME mate-edid-tests COMMAND mate-edid-tests)
endif()
