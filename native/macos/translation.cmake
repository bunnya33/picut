function(picut_add_translation_worker)
    # Fetch the same pinned dependencies as the Windows translation SDK.
    function(picut_translation_source name repository revision sha256)
        FetchContent_Declare(${name}
            URL "https://codeload.github.com/${repository}/zip/${revision}"
            URL_HASH "SHA256=${sha256}"
            SOURCE_SUBDIR picut-fetch-only)
        FetchContent_MakeAvailable(${name})
        set(${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    endfunction()
    picut_translation_source(picut_ct2 OpenNMT/CTranslate2 v4.6.0
        745a21e728535691cc800791d4d501d90422ce43911b56d5a5de366b3dd7339f)
    picut_translation_source(picut_cpu_features google/cpu_features 8a494eb1e158ec2050e5f699a504fbc9b896a43b
        4ccded88992089effa9852740b192e404e7ba26541b185ad2a4a0e8aa95a109c)
    picut_translation_source(picut_spdlog gabime/spdlog 76fb40d95455f249bd70824ecfcae7a8f0930fa3
        9a00dd50318b9467148adc5e822e55221c65d8d8794c6890ba034eed222dcf64)
    picut_translation_source(picut_ruy google/ruy 363f252289fb7a1fba1703d99196524698cb884d
        6ce5d6b89def2c4f2772074fe89451a3de69c9bfe30a3887a6f2749111824094)
    picut_translation_source(picut_cpuinfo pytorch/cpuinfo 082deffc80ce517f81dc2f3aebe6ba671fcd09c9
        cd4d4a7659581260f57f821c151826190813f2e3741e7edae4aef336d50d6496)
    picut_translation_source(picut_spm google/sentencepiece v0.2.0
        67c34082deb3d89f589d8614b1ae58aa4af250c79e18737d8103242ec62a28f9)
    foreach(dependency IN ITEMS cpu_features spdlog ruy)
        file(COPY "${picut_${dependency}_SOURCE_DIR}/"
            DESTINATION "${picut_ct2_SOURCE_DIR}/third_party/${dependency}")
    endforeach()
    file(COPY "${picut_cpuinfo_SOURCE_DIR}/"
        DESTINATION "${picut_ct2_SOURCE_DIR}/third_party/ruy/third_party/cpuinfo")

    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(CMAKE_AUTOMOC OFF)
    set(WITH_MKL OFF)
    set(WITH_ACCELERATE ON)
    set(WITH_RUY ON)
    set(OPENMP_RUNTIME NONE CACHE STRING "CTranslate2 OpenMP runtime" FORCE)
    set(BUILD_CLI OFF)
    set(BUILD_TESTS OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS ON)
    set(RUY_MINIMAL_BUILD ON)
    add_subdirectory("${picut_ct2_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ctranslate2" EXCLUDE_FROM_ALL)
    set(SPM_ENABLE_SHARED OFF)
    set(SPM_BUILD_TEST OFF)
    set(SPM_ENABLE_TCMALLOC OFF)
    set(SPM_ENABLE_NFKC_COMPILE OFF)
    add_subdirectory("${picut_spm_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sentencepiece" EXCLUDE_FROM_ALL)

    add_executable(PicutTranslate src/translation_worker.cpp src/translation_terms.cpp)
    target_link_libraries(PicutTranslate PRIVATE Qt6::Core ctranslate2 sentencepiece-static)
    target_include_directories(PicutTranslate PRIVATE "${picut_spm_SOURCE_DIR}/src")
    set(worker_directory "${CMAKE_CURRENT_BINARY_DIR}")
    if(CMAKE_CONFIGURATION_TYPES)
        string(APPEND worker_directory "/$<CONFIG>")
    endif()
    set_target_properties(PicutTranslate PROPERTIES OUTPUT_NAME picut-translate
        RUNTIME_OUTPUT_DIRECTORY "${worker_directory}/picut.app/Contents/MacOS")
    add_dependencies(FloatShotNative PicutTranslate)
endfunction()

picut_add_translation_worker()

set(PICUT_TRANSLATION_MODELS "${CMAKE_CURRENT_SOURCE_DIR}/../build/translation-sdk/models"
    CACHE PATH "Directory containing the converted picut OPUS-MT models")
file(GLOB_RECURSE translation_models CONFIGURE_DEPENDS "${PICUT_TRANSLATION_MODELS}/*")
foreach(model_file IN LISTS translation_models)
    file(RELATIVE_PATH model_path "${PICUT_TRANSLATION_MODELS}" "${model_file}")
    get_filename_component(model_dir "${model_path}" DIRECTORY)
    set_source_files_properties("${model_file}" PROPERTIES
        HEADER_FILE_ONLY ON MACOSX_PACKAGE_LOCATION "Resources/translation/${model_dir}")
    target_sources(FloatShotNative PRIVATE "${model_file}")
endforeach()
