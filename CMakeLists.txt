cmake_minimum_required(VERSION 3.24)
project(GHOVLZ_DAW VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 6.0.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(rtmidi)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(miniaudio)
if(NOT miniaudio_POPULATED)
    FetchContent_Populate(miniaudio)
endif()

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 9600522de53973336fa033a289858a4354cbb04c
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_BUNDLE_FOR_WINDOWS ON CACHE BOOL "" FORCE)
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.8.1_build_84
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "base;cmake;pluginterfaces;public.sdk")
FetchContent_GetProperties(vst3sdk)
if(NOT vst3sdk_POPULATED)
    FetchContent_Populate(vst3sdk)
endif()

set(SDK_ROOT "${vst3sdk_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${SDK_ROOT}/cmake/modules")
include(SMTG_VST3_SDK)
smtg_setup_platform_toolset()
smtg_setup_symbol_visibility()
include_directories(${SDK_ROOT})
smtg_create_lib_base_target()
smtg_create_pluginterfaces_target()
smtg_create_public_sdk_common_target()
target_sources(sdk_common PRIVATE "${SDK_ROOT}/public.sdk/source/common/memorystream.cpp")
smtg_create_public_sdk_target()
target_sources(sdk PRIVATE "${SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp")
set_source_files_properties("${SDK_ROOT}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
    PROPERTIES COMPILE_DEFINITIONS PROJECT_INCLUDES_VSTEDITCONTROLLER=1)
smtg_create_public_sdk_hosting_target()
if(WIN32)
    target_sources(sdk_hosting PRIVATE "${SDK_ROOT}/public.sdk/source/vst/hosting/module_win32.cpp")
else()
    target_sources(sdk_hosting PRIVATE "${SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp")
    target_link_libraries(sdk_hosting PUBLIC dl)
endif()

set(GHOVLZ_VST3_LIBS sdk sdk_hosting sdk_common base pluginterfaces)

if(MSVC)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${cfg} cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_u} "${CMAKE_BINARY_DIR}/bin/${cfg}")
endforeach()

add_library(ghovlz_core STATIC
    src/core/RtGuard.cpp
    src/dsp/Devices.cpp
    src/io/WavFile.cpp
    src/model/Project.cpp
    src/engine/Engine.cpp
    src/host/PluginHost.cpp
    src/audio/FakeBackend.cpp)
target_include_directories(ghovlz_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ghovlz_core PUBLIC nlohmann_json::nlohmann_json ${GHOVLZ_VST3_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(ghovlz_core PUBLIC pthread dl)
endif()

if(WIN32)
    set(GHOVLZ_VST3_MAIN "${SDK_ROOT}/public.sdk/source/main/dllmain.cpp")
elseif(APPLE)
    set(GHOVLZ_VST3_MAIN "${SDK_ROOT}/public.sdk/source/main/macmain.cpp")
else()
    set(GHOVLZ_VST3_MAIN "${SDK_ROOT}/public.sdk/source/main/linuxmain.cpp")
endif()
smtg_add_vst3plugin(GhovlzTestGain PACKAGE_NAME "GHOVLZ Test Gain" SOURCES_LIST plugins/Gain.cpp "${GHOVLZ_VST3_MAIN}")
target_link_libraries(GhovlzTestGain PRIVATE sdk sdk_common base pluginterfaces)
smtg_add_vst3plugin(GhovlzCrash PACKAGE_NAME "GHOVLZ Crash" SOURCES_LIST plugins/Crash.cpp "${GHOVLZ_VST3_MAIN}")
target_link_libraries(GhovlzCrash PRIVATE sdk sdk_common base pluginterfaces)

add_executable(GhovlzPluginScan scanner/main.cpp)
target_link_libraries(GhovlzPluginScan PRIVATE ${GHOVLZ_VST3_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(GhovlzPluginScan PRIVATE pthread dl)
endif()

add_executable(GhovlzTests tests/Tests.cpp)
target_link_libraries(GhovlzTests PRIVATE ghovlz_core Catch2::Catch2WithMain)
add_dependencies(GhovlzTests GhovlzTestGain GhovlzCrash GhovlzPluginScan)

if(WIN32)
    option(GHOVLZ_WITH_ASIO "Build the ASIO backend. The SDK is downloaded into the build tree unless GHOVLZ_ASIO_SDK_DIR is set." ON)
else()
    option(GHOVLZ_WITH_ASIO "Build the ASIO backend. Windows only." OFF)
endif()
set(GHOVLZ_ASIO_SDK_DIR "" CACHE PATH "Local ASIO SDK directory containing common/asio.h. Leave empty to download it into the build tree.")

add_executable(GhovlzDAW
    src/app/main.cpp
    src/audio/AudioIO.cpp
    src/audio/AsioBackend.cpp
    src/midi/MidiIO.cpp
    src/ui/SoftwareCanvas.cpp
    src/ui/Paint.cpp)
if(WIN32)
    target_sources(GhovlzDAW PRIVATE src/ui/Direct2DCanvas.cpp src/app/PlatformWin.cpp)
    target_link_libraries(GhovlzDAW PRIVATE d3d11 dxgi d2d1 dwrite windowscodecs ole32 uuid avrt)
else()
    target_sources(GhovlzDAW PRIVATE src/app/PlatformLinux.cpp)
    find_package(X11 REQUIRED)
    target_link_libraries(GhovlzDAW PRIVATE X11)
endif()
target_include_directories(GhovlzDAW PRIVATE "${miniaudio_SOURCE_DIR}" "${stb_SOURCE_DIR}")
target_link_libraries(GhovlzDAW PRIVATE ghovlz_core rtmidi)
target_compile_definitions(GhovlzDAW PRIVATE GHOVLZ_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
if(GHOVLZ_WITH_ASIO)
    if(NOT WIN32)
        message(FATAL_ERROR "GHOVLZ_WITH_ASIO is the Windows ASIO host. Configure it on Windows.")
    endif()
    set(_ghovlz_asio "${GHOVLZ_ASIO_SDK_DIR}")
    if(NOT _ghovlz_asio)
        set(_ghovlz_asio_zip "${CMAKE_BINARY_DIR}/asio-sdk/ASIO-SDK_2.3.4_2025-10-15.zip")
        set(_ghovlz_asio_root "${CMAKE_BINARY_DIR}/asio-sdk")
        if(NOT EXISTS "${_ghovlz_asio_root}/ASIOSDK/common/asio.h")
            message(STATUS "Downloading the ASIO SDK into the build tree")
            file(DOWNLOAD
                https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
                "${_ghovlz_asio_zip}"
                EXPECTED_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca
                SHOW_PROGRESS
                STATUS _ghovlz_asio_status)
            list(GET _ghovlz_asio_status 0 _ghovlz_asio_code)
            if(NOT _ghovlz_asio_code EQUAL 0)
                message(FATAL_ERROR "ASIO SDK download failed: ${_ghovlz_asio_status}. Set GHOVLZ_ASIO_SDK_DIR to a local copy.")
            endif()
            file(ARCHIVE_EXTRACT INPUT "${_ghovlz_asio_zip}" DESTINATION "${_ghovlz_asio_root}")
        endif()
        set(_ghovlz_asio "${_ghovlz_asio_root}/ASIOSDK")
    endif()
    if(NOT EXISTS "${_ghovlz_asio}/common/asio.h")
        message(FATAL_ERROR "GHOVLZ_ASIO_SDK_DIR does not contain common/asio.h: ${_ghovlz_asio}")
    endif()
    target_compile_definitions(GhovlzDAW PRIVATE GHOVLZ_WITH_ASIO=1)
    target_include_directories(GhovlzDAW PRIVATE
        "${_ghovlz_asio}/common"
        "${_ghovlz_asio}/host"
        "${_ghovlz_asio}/host/pc")
    target_sources(GhovlzDAW PRIVATE
        "${_ghovlz_asio}/host/asiodrivers.cpp"
        "${_ghovlz_asio}/host/pc/asiolist.cpp")
    set(_ghovlz_asio_prefix "${CMAKE_SOURCE_DIR}/src/audio/asio_host_prefix.h")
    set_source_files_properties(
        "${_ghovlz_asio}/host/asiodrivers.cpp"
        "${_ghovlz_asio}/host/pc/asiolist.cpp"
        TARGET_DIRECTORY GhovlzDAW
        PROPERTIES COMPILE_OPTIONS "/UUNICODE;/U_UNICODE;/FI${_ghovlz_asio_prefix}")
    message(STATUS "ASIO backend enabled from ${_ghovlz_asio}")
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(GhovlzDAW PRIVATE asound pthread)
endif()
set_target_properties(GhovlzDAW PROPERTIES OUTPUT_NAME "GHOVLZ DAW")

include(CTest)
add_test(NAME ghovlz_tests COMMAND GhovlzTests)
add_test(NAME no_juce COMMAND ${CMAKE_COMMAND} -DGHOVLZ_SOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/NoJuce.cmake)
add_test(NAME no_asio_sdk COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/tools/check_no_asio_sdk.py)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT GhovlzDAW)
