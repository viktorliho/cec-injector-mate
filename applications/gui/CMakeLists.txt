set(CMAKE_AUTOMOC ON)

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

add_executable(cec-injector-mate-gui src/main.cpp)
target_link_libraries(cec-injector-mate-gui PRIVATE
    cec_injector_mate_gui_support)

if(BUILD_TESTING)
    add_executable(mate-gui-tests tests/gui_test.cpp)
    target_link_libraries(mate-gui-tests PRIVATE
        cec_injector_mate_gui_support)
    add_test(NAME mate-gui COMMAND mate-gui-tests)
    set_tests_properties(mate-gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(CEC_INJECTOR_MATE_BUILD_LINUX)
        add_executable(mate-dbus-vertical-tests tests/dbus_vertical_test.cpp)
        target_link_libraries(mate-dbus-vertical-tests PRIVATE
            cec_injector_mate_gui_support CECInjector::MateService)
        add_test(NAME mate-dbus-vertical
            COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/dbus_vertical_test.sh
                $<TARGET_FILE:mate-dbus-vertical-tests>)
    endif()
endif()
