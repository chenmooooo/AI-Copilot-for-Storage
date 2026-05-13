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
    void updateFileTree(const FileNode* node, int depth);

    std::unique_ptr<AIClient>   m_aiClient;
    std::unique_ptr<RuleEngine> m_ruleEngine;
    std::unique_ptr<Database>   m_database;
};
