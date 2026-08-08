cmake_minimum_required(VERSION 3.25)

project(cec_injector_mate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

if(BUILD_TESTING)
    add_test(
        NAME mate-genesis-sanity
        COMMAND ${CMAKE_COMMAND} -E true
    )
endif()
