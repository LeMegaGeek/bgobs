# SPDX-FileCopyrightText: 2026 Kaito Udagawa <umireon@kaito.tokyo>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

### Options
set(PLUGIN_NAME "obs-backgroundremoval" CACHE STRING "Name of plugin")
set(PLUGIN_DISPLAY_NAME "BGOBS" CACHE STRING "Display name of plugin")
set(PLUGIN_AUTHOR "LeMegaGeek" CACHE STRING "Author of plugin")
set(PLUGIN_EMAIL "d.github@chey.net" CACHE STRING "Author email of plugin")
set(PLUGIN_BUILD_NUMBER "1" CACHE STRING "Build number in plugin metadata")
set(PLUGIN_BUNDLE_ID "com.royshilkrot.obs-backgroundremoval" CACHE STRING "Bundle ID of plugin installer")
set(PACKAGE_VERSION_SUFFIX "" CACHE STRING "Version suffix for plugin package")

### Project
block(SCOPE_FOR POLICIES VARIABLES PROPAGATE PLUGIN_VERSION)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_CONTENT)
  string(STRIP "${VERSION_CONTENT}" PLUGIN_VERSION)
endblock()

project(obs-backgroundremoval VERSION "${PLUGIN_VERSION}" LANGUAGES C CXX)

if(MSVC)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows")
  include(defaults)
endif()

block(SCOPE_FOR POLICIES VARIABLES PROPAGATE OS_LINUX_DETECTED)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(EXISTS "/etc/os-release")
      file(STRINGS "/etc/os-release" OS_RELEASE_ID_LINE REGEX "^ID=")
      string(TOLOWER "${OS_RELEASE_ID_LINE}" OS_RELEASE_ID_LINE_LOWER)

      if(OS_RELEASE_ID_LINE_LOWER MATCHES [[^id=['"]?debian['"]?$]])
        set(OS_LINUX_DETECTED Debian)
      elseif(OS_RELEASE_ID_LINE_LOWER MATCHES [[^id=['"]?ubuntu['"]?$]])
        set(OS_LINUX_DETECTED Ubuntu)
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
  find_package(OpenCV CONFIG QUIET)

  if(NOT OpenCV_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(PC_OPENCV opencv4 IMPORTED_TARGET)
    if(PC_OPENCV_FOUND)
      add_library(OpenCV::opencv_core ALIAS PkgConfig::PC_OPENCV)
      add_library(OpenCV::opencv_imgproc ALIAS PkgConfig::PC_OPENCV)
      set(OpenCV_FOUND TRUE)
    endif()
  endif()

  if(NOT OpenCV_FOUND)
    message(FATAL_ERROR "OpenCV not found via CMake or pkg-config.")
  endif()

  if(NOT TARGET OpenCV::opencv_core)
    add_library(OpenCV::opencv_core ALIAS opencv_core)
  endif()

  if(NOT TARGET OpenCV::opencv_imgproc)
    add_library(OpenCV::opencv_imgproc ALIAS opencv_imgproc)
  endif()
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

set(
  PLUGIN_SOURCES
  src/plugin-main.c
  src/plugin-support.c
  src/ort-utils/ort-session-utils.cpp
  src/obs-utils/obs-utils.cpp
  src/obs-utils/obs-config-utils.cpp
  src/update-checker/github-utils.cpp
  src/update-checker/update-checker.cpp
  ${BACKGROUND_FILTER_SOURCES}
  ${ENHANCE_FILTER_SOURCES}
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
    OpenCV::opencv_core
    OpenCV::opencv_imgproc
    bgobs_core
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})

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
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "plugin")
  set_source_files_properties(${EFFECTS_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/effects")
  set_source_files_properties(${LOCALE_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/locale")
  set_source_files_properties(${MODELS_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/models")
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${EFFECTS_FILES} ${LOCALE_FILES} ${MODELS_FILES})

  install(
    TARGETS "${CMAKE_PROJECT_NAME}"
    BUNDLE DESTINATION "Library/Application Support/obs-studio/plugins"
    LIBRARY DESTINATION "Library/Application Support/obs-studio/plugins"
  )

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
elseif(OS_LINUX MATCHES "(Debian|Ubuntu)")
  set(OBS_PLUGIN_DATA_DIR "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${CMAKE_PROJECT_NAME}")
  set(OBS_PLUGIN_DOC_DIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/${CMAKE_PROJECT_NAME}")
  set(OBS_PLUGIN_LIB_DIR "${CMAKE_INSTALL_LIBDIR}/obs-plugins")

  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")

  install(
    TARGETS "${CMAKE_PROJECT_NAME}"
    BUNDLE DESTINATION "${OBS_PLUGIN_LIB_DIR}"
    LIBRARY DESTINATION "${OBS_PLUGIN_LIB_DIR}"
  )
  install(FILES ${EFFECTS_FILES} DESTINATION "${OBS_PLUGIN_DATA_DIR}/effects")
  install(FILES ${LOCALE_FILES} DESTINATION "${OBS_PLUGIN_DATA_DIR}/locale")
  install(FILES ${MODELS_FILES} DESTINATION "${OBS_PLUGIN_DATA_DIR}/models" OPTIONAL)
  install(FILES LICENSE DESTINATION "${OBS_PLUGIN_DOC_DIR}" RENAME copyright)
else()
  message("Installing on this platform is not supported officially.")
endif()

### CPack configurations
set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}${PACKAGE_VERSION_SUFFIX}")
set(CPACK_PACKAGE_CONTACT "${PLUGIN_AUTHOR} <${PLUGIN_EMAIL}>")

set(CPACK_ARCHIVE_THREADS 0)
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/release")
set(CPACK_SET_DESTDIR TRUE)
set(CPACK_VERBATIM_VARIABLES TRUE)

set(CPACK_DEBIAN_COMPRESSION_LEVEL 19)
set(CPACK_DEBIAN_COMPRESSION_TYPE zstd)
set(CPACK_DEBIAN_DEBUGINFO_PACKAGE TRUE)
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS TRUE)

include(CPack) # This line must be placed after all the package configurations

### Export variables
block(SCOPE_FOR POLICIES VARIABLES)
  set(SH_CONTENT "")
  set(PS1_CONTENT "")

  foreach(VAR_NAME PLUGIN_BUNDLE_ID PLUGIN_DISPLAY_NAME PLUGIN_NAME PLUGIN_VERSION)
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
