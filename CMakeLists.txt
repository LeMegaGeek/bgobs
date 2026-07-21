# SPDX-FileCopyrightText: 2026 Kaito Udagawa <umireon@kaito.tokyo>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

### Options
set(PLUGIN_NAME "bgobs" CACHE STRING "Name of plugin")
set(PLUGIN_DISPLAY_NAME "BGOBS" CACHE STRING "Display name of plugin")
set(PLUGIN_AUTHOR "LeMegaGeek" CACHE STRING "Author of plugin")
set(PLUGIN_EMAIL "d.github@chey.net" CACHE STRING "Author email of plugin")
set(PLUGIN_BUILD_NUMBER "1" CACHE STRING "Build number in plugin metadata")
set(PLUGIN_BUNDLE_ID "net.lemegageek.bgobs" CACHE STRING "Bundle ID of plugin installer")
set(PACKAGE_VERSION_SUFFIX "" CACHE STRING "Version suffix for plugin package")
set(BGOBS_PACKAGE_SIGNING_STATUS "unsigned" CACHE STRING "Package signing status: signed or unsigned")
set_property(CACHE BGOBS_PACKAGE_SIGNING_STATUS PROPERTY STRINGS signed unsigned)
option(BGOBS_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(BGOBS_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)

if(BGOBS_ENABLE_SANITIZERS AND BGOBS_ENABLE_THREAD_SANITIZER)
  message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer must use separate builds")
endif()

if(PLUGIN_NAME STREQUAL "obs-backgroundremoval")
  set(PLUGIN_NAME "bgobs" CACHE STRING "Name of plugin" FORCE)
endif()

if(PLUGIN_DISPLAY_NAME STREQUAL "OBS Background Removal")
  set(PLUGIN_DISPLAY_NAME "BGOBS" CACHE STRING "Display name of plugin" FORCE)
endif()

if(PLUGIN_AUTHOR STREQUAL "Roy Shilkrot")
  set(PLUGIN_AUTHOR "LeMegaGeek" CACHE STRING "Author of plugin" FORCE)
endif()

if(PLUGIN_EMAIL STREQUAL "roy.shil@gmail.com")
  set(PLUGIN_EMAIL "d.github@chey.net" CACHE STRING "Author email of plugin" FORCE)
endif()

if(PLUGIN_BUNDLE_ID STREQUAL "com.royshilkrot.obs-backgroundremoval")
  set(PLUGIN_BUNDLE_ID "net.lemegageek.bgobs" CACHE STRING "Bundle ID of plugin installer" FORCE)
endif()

### Project
block(SCOPE_FOR POLICIES VARIABLES PROPAGATE PLUGIN_VERSION)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_CONTENT)
  string(STRIP "${VERSION_CONTENT}" PLUGIN_VERSION)
endblock()

if(
  NOT
    PLUGIN_VERSION
      MATCHES
      "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z][0-9A-Za-z.-]*)?(\\+[0-9A-Za-z][0-9A-Za-z.-]*)?$"
)
  message(FATAL_ERROR "VERSION must be a semantic version, got: ${PLUGIN_VERSION}")
endif()

set(PLUGIN_PACKAGE_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
project(bgobs VERSION "${PLUGIN_PACKAGE_VERSION}" LANGUAGES C CXX)

if(NOT BGOBS_PACKAGE_SIGNING_STATUS MATCHES "^(signed|unsigned)$")
  message(FATAL_ERROR "BGOBS_PACKAGE_SIGNING_STATUS must be signed or unsigned")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" BUILDSPEC_CONTENT)
string(JSON BUILDSPEC_PLUGIN_NAME GET "${BUILDSPEC_CONTENT}" name)
string(JSON BUILDSPEC_PLUGIN_VERSION GET "${BUILDSPEC_CONTENT}" version)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/manifest.json" MANIFEST_CONTENT)
string(JSON MANIFEST_PLUGIN_ID GET "${MANIFEST_CONTENT}" id)
string(JSON MANIFEST_PLUGIN_NAME GET "${MANIFEST_CONTENT}" name)
string(JSON MANIFEST_PLUGIN_VERSION GET "${MANIFEST_CONTENT}" version)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/config.json" CONFIG_CONTENT)
string(JSON CONFIG_UPDATE_CHECK_TYPE TYPE "${CONFIG_CONTENT}" check_for_updates)

if(NOT BUILDSPEC_PLUGIN_NAME STREQUAL PLUGIN_NAME)
  message(FATAL_ERROR "buildspec.json name (${BUILDSPEC_PLUGIN_NAME}) must match PLUGIN_NAME (${PLUGIN_NAME})")
endif()
if(NOT BUILDSPEC_PLUGIN_VERSION STREQUAL PLUGIN_VERSION)
  message(FATAL_ERROR "buildspec.json version (${BUILDSPEC_PLUGIN_VERSION}) must match VERSION (${PLUGIN_VERSION})")
endif()
if(NOT MANIFEST_PLUGIN_NAME STREQUAL PLUGIN_NAME)
  message(FATAL_ERROR "data/manifest.json name (${MANIFEST_PLUGIN_NAME}) must match PLUGIN_NAME (${PLUGIN_NAME})")
endif()
if(NOT MANIFEST_PLUGIN_ID STREQUAL PLUGIN_BUNDLE_ID)
  message(FATAL_ERROR "data/manifest.json id (${MANIFEST_PLUGIN_ID}) must match PLUGIN_BUNDLE_ID (${PLUGIN_BUNDLE_ID})")
endif()
if(NOT MANIFEST_PLUGIN_VERSION STREQUAL PLUGIN_VERSION)
  message(FATAL_ERROR "data/manifest.json version (${MANIFEST_PLUGIN_VERSION}) must match VERSION (${PLUGIN_VERSION})")
endif()
if(NOT CONFIG_UPDATE_CHECK_TYPE STREQUAL "BOOLEAN")
  message(FATAL_ERROR "data/config.json check_for_updates must be a JSON boolean")
endif()

set(BGOBS_SIGNING_STATUS_FILE "${CMAKE_CURRENT_BINARY_DIR}/BGOBS-SIGNING-STATUS.txt")
file(
  GENERATE OUTPUT "${BGOBS_SIGNING_STATUS_FILE}"
  CONTENT
    "BGOBS package signing status: ${BGOBS_PACKAGE_SIGNING_STATUS}\nVersion: ${PLUGIN_VERSION}\nSource: https://github.com/LeMegaGeek/bgobs\n"
)

set(BGOBS_ONNXRUNTIME_COPYRIGHT "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES/MIT.txt")
set(BGOBS_ONNXRUNTIME_NOTICES "")
foreach(
  BGOBS_ONNXRUNTIME_ROOT
  IN
  ITEMS
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/onnxruntime"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps_vendor/onnxruntime"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps_vendor/ort_x64-prefix"
    "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime"
)
  if(EXISTS "${BGOBS_ONNXRUNTIME_ROOT}/LICENSE")
    set(BGOBS_ONNXRUNTIME_COPYRIGHT "${BGOBS_ONNXRUNTIME_ROOT}/LICENSE")
  endif()
  if(EXISTS "${BGOBS_ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt")
    set(BGOBS_ONNXRUNTIME_NOTICES "${BGOBS_ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt")
  endif()
endforeach()

if(MSVC)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows")
  include(defaults)
endif()

if(BGOBS_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
  else()
    message(FATAL_ERROR "BGOBS_ENABLE_SANITIZERS requires Clang or GCC")
  endif()
endif()

if(BGOBS_ENABLE_THREAD_SANITIZER)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=thread -fno-sanitize-recover=all)
    add_link_options(-fsanitize=thread -fno-sanitize-recover=all)
  else()
    message(FATAL_ERROR "BGOBS_ENABLE_THREAD_SANITIZER requires Clang or GCC")
  endif()
endif()

block(SCOPE_FOR POLICIES VARIABLES PROPAGATE OS_LINUX_DETECTED)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(EXISTS "/etc/os-release")
      file(STRINGS "/etc/os-release" OS_RELEASE_ID_LINES REGEX "^(ID|ID_LIKE)=")
      string(TOLOWER "${OS_RELEASE_ID_LINES}" OS_RELEASE_ID_LINES_LOWER)

      if(
        OS_RELEASE_ID_LINES_LOWER MATCHES [[(^|;)id=['"]?ubuntu['"]?($|;)]]
        OR OS_RELEASE_ID_LINES_LOWER MATCHES [[(^|;)id_like=['"]?[^;]*ubuntu]]
      )
        set(OS_LINUX_DETECTED Ubuntu)
      elseif(
        OS_RELEASE_ID_LINES_LOWER MATCHES [[(^|;)id=['"]?debian['"]?($|;)]]
        OR OS_RELEASE_ID_LINES_LOWER MATCHES [[(^|;)id_like=['"]?[^;]*debian]]
      )
        set(OS_LINUX_DETECTED Debian)
      else()
        set(OS_LINUX_DETECTED Linux)
      endif()
    else()
      set(OS_LINUX_DETECTED Linux)
    endif()
  else()
    set(OS_LINUX_DETECTED FALSE)
  endif()
endblock()

set(OS_LINUX ${OS_LINUX_DETECTED} CACHE STRING "Linux variant: Linux Debian Ubuntu FALSE")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND OS_LINUX MATCHES "(Debian|Ubuntu)")
  set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

include(GNUInstallDirs)

### Global configurations
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_C_EXTENSIONS FALSE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

if(MSVC)
  include(compilerconfig)
  include(helpers)
endif()

if(MSVC)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(APPLE OR OS_LINUX)
  add_compile_definitions(SIMDE_ENABLE_OPENMP)
  add_compile_options(-fopenmp-simd -fno-omit-frame-pointer -fstack-protector-strong)
endif()

### Configure paths
if(DEFINED ENV{PLUGIN_BUILD_DIR})
  set(PLUGIN_BUILD_DIR "$ENV{PLUGIN_BUILD_DIR}")
else()
  set(PLUGIN_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(APPLE)
  list(
    PREPEND CMAKE_PREFIX_PATH
    "${PLUGIN_BUILD_DIR}/.deps/obs-deps"
    "${PLUGIN_BUILD_DIR}/.deps/obs-deps-qt6"
    "${PLUGIN_BUILD_DIR}/.deps/Debug"
    "${PLUGIN_BUILD_DIR}/.deps/Debug/Frameworks"
    "${PLUGIN_BUILD_DIR}/.deps/Release"
    "${PLUGIN_BUILD_DIR}/.deps/Release/Frameworks"
    "${PLUGIN_BUILD_DIR}/vcpkg_installed/universal-osx-obs"
    "${PLUGIN_BUILD_DIR}/vcpkg_installed_ort/universal-osx"
    "${PLUGIN_BUILD_DIR}/ort_installed"
  )
endif()

### Package dependencies
find_package(PkgConfig QUIET)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

block(SCOPE_FOR POLICIES VARIABLES PROPAGATE CURL_FOUND)
  find_package(CURL QUIET)

  if(NOT CURL_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(PC_CURL libcurl IMPORTED_TARGET)
    if(PC_CURL_FOUND)
      add_library(CURL::libcurl ALIAS PkgConfig::PC_CURL)
      set(CURL_FOUND TRUE)
    endif()
  endif()

  if(NOT CURL_FOUND)
    message(FATAL_ERROR "CURL not found via CMake or pkg-config.")
  endif()
endblock()

block(SCOPE_FOR POLICIES VARIABLES PROPAGATE OpenCV_FOUND)
  set(OBS_BG_OPENCV_COMPONENTS core imgproc imgcodecs)
  find_package(OpenCV CONFIG QUIET COMPONENTS ${OBS_BG_OPENCV_COMPONENTS})

  foreach(OBS_BG_OPENCV_COMPONENT IN LISTS OBS_BG_OPENCV_COMPONENTS)
    if(NOT TARGET OpenCV::opencv_${OBS_BG_OPENCV_COMPONENT} AND TARGET opencv_${OBS_BG_OPENCV_COMPONENT})
      add_library(OpenCV::opencv_${OBS_BG_OPENCV_COMPONENT} ALIAS opencv_${OBS_BG_OPENCV_COMPONENT})
    endif()
  endforeach()

  if(PkgConfig_FOUND)
    pkg_check_modules(PC_OPENCV QUIET opencv4)
  endif()

  foreach(OBS_BG_OPENCV_COMPONENT IN LISTS OBS_BG_OPENCV_COMPONENTS)
    if(NOT TARGET OpenCV::opencv_${OBS_BG_OPENCV_COMPONENT} AND PC_OPENCV_FOUND)
      string(TOUPPER "${OBS_BG_OPENCV_COMPONENT}" OBS_BG_OPENCV_COMPONENT_UPPER)
      find_library(
        OBS_BG_OPENCV_${OBS_BG_OPENCV_COMPONENT_UPPER}_LIBRARY
        NAMES opencv_${OBS_BG_OPENCV_COMPONENT}
        HINTS ${PC_OPENCV_LIBRARY_DIRS}
      )
      find_path(OBS_BG_OPENCV_INCLUDE_DIR NAMES opencv2/core.hpp HINTS ${PC_OPENCV_INCLUDE_DIRS} PATH_SUFFIXES opencv4)

      if(OBS_BG_OPENCV_${OBS_BG_OPENCV_COMPONENT_UPPER}_LIBRARY AND OBS_BG_OPENCV_INCLUDE_DIR)
        add_library(OBS_BG_OPENCV_${OBS_BG_OPENCV_COMPONENT} UNKNOWN IMPORTED)
        set_target_properties(
          OBS_BG_OPENCV_${OBS_BG_OPENCV_COMPONENT}
          PROPERTIES
            IMPORTED_LOCATION "${OBS_BG_OPENCV_${OBS_BG_OPENCV_COMPONENT_UPPER}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OBS_BG_OPENCV_INCLUDE_DIR}"
        )
        add_library(OpenCV::opencv_${OBS_BG_OPENCV_COMPONENT} ALIAS OBS_BG_OPENCV_${OBS_BG_OPENCV_COMPONENT})
      endif()
    endif()
  endforeach()

  foreach(OBS_BG_OPENCV_COMPONENT IN LISTS OBS_BG_OPENCV_COMPONENTS)
    if(NOT TARGET OpenCV::opencv_${OBS_BG_OPENCV_COMPONENT})
      message(
        FATAL_ERROR
        "OpenCV ${OBS_BG_OPENCV_COMPONENT} was not found. Install the matching OpenCV development component."
      )
    endif()
  endforeach()

  set(OpenCV_FOUND TRUE)
endblock()

block(SCOPE_FOR POLICIES VARIABLES PROPAGATE onnxruntime_FOUND OBS_BG_ONNXRUNTIME_DELAYLOAD)
  set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON)
  set(OBS_BG_ONNXRUNTIME_DELAYLOAD FALSE)

  if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
  endif()

  find_package(onnxruntime CONFIG)

  if(NOT onnxruntime_FOUND AND WIN32)
    find_path(OBS_BG_ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h PATH_SUFFIXES include)
    find_library(OBS_BG_ONNXRUNTIME_LIBRARY NAMES onnxruntime PATH_SUFFIXES lib)
    find_file(OBS_BG_ONNXRUNTIME_RUNTIME NAMES onnxruntime.dll PATH_SUFFIXES bin lib)

    if(OBS_BG_ONNXRUNTIME_INCLUDE_DIR AND OBS_BG_ONNXRUNTIME_LIBRARY)
      add_library(onnxruntime::onnxruntime SHARED IMPORTED)
      set_target_properties(
        onnxruntime::onnxruntime
        PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${OBS_BG_ONNXRUNTIME_INCLUDE_DIR}"
          IMPORTED_IMPLIB "${OBS_BG_ONNXRUNTIME_LIBRARY}"
      )
      if(OBS_BG_ONNXRUNTIME_RUNTIME)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES IMPORTED_LOCATION "${OBS_BG_ONNXRUNTIME_RUNTIME}")
      endif()
      set(onnxruntime_FOUND TRUE)
      set(OBS_BG_ONNXRUNTIME_DELAYLOAD TRUE)
    endif()
  endif()

  if(NOT onnxruntime_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(PC_onnxruntime onnxruntime IMPORTED_TARGET)
    if(PC_onnxruntime_FOUND)
      add_library(onnxruntime::onnxruntime ALIAS PkgConfig::PC_onnxruntime)
      set(onnxruntime_FOUND TRUE)
    endif()
  endif()

  if(NOT onnxruntime_FOUND)
    message(FATAL_ERROR "ONNX Runtime not found via CMake or pkg-config.")
  endif()

  if(NOT TARGET onnxruntime::onnxruntime)
    add_library(onnxruntime::onnxruntime ALIAS onnxruntime)
  endif()
endblock()

### Rust core
find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(BGOBS_CORE_CARGO_PROFILE "release" CACHE STRING "Cargo profile used to build bgobs-core")
set_property(CACHE BGOBS_CORE_CARGO_PROFILE PROPERTY STRINGS debug release)

if(NOT BGOBS_CORE_CARGO_PROFILE MATCHES "^(debug|release)$")
  message(FATAL_ERROR "BGOBS_CORE_CARGO_PROFILE must be either debug or release.")
endif()

if(BGOBS_CORE_CARGO_PROFILE STREQUAL "release")
  set(BGOBS_CORE_CARGO_BUILD_ARGS --release)
else()
  set(BGOBS_CORE_CARGO_BUILD_ARGS)
endif()

set(BGOBS_CORE_CRATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/crates/bgobs-core")
set(BGOBS_CORE_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(
  BGOBS_CORE_STATIC_LIBRARY
  "${BGOBS_CORE_CARGO_TARGET_DIR}/${BGOBS_CORE_CARGO_PROFILE}/${CMAKE_STATIC_LIBRARY_PREFIX}bgobs_core${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

file(GLOB_RECURSE BGOBS_CORE_RUST_SOURCES CONFIGURE_DEPENDS "${BGOBS_CORE_CRATE_DIR}/src/*.rs")

add_custom_command(
  OUTPUT "${BGOBS_CORE_STATIC_LIBRARY}"
  COMMAND
    "${CARGO_EXECUTABLE}" build --manifest-path "${BGOBS_CORE_CRATE_DIR}/Cargo.toml" --target-dir
    "${BGOBS_CORE_CARGO_TARGET_DIR}" ${BGOBS_CORE_CARGO_BUILD_ARGS}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
    "${BGOBS_CORE_CRATE_DIR}/Cargo.toml"
    ${BGOBS_CORE_RUST_SOURCES}
  COMMENT "Building Rust crate bgobs-core"
  VERBATIM
)

add_custom_target(bgobs-core-rust DEPENDS "${BGOBS_CORE_STATIC_LIBRARY}")
add_library(bgobs_core INTERFACE)
add_dependencies(bgobs_core bgobs-core-rust)
target_include_directories(bgobs_core INTERFACE "${BGOBS_CORE_CRATE_DIR}/include")

set(BGOBS_CORE_NATIVE_LIBRARIES)
if(WIN32)
  list(
    APPEND BGOBS_CORE_NATIVE_LIBRARIES
    advapi32
    bcrypt
    ntdll
    userenv
    ws2_32
  )
elseif(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  list(
    APPEND BGOBS_CORE_NATIVE_LIBRARIES
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
    rt
    util
  )
endif()

target_link_libraries(bgobs_core INTERFACE "${BGOBS_CORE_STATIC_LIBRARY}" ${BGOBS_CORE_NATIVE_LIBRARIES})

### Plugin main
set(
  BACKGROUND_FILTER_SOURCES
  src/background-filter-info.c
  src/background-filter.cpp
  src/background/mask-post-processing.cpp
)

set(ENHANCE_FILTER_SOURCES src/enhance-filter.cpp src/enhance-filter-info.c)

set(CACAM_USB_SOURCE_SOURCES src/cacam-usb-source.cpp src/cacam-usb-source-info.c)

set(
  PLUGIN_SOURCES
  src/plugin-main.c
  src/plugin-support.c
  src/ort-utils/ort-session-utils.cpp
  src/obs-utils/obs-utils.cpp
  src/obs-utils/obs-config-utils.cpp
  src/update-checker/github-utils.cpp
  src/update-checker/semver.cpp
  src/update-checker/update-checker.cpp
  ${BACKGROUND_FILTER_SOURCES}
  ${ENHANCE_FILTER_SOURCES}
  ${CACAM_USB_SOURCE_SOURCES}
)

add_library(${CMAKE_PROJECT_NAME} MODULE)

if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W4 /WX /MP)
elseif(APPLE OR OS_LINUX)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
endif()

add_subdirectory(src/update-checker/CurlClient)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    onnxruntime::onnxruntime
    CurlClient
    CURL::libcurl
    OpenCV::opencv_core
    OpenCV::opencv_imgcodecs
    OpenCV::opencv_imgproc
    bgobs_core
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BGOBS_VERSION_STRING="${PLUGIN_VERSION}")

include(CTest)
if(BUILD_TESTING)
  set(
    MASK_POST_PROCESSING_TEST_SOURCES
    tests/background/mask-post-processing-test.cpp
    src/background/mask-post-processing.cpp
  )

  add_executable(mask-post-processing-test)
  target_sources(mask-post-processing-test PRIVATE ${MASK_POST_PROCESSING_TEST_SOURCES})
  target_include_directories(mask-post-processing-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(mask-post-processing-test PRIVATE OpenCV::opencv_core OpenCV::opencv_imgproc bgobs_core)
  add_test(NAME mask-post-processing COMMAND mask-post-processing-test)

  add_executable(semver-test tests/update-checker/semver-test.cpp src/update-checker/semver.cpp)
  target_include_directories(semver-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME semver COMMAND semver-test)
endif()

### Hide symbols
if(APPLE)
  target_link_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/src/exported_symbols_macos.txt"
  )
elseif(MSVC)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/exported_symbols_windows.def")
  if(OBS_BG_ONNXRUNTIME_DELAYLOAD)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/DelayLoad.cpp")
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE delayimp)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "/DELAYLOAD:onnxruntime.dll")
  endif()
elseif(OS_LINUX)
  target_link_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/src/exported_symbols_linux.map" "-Wl,-Bsymbolic"
  )
endif()

### Install configurations
set(
  EFFECTS_FILES
  data/effects/blend_images.effect
  data/effects/kawase_blur.effect
  data/effects/mask_alpha_filter.effect
)

set(GENERAL_DATA_FILES data/config.json data/manifest.json)
set(LEGAL_FILES LICENSE NOTICE THIRD_PARTY_NOTICES.md)

function(bgobs_install_vcpkg_copyrights DESTINATION)
  set(BGOBS_VCPKG_SHARE_DIRS)
  set(BGOBS_VCPKG_COPYRIGHTS_REQUIRED FALSE)

  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND BGOBS_VCPKG_SHARE_DIRS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
    set(BGOBS_VCPKG_COPYRIGHTS_REQUIRED TRUE)
  endif()

  if(APPLE)
    list(APPEND BGOBS_VCPKG_SHARE_DIRS "${PLUGIN_BUILD_DIR}/vcpkg_installed/universal-osx-obs/share")
    set(BGOBS_VCPKG_COPYRIGHTS_REQUIRED TRUE)
  elseif(MSVC)
    list(APPEND BGOBS_VCPKG_SHARE_DIRS "${PLUGIN_BUILD_DIR}/.deps_vendor/libusb_installed/x64-windows/share")
  endif()

  list(REMOVE_DUPLICATES BGOBS_VCPKG_SHARE_DIRS)
  set(BGOBS_VCPKG_COPYRIGHT_COUNT 0)
  foreach(BGOBS_VCPKG_SHARE_DIR IN LISTS BGOBS_VCPKG_SHARE_DIRS)
    if(NOT IS_DIRECTORY "${BGOBS_VCPKG_SHARE_DIR}")
      continue()
    endif()
    file(GLOB BGOBS_VCPKG_COPYRIGHT_FILES LIST_DIRECTORIES FALSE "${BGOBS_VCPKG_SHARE_DIR}/*/copyright")
    foreach(BGOBS_VCPKG_COPYRIGHT IN LISTS BGOBS_VCPKG_COPYRIGHT_FILES)
      get_filename_component(BGOBS_VCPKG_PORT_DIR "${BGOBS_VCPKG_COPYRIGHT}" DIRECTORY)
      get_filename_component(BGOBS_VCPKG_PORT_NAME "${BGOBS_VCPKG_PORT_DIR}" NAME)
      install(
        FILES "${BGOBS_VCPKG_COPYRIGHT}"
        DESTINATION "${DESTINATION}/third-party-licenses/vcpkg/${BGOBS_VCPKG_PORT_NAME}"
      )
      math(EXPR BGOBS_VCPKG_COPYRIGHT_COUNT "${BGOBS_VCPKG_COPYRIGHT_COUNT} + 1")
    endforeach()
  endforeach()

  if(BGOBS_VCPKG_COPYRIGHTS_REQUIRED AND BGOBS_VCPKG_COPYRIGHT_COUNT EQUAL 0)
    message(FATAL_ERROR "No vcpkg copyright files were found for the packaged static dependencies")
  endif()
endfunction()

set(
  LOCALE_FILES
  data/locale/ar-EG.ini
  data/locale/bn-IN.ini
  data/locale/en-US.ini
  data/locale/es-SP.ini
  data/locale/fr-FR.ini
  data/locale/hi-IN.ini
  data/locale/it-IT.ini
  data/locale/ja-JP.ini
  data/locale/ko-KR.ini
  data/locale/pt-BR.ini
  data/locale/ru-RU.ini
  data/locale/ta-IN.ini
  data/locale/tr-TR.ini
  data/locale/zh-CN.ini
)

set(
  MODELS_FILES
  data/models/mediapipe.with_runtime_opt.ort
  data/models/mediapipe.with_runtime_opt.ort.license
  data/models/pphumanseg_fp32.with_runtime_opt.ort
  data/models/pphumanseg_fp32.with_runtime_opt.ort.license
  data/models/rvm_mobilenetv3_fp32.with_runtime_opt.ort
  data/models/rvm_mobilenetv3_fp32.with_runtime_opt.ort.license
  data/models/selfie_multiclass_256x256.with_runtime_opt.ort
  data/models/selfie_multiclass_256x256.with_runtime_opt.ort.license
  data/models/selfie_segmentation.with_runtime_opt.ort
  data/models/selfie_segmentation.with_runtime_opt.ort.license
  data/models/semantic_guided_llie_180x324.with_runtime_opt.ort
  data/models/semantic_guided_llie_180x324.with_runtime_opt.ort.license
  data/models/SINet_Softmax_simple.with_runtime_opt.ort
  data/models/SINet_Softmax_simple.with_runtime_opt.ort.license
  data/models/tbefn_fp32.with_runtime_opt.ort
  data/models/tbefn_fp32.with_runtime_opt.ort.license
  data/models/tcmonodepth_tcsmallnet_192x320.with_runtime_opt.ort
  data/models/tcmonodepth_tcsmallnet_192x320.with_runtime_opt.ort.license
  data/models/uretinex_net_180x320.with_runtime_opt.ort
  data/models/uretinex_net_180x320.with_runtime_opt.ort.license
)

if(APPLE)
  find_library(
    BGOBS_LIBUSB_RUNTIME
    NAMES usb-1.0
    PATHS "${PLUGIN_BUILD_DIR}/vcpkg_installed/universal-osx-obs/lib"
    NO_DEFAULT_PATH
  )
  set(BGOBS_LIBUSB_COPYRIGHT "${PLUGIN_BUILD_DIR}/vcpkg_installed/universal-osx-obs/share/libusb/copyright")
  if(NOT BGOBS_LIBUSB_RUNTIME OR NOT EXISTS "${BGOBS_LIBUSB_COPYRIGHT}")
    message(FATAL_ERROR "The universal macOS libusb runtime or its copyright file is missing")
  endif()

  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "plugin" INSTALL_RPATH "@loader_path/../Frameworks"
  )
  set_source_files_properties(${GENERAL_DATA_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  set_source_files_properties(${LEGAL_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  set_source_files_properties("${BGOBS_SIGNING_STATUS_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  set_source_files_properties(${EFFECTS_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/effects")
  set_source_files_properties(${LOCALE_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/locale")
  set_source_files_properties(${MODELS_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/models")
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      ${GENERAL_DATA_FILES}
      ${LEGAL_FILES}
      "${BGOBS_SIGNING_STATUS_FILE}"
      ${EFFECTS_FILES}
      ${LOCALE_FILES}
      ${MODELS_FILES}
  )

  install(
    TARGETS "${CMAKE_PROJECT_NAME}"
    BUNDLE DESTINATION "Library/Application Support/obs-studio/plugins"
    LIBRARY DESTINATION "Library/Application Support/obs-studio/plugins"
  )
  install(
    DIRECTORY "${PLUGIN_BUILD_DIR}/vcpkg_installed/universal-osx-obs/lib/"
    DESTINATION "Library/Application Support/obs-studio/plugins/${CMAKE_PROJECT_NAME}.plugin/Contents/Frameworks"
    FILES_MATCHING
    PATTERN "libusb-1.0*.dylib"
  )
  install(
    FILES "${BGOBS_LIBUSB_COPYRIGHT}"
    DESTINATION "Library/Application Support/obs-studio/plugins/${CMAKE_PROJECT_NAME}.plugin/Contents/Resources"
    RENAME libusb-copyright
  )
  install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES"
    DESTINATION "Library/Application Support/obs-studio/plugins/${CMAKE_PROJECT_NAME}.plugin/Contents/Resources"
  )
  bgobs_install_vcpkg_copyrights(
    "Library/Application Support/obs-studio/plugins/${CMAKE_PROJECT_NAME}.plugin/Contents/Resources"
  )
  install(
    FILES "${BGOBS_ONNXRUNTIME_COPYRIGHT}"
    DESTINATION "Library/Application Support/obs-studio/plugins/${CMAKE_PROJECT_NAME}.plugin/Contents/Resources"
    RENAME onnxruntime-copyright
  )
  if(BGOBS_ONNXRUNTIME_NOTICES)
    install(
      FILES "${BGOBS_ONNXRUNTIME_NOTICES}"
      DESTINATION "Library/Application Support/obs-studio/plugins/${CMAKE_PROJECT_NAME}.plugin/Contents/Resources"
      RENAME onnxruntime-third-party-notices.txt
    )
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/pkg/Distribution.xml.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Distribution"
    @ONLY
  )

  install(
    CODE
      "set(INSTALL_DIR \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
set(PLUGIN_DIR \"\${INSTALL_DIR}/Library/Application Support/obs-studio/plugins\")

set(BUNDLE_PATH \"\${PLUGIN_DIR}/${CMAKE_PROJECT_NAME}.plugin\")

if(EXISTS \"\${BUNDLE_PATH}\")
  message(STATUS \"Re-signing: \${BUNDLE_PATH}...\")

  execute_process(
    COMMAND xattr -cr \"\${BUNDLE_PATH}\"
    COMMAND_ERROR_IS_FATAL NONE
  )

  execute_process(
    COMMAND dot_clean -m \"\${BUNDLE_PATH}\"
    COMMAND_ERROR_IS_FATAL NONE
  )

  execute_process(
    COMMAND codesign -s - -f -o runtime --deep \"\${BUNDLE_PATH}\"
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()"
  )
elseif(MSVC)
  set_target_properties_plugin(${CMAKE_PROJECT_NAME})
  install(FILES ${LEGAL_FILES} "${BGOBS_SIGNING_STATUS_FILE}" DESTINATION "${CMAKE_PROJECT_NAME}/data")
  install(DIRECTORY LICENSES DESTINATION "${CMAKE_PROJECT_NAME}/data")
  bgobs_install_vcpkg_copyrights("${CMAKE_PROJECT_NAME}/data")
  install(FILES "${BGOBS_ONNXRUNTIME_COPYRIGHT}" DESTINATION "${CMAKE_PROJECT_NAME}/data" RENAME onnxruntime-copyright)
  if(BGOBS_ONNXRUNTIME_NOTICES)
    install(
      FILES "${BGOBS_ONNXRUNTIME_NOTICES}"
      DESTINATION "${CMAKE_PROJECT_NAME}/data"
      RENAME onnxruntime-third-party-notices.txt
    )
  endif()
elseif(OS_LINUX MATCHES "(Debian|Ubuntu)")
  set(OBS_PLUGIN_DATA_DIR "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${CMAKE_PROJECT_NAME}")
  set(OBS_PLUGIN_DOC_DIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${CMAKE_PROJECT_NAME}")
  set(OBS_PLUGIN_LIB_DIR "${CMAKE_INSTALL_LIBDIR}/obs-plugins")

  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")

  get_target_property(BGOBS_ONNXRUNTIME_TARGET_TYPE onnxruntime::onnxruntime TYPE)
  if(BGOBS_ONNXRUNTIME_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
    set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES INSTALL_RPATH "\$ORIGIN/bgobs-runtime")
    install(
      IMPORTED_RUNTIME_ARTIFACTS onnxruntime::onnxruntime
      LIBRARY DESTINATION "${OBS_PLUGIN_LIB_DIR}/bgobs-runtime"
    )
    install(FILES "${BGOBS_ONNXRUNTIME_COPYRIGHT}" DESTINATION "${OBS_PLUGIN_DOC_DIR}" RENAME onnxruntime-copyright)
    if(BGOBS_ONNXRUNTIME_NOTICES)
      install(
        FILES "${BGOBS_ONNXRUNTIME_NOTICES}"
        DESTINATION "${OBS_PLUGIN_DOC_DIR}"
        RENAME onnxruntime-third-party-notices.txt
      )
    endif()
  endif()

  install(
    TARGETS "${CMAKE_PROJECT_NAME}"
    BUNDLE DESTINATION "${OBS_PLUGIN_LIB_DIR}"
    LIBRARY DESTINATION "${OBS_PLUGIN_LIB_DIR}"
  )
  install(FILES ${EFFECTS_FILES} DESTINATION "${OBS_PLUGIN_DATA_DIR}/effects")
  install(
    FILES ${GENERAL_DATA_FILES} ${LEGAL_FILES} "${BGOBS_SIGNING_STATUS_FILE}"
    DESTINATION "${OBS_PLUGIN_DATA_DIR}"
  )
  install(DIRECTORY LICENSES DESTINATION "${OBS_PLUGIN_DATA_DIR}")
  bgobs_install_vcpkg_copyrights("${OBS_PLUGIN_DATA_DIR}")
  install(FILES ${LOCALE_FILES} DESTINATION "${OBS_PLUGIN_DATA_DIR}/locale")
  install(FILES ${MODELS_FILES} DESTINATION "${OBS_PLUGIN_DATA_DIR}/models" OPTIONAL)
  install(FILES LICENSE DESTINATION "${OBS_PLUGIN_DOC_DIR}" RENAME copyright)
else()
  message("Installing on this platform is not supported officially.")
endif()

### CPack configurations
set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PLUGIN_VERSION}${PACKAGE_VERSION_SUFFIX}")
set(CPACK_PACKAGE_CONTACT "${PLUGIN_AUTHOR} <${PLUGIN_EMAIL}>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_ARCHIVE_THREADS 0)
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/release")
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_SET_DESTDIR TRUE)
set(CPACK_VERBATIM_VARIABLES TRUE)

set(CPACK_DEBIAN_COMPRESSION_LEVEL 19)
set(CPACK_DEBIAN_COMPRESSION_TYPE zstd)
set(CPACK_DEBIAN_DEBUGINFO_PACKAGE TRUE)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS TRUE)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libusb-1.0-0")

set(BGOBS_ONNXRUNTIME_SHLIBDEPS_PRIVATE_DIRS)
foreach(BGOBS_ONNXRUNTIME_PREFIX IN LISTS CMAKE_PREFIX_PATH)
  foreach(BGOBS_ONNXRUNTIME_LIB_SUFFIX lib lib64)
    if(EXISTS "${BGOBS_ONNXRUNTIME_PREFIX}/${BGOBS_ONNXRUNTIME_LIB_SUFFIX}")
      list(
        APPEND BGOBS_ONNXRUNTIME_SHLIBDEPS_PRIVATE_DIRS
        "${BGOBS_ONNXRUNTIME_PREFIX}/${BGOBS_ONNXRUNTIME_LIB_SUFFIX}"
      )
    endif()
  endforeach()
endforeach()

foreach(
  BGOBS_ONNXRUNTIME_LIB_DIR
  IN
  ITEMS
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/onnxruntime/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/onnxruntime/lib64"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps_vendor/ort_x86_64-prefix/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps_vendor/ort_x86_64-prefix/lib64"
    "${PLUGIN_BUILD_DIR}/.deps/onnxruntime/lib"
    "${PLUGIN_BUILD_DIR}/.deps/onnxruntime/lib64"
    "${PLUGIN_BUILD_DIR}/.deps_vendor/ort_x86_64-prefix/lib"
    "${PLUGIN_BUILD_DIR}/.deps_vendor/ort_x86_64-prefix/lib64"
    "${PLUGIN_BUILD_DIR}/ort_installed/lib"
    "${PLUGIN_BUILD_DIR}/ort_installed/lib64"
)
  if(
    EXISTS "${BGOBS_ONNXRUNTIME_LIB_DIR}/libonnxruntime.so"
    OR EXISTS "${BGOBS_ONNXRUNTIME_LIB_DIR}/libonnxruntime.so.1"
    OR EXISTS "${BGOBS_ONNXRUNTIME_LIB_DIR}/libonnxruntime.dylib"
  )
    list(APPEND BGOBS_ONNXRUNTIME_SHLIBDEPS_PRIVATE_DIRS "${BGOBS_ONNXRUNTIME_LIB_DIR}")
  endif()
endforeach()

if(BGOBS_ONNXRUNTIME_SHLIBDEPS_PRIVATE_DIRS)
  list(REMOVE_DUPLICATES BGOBS_ONNXRUNTIME_SHLIBDEPS_PRIVATE_DIRS)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "${BGOBS_ONNXRUNTIME_SHLIBDEPS_PRIVATE_DIRS}")
endif()

include(CPack) # This line must be placed after all the package configurations

### Export variables
block(SCOPE_FOR POLICIES VARIABLES)
  set(SH_CONTENT "")
  set(PS1_CONTENT "")

  foreach(
    VAR_NAME
    PLUGIN_BUNDLE_ID
    PLUGIN_DISPLAY_NAME
    PLUGIN_NAME
    PLUGIN_PACKAGE_VERSION
    PLUGIN_VERSION
  )
    set(VAR_VALUE "${${VAR_NAME}}")
    if(NOT VAR_VALUE MATCHES [[^[-A-Za-z0-9._ ]+$]])
      message(FATAL_ERROR "${VAR_NAME} contains unsupported characters: ${VAR_VALUE}")
    endif()
    string(APPEND SH_CONTENT "${VAR_NAME}=\'${VAR_VALUE}\'\n")
    string(APPEND PS1_CONTENT "\$${VAR_NAME} = \'${VAR_VALUE}\'\n")
  endforeach()

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_export.sh" "${SH_CONTENT}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_export.ps1" "${PS1_CONTENT}")
endblock()
