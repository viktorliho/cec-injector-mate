add_library(cec_injector_mate_frontend
    src/fake_frontend_backend.cpp
)
add_library(CECInjector::MateFrontend ALIAS cec_injector_mate_frontend)

target_include_directories(cec_injector_mate_frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cec_injector_mate_frontend PUBLIC CECInjector::MateCore)
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
endif()
