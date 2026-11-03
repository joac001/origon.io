cmake_minimum_required(VERSION 3.24)

project(origon
    VERSION 0.1.0
    DESCRIPTION "Origon - custom C++20 FPS engine and game"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(ORIGON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(cmake/Dependencies.cmake)

# Warning flags for our own targets only (dependencies keep their own flags).
add_library(origon_warnings INTERFACE)
if(MSVC)
    target_compile_options(origon_warnings INTERFACE /W4 /permissive-)
    if(ORIGON_WARNINGS_AS_ERRORS)
        target_compile_options(origon_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(origon_warnings INTERFACE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
    if(ORIGON_WARNINGS_AS_ERRORS)
        target_compile_options(origon_warnings INTERFACE -Werror)
    endif()
endif()

add_subdirectory(engine)
add_subdirectory(game)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
