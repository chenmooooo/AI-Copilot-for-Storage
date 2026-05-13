# ============================================================
# Proxy configuration
# Set via: cmake -B build -DHTTP_PROXY=http://127.0.0.1:7897
# Or set http_proxy / https_proxy environment variables.
# ============================================================
if(HTTP_PROXY)
    if(NOT DEFINED ENV{http_proxy})
        set(ENV{http_proxy} "${HTTP_PROXY}")
    endif()
    if(NOT DEFINED ENV{https_proxy})
        set(ENV{https_proxy} "${HTTP_PROXY}")
    endif()
    message(STATUS "Using HTTP proxy: ${HTTP_PROXY}")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ============================================================
# nlohmann/json - header-only JSON library
# ============================================================
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ============================================================
# CPR - C++ HTTP Client (libcurl wrapper)
# Build as static library to avoid missing cpr.dll at runtime.
# ============================================================
set(CPR_FORCE_USE_SYSTEM_CURL OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_STATIC_CRT ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.11.1
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(cpr)

# ============================================================
# imgui - immediate mode GUI (docking branch)
# ============================================================
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8-docking.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

# ============================================================
# sqlite3 - embedded database (amalgamation)
# ============================================================
FetchContent_Declare(
    sqlite3
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3480000.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sqlite3)

# ============================================================
# Organize third-party targets into Visual Studio solution folders
# ============================================================
if(TARGET cpr)
    set_property(TARGET cpr PROPERTY FOLDER "external/cpr")
endif()
if(TARGET libcurl_static)
    set_property(TARGET libcurl_static PROPERTY FOLDER "external/curl")
endif()
if(TARGET libcurl_object)
    set_property(TARGET libcurl_object PROPERTY FOLDER "external/curl")
endif()
if(TARGET curl_uninstall)
    set_property(TARGET curl_uninstall PROPERTY FOLDER "external/curl")
endif()
if(TARGET "curl-generate-curl-config.1")
    set_property(TARGET "curl-generate-curl-config.1" PROPERTY FOLDER "external/curl")
endif()
if(TARGET "curl-generate-mk-ca-bundle.1")
    set_property(TARGET "curl-generate-mk-ca-bundle.1" PROPERTY FOLDER "external/curl")
endif()
if(TARGET curl-man)
    set_property(TARGET curl-man PROPERTY FOLDER "external/curl")
endif()
if(TARGET curl-opts-man)
    set_property(TARGET curl-opts-man PROPERTY FOLDER "external/curl")
endif()
if(TARGET zlib)
    set_property(TARGET zlib PROPERTY FOLDER "external/zlib")
endif()
foreach(POPULATE_TARGET
    cpr-populate curl-populate imgui-populate
    nlohmann_json-populate sqlite3-populate zlib-populate
)
    if(TARGET ${POPULATE_TARGET})
        set_property(TARGET ${POPULATE_TARGET} PROPERTY FOLDER "external/populate")
    endif()
endforeach()
