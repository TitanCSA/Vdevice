vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/TitanCSA/Vdevice/releases/download/sdk-v1.0.5/vdevice-sdk-1.0.5.tar.gz"
    FILENAME "vdevice-sdk-1.0.5.tar.gz"
    SHA512 51945c8748460333bd2cda03ecdf8fd4eed8360a8c3e7b30c10c77deb66b72536b32460421517323b2b6289db8c9a9f73d8f4b162366ded6ef153930958afefa
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME VdeviceSDK CONFIG_PATH lib/cmake/VdeviceSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
