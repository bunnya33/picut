cmake_minimum_required(VERSION 3.24)
project(FloatShotNative VERSION 0.5.21 LANGUAGES CXX RC)
# CMake can misdecode the Chinese MSVC /showIncludes prefix on UTF-8 consoles.
if(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX MATCHES "娉ㄦ剰")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件: ")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Test)
find_package(Qt6 6.8 REQUIRED COMPONENTS Multimedia MultimediaWidgets)
set(OCR_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../build/native-sdk")
if(NOT EXISTS "${OCR_SDK}/paddleocr-small/models.json")
    message(FATAL_ERROR "OCR SDK missing: run native/setup-ocr-sdk.ps1 first")
endif()
# OpenCV's minimal install exports unused codec targets without their archives.
# Import only the built image-processing libraries and their zlib dependency.
foreach(component IN ITEMS opencv_imgproc4110 opencv_core4110 zlib)
    add_library(${component} STATIC IMPORTED)
    set_target_properties(${component} PROPERTIES IMPORTED_LOCATION
        "${OCR_SDK}/opencv-min/x64/vc17/staticlib/${component}.lib")
endforeach()
add_library(OnnxRuntime SHARED IMPORTED)
set_target_properties(OnnxRuntime PROPERTIES
    IMPORTED_IMPLIB "${OCR_SDK}/onnxruntime-win-x64-1.22.0/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${OCR_SDK}/onnxruntime-win-x64-1.22.0/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${OCR_SDK}/onnxruntime-win-x64-1.22.0/include")
qt_add_executable(FloatShotPaddleOcr src/paddle_ocr_worker.cpp)
target_compile_definitions(FloatShotPaddleOcr PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(FloatShotPaddleOcr PRIVATE /utf-8 /W4)
target_include_directories(FloatShotPaddleOcr PRIVATE "${OCR_SDK}/opencv-min/include")
target_link_libraries(FloatShotPaddleOcr PRIVATE Qt6::Core Qt6::Gui OnnxRuntime opencv_imgproc4110 opencv_core4110 zlib psapi dxgi d3d12)
add_custom_target(OcrRuntime ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OCR_SDK}/onnxruntime-win-x64-1.22.0/lib/onnxruntime.dll" "${CMAKE_CURRENT_BINARY_DIR}/onnxruntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${OCR_SDK}/paddleocr-small" "${CMAKE_CURRENT_BINARY_DIR}/ocr")
add_dependencies(FloatShotPaddleOcr OcrRuntime)
if(NOT EXISTS "${OCR_SDK}/directml-1.15.4/bin/x64-win/DirectML.dll")
    message(FATAL_ERROR "GPU OCR SDK missing: run native/setup-ocr-sdk.ps1")
endif()
add_custom_target(OcrGpuRuntime ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gpu"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:FloatShotPaddleOcr>" "${CMAKE_CURRENT_BINARY_DIR}/gpu/FloatShotPaddleOcr.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OCR_SDK}/onnxruntime-directml-1.22.0/runtimes/win-x64/native/onnxruntime.dll" "${CMAKE_CURRENT_BINARY_DIR}/gpu/onnxruntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OCR_SDK}/onnxruntime-directml-1.22.0/runtimes/win-x64/native/onnxruntime_providers_shared.dll" "${CMAKE_CURRENT_BINARY_DIR}/gpu/onnxruntime_providers_shared.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OCR_SDK}/directml-1.15.4/bin/x64-win/DirectML.dll" "${CMAKE_CURRENT_BINARY_DIR}/gpu/DirectML.dll")
add_dependencies(OcrGpuRuntime FloatShotPaddleOcr)
qt_add_library(NativeVideoPreview MODULE src/video_preview.cpp)
target_compile_options(NativeVideoPreview PRIVATE /utf-8 /W4)
target_link_libraries(NativeVideoPreview PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_WRITERS OLD CACHE STRING "" FORCE)
set(ZXING_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../build/native-sdk/zxing-cpp-2.3.0/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../build/native-sdk/zxing-cpp-2.3.0" "${CMAKE_CURRENT_BINARY_DIR}/zxing")
else()
    include(FetchContent)
    FetchContent_Declare(zxing URL https://codeload.github.com/zxing-cpp/zxing-cpp/zip/refs/tags/v2.3.0
        URL_HASH SHA256=89b70f6175c6347d72bdc72722d643c0f461dc8a0d9bbc4c927a240b32b706f2)
    FetchContent_MakeAvailable(zxing)
endif()
qt_add_executable(FloatShotNative WIN32
    src/main.cpp src/store.cpp src/annotation.cpp src/toolbar.cpp src/text_editor.cpp src/text_editor.h src/image_text.cpp src/image_text.h src/recording_settings.cpp src/recording_settings.h src/scroll_capture.cpp src/scroll_capture.h src/scroll_stitcher.cpp src/scroll_grabber.cpp src/scroll_grabber.h
    src/editing.cpp src/element_detector.cpp src/element_detector.h src/capture.cpp src/pin.cpp src/recorder.cpp src/recognition.cpp src/application.cpp src/recording_overlay.cpp src/recording_overlay.h
    src/store.h src/annotation.h src/toolbar.h src/capture.h src/pin.h
    src/editing.h src/recorder.h src/application.h src/native_windows.h app.rc)
target_compile_definitions(FloatShotNative PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(FloatShotNative PROPERTIES OUTPUT_NAME picut)
target_compile_options(FloatShotNative PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(FloatShotNative PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    user32 gdi32 ole32 uuid shell32 dwmapi psapi avrt windowsapp ZXing::ZXing)
set(TOOLBAR_GLYPHS
    assets/toolbar/panel-check.png assets/toolbar/panel-up.png assets/toolbar/panel-down.png
    assets/toolbar/shapes.png assets/toolbar/arrow.png assets/toolbar/pen.png
    assets/toolbar/marker.png assets/toolbar/mosaic.png assets/toolbar/text.png
    assets/toolbar/eraser.png assets/toolbar/undo.png assets/toolbar/redo.png
    assets/toolbar/cancel.png assets/toolbar/pin.png assets/toolbar/save.png
    assets/toolbar/copy.png
    assets/toolbar/text-bold.png assets/toolbar/text-italic.png assets/toolbar/text-outline.png
    assets/toolbar/text-nw.png assets/toolbar/text-delete.png assets/toolbar/text-sw.png
    assets/toolbar/text-se.png assets/toolbar/text-rotate.png)
qt_add_resources(FloatShotNative toolbar_icons PREFIX "/toolbar"
    BASE "assets/toolbar" FILES ${TOOLBAR_GLYPHS})
include(CTest)
if(BUILD_TESTING)
    qt_add_executable(FloatShotOcrTests tests/ocr_tests.cpp src/recognition.cpp src/recognition.h src/store.cpp src/store.h src/image_text.cpp src/image_text.h)
    target_compile_definitions(FloatShotOcrTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(FloatShotOcrTests PRIVATE /utf-8 /W4)
    target_include_directories(FloatShotOcrTests PRIVATE src)
    target_link_libraries(FloatShotOcrTests PRIVATE Qt6::Test Qt6::Widgets ZXing::ZXing windowsapp)
    add_dependencies(FloatShotOcrTests FloatShotPaddleOcr OcrGpuRuntime)
    add_test(NAME offline-ocr COMMAND FloatShotOcrTests)
    set_tests_properties(offline-ocr PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
    qt_add_executable(FloatShotRecordingOverlayTests tests/recording_overlay_tests.cpp
        src/recorder.cpp src/recording_overlay.cpp src/recorder.h src/recording_overlay.h)
    target_compile_definitions(FloatShotRecordingOverlayTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(FloatShotRecordingOverlayTests PRIVATE /utf-8 /W4)
    target_include_directories(FloatShotRecordingOverlayTests PRIVATE src)
    target_link_libraries(FloatShotRecordingOverlayTests PRIVATE Qt6::Test Qt6::Widgets
        user32 gdi32 ole32 uuid psapi avrt dwmapi)
    add_test(NAME recording-ui-windows COMMAND FloatShotRecordingOverlayTests)
    set_tests_properties(recording-ui-windows PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 90)
    qt_add_executable(FloatShotNativeTests tests/tests.cpp src/store.cpp src/annotation.cpp src/editing.cpp
        src/toolbar.cpp src/text_editor.cpp src/text_editor.h src/image_text.cpp src/image_text.h src/element_detector.cpp src/element_detector.h src/capture.cpp src/pin.cpp src/recorder.cpp src/recognition.cpp src/scroll_capture.cpp src/scroll_capture.h src/scroll_stitcher.cpp src/scroll_grabber.cpp src/scroll_grabber.h
        src/application.cpp src/application.h src/recording_overlay.cpp src/recording_overlay.h src/recording_settings.cpp src/recording_settings.h
        src/store.h src/annotation.h src/editing.h src/toolbar.h src/capture.h src/pin.h src/recorder.h src/menu.h)
    target_compile_definitions(FloatShotNativeTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(FloatShotNativeTests PRIVATE /utf-8 /W4)
    target_include_directories(FloatShotNativeTests PRIVATE src)
    target_link_libraries(FloatShotNativeTests PRIVATE Qt6::Test Qt6::Widgets Qt6::Network
        user32 gdi32 ole32 uuid shell32 dwmapi psapi avrt windowsapp ZXing::ZXing)
    qt_add_resources(FloatShotNativeTests toolbar_icons PREFIX "/toolbar"
        BASE "assets/toolbar" FILES ${TOOLBAR_GLYPHS})
    qt_add_resources(FloatShotNativeTests toolbar_reference PREFIX "/fixtures"
        BASE "tests/fixtures" FILES tests/fixtures/toolbar-reference.png)
    add_test(NAME native-workflow COMMAND FloatShotNativeTests)
    set_tests_properties(native-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME scroll-ui-windows COMMAND FloatShotNativeTests scrollDesktopIndependentProducer scrollDesktopManualAndAutomatic)
    set_tests_properties(scroll-ui-windows PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 60)
endif()
