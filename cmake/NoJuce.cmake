# Fails the build when JUCE sources, modules, or Projucer files are present.
# Documentation may name JUCE where it describes the port. Source and build
# files may not.
if(NOT DEFINED GHOVLZ_SOURCE_DIR)
    set(GHOVLZ_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
file(GLOB_RECURSE ghovlz_sources
    "${GHOVLZ_SOURCE_DIR}/src/*.h"
    "${GHOVLZ_SOURCE_DIR}/src/*.hpp"
    "${GHOVLZ_SOURCE_DIR}/src/*.cpp"
    "${GHOVLZ_SOURCE_DIR}/plugins/*.cpp"
    "${GHOVLZ_SOURCE_DIR}/plugins/*.h"
    "${GHOVLZ_SOURCE_DIR}/scanner/*.cpp"
    "${GHOVLZ_SOURCE_DIR}/tests/*.cpp"
    "${GHOVLZ_SOURCE_DIR}/CMakeLists.txt"
    "${GHOVLZ_SOURCE_DIR}/cmake/*.cmake")
file(GLOB_RECURSE ghovlz_jucer "${GHOVLZ_SOURCE_DIR}/*.jucer")
if(ghovlz_jucer)
    message(FATAL_ERROR "JUCE Projucer file is not allowed: ${ghovlz_jucer}")
endif()
set(ghovlz_hits "")
foreach(f ${ghovlz_sources})
    get_filename_component(ghovlz_name "${f}" NAME)
    if(ghovlz_name STREQUAL "NoJuce.cmake")
        continue()
    endif()
    file(READ "${f}" body)
    string(REGEX MATCH "juce::|JuceHeader|juce_|JUCE_MODULE|find_package\\(JUCE|gin::|melatonin_blur|MelatoninBlur" hit "${body}")
    if(hit)
        list(APPEND ghovlz_hits "${f}")
    endif()
endforeach()
if(ghovlz_hits)
    message(FATAL_ERROR "JUCE reference in source or CMake (LIC-08): ${ghovlz_hits}")
endif()
message(STATUS "No JUCE sources, modules, or Projucer files")
