cmake_minimum_required(VERSION 3.25)

project(cec_injector_mate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(CEC_INJECTOR_MATE_BUILD_LIBCEC
       "Build the production adapter for the required patched libCEC ABI" OFF)

add_subdirectory(mate-core)
add_subdirectory(service)
add_subdirectory(adapters/libcec)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
