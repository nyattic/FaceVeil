cmake_minimum_required(VERSION 3.24)

project(Redactly VERSION 1.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS LinguistTools Svg)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)
find_package(spdlog REQUIRED)
find_package(exiv2 CONFIG QUIET)
set(REDACTLY_EXIV2_TARGET "")
if(exiv2_FOUND)
    if(TARGET Exiv2::exiv2lib)
        set(REDACTLY_EXIV2_TARGET Exiv2::exiv2lib)
    elseif(TARGET exiv2lib)
        set(REDACTLY_EXIV2_TARGET exiv2lib)
    endif()
endif()
if(NOT REDACTLY_EXIV2_TARGET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(EXIV2 IMPORTED_TARGET exiv2)
        if(EXIV2_FOUND)
            set(REDACTLY_EXIV2_TARGET PkgConfig::EXIV2)
        endif()
    endif()
endif()
include(CTest)

set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to the ONNX Runtime package root")

function(redactly_onnxruntime_from_root)
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
    redactly_onnxruntime_from_root()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ONNXRUNTIME IMPORTED_TARGET libonnxruntime)
    endif()

    if(ONNXRUNTIME_FOUND)
        add_library(onnxruntime::onnxruntime ALIAS PkgConfig::ONNXRUNTIME)
    else()
        redactly_onnxruntime_from_root()
    endif()
endif()

set(REDACTLY_APP_OPTIONS)
set(REDACTLY_APP_RESOURCES)
if(APPLE)
    list(APPEND REDACTLY_APP_OPTIONS MACOSX_BUNDLE)
    set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/assets/Redactly.icns")
    if(EXISTS "${APP_ICON_MACOS}")
        set_source_files_properties("${APP_ICON_MACOS}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        list(APPEND REDACTLY_APP_RESOURCES "${APP_ICON_MACOS}")
    endif()
elseif(WIN32)
    list(APPEND REDACTLY_APP_OPTIONS WIN32)
endif()

add_library(redactly_core STATIC
    src/ImageIo.cpp
    src/ImageScanner.cpp
    src/Mosaic.cpp
    src/OnnxGraphPatch.cpp
    src/OrtAcceleration.cpp
    src/OutputPlan.cpp
    src/PlateDetector.cpp
    src/ProcessorWorker.cpp
    src/SceneCut.cpp
    src/ScrfdFaceDetector.cpp
    src/Tracking.cpp
    src/VideoIo.cpp
    src/VideoProcessor.cpp
    include/redactly/DetectionGeometry.hpp
    include/redactly/Detector.hpp
    include/redactly/FaceDetection.hpp
    include/redactly/ImageIo.hpp
    include/redactly/ImageScanner.hpp
    include/redactly/Mosaic.hpp
    include/redactly/OnnxGraphPatch.hpp
    include/redactly/OrderedParallel.hpp
    include/redactly/OrtAcceleration.hpp
    include/redactly/OutputPlan.hpp
    include/redactly/PathSafety.hpp
    include/redactly/PathUtil.hpp
    include/redactly/PlateDetector.hpp
    include/redactly/ProcessorWorker.hpp
    include/redactly/ReviewTypes.hpp
    include/redactly/SceneCut.hpp
    include/redactly/ScrfdFaceDetector.hpp
    include/redactly/Tracking.hpp
    include/redactly/VideoIo.hpp
    include/redactly/VideoProcessor.hpp
    include/redactly/VideoReviewTypes.hpp
)

target_include_directories(redactly_core PUBLIC include)
target_link_libraries(redactly_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    ${OpenCV_LIBS}
    onnxruntime::onnxruntime
    spdlog::spdlog
)

if(REDACTLY_EXIV2_TARGET)
    target_link_libraries(redactly_core PUBLIC ${REDACTLY_EXIV2_TARGET})
    target_compile_definitions(redactly_core PUBLIC REDACTLY_HAVE_EXIV2)
    message(STATUS "Redactly: metadata preservation enabled (${REDACTLY_EXIV2_TARGET})")
else()
    message(WARNING "exiv2 not found; building without metadata preservation.")
endif()

qt_add_executable(Redactly ${REDACTLY_APP_OPTIONS}
    src/main.cpp
    src/MainWindow.cpp
    src/ModelCatalog.cpp
    src/ModelDownloader.cpp
    src/ReviewDialog.cpp
    src/SettingsDialog.cpp
    src/Theme.cpp
    src/UpdateChecker.cpp
    src/VideoReviewDialog.cpp
    include/redactly/MainWindow.hpp
    include/redactly/ModelCatalog.hpp
    include/redactly/ModelDownloader.hpp
    include/redactly/ReviewDialog.hpp
    include/redactly/SettingsDialog.hpp
    include/redactly/Theme.hpp
    include/redactly/UpdateChecker.hpp
    include/redactly/VideoReviewDialog.hpp
    ${REDACTLY_APP_RESOURCES}
)

if(WIN32)
    target_sources(Redactly PRIVATE assets/Redactly.rc)
endif()

target_compile_definitions(Redactly PRIVATE REDACTLY_VERSION="${PROJECT_VERSION}")
target_link_libraries(Redactly PRIVATE
    redactly_core
    Qt6::Widgets
    Qt6::Network
)

if(TARGET Qt6::Svg)
    target_link_libraries(Redactly PRIVATE Qt6::Svg)
    target_compile_definitions(Redactly PRIVATE REDACTLY_HAVE_SVG)
    message(STATUS "Redactly: SVG settings icon enabled")
else()
    message(STATUS "Redactly: Qt Svg not found; using a text glyph for the settings icon")
endif()

if(Qt6LinguistTools_FOUND)
    qt_add_translations(Redactly
        TS_FILES translations/redactly_ko.ts
                 translations/redactly_ja.ts
    )
else()
    message(WARNING "Qt LinguistTools not found; building without embedded translations (English only).")
endif()

function(redactly_harden target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /guard:cf
            /GS
            /sdl
        )
        target_link_options(${target} PRIVATE
            /guard:cf
            /DYNAMICBASE
            /NXCOMPAT
            /HIGHENTROPYVA
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -fstack-protector-strong
            -fno-strict-aliasing
            $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>
        )
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(NOT APPLE)
            target_link_options(${target} PRIVATE
                -Wl,-z,relro
                -Wl,-z,now
                -Wl,-z,noexecstack
            )
        endif()
    endif()
endfunction()

redactly_harden(redactly_core)
redactly_harden(Redactly)

if(APPLE)
    set_target_properties(Redactly PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.redactly.app"
        MACOSX_BUNDLE_BUNDLE_NAME "Redactly"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "Redactly.icns"
    )
endif()

install(TARGETS Redactly
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

if(UNIX AND NOT APPLE)
    install(FILES assets/redactly.desktop DESTINATION share/applications)
    install(FILES assets/redactly-512.png
        DESTINATION share/icons/hicolor/512x512/apps RENAME redactly.png)
endif()

add_executable(redactly_inspect_model
    tools/inspect_model.cpp
)

target_link_libraries(redactly_inspect_model PRIVATE onnxruntime::onnxruntime spdlog::spdlog)
redactly_harden(redactly_inspect_model)

if(BUILD_TESTING)
    add_executable(redactly_tests
        tests/test_core.cpp
    )
    if(MSVC)
        target_compile_options(redactly_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(redactly_tests PRIVATE -UNDEBUG)
    endif()
    target_link_libraries(redactly_tests PRIVATE redactly_core)
    redactly_harden(redactly_tests)
    add_test(NAME redactly_tests COMMAND redactly_tests)

    add_executable(redactly_tracking_tests
        tests/test_tracking.cpp
    )
    if(MSVC)
        target_compile_options(redactly_tracking_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(redactly_tracking_tests PRIVATE -UNDEBUG)
    endif()
    target_link_libraries(redactly_tracking_tests PRIVATE redactly_core)
    redactly_harden(redactly_tracking_tests)
    add_test(NAME redactly_tracking_tests COMMAND redactly_tracking_tests)

    add_executable(redactly_parallel_tests
        tests/test_parallel.cpp
    )
    if(MSVC)
        target_compile_options(redactly_parallel_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(redactly_parallel_tests PRIVATE -UNDEBUG)
    endif()
    target_link_libraries(redactly_parallel_tests PRIVATE redactly_core)
    redactly_harden(redactly_parallel_tests)
    add_test(NAME redactly_parallel_tests COMMAND redactly_parallel_tests)

    add_executable(redactly_video_io_tests
        tests/test_video_io.cpp
    )
    if(MSVC)
        target_compile_options(redactly_video_io_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(redactly_video_io_tests PRIVATE -UNDEBUG)
    endif()
    target_link_libraries(redactly_video_io_tests PRIVATE redactly_core)
    redactly_harden(redactly_video_io_tests)
    add_test(NAME redactly_video_io_tests COMMAND redactly_video_io_tests)
    set_tests_properties(redactly_video_io_tests PROPERTIES SKIP_RETURN_CODE 77)
endif()
