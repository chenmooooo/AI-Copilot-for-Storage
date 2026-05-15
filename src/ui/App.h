#pragma once
#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class AIClient;
class RuleEngine;
class Database;
struct FileNode;

class App {
public:
    App();
    ~App();

    bool init(int width, int height, const char* title);
    void run();
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool createD3D11Device();
    void cleanupD3D11Device();
    void setupImGui();
    void renderMainMenuBar();
    void renderScannerPanel();
    void renderFileTreePanel();
    void renderAIPanel();
    void renderRulePanel();
    void renderConfigDialog();

    void scanPath(const std::wstring& path);
    void openFolderPicker();
    void analyzeSelectedNode();
    void updateFileTree(const FileNode* node, int depth, int64_t parentSize = 0);

    // Comparison snapshot
    struct ScanResult {
        std::string mode;
        double elapsedMs = 0;
        int64_t bytes = 0;
        int64_t bytesOnDisk = 0;
        size_t files = 0;
        size_t dirs = 0;
    };
    void storeScanResult(const char* mode, double ms, int64_t bytes, int64_t bytesOnDisk, size_t files, size_t dirs);

    std::unique_ptr<AIClient>   m_aiClient;
    std::unique_ptr<RuleEngine> m_ruleEngine;
    std::unique_ptr<Database>   m_database;
};
