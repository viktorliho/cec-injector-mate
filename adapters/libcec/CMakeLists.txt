add_library(cec_injector_mate_libcec_adapter
    src/libcec_device_session.cpp
    src/wake_extension_client.cpp)
add_library(CECInjector::LibCecAdapter ALIAS cec_injector_mate_libcec_adapter)
target_include_directories(cec_injector_mate_libcec_adapter PUBLIC include)
target_link_libraries(cec_injector_mate_libcec_adapter PUBLIC
    CECInjector::MateCore
    CECInjector::MateComposition)
target_compile_features(cec_injector_mate_libcec_adapter PUBLIC cxx_std_20)

if(CEC_INJECTOR_MATE_BUILD_LIBCEC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCEC REQUIRED IMPORTED_TARGET libcec=7.1.1)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES PkgConfig::LIBCEC ${CMAKE_DL_LIBS})
    check_cxx_source_compiles([[extern "C" int cec_project_adapter_transaction_v1(
        void*, unsigned char, const unsigned char*, unsigned int, unsigned char*,
        unsigned int, unsigned int*, unsigned int);
      int main() { return cec_project_adapter_transaction_v1(nullptr, 0, nullptr,
          0, nullptr, 0, nullptr, 0); }]] CEC_INJECTOR_MATE_HAS_PROJECT_ABI)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT CEC_INJECTOR_MATE_HAS_PROJECT_ABI)
        message(FATAL_ERROR "libCEC lacks required cec_project_adapter_transaction_v1 ABI")
    endif()
    target_sources(cec_injector_mate_libcec_adapter PRIVATE src/real_libcec_api.cpp)
    target_compile_definitions(cec_injector_mate_libcec_adapter PRIVATE
        CEC_INJECTOR_MATE_HAVE_REAL_LIBCEC=1)
    target_link_libraries(cec_injector_mate_libcec_adapter PRIVATE
        PkgConfig::LIBCEC ${CMAKE_DL_LIBS})
endif()

if(BUILD_TESTING)
    add_executable(libcec_adapter_test tests/libcec_adapter_test.cpp)
    target_link_libraries(libcec_adapter_test PRIVATE CECInjector::LibCecAdapter)
    add_test(NAME libcec-adapter COMMAND libcec_adapter_test)
endif()
