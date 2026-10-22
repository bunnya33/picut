# CTranslate2 4.6.0's non-OpenMP backend owns a thread-local BS thread pool.
# Ruy also owns a thread-local context with an internal worker pool. On Windows,
# destroying either during DLL TLS teardown joins threads under the loader lock.
# Release both explicitly before a replica worker exits, and on the
# caller after joining replicas. Keep normal RAII process exit and no extra DLL.
set(parallel_file "${SDK}/ctranslate2/src/cpu/parallel.cc")
file(READ "${parallel_file}" parallel)
if(NOT parallel MATCHES "picut_thread_pool")
    string(REPLACE "#include \"parallel.h\"" "#include \"parallel.h\"\n#include <memory>" parallel "${parallel}")
    string(REPLACE "    BS::thread_pool_light& get_thread_pool() {\n      static thread_local BS::thread_pool_light thread_pool(num_threads);\n      return thread_pool;\n    }"
        "    static thread_local std::unique_ptr<BS::thread_pool_light> picut_thread_pool;\n\n    BS::thread_pool_light& get_thread_pool() {\n      if (!picut_thread_pool) picut_thread_pool = std::make_unique<BS::thread_pool_light>(num_threads);\n      return *picut_thread_pool;\n    }\n\n    void release_thread_pool() { picut_thread_pool.reset(); }"
        parallel "${parallel}")
    if(NOT parallel MATCHES "picut_thread_pool")
        message(FATAL_ERROR "Unexpected CTranslate2 parallel.cc; cannot apply Windows cleanup fix")
    endif()
    file(WRITE "${parallel_file}" "${parallel}")
endif()
if(NOT parallel MATCHES "release_ruy_context")
    string(REPLACE "    void release_thread_pool() { picut_thread_pool.reset(); }"
        "#ifdef CT2_WITH_RUY\n    void release_ruy_context();\n#endif\n    void release_thread_pool() {\n      picut_thread_pool.reset();\n#ifdef CT2_WITH_RUY\n      release_ruy_context();\n#endif\n    }" parallel "${parallel}")
    file(WRITE "${parallel_file}" "${parallel}")
endif()
set(backend_file "${SDK}/ctranslate2/src/cpu/backend.cc")
file(READ "${backend_file}" backend)
if(NOT backend MATCHES "picut_ruy_context")
    string(REPLACE "#include \"backend.h\"" "#include \"backend.h\"\n#include <memory>" backend "${backend}")
    string(REPLACE "    ruy::Context *get_ruy_context() {\n      static thread_local ruy::Context context;\n      return &context;\n    }"
        "    static thread_local std::unique_ptr<ruy::Context> picut_ruy_context;\n    ruy::Context *get_ruy_context() {\n      if (!picut_ruy_context) picut_ruy_context = std::make_unique<ruy::Context>();\n      return picut_ruy_context.get();\n    }\n    void release_ruy_context() { picut_ruy_context.reset(); }" backend "${backend}")
    if(NOT backend MATCHES "picut_ruy_context")
        message(FATAL_ERROR "Unexpected CTranslate2 backend.cc; cannot apply Windows cleanup fix")
    endif()
    file(WRITE "${backend_file}" "${backend}")
endif()
set(pool_file "${SDK}/ctranslate2/src/thread_pool.cc")
file(READ "${pool_file}" pool)
if(NOT pool MATCHES "release_thread_pool")
    string(REPLACE "namespace ctranslate2 {" "namespace ctranslate2 {\n#if defined(_WIN32) && !defined(_OPENMP)\n  namespace cpu { void release_thread_pool(); }\n#endif" pool "${pool}")
    string(REPLACE "    finalize();\n    local_worker = nullptr;"
        "    finalize();\n#if defined(_WIN32) && !defined(_OPENMP)\n    cpu::release_thread_pool();\n#endif\n    local_worker = nullptr;" pool "${pool}")
    string(REPLACE "      worker->join();\n  }"
        "      worker->join();\n#if defined(_WIN32) && !defined(_OPENMP)\n    cpu::release_thread_pool();\n#endif\n  }" pool "${pool}")
    file(WRITE "${pool_file}" "${pool}")
endif()
