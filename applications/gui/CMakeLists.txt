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
endif()
