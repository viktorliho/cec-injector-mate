add_library(cec_injector_mate_composition INTERFACE)
add_library(CECInjector::MateComposition ALIAS cec_injector_mate_composition)

target_include_directories(cec_injector_mate_composition
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cec_injector_mate_composition
    INTERFACE
        CECInjector::MateCore
)

target_compile_features(cec_injector_mate_composition INTERFACE cxx_std_20)
