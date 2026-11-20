enable_language(OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(CTest)
include(FetchContent)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network)
# Qt 6.8's OpenGL finder adds AGL, which newer macOS SDKs no longer ship.
execute_process(COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE picut_sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
if(IS_ABSOLUTE "${CMAKE_OSX_SYSROOT}")
    set(picut_sdk "${CMAKE_OSX_SYSROOT}")
endif()
if(TARGET WrapOpenGL::WrapOpenGL AND NOT EXISTS "${picut_sdk}/System/Library/Frameworks/AGL.framework")
    get_target_property(qt_opengl_links WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    list(FILTER qt_opengl_links EXCLUDE REGEX "AGL")
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${qt_opengl_links}")
endif()

set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_WRITERS OLD CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zxing URL https://codeload.github.com/zxing-cpp/zxing-cpp/zip/refs/tags/v2.3.0
    URL_HASH SHA256=89b70f6175c6347d72bdc72722d643c0f461dc8a0d9bbc4c927a240b32b706f2)
FetchContent_MakeAvailable(zxing)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc video)

set(picut_sources
    src/main.cpp src/store.cpp src/annotation.cpp src/toolbar.cpp
    src/text_editor.cpp src/image_text.cpp src/recording_settings.cpp
    src/scroll_capture.cpp src/scroll_stitcher.cpp src/scroll_stitcher_v2.cpp
    src/scroll_motion.cpp src/scroll_grabber.cpp src/editing.cpp
    src/capture.cpp src/pin.cpp src/recorder.cpp src/recording_markup.cpp
    src/recognition.cpp src/application.cpp src/recording_overlay.cpp
    src/recording_editor.cpp src/translation.cpp src/image_translation.cpp
    macos/platform.mm macos/screen_capture.mm macos/recognition.mm
    macos/recording.mm)
qt_add_executable(FloatShotNative MACOSX_BUNDLE ${picut_sources})
set_target_properties(FloatShotNative PROPERTIES
    OUTPUT_NAME picut
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in")
target_include_directories(FloatShotNative PRIVATE src macos ${OpenCV_INCLUDE_DIRS})
target_compile_options(FloatShotNative PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_link_libraries(FloatShotNative PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network ZXing::ZXing ${OpenCV_LIBS}
    "-framework AppKit" "-framework ApplicationServices" "-framework Carbon"
    "-framework ScreenCaptureKit" "-framework CoreMedia" "-framework CoreVideo"
    "-framework Vision" "-framework CoreML" "-framework AudioToolbox" "-framework AVFoundation"
    "-framework ServiceManagement")

file(GLOB toolbar_glyphs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/toolbar/*.png")
qt_add_resources(FloatShotNative toolbar_icons PREFIX "/toolbar"
    BASE "assets/toolbar" FILES ${toolbar_glyphs})
qt_add_resources(FloatShotNative app_icon PREFIX "/app"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../assets/picut.png")

set(app_icon "${CMAKE_CURRENT_BINARY_DIR}/picut.icns")
add_custom_command(OUTPUT "${app_icon}"
    COMMAND bash "${CMAKE_CURRENT_LIST_DIR}/make-icon.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/picut.png" "${app_icon}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/picut.png" "${CMAKE_CURRENT_LIST_DIR}/make-icon.sh"
    VERBATIM)
set_source_files_properties("${app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(FloatShotNative PRIVATE "${app_icon}")

include(macos/translation.cmake)

if(BUILD_TESTING)
    add_test(NAME macos-workflow COMMAND FloatShotNative --verify-features)
    add_test(NAME macos-offline-ocr COMMAND FloatShotNative --verify-ocr-memory)
    add_test(NAME macos-recording COMMAND FloatShotNative --verify-recording)
    add_test(NAME macos-scroll COMMAND FloatShotNative --verify-scroll)
    add_test(NAME macos-offline-translation COMMAND FloatShotNative --verify-translation)
    foreach(test IN ITEMS macos-workflow macos-offline-ocr macos-scroll macos-offline-translation)
        set_tests_properties(${test} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FLOATSHOT_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/verify/${test}"
            TIMEOUT 90)
    endforeach()
    set_tests_properties(macos-offline-translation PROPERTIES TIMEOUT 120)
    set_tests_properties(macos-recording PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=cocoa;FLOATSHOT_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/verify/macos-recording"
        TIMEOUT 45)
endif()
