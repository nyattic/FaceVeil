cmake_minimum_required(VERSION 3.24)

project(FaceVeil VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS LinguistTools)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)
find_package(spdlog REQUIRED)
find_package(exiv2 CONFIG QUIET)
set(FACEVEIL_EXIV2_TARGET "")
if(exiv2_FOUND)
    if(TARGET Exiv2::exiv2lib)
        set(FACEVEIL_EXIV2_TARGET Exiv2::exiv2lib)
    elseif(TARGET exiv2lib)
        set(FACEVEIL_EXIV2_TARGET exiv2lib)
    endif()
endif()
if(NOT FACEVEIL_EXIV2_TARGET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(EXIV2 IMPORTED_TARGET exiv2)
        if(EXIV2_FOUND)
            set(FACEVEIL_EXIV2_TARGET PkgConfig::EXIV2)
        endif()
    endif()
endif()
include(CTest)

set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to the ONNX Runtime package root")

function(faceveil_onnxruntime_from_root)
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

if(WIN32)
    faceveil_onnxruntime_from_root()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ONNXRUNTIME IMPORTED_TARGET libonnxruntime)
    endif()

    if(ONNXRUNTIME_FOUND)
        add_library(onnxruntime::onnxruntime ALIAS PkgConfig::ONNXRUNTIME)
    else()
        faceveil_onnxruntime_from_root()
    endif()
endif()

set(FACEVEIL_APP_OPTIONS)
set(FACEVEIL_APP_RESOURCES)
if(APPLE)
    list(APPEND FACEVEIL_APP_OPTIONS MACOSX_BUNDLE)
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/assets/FaceVeil.icns")
    if(EXISTS "${APP_ICON_MACOS}")
        set_source_files_properties("${APP_ICON_MACOS}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        list(APPEND FACEVEIL_APP_RESOURCES "${APP_ICON_MACOS}")
    endif()
elseif(WIN32)
    list(APPEND FACEVEIL_APP_OPTIONS WIN32)
endif()

qt_add_executable(FaceVeil ${FACEVEIL_APP_OPTIONS}
    src/main.cpp
    src/MainWindow.cpp
    src/ImageScanner.cpp
    src/ImageIo.cpp
    src/Mosaic.cpp
    src/ProcessorWorker.cpp
    src/ReviewDialog.cpp
    src/ScrfdFaceDetector.cpp
    include/faceveil/FaceDetection.hpp
    include/faceveil/ImageIo.hpp
    include/faceveil/ImageScanner.hpp
    include/faceveil/MainWindow.hpp
    include/faceveil/Mosaic.hpp
    include/faceveil/ProcessorWorker.hpp
    include/faceveil/ReviewDialog.hpp
    include/faceveil/ReviewTypes.hpp
    include/faceveil/ScrfdFaceDetector.hpp
    ${FACEVEIL_APP_RESOURCES}
)

if(WIN32)
    target_sources(FaceVeil PRIVATE assets/FaceVeil.rc)
endif()

target_include_directories(FaceVeil PRIVATE include)
target_compile_definitions(FaceVeil PRIVATE FACEVEIL_VERSION="${PROJECT_VERSION}")
target_link_libraries(FaceVeil PRIVATE
    Qt6::Widgets
    Qt6::Network
    ${OpenCV_LIBS}
    onnxruntime::onnxruntime
    spdlog::spdlog
)

if(FACEVEIL_EXIV2_TARGET)
    target_link_libraries(FaceVeil PRIVATE ${FACEVEIL_EXIV2_TARGET})
    target_compile_definitions(FaceVeil PRIVATE FACEVEIL_HAVE_EXIV2)
    message(STATUS "FaceVeil: metadata preservation enabled (${FACEVEIL_EXIV2_TARGET})")
else()
    message(WARNING "exiv2 not found; building without metadata preservation.")
endif()

if(Qt6LinguistTools_FOUND)
    qt_add_translations(FaceVeil
        TS_FILES translations/faceveil_ko.ts
    )
else()
    message(WARNING "Qt LinguistTools not found; building without embedded translations (English only).")
endif()

# ── Security hardening ────────────────────────────────────────────
# Opt-in compile/link flags that catch common memory-safety issues at runtime
# and make exploitation harder. All of these are widely supported and cheap.
if(MSVC)
    target_compile_options(FaceVeil PRIVATE
        /W4              # High warning level
        /guard:cf        # Control Flow Guard
        /GS              # Buffer security checks
        /sdl             # Additional security checks
    )
    target_link_options(FaceVeil PRIVATE
        /guard:cf
        /DYNAMICBASE     # ASLR
        /NXCOMPAT        # DEP
        /HIGHENTROPYVA
    )
else()
    target_compile_options(FaceVeil PRIVATE
        -Wall
        -Wextra
        -fstack-protector-strong
        -fno-strict-aliasing
        $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>
    )
    # PIE: required by most distro hardening policies and by macOS anyway.
    set_target_properties(FaceVeil PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(NOT APPLE)
        target_link_options(FaceVeil PRIVATE
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,-z,noexecstack
        )
    endif()
endif()

if(APPLE)
    set_target_properties(FaceVeil PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.faceveil.app"
        MACOSX_BUNDLE_BUNDLE_NAME "FaceVeil"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "FaceVeil.icns"
    )
endif()

install(TARGETS FaceVeil
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

if(UNIX AND NOT APPLE)
    install(FILES assets/faceveil.desktop DESTINATION share/applications)
    install(FILES assets/faceveil-512.png
        DESTINATION share/icons/hicolor/512x512/apps RENAME faceveil.png)
endif()

add_executable(faceveil_inspect_model
    tools/inspect_model.cpp
)

target_link_libraries(faceveil_inspect_model PRIVATE onnxruntime::onnxruntime spdlog::spdlog)

if(BUILD_TESTING)
    add_executable(faceveil_tests
        tests/test_core.cpp
        src/ImageScanner.cpp
        src/ImageIo.cpp
        src/Mosaic.cpp
        include/faceveil/FaceDetection.hpp
        include/faceveil/ImageIo.hpp
        include/faceveil/ImageScanner.hpp
        include/faceveil/Mosaic.hpp
    )
    target_include_directories(faceveil_tests PRIVATE include)
    target_link_libraries(faceveil_tests PRIVATE
        Qt6::Core
        ${OpenCV_LIBS}
    )
    if(FACEVEIL_EXIV2_TARGET)
        target_link_libraries(faceveil_tests PRIVATE ${FACEVEIL_EXIV2_TARGET})
        target_compile_definitions(faceveil_tests PRIVATE FACEVEIL_HAVE_EXIV2)
    endif()
    add_test(NAME faceveil_tests COMMAND faceveil_tests)
endif()
