cmake_minimum_required(VERSION 3.16)
project(SevenZipApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(SevenZipApp MACOSX_BUNDLE
    src/main.cpp src/MainWindow.cpp src/ArchiveService.cpp
    src/CreateArchiveDialog.cpp src/ExtractDialog.cpp
)

target_link_libraries(SevenZipApp PRIVATE Qt6::Widgets)
target_include_directories(SevenZipApp PRIVATE src)

set_target_properties(SevenZipApp PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.sevenzip.macos"
    MACOSX_BUNDLE_BUNDLE_NAME "7-Zip"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist"
    OUTPUT_NAME "7-Zip"
)

# ── Bundle resources into app bundle ──────────────────────────────────────
set(RES_DIR "${CMAKE_SOURCE_DIR}/resources")
set(BUNDLE_RES "$<TARGET_BUNDLE_CONTENT_DIR:SevenZipApp>/Resources")

# 7za binary
add_custom_command(TARGET SevenZipApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_RES}/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RES_DIR}/bin/7za" "${BUNDLE_RES}/bin/7za"
    COMMAND /bin/chmod 755 "${BUNDLE_RES}/bin/7za"
    COMMENT "Bundling 7za..."
)

# Stylesheet
add_custom_command(TARGET SevenZipApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_RES}/styles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RES_DIR}/styles/style.qss" "${BUNDLE_RES}/styles/style.qss"
    COMMENT "Bundling stylesheet..."
)

# ── Also copy to build dir for direct execution ──────────────────────────
add_custom_command(TARGET SevenZipApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RES_DIR}/bin/7za" "${CMAKE_BINARY_DIR}/bin/7za"
    COMMAND /bin/chmod 755 "${CMAKE_BINARY_DIR}/bin/7za"
    COMMENT "Copying 7za to build dir..."
)

add_custom_command(TARGET SevenZipApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/styles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RES_DIR}/styles/style.qss" "${CMAKE_BINARY_DIR}/styles/style.qss"
    COMMENT "Copying stylesheet to build dir..."
)

# Ad-hoc sign so `open build/7-Zip.app` works
add_custom_command(TARGET SevenZipApp POST_BUILD
    COMMAND /usr/bin/codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:SevenZipApp>"
    COMMENT "Signing bundle for macOS..."
)
