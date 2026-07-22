cmake_minimum_required(VERSION 3.24)

if(APPLE AND (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR
              CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
        "Minimum supported macOS version" FORCE)
endif()

project(CloakFrame VERSION 1.10.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CloakFrameHarden)
include(FindOnnxRuntimeLocal)

find_package(Qt6 6.8.1 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt6 6.8.1 QUIET OPTIONAL_COMPONENTS LinguistTools Svg)
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc)
if(OpenCV_VERSION VERSION_LESS 4.10.0)
    message(FATAL_ERROR "CloakFrame requires OpenCV 4.10.0 or newer")
endif()
find_package(spdlog REQUIRED)
find_package(exiv2 CONFIG QUIET)
set(CLOAKFRAME_EXIV2_TARGET "")
if(exiv2_FOUND)
    if(TARGET Exiv2::exiv2lib)
        set(CLOAKFRAME_EXIV2_TARGET Exiv2::exiv2lib)
    elseif(TARGET exiv2lib)
        set(CLOAKFRAME_EXIV2_TARGET exiv2lib)
    endif()
endif()
if(NOT CLOAKFRAME_EXIV2_TARGET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(EXIV2 IMPORTED_TARGET GLOBAL exiv2)
        if(EXIV2_FOUND)
            set(CLOAKFRAME_EXIV2_TARGET PkgConfig::EXIV2)
        endif()
    endif()
endif()

set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to the ONNX Runtime package root")

if(WIN32)
    cloakframe_onnxruntime_from_root()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ONNXRUNTIME IMPORTED_TARGET GLOBAL libonnxruntime)
    endif()

    if(ONNXRUNTIME_FOUND)
        add_library(onnxruntime::onnxruntime ALIAS PkgConfig::ONNXRUNTIME)
    else()
        cloakframe_onnxruntime_from_root()
    endif()
endif()

include(CTest)

if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME translation_quality
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_translations.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/translations/cloakframe_ko.ts"
                    "${CMAKE_CURRENT_SOURCE_DIR}/translations/cloakframe_ja.ts"
                    "${CMAKE_CURRENT_SOURCE_DIR}/translations/cloakframe_zh_CN.ts")
        add_test(NAME translation_quality_zh_CN
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_translations.py"
                    --strict-source-equality
                    "${CMAKE_CURRENT_SOURCE_DIR}/translations/cloakframe_zh_CN.ts")
    endif()
endif()

add_subdirectory(src)
add_subdirectory(tools)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
