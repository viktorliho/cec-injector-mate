add_library(cec_injector_mate_frontend
    src/compatibility_capture.cpp
    src/fake_frontend_backend.cpp
    src/frontend_state_projector.cpp
)
add_library(CECInjector::MateFrontend ALIAS cec_injector_mate_frontend)

target_include_directories(cec_injector_mate_frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cec_injector_mate_frontend PUBLIC
    CECInjector::MateCore CECInjector::MateService)
target_compile_features(cec_injector_mate_frontend PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cec_injector_mate_frontend PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_frontend PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(mate-frontend-tests tests/frontend_test.cpp)
    target_link_libraries(mate-frontend-tests PRIVATE
        CECInjector::MateFrontend)
    target_compile_features(mate-frontend-tests PRIVATE cxx_std_20)
    add_test(NAME mate-frontend COMMAND mate-frontend-tests)
    add_executable(mate-compatibility-capture-tests
        tests/compatibility_capture_test.cpp)
    target_link_libraries(mate-compatibility-capture-tests PRIVATE
        CECInjector::MateFrontend)
    target_compile_features(mate-compatibility-capture-tests PRIVATE cxx_std_20)
    target_compile_definitions(mate-compatibility-capture-tests PRIVATE
        MATE_FRONTEND_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME mate-compatibility-capture COMMAND
        mate-compatibility-capture-tests)
endif()
