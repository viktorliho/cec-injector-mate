add_library(cec_injector_mate_core
    src/device_registry.cpp
    src/policy.cpp
    src/semantic_event_queue.cpp
)
add_library(CECInjector::MateCore ALIAS cec_injector_mate_core)

target_include_directories(cec_injector_mate_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(cec_injector_mate_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(cec_injector_mate_core PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
