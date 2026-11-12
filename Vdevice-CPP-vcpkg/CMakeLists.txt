cmake_minimum_required(VERSION 3.24)
project(VdeviceSDK VERSION 1.0.5.0 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanVdevice STATIC
  "sdk/Vdevice/Vdevice.cpp"
)
add_library(VdeviceSDK::sdk ALIAS TitanVdevice)
set_target_properties(TitanVdevice PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanVdevice PUBLIC cxx_std_17)
target_compile_definitions(TitanVdevice PUBLIC )
target_compile_definitions(TitanVdevice PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanVdevice PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanVdevice PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/Vdevice>"
  "$<INSTALL_INTERFACE:include/Vdevice>"
)
target_link_libraries(TitanVdevice PUBLIC advapi32 cfgmgr32 comctl32 crypt32 gdi32 hid newdev setupapi shell32 user32 wintrust)
install(TARGETS TitanVdevice EXPORT VdeviceSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/vdevice)
install(EXPORT VdeviceSDKTargets NAMESPACE VdeviceSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VdeviceSDK)
configure_file("VdeviceSDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/VdeviceSDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VdeviceSDKConfigVersion.cmake" VERSION 1.0.5.0 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VdeviceSDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/VdeviceSDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VdeviceSDK)
