cmake_minimum_required(VERSION 3.25)

project(cec_injector_mate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(CEC_INJECTOR_MATE_BUILD_LIBCEC
       "Build the production adapter for the required patched libCEC ABI" OFF)
option(CEC_INJECTOR_MATE_BUILD_LINUX
       "Build Linux platform implementations requiring host dependencies" OFF)

add_subdirectory(mate-core)
add_subdirectory(service)
add_subdirectory(adapters/libcec)
add_subdirectory(platforms/linux)

if(CEC_INJECTOR_MATE_BUILD_LINUX AND CEC_INJECTOR_MATE_BUILD_LIBCEC)
    add_executable(cec-injector-mate service/src/linux_main.cpp)
    target_link_libraries(cec-injector-mate PRIVATE
        CECInjector::MateService
        CECInjector::MateLinuxHostPlatform
        CECInjector::MateLinuxSessionFactory)
    target_compile_features(cec-injector-mate PRIVATE cxx_std_20)
    target_compile_options(cec-injector-mate PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
