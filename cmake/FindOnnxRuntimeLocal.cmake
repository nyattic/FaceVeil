function(cloakframe_onnxruntime_from_root)
    find_path(ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        HINTS
            "${ONNXRUNTIME_ROOT}/include"
            "$ENV{ONNXRUNTIME_ROOT}/include"
    )
    find_library(ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS
            "${ONNXRUNTIME_ROOT}/lib"
            "$ENV{ONNXRUNTIME_ROOT}/lib"
    )

    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR "ONNX Runtime was not found. Configure with -DONNXRUNTIME_ROOT=/path/to/onnxruntime")
    endif()

    add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED GLOBAL)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
    )
endfunction()
