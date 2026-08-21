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
option(CEC_INJECTOR_MATE_BUILD_MACOS
       "Build macOS platform implementations requiring Apple frameworks" OFF)
option(CEC_INJECTOR_MATE_BUILD_GUI
       "Build the optional Qt 6 Widgets frontend" OFF)

add_subdirectory(mate-core)
add_subdirectory(platform-support/edid)
add_subdirectory(service)
add_subdirectory(adapters/libcec)
add_subdirectory(applications/frontend)
if(CEC_INJECTOR_MATE_BUILD_LINUX)
    add_subdirectory(platforms/linux)
endif()
if(CEC_INJECTOR_MATE_BUILD_MACOS)
    add_subdirectory(platforms/macos)
endif()
if(CEC_INJECTOR_MATE_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
    add_subdirectory(applications/gui)
endif()

if(CEC_INJECTOR_MATE_BUILD_LINUX AND CEC_INJECTOR_MATE_BUILD_LIBCEC)
    add_executable(cec-injector-mate service/src/linux_main.cpp)
    target_link_libraries(cec-injector-mate PRIVATE
        CECInjector::MateService
        CECInjector::MateLinuxHostPlatform
        CECInjector::MateLinuxSessionFactory)
    target_compile_features(cec-injector-mate PRIVATE cxx_std_20)
    target_compile_options(cec-injector-mate PRIVATE
        -Wall -Wextra -Wpedantic)
    if(BUILD_TESTING)
        add_test(NAME mate-linux-cli-generic
            COMMAND cec-injector-mate --help)
        add_test(NAME mate-linux-cli-steamos-parity
            COMMAND cec-injector-mate --remote-profile steamos-parity --help)
        add_test(NAME mate-linux-cli-boot-once
            COMMAND cec-injector-mate --boot-once --startup-timeout 30 --help)
        add_test(NAME mate-linux-cli-removed-guide-ctrl-1
            COMMAND cec-injector-mate --guide-ctrl-1 --help)
        set_tests_properties(mate-linux-cli-removed-guide-ctrl-1
            PROPERTIES WILL_FAIL TRUE)
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
