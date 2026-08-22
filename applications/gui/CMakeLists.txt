set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CEC_INJECTOR_MATE_GUI_GIT_SHORT_HASH "unknown")
set(CEC_INJECTOR_MATE_GUI_GIT_HASH "unknown")
set(CEC_INJECTOR_MATE_GUI_BUNDLE_VERSION "0")
set(CEC_INJECTOR_MATE_GUI_BUNDLE_SHORT_VERSION "0.0.0")
set(CEC_INJECTOR_MATE_GUI_MINIMUM_SYSTEM_VERSION "13.0")
if(APPLE)
    execute_process(
        COMMAND git -C ${PROJECT_SOURCE_DIR} rev-parse --short=12 HEAD
        OUTPUT_VARIABLE CEC_INJECTOR_MATE_GUI_GIT_SHORT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND git -C ${PROJECT_SOURCE_DIR} rev-parse HEAD
        OUTPUT_VARIABLE CEC_INJECTOR_MATE_GUI_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND git -C ${PROJECT_SOURCE_DIR} rev-list --count HEAD
        OUTPUT_VARIABLE CEC_INJECTOR_MATE_GUI_BUNDLE_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    set(CEC_INJECTOR_MATE_GUI_BUNDLE_SHORT_VERSION
        "0.0.${CEC_INJECTOR_MATE_GUI_BUNDLE_VERSION}")
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CEC_INJECTOR_MATE_GUI_MINIMUM_SYSTEM_VERSION
            "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endif()

add_library(cec_injector_mate_gui_support
    include/cec_injector/mate/gui/backend_qt_bridge.h
    include/cec_injector/mate/gui/cec_monitor_model.h
    include/cec_injector/mate/gui/main_window.h
    src/backend_qt_bridge.cpp
    src/cec_monitor_model.cpp
    src/main_window.cpp
)
target_include_directories(cec_injector_mate_gui_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cec_injector_mate_gui_support PUBLIC
    CECInjector::MateFrontend Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_features(cec_injector_mate_gui_support PUBLIC cxx_std_20)
if(CEC_INJECTOR_MATE_BUILD_LINUX)
    target_sources(cec_injector_mate_gui_support PRIVATE
        include/cec_injector/mate/gui/daemon_mate_frontend_backend.h
        src/daemon_mate_frontend_backend.cpp)
    target_link_libraries(cec_injector_mate_gui_support PUBLIC
        CECInjector::MateLinuxDbusFrontend Qt6::DBus)
    target_compile_definitions(cec_injector_mate_gui_support PUBLIC
        CEC_INJECTOR_MATE_GUI_LINUX_DBUS=1)
endif()
if(CEC_INJECTOR_MATE_BUILD_MACOS)
    target_sources(cec_injector_mate_gui_support PRIVATE
        include/cec_injector/mate/gui/macos/mac_in_process_mate_frontend_backend.h
        platforms/macos/src/mac_in_process_mate_frontend_backend.cpp)
    target_link_libraries(cec_injector_mate_gui_support PUBLIC
        CECInjector::MateService
        CECInjector::MateMacDiscovery
        CECInjector::MateMacPhysicalAddress
        CECInjector::MateMacSessionFactory
        "-framework CoreGraphics"
        "-framework IOKit"
        "-framework CoreFoundation")
    target_compile_definitions(cec_injector_mate_gui_support PUBLIC
        CEC_INJECTOR_MATE_GUI_MACOS_IN_PROCESS=1)
endif()

add_executable(cec-injector-mate-gui
    resources/gui.qrc
    src/main.cpp)
target_link_libraries(cec-injector-mate-gui PRIVATE
    cec_injector_mate_gui_support)
if(APPLE)
    set_target_properties(cec-injector-mate-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "CEC Injector Mate"
        MACOSX_BUNDLE_BUNDLE_VERSION
            ${CEC_INJECTOR_MATE_GUI_BUNDLE_VERSION}
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) CEC Injector contributors"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.cecinjector.Mate"
        MACOSX_BUNDLE_SHORT_VERSION_STRING
            ${CEC_INJECTOR_MATE_GUI_BUNDLE_SHORT_VERSION}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.cecinjector.Mate"
        XCODE_ATTRIBUTE_MARKETING_VERSION
            ${CEC_INJECTOR_MATE_GUI_BUNDLE_SHORT_VERSION}
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION
            ${CEC_INJECTOR_MATE_GUI_BUNDLE_SHORT_VERSION}
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

if(BUILD_TESTING)
    add_executable(mate-gui-tests tests/gui_test.cpp)
    target_link_libraries(mate-gui-tests PRIVATE
        cec_injector_mate_gui_support)
    add_test(NAME mate-gui COMMAND mate-gui-tests)
    set_tests_properties(mate-gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(CEC_INJECTOR_MATE_BUILD_MACOS)
        add_executable(mate-macos-frontend-backend-tests
            tests/mac_in_process_backend_test.cpp)
        target_link_libraries(mate-macos-frontend-backend-tests PRIVATE
            cec_injector_mate_gui_support)
        add_test(NAME mate-macos-frontend-backend COMMAND
            mate-macos-frontend-backend-tests)
    endif()
    if(CEC_INJECTOR_MATE_BUILD_LINUX)
        add_executable(mate-dbus-vertical-tests tests/dbus_vertical_test.cpp)
        target_link_libraries(mate-dbus-vertical-tests PRIVATE
            cec_injector_mate_gui_support CECInjector::MateService)
        add_test(NAME mate-dbus-vertical
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/dbus_vertical_test.sh
                $<TARGET_FILE:mate-dbus-vertical-tests>)
    endif()
endif()
