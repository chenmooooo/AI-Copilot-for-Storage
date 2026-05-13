# ============================================================
# Core library - fundamental data structures
# ============================================================
add_library(core_lib STATIC
    core/FileNode.h
    core/FileNode.cpp
    core/Scanner.h
    core/Scanner.cpp
)
target_include_directories(core_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_lib PUBLIC nlohmann_json::nlohmann_json)

# ============================================================
# Database library - SQLite wrapper
# ============================================================
add_library(db_lib STATIC
    db/Database.h
    db/Database.cpp
    ${sqlite3_SOURCE_DIR}/sqlite3.c
)
target_include_directories(db_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${sqlite3_SOURCE_DIR}
)
target_link_libraries(db_lib PUBLIC
    nlohmann_json::nlohmann_json
)

# ============================================================
# AI library - LLM client and prompt system
# ============================================================
add_library(ai_lib STATIC
    ai/AIClient.h
    ai/AIClient.cpp
    ai/DeepSeekProvider.h
    ai/DeepSeekProvider.cpp
    ai/PromptBuilder.h
    ai/PromptBuilder.cpp
)
target_include_directories(ai_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(ai_lib PUBLIC
    nlohmann_json::nlohmann_json
    core_lib
    cpr::cpr
)

# ============================================================
# Rule library - risk analysis engine
# ============================================================
add_library(rule_lib STATIC
    rule/RuleEngine.h
    rule/RuleEngine.cpp
)
target_include_directories(rule_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rule_lib PUBLIC core_lib)

# ============================================================
# UI library - ImGui application (Win32 + D3D11)
# ============================================================
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

add_library(ui_lib STATIC
    ${IMGUI_SOURCES}
    ui/App.h
    ui/App.cpp
)
target_include_directories(ui_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(ui_lib PUBLIC
    d3d11.lib
    d3dcompiler.lib
    ai_lib
    rule_lib
    db_lib
    core_lib
)

# ============================================================
# Main executable
# ============================================================
add_executable(AICopilotForStorage
    main.cpp
)
target_include_directories(AICopilotForStorage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(AICopilotForStorage PRIVATE
    ui_lib
    ai_lib
    rule_lib
    db_lib
    core_lib
    d3d11.lib
    d3dcompiler.lib
    cpr::cpr
    nlohmann_json::nlohmann_json
)

# Copy dependency DLLs to exe directory if they exist (supports both static and shared builds).
# Using cmd.exe to avoid build failure when the file doesn't exist.
add_custom_command(TARGET AICopilotForStorage POST_BUILD
    COMMAND cmd.exe /c "if exist \"${CMAKE_BINARY_DIR}/_deps/cpr-build/cpr/$<CONFIG>/cpr.dll\" copy /y \"${CMAKE_BINARY_DIR}/_deps/cpr-build/cpr/$<CONFIG>/cpr.dll\" \"$<TARGET_FILE_DIR:AICopilotForStorage>\" >nul 2>&1"
    COMMAND cmd.exe /c "if exist \"${CMAKE_BINARY_DIR}/_deps/curl-build/lib/$<CONFIG>/libcurl.dll\" copy /y \"${CMAKE_BINARY_DIR}/_deps/curl-build/lib/$<CONFIG>/libcurl.dll\" \"$<TARGET_FILE_DIR:AICopilotForStorage>\" >nul 2>&1"
    COMMAND cmd.exe /c "if exist \"${CMAKE_BINARY_DIR}/_deps/zlib-build/$<CONFIG>/zlib.dll\" copy /y \"${CMAKE_BINARY_DIR}/_deps/zlib-build/$<CONFIG>/zlib.dll\" \"$<TARGET_FILE_DIR:AICopilotForStorage>\" >nul 2>&1"
)

# Organize project targets into Visual Studio solution folders
set_property(TARGET core_lib PROPERTY FOLDER "src")
set_property(TARGET db_lib PROPERTY FOLDER "src")
set_property(TARGET ai_lib PROPERTY FOLDER "src")
set_property(TARGET rule_lib PROPERTY FOLDER "src")
set_property(TARGET ui_lib PROPERTY FOLDER "src")
set_property(TARGET AICopilotForStorage PROPERTY FOLDER "src")
