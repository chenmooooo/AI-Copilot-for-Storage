cmake_minimum_required(VERSION 3.20)
project(AICopilotForStorage VERSION 0.1.0 LANGUAGES CXX C)

# HTTP proxy for dependency downloads
# Usage: cmake -B build -DHTTP_PROXY=http://127.0.0.1:7897
set(HTTP_PROXY "" CACHE STRING "HTTP proxy URL for downloading dependencies")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC OFF)

# Enable Visual Studio solution folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(src)
