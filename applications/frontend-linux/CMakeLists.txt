add_library(cec_injector_mate_linux_dbus_frontend
    src/linux_dbus_frontend_service.cpp)
add_library(CECInjector::MateLinuxDbusFrontend ALIAS
    cec_injector_mate_linux_dbus_frontend)
target_include_directories(cec_injector_mate_linux_dbus_frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cec_injector_mate_linux_dbus_frontend PUBLIC
    CECInjector::MateFrontend PkgConfig::FRONTEND_LIBSYSTEMD Threads::Threads)
target_compile_features(cec_injector_mate_linux_dbus_frontend PUBLIC cxx_std_20)
target_compile_options(cec_injector_mate_linux_dbus_frontend PRIVATE
    -Wall -Wextra -Wpedantic)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FRONTEND_LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
find_package(Threads REQUIRED)
