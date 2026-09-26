cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GDMenu VERSION 1.0.0)

# Add all source files inside src (recursively)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
# Apple platforms: Objective-C++ sources (clipboard)
if (APPLE)
    file(GLOB_RECURSE APPLE_SOURCES CONFIGURE_DEPENDS src/*.mm)
    list(APPEND SOURCES ${APPLE_SOURCES})
endif()

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# GDReplayFormat v2 (header-only). DOWNLOAD_ONLY so its test executable isn't built/run when cross-compiling.
CPMAddPackage(
    NAME libGDR
    GITHUB_REPOSITORY maxnut/GDReplayFormat
    GIT_TAG 2a0ca3cff9b1d16863037a464da5fe27c28a5823
    DOWNLOAD_ONLY YES
)
target_include_directories(${PROJECT_NAME} PRIVATE ${libGDR_SOURCE_DIR}/include)

# Set up dependencies, resources, and link Geode.
setup_geode_mod(${PROJECT_NAME})
