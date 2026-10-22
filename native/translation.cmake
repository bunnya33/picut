# No inference library is linked into the tray application. It is loaded only by
# the short-lived native worker, after OCR has completed.
set(TRANSLATION_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../build/translation-sdk")
set(TRANSLATION_BUILD "${TRANSLATION_SDK}/build")
if(NOT EXISTS "${TRANSLATION_BUILD}/ctranslate2/ctranslate2.lib")
    message(FATAL_ERROR "Translation SDK missing: run native/setup-translation-sdk.ps1")
endif()
add_library(PicutCTranslate2 SHARED IMPORTED)
set_target_properties(PicutCTranslate2 PROPERTIES
    IMPORTED_IMPLIB "${TRANSLATION_BUILD}/ctranslate2/ctranslate2.lib"
    IMPORTED_LOCATION "${TRANSLATION_BUILD}/ctranslate2/ctranslate2.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${TRANSLATION_SDK}/ctranslate2/include")
add_library(PicutSentencePiece STATIC IMPORTED)
set_target_properties(PicutSentencePiece PROPERTIES
    IMPORTED_LOCATION "${TRANSLATION_BUILD}/sentencepiece/src/sentencepiece.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${TRANSLATION_SDK}/sentencepiece/src")
# find_package is repeated here because this file is included before the main
# project's complete Qt component discovery.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
qt_add_executable(FloatShotTranslate src/translation_worker.cpp src/translation_terms.cpp src/translation_terms.h)
target_compile_definitions(FloatShotTranslate PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(FloatShotTranslate PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(FloatShotTranslate PRIVATE Qt6::Core PicutCTranslate2 PicutSentencePiece psapi)
add_custom_command(TARGET FloatShotTranslate POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PicutCTranslate2>" "${CMAKE_CURRENT_BINARY_DIR}/ctranslate2.dll")
if(EXISTS "${TRANSLATION_SDK}/models/models.json")
    add_custom_target(TranslationModels ALL COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${TRANSLATION_SDK}/models" "${CMAKE_CURRENT_BINARY_DIR}/translation")
endif()
