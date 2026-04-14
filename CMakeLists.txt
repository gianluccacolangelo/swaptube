cmake_minimum_required(VERSION 3.24)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
message(STATUS "C++ standard set to ${CMAKE_CXX_STANDARD}")
project(swaptube LANGUAGES CXX)
message(STATUS "Project '${PROJECT_NAME}' initialized")

option(SWAPTUBE_ENABLE_SURGE_XT "Enable headless Surge XT integration" OFF)
set(SURGE_XT_SOURCE_DIR "" CACHE PATH "Path to a Surge XT source checkout")

if((NOT SURGE_XT_SOURCE_DIR OR SURGE_XT_SOURCE_DIR STREQUAL "") AND DEFINED ENV{SWAPTUBE_SURGE_XT_SOURCE_DIR})
    set(SURGE_XT_SOURCE_DIR "$ENV{SWAPTUBE_SURGE_XT_SOURCE_DIR}" CACHE PATH "Path to a Surge XT source checkout" FORCE)
endif()

if(NOT APPLE)
    set(ENV{GLIBCXX_FORCE_NEW} 1)
endif()
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libswscale libavcodec libavformat libavdevice libavutil libavfilter)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0 gio-2.0 gdk-pixbuf-2.0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(RSVG REQUIRED IMPORTED_TARGET librsvg-2.0)
pkg_check_modules(LIBPNG REQUIRED IMPORTED_TARGET libpng)
find_package(nlohmann_json REQUIRED)

set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")

# Add globbed cpp files: all .cpp files in src, including subdirectories.
# Exclude the src/Projects/ directory.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/Projects/.*")

if(DEFINED PROJECT_SOURCE_FILE AND NOT PROJECT_SOURCE_FILE STREQUAL "")
    set(ACTIVE_PROJECT_SOURCE "${PROJECT_SOURCE_FILE}")
else()
    file(GLOB_RECURSE ACTIVE_PROJECT_SOURCE CONFIGURE_DEPENDS "src/Projects/.active_project.cpp")
endif()

if(NOT ACTIVE_PROJECT_SOURCE)
    message(FATAL_ERROR "No active project source selected. Pass -DPROJECT_SOURCE_FILE=/abs/path/to/project.cpp or create src/Projects/.active_project.cpp")
endif()

include(CheckLanguage)
message(STATUS "Checking for CUDA and HIP support...")
check_language(CUDA)
check_language(HIP)
if(CMAKE_CUDA_COMPILER AND NOT ${USE_HIP})
    message(STATUS "CUDA compiler detected.")
    enable_language(CUDA)
    add_definitions(-DUSE_GPU)
    message(STATUS "CUDA compiler found: ${CMAKE_CUDA_COMPILER}")
    set(GPU_LANGUAGE CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES native)
    file(GLOB_RECURSE GPU_SOURCES "src/CUDA/*.cu")
    set(CMAKE_CONFIGURE_DEPENDS ${GPU_SOURCES})
    add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:-Xcudafe> $<$<COMPILE_LANGUAGE:CUDA>:"--diag_suppress=20012">)
elseif(CMAKE_HIP_COMPILER)
    message(STATUS "HIP compiler detected.")
    enable_language(HIP)
    add_definitions(-DUSE_GPU)
    message(STATUS "HIP compiler found: ${CMAKE_HIP_COMPILER}")
    set(GPU_LANGUAGE HIP)
    set(CMAKE_HIP_STANDARD 17)
    set(CMAKE_HIP_STANDARD_REQUIRED ON)
    set(CMAKE_HIP_ARCHITECTURES native)
    message(STATUS "Translating CUDA to HIP")
    find_program(HIPIFY_PERL hipify-perl REQUIRED)
    file(GLOB_RECURSE CUDA_TO_TRANSLATE_CU CONFIGURE_DEPENDS "src/CUDA/*.cu")
    file(GLOB_RECURSE CUDA_TO_TRANSLATE_CUH CONFIGURE_DEPENDS "src/CUDA/*.cuh")
    foreach(cuda_file ${CUDA_TO_TRANSLATE_CUH})
        file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}/src/CUDA" "${cuda_file}")
        set(hip_file "${CMAKE_SOURCE_DIR}/src/HIP/${rel_path}")
        add_custom_command(
            OUTPUT ${hip_file}
            COMMAND ${CMAKE_COMMAND} -E copy ${cuda_file} ${hip_file}
            COMMAND ${HIPIFY_PERL} ${hip_file} -inplace
            COMMAND rm "${hip_file}.prehip"
            DEPENDS ${cuda_file}
            COMMAND echo "Hipifying ${rel_path}"
            VERBATIM
        )
        list(APPEND HIP_HEADERS ${hip_file})
    endforeach()
    foreach(cuda_file ${CUDA_TO_TRANSLATE_CU})
        file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}/src/CUDA" "${cuda_file}")
        set(hip_file "${CMAKE_SOURCE_DIR}/src/HIP/${rel_path}")
        add_custom_command(
            OUTPUT ${hip_file}
            COMMAND ${CMAKE_COMMAND} -E copy ${cuda_file} ${hip_file}
            COMMAND ${HIPIFY_PERL} ${hip_file} -inplace
            COMMAND rm "${hip_file}.prehip"
            DEPENDS ${cuda_file} ${HIP_HEADERS}
            COMMAND echo "Hipifying ${rel_path}"
            VERBATIM
        )
        list(APPEND GPU_SOURCES ${hip_file})
    endforeach()
    set_source_files_properties(${GPU_SOURCES} PROPERTIES LANGUAGE HIP)
    set(CMAKE_CONFIGURE_DEPENDS ${GPU_SOURCES})
    # TODO: Check error ouput on CUDA/HIP function calls so we don't have to suppress warnings
    add_compile_options("-Wno-unused-value")
else()
    message(WARNING "Neither CUDA nor HIP found. Please refer to readme for building without GPU support.")
endif()

add_compile_options(-fdiagnostics-color=always)

add_executable(${PROJECT_NAME} ${SOURCES} ${ACTIVE_PROJECT_SOURCE} ${GPU_SOURCES})
message(STATUS "Executable target ${PROJECT_NAME} added")

# Pass user-specified values as preprocessor macros
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PROJECT_NAME_MACRO="${PROJECT_NAME_MACRO}"
    AUDIO_HINTS=${AUDIO_HINTS}
    AUDIO_SFX=${AUDIO_SFX}
)
message(STATUS "Preprocessor definitions added")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROJECT_NAME}
)

if(SWAPTUBE_ENABLE_SURGE_XT)
    if(NOT SURGE_XT_SOURCE_DIR OR NOT EXISTS "${SURGE_XT_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "SWAPTUBE_ENABLE_SURGE_XT is ON, but SURGE_XT_SOURCE_DIR is not a valid Surge XT checkout.")
    endif()

    set(_SWAPTUBE_PREV_CMAKE_PROJECT_VERSION_MAJOR "${CMAKE_PROJECT_VERSION_MAJOR}")
    set(_SWAPTUBE_PREV_CMAKE_PROJECT_VERSION_MINOR "${CMAKE_PROJECT_VERSION_MINOR}")
    file(STRINGS "${SURGE_XT_SOURCE_DIR}/CMakeLists.txt" _SWAPTUBE_SURGE_PROJECT_LINE
        REGEX "^[ \t]*project\\(Surge VERSION [0-9]+\\.[0-9]+\\.[0-9]+"
    )
    if(_SWAPTUBE_SURGE_PROJECT_LINE MATCHES "VERSION[ \t]+([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        set(CMAKE_PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(CMAKE_PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
    else()
        set(CMAKE_PROJECT_VERSION_MAJOR "1")
        set(CMAKE_PROJECT_VERSION_MINOR "4")
    endif()

    set(SURGE_BUILD_FX OFF CACHE BOOL "" FORCE)
    set(SURGE_BUILD_XT OFF CACHE BOOL "" FORCE)
    set(SURGE_BUILD_TESTRUNNER OFF CACHE BOOL "" FORCE)
    set(SURGE_BUILD_PYTHON_BINDINGS OFF CACHE BOOL "" FORCE)
    set(SURGE_BUILD_RS OFF CACHE BOOL "" FORCE)
    set(SURGE_BUILD_RS_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SURGE_COPY_TO_PRODUCTS OFF CACHE BOOL "" FORCE)
    set(SURGE_COPY_AFTER_BUILD OFF CACHE BOOL "" FORCE)
    set(SURGE_SKIP_STANDALONE TRUE CACHE BOOL "" FORCE)
    set(SURGE_SKIP_JUCE_FOR_RACK TRUE CACHE BOOL "" FORCE)
    set(SURGE_SKIP_WERROR TRUE CACHE BOOL "" FORCE)
    set(SURGE_SKIP_PIE_CHANGE TRUE CACHE BOOL "" FORCE)

    add_subdirectory("${SURGE_XT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/surge-xt" EXCLUDE_FROM_ALL)

    set(CMAKE_PROJECT_VERSION_MAJOR "${_SWAPTUBE_PREV_CMAKE_PROJECT_VERSION_MAJOR}")
    set(CMAKE_PROJECT_VERSION_MINOR "${_SWAPTUBE_PREV_CMAKE_PROJECT_VERSION_MINOR}")

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        SWAPTUBE_USE_SURGE_XT=1
        SWAPTUBE_SURGE_XT_DATA_DIR="${SURGE_XT_SOURCE_DIR}/resources/data"
    )
endif()

target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    PkgConfig::GLIB
    PkgConfig::FFMPEG
    PkgConfig::CAIRO
    PkgConfig::RSVG
    PkgConfig::LIBPNG
    ${GPU_LIBRARIES}
    nlohmann_json::nlohmann_json
)

if(SWAPTUBE_ENABLE_SURGE_XT)
    target_link_libraries(${PROJECT_NAME} PRIVATE surge::surge-common)
endif()

message(STATUS "Link libraries configured for ${PROJECT_NAME}")
target_compile_options(${PROJECT_NAME} PRIVATE -Wreorder)
message(STATUS "Configuration complete.")
