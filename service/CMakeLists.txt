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

find_package(Threads REQUIRED)
add_library(cec_injector_mate_service src/mate_service.cpp)
add_library(CECInjector::MateService ALIAS cec_injector_mate_service)
target_include_directories(cec_injector_mate_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cec_injector_mate_service PUBLIC
    CECInjector::MateComposition Threads::Threads)
target_compile_features(cec_injector_mate_service PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(cec_injector_mate_service PRIVATE /W4)
else()
    target_compile_options(cec_injector_mate_service PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
