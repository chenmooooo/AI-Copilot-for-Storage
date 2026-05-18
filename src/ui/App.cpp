#include "ui/App.h"
#include "ai/AIClient.h"
#include "ai/PromptBuilder.h"
#include "rule/RuleEngine.h"
#include "db/Database.h"
#include "core/FileNode.h"
#include "core/Scanner.h"
#include "core/NtfsScanner.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <sstream>
#include <iomanip>
#include <ctime>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <future>
#include <mutex>

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------
static std::string formatSize(int64_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unit = 0;
    double d = (double)bytes;
    while (d >= 1024.0 && unit < 4) { d /= 1024.0; unit++; }
    char buf[32];
    if (unit == 0) snprintf(buf, sizeof(buf), "%lld B", (long long)bytes);
    else           snprintf(buf, sizeof(buf), "%.2f %s", d, units[unit]);
    return buf;
}

static std::string formatTime(const std::chrono::system_clock::time_point& tp) {
    if (tp == std::chrono::system_clock::time_point{}) return "-";
    auto tt = std::chrono::system_clock::to_time_t(tp);
    struct tm local;
    localtime_s(&local, &tt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
    return buf;
}

static bool isVolumeRoot(const std::wstring& path) {
    // "X:\" or "X:" or "X:" with trailing backslash
    if (path.size() >= 2 && path[1] == L':') {
        if (path.size() == 2) return true;
        if (path.size() == 3 && (path[2] == L'\\' || path[2] == L'/')) return true;
    }
    return false;
}

static bool isProcessElevated() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;
    TOKEN_ELEVATION te;
    DWORD size = 0;
    bool elevated = GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &size) && te.TokenIsElevated;
    CloseHandle(hToken);
    return elevated;
}

static void restartAsAdmin(const char* path) {
    WCHAR widePath[4096];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, 4096);
    ShellExecuteW(nullptr, L"runas", widePath, nullptr, nullptr, SW_SHOWNORMAL);
}

static std::string formatFileType(FileType type) {
    switch (type) {
    case FileType::Directory: return "Dir";
    case FileType::File:      return "File";
    case FileType::Symlink:   return "Link";
    default:                  return "?";
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct App::Impl {
    HWND hwnd = nullptr;
    WNDCLASSEXW wc = {};
    bool running = true;

    // D3D11
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dDeviceContext = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* mainRenderTargetView = nullptr;

    // State
    std::unique_ptr<FileNode> scanRoot;
    FileNode* selectedNode = nullptr;
    std::string selectedPath;
    std::string aiResponse;
    bool aiBusy = false;
    bool showConfigDialog = false;
    bool showDemoWindow = false;

    // Config
    std::string apiKey;
    std::string apiBaseUrl = "https://api.deepseek.com";
    std::string apiModel = "deepseek-v4-flash";
    int         apiModelIdx = 0;
    bool        testBusy = false;
    std::string testLog;

    // Stats
    int64_t scanBytes = 0;
    int64_t scanBytesOnDisk = 0;
    size_t scanFiles = 0;
    size_t scanDirs = 0;
    double scanTimeMs = 0.0;
    double scanIoMs = 0.0;
    double scanBuildMs = 0.0;
    bool scanning = false;

    // Scan mode: 0 = Auto, 1 = Legacy (FindFirstFile), 2 = NTFS Fast (MFT)
    int scanMode = 0;
    static const char* scanModeNames[3];

    // Comparison snapshots
    App::ScanResult results[2]; // index 0 = Legacy, 1 = NTFS

    // Actual scan mode used (after Auto resolution)
    int actualScanMode = 1;

    // Rule evaluation cache (avoid re-evaluating every frame)
    FileNode* ruleCacheNode = nullptr;
    std::vector<RuleMatch> ruleCacheMatches;

    // Async AI state
    std::future<void> aiFuture;
    std::mutex aiMutex;
    std::string aiStreamBuffer;
    bool aiFromCache = false;
    bool aiForceRefresh = false;

    // Last scan error message
    std::string scanError;

    // Input
    char pathInput[1024] = "C:\\";
};

const char* App::Impl::scanModeNames[3] = { "Auto", "Legacy (FindFirstFile)", "NTFS Fast (MFT)" };

App::App()
    : m_aiClient(AIClientFactory::create(AIClientFactory::DeepSeek))
    , m_ruleEngine(std::make_unique<RuleEngine>())
    , m_database(std::make_unique<Database>("ai_copilot.db"))
{
    m_impl = std::make_unique<Impl>();
    m_ruleEngine->loadBuiltinRules();
}

App::~App() = default;

LRESULT CALLBACK App::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool App::createD3D11Device() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_impl->hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[1] = { D3D_FEATURE_LEVEL_11_0 };

    if (D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        featureLevels, 1, D3D11_SDK_VERSION, &sd,
        &m_impl->swapChain, &m_impl->d3dDevice, &featureLevel, &m_impl->d3dDeviceContext) != S_OK) {
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    m_impl->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        m_impl->d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &m_impl->mainRenderTargetView);
        backBuffer->Release();
    }
    return true;
}

void App::cleanupD3D11Device() {
    if (m_impl->mainRenderTargetView) { m_impl->mainRenderTargetView->Release(); m_impl->mainRenderTargetView = nullptr; }
    if (m_impl->swapChain) { m_impl->swapChain->Release(); m_impl->swapChain = nullptr; }
    if (m_impl->d3dDeviceContext) { m_impl->d3dDeviceContext->Release(); m_impl->d3dDeviceContext = nullptr; }
    if (m_impl->d3dDevice) { m_impl->d3dDevice->Release(); m_impl->d3dDevice = nullptr; }
}

bool App::init(int width, int height, const char* title) {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AICopilotForStorage";
    m_impl->wc = wc;
    RegisterClassExW(&m_impl->wc);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    std::wstring wtitle(wlen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, title, -1, &wtitle[0], wlen);
    m_impl->hwnd = CreateWindowExW(
        0, m_impl->wc.lpszClassName, wtitle.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        width, height, nullptr, nullptr, m_impl->wc.hInstance, nullptr);

    if (!m_impl->hwnd) return false;
    if (!createD3D11Device()) return false;

    ShowWindow(m_impl->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_impl->hwnd);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    setupImGui();
    m_database->open();

    std::string savedKey = m_database->loadConfig("api_key").value_or("");
    std::string savedUrl = m_database->loadConfig("api_base_url").value_or("https://api.deepseek.com");
    std::string savedModel = m_database->loadConfig("api_model").value_or("deepseek-v4-flash");

    const char* knownModels[] = { "deepseek-v4-flash", "deepseek-v4-pro", "deepseek-chat", "deepseek-reasoner" };
    for (int i = 0; i < 4; i++) {
        if (savedModel == knownModels[i]) { m_impl->apiModelIdx = i; break; }
    }

    if (!savedKey.empty()) {
        m_impl->apiKey = savedKey;
        m_impl->apiBaseUrl = savedUrl;
        m_impl->apiModel = savedModel;
        m_aiClient->setApiKey(savedKey);
        m_aiClient->setBaseUrl(savedUrl);
        m_aiClient->setModel(savedModel);
    }

    return true;
}

void App::setupImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Load CJK-capable font for Chinese filename / AI response display
    ImFontConfig cfg;
    cfg.MergeMode = false;
    ImFont* cjkFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 15.0f, &cfg,
        io.Fonts->GetGlyphRangesChineseFull());
    if (!cjkFont) {
        // Fallback: try SimSun for older Windows
        cjkFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\simsun.ttc", 15.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseFull());
    }
    if (!cjkFont) {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplWin32_Init(m_impl->hwnd);
    ImGui_ImplDX11_Init(m_impl->d3dDevice, m_impl->d3dDeviceContext);
}

void App::run() {
    MSG msg;
    while (m_impl->running) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) m_impl->running = false;
        }
        if (!m_impl->running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (m_impl->showDemoWindow) {
            ImGui::ShowDemoWindow(&m_impl->showDemoWindow);
        }

        renderMainMenuBar();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        renderScannerPanel();
        renderFileTreePanel();
        renderAIPanel();
        renderRulePanel();
        renderConfigDialog();

        RECT rect;
        GetClientRect(m_impl->hwnd, &rect);
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (FLOAT)(rect.right - rect.left), (FLOAT)(rect.bottom - rect.top), 0.0f, 1.0f };

        ImGui::Render();
        m_impl->d3dDeviceContext->OMSetRenderTargets(1, &m_impl->mainRenderTargetView, nullptr);
        m_impl->d3dDeviceContext->RSSetViewports(1, &vp);
        ImVec4 clear = { 0.1f, 0.1f, 0.12f, 1.0f };
        m_impl->d3dDeviceContext->ClearRenderTargetView(m_impl->mainRenderTargetView, (float*)&clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_impl->swapChain->Present(1, 0);
    }
}

void App::shutdown() {
    // Wait for any in-flight AI analysis to complete
    if (m_impl->aiFuture.valid()) {
        m_impl->aiFuture.wait();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanupD3D11Device();
    if (m_impl->hwnd) {
        DestroyWindow(m_impl->hwnd);
    }
    UnregisterClassW(m_impl->wc.lpszClassName, m_impl->wc.hInstance);

    m_database->close();

    CoUninitialize();
}

void App::renderMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Scan Path...")) {}
            if (ImGui::MenuItem("Exit")) { m_impl->running = false; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Configuration")) { m_impl->showConfigDialog = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("ImGui Demo")) { m_impl->showDemoWindow = true; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {}
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void App::renderScannerPanel() {
    ImGui::Begin("Scanner");

    float browseBtnWidth = 80.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseBtnWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##path", m_impl->pathInput, sizeof(m_impl->pathInput));
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(browseBtnWidth, 0))) {
        openFolderPicker();
    }

    // Scan mode selector
    ImGui::Combo("Scan Mode", &m_impl->scanMode, Impl::scanModeNames, 3);
    if (m_impl->scanMode == 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Auto: uses NTFS for volume roots (C:\), Legacy for subdirectories");
    } else if (m_impl->scanMode == 2) {
        if (!isVolumeRoot(std::wstring(m_impl->pathInput, m_impl->pathInput + strlen(m_impl->pathInput)))) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Note: NTFS mode reads the full volume MFT.\nFor small directories, Legacy is faster.");
        }
    }

    if (ImGui::Button("Scan", ImVec2(120, 0))) {
        m_impl->scanError.clear();
        std::wstring wpath;
        int len = MultiByteToWideChar(CP_UTF8, 0, m_impl->pathInput, -1, nullptr, 0);
        if (len > 0) {
            wpath.resize(len - 1);
            MultiByteToWideChar(CP_UTF8, 0, m_impl->pathInput, -1, &wpath[0], len);
        }
        scanPath(wpath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {}

    if (m_impl->scanning) {
        ImGui::Text("Scanning...");
    }

    // Show scan error if any
    if (!m_impl->scanError.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error:");
        ImGui::TextWrapped("%s", m_impl->scanError.c_str());
        if (m_impl->scanMode == 1 && !isProcessElevated()) {
            ImGui::Separator();
            ImGui::Text("Tip: NTFS Fast scan needs Administrator privileges.");
            char exePath[4096];
            GetModuleFileNameA(nullptr, exePath, sizeof(exePath));
            if (ImGui::Button("Restart as Administrator")) {
                restartAsAdmin(exePath);
            }
        }
    }

    if (m_impl->scanRoot) {
        ImGui::Separator();
        ImGui::Text("Path: %s", m_impl->scanRoot->narrowPath().c_str());
        ImGui::Text("Type: %s", m_impl->scanRoot->isDirectory() ? "Directory" : "File");
        if (m_impl->scanMode == 0)
            ImGui::Text("Mode: Auto -> %s", Impl::scanModeNames[m_impl->actualScanMode]);
        else
            ImGui::Text("Mode: %s", Impl::scanModeNames[m_impl->actualScanMode]);
        ImGui::Text("Total: %.0f ms", m_impl->scanTimeMs);
        if (m_impl->scanIoMs > 0 || m_impl->scanBuildMs > 0) {
            ImGui::Text("  Scan I/O:  %.0f ms", m_impl->scanIoMs);
            ImGui::Text("  Build:     %.0f ms", m_impl->scanBuildMs);
        }
        ImGui::Text("Logical Size: %s", formatSize(m_impl->scanBytes).c_str());
        ImGui::Text("Physical Size: %s", formatSize(m_impl->scanBytesOnDisk).c_str());
        ImGui::Text("Files: %zu  Dirs: %zu", m_impl->scanFiles, m_impl->scanDirs);
    }

    // Comparison section
    bool hasLegacy = m_impl->results[0].elapsedMs > 0;
    bool hasNtfs   = m_impl->results[1].elapsedMs > 0;
    if (hasLegacy || hasNtfs) {
        ImGui::Separator();
        ImGui::Text("Speed Comparison");
        if (ImGui::BeginTable("##cmp", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("");
            ImGui::TableSetupColumn("Legacy");
            ImGui::TableSetupColumn("NTFS Fast");
            ImGui::TableHeadersRow();

            auto row = [&](const char* label,
                const char* leg, const char* ntfs) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%s", label);
                ImGui::TableNextColumn(); ImGui::Text("%s", leg ? leg : "-");
                ImGui::TableNextColumn(); ImGui::Text("%s", ntfs ? ntfs : "-");
            };

            auto fmt = [](double ms) {
                static char buf[32];
                if (ms < 1000) snprintf(buf, sizeof(buf), "%.0f ms", ms);
                else           snprintf(buf, sizeof(buf), "%.2f s", ms / 1000);
                return buf;
            };

            row("Total",
                hasLegacy ? fmt(m_impl->results[0].elapsedMs) : nullptr,
                hasNtfs   ? fmt(m_impl->results[1].elapsedMs) : nullptr);
            row("Scan I/O",
                hasLegacy && m_impl->results[0].scanMs > 0 ? fmt(m_impl->results[0].scanMs) : nullptr,
                hasNtfs   && m_impl->results[1].scanMs > 0 ? fmt(m_impl->results[1].scanMs) : nullptr);
            row("Build",
                hasLegacy && m_impl->results[0].buildMs > 0 ? fmt(m_impl->results[0].buildMs) : nullptr,
                hasNtfs   && m_impl->results[1].buildMs > 0 ? fmt(m_impl->results[1].buildMs) : nullptr);
            row("Logical Size",
                hasLegacy ? formatSize(m_impl->results[0].bytes).c_str() : nullptr,
                hasNtfs   ? formatSize(m_impl->results[1].bytes).c_str() : nullptr);
            row("Physical Size",
                hasLegacy ? formatSize(m_impl->results[0].bytesOnDisk).c_str() : nullptr,
                hasNtfs   ? formatSize(m_impl->results[1].bytesOnDisk).c_str() : nullptr);
            row("Files",
                hasLegacy ? std::to_string(m_impl->results[0].files).c_str() : nullptr,
                hasNtfs   ? std::to_string(m_impl->results[1].files).c_str() : nullptr);
            row("Dirs",
                hasLegacy ? std::to_string(m_impl->results[0].dirs).c_str() : nullptr,
                hasNtfs   ? std::to_string(m_impl->results[1].dirs).c_str() : nullptr);

            // Speedup ratio
            if (hasLegacy && hasNtfs && m_impl->results[1].elapsedMs > 0) {
                double ratio = m_impl->results[0].elapsedMs / m_impl->results[1].elapsedMs;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("Speedup");
                ImGui::TableNextColumn(); ImGui::Text("1.00x");
                char ratioBuf[32];
                snprintf(ratioBuf, sizeof(ratioBuf), "%.1fx", ratio);
                ImGui::TableNextColumn(); ImGui::Text("%s", ratioBuf);
            }

            ImGui::EndTable();
        }
    }

    ImGui::End();
}

void App::renderFileTreePanel() {
    ImGui::Begin("File Tree");
    if (m_impl->scanRoot) {
        updateFileTree(m_impl->scanRoot.get(), 0);
    } else {
        ImGui::Text("No scan data. Enter a path and click Scan.");
    }
    ImGui::End();
}

void App::updateFileTree(const FileNode* node, int depth, int64_t parentSize) {
    if (!node) return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
    if (node->children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (node == m_impl->selectedNode) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string label = node->narrowName();
    if (node->isDirectory()) {
        label += "/";
    }

    label += "  (" + formatFileType(node->type);
    label += ", " + formatSize(node->sizeBytes);
    if (parentSize > 0) {
        double pct = 100.0 * node->sizeBytes / parentSize;
        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), " %.1f%%", pct);
        label += pctBuf;
    }
    label += ", " + formatTime(node->lastModified);
    label += ")";

    bool open = ImGui::TreeNodeEx(node, flags, "%s", label.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        m_impl->selectedNode = const_cast<FileNode*>(node);
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("AI Analysis")) {
            m_impl->selectedNode = const_cast<FileNode*>(node);
            analyzeSelectedNode();
        }
        ImGui::EndPopup();
    }

    if (open) {
        for (const auto& child : node->children) {
            updateFileTree(&child, depth + 1, node->sizeBytes);
        }
        ImGui::TreePop();
    }
}

void App::renderAIPanel() {
    ImGui::Begin("AI Analysis");

    // Poll async AI completion
    if (m_impl->aiBusy && m_impl->aiFuture.valid() &&
        m_impl->aiFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_impl->aiFuture.get();
        m_impl->aiBusy = false;
    }

    if (m_impl->selectedNode) {
        ImGui::TextWrapped("Analyzing: %s", m_impl->selectedNode->narrowPath().c_str());

        if (m_impl->aiBusy) {
            // Spinner animation
            static const char spinner[] = "|/-\\";
            int frame = (int)(ImGui::GetTime() * 10.0) % 4;
            ImGui::Text("AI \u5206\u6790\u4e2d... %c", spinner[frame]);

            // Streaming text
            std::string streamText;
            {
                std::lock_guard<std::mutex> lock(m_impl->aiMutex);
                streamText = m_impl->aiStreamBuffer;
            }
            if (!streamText.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", streamText.c_str());
            }
        } else {
            // Cache source badge
            if (m_impl->aiFromCache) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "(\u6765\u6e90\uff1a\u7f13\u5b58)");
            }

            if (!m_impl->aiResponse.empty()) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", m_impl->aiResponse.c_str());

                if (ImGui::Button("Copy")) {
                    ImGui::SetClipboardText(m_impl->aiResponse.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button("\u91cd\u65b0\u5206\u6790")) {
                    m_impl->aiForceRefresh = true;
                    analyzeSelectedNode();
                }
            }
        }
    } else {
        ImGui::Text("Select a directory in the File Tree to analyze.");
    }
    ImGui::End();
}

void App::renderRulePanel() {
    ImGui::Begin("Rule Matches");
    if (m_impl->selectedNode) {
        if (m_impl->selectedNode != m_impl->ruleCacheNode) {
            m_impl->ruleCacheNode = m_impl->selectedNode;
            m_impl->ruleCacheMatches = m_ruleEngine->evaluate(*m_impl->selectedNode);
        }
        auto& matches = m_impl->ruleCacheMatches;
        if (matches.empty()) {
            ImGui::Text("No rule matches found.");
        } else {
            for (const auto& match : matches) {
                if (ImGui::CollapsingHeader(match.rule->name.c_str())) {
                    ImGui::Text("Category: %s", match.rule->category.c_str());
                    ImGui::TextWrapped("Description: %s", match.rule->description.c_str());
                    ImGui::Text("Can clean: %s", match.rule->canClean ? "Yes" : "No");
                }
            }
        }
    } else {
        m_impl->ruleCacheNode = nullptr;
        m_impl->ruleCacheMatches.clear();
    }
    ImGui::End();
}

void App::renderConfigDialog() {
    if (!m_impl->showConfigDialog) return;

    static const char* modelNames[] = {
        "deepseek-v4-flash",
        "deepseek-v4-pro",
        "deepseek-chat (deprecated)",
        "deepseek-reasoner (deprecated)"
    };
    static const char* modelValues[] = {
        "deepseek-v4-flash",
        "deepseek-v4-pro",
        "deepseek-chat",
        "deepseek-reasoner"
    };
    static const int modelCount = 4;

    ImGui::OpenPopup("Configuration");
    if (ImGui::BeginPopupModal("Configuration", &m_impl->showConfigDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("AI Provider Settings");

        char keyBuf[1024];
        strncpy_s(keyBuf, m_impl->apiKey.c_str(), sizeof(keyBuf));
        if (ImGui::InputText("API Key", keyBuf, sizeof(keyBuf))) {
            m_impl->apiKey = keyBuf;
            m_aiClient->setApiKey(m_impl->apiKey);
        }

        char urlBuf[1024];
        strncpy_s(urlBuf, m_impl->apiBaseUrl.c_str(), sizeof(urlBuf));
        if (ImGui::InputText("Base URL", urlBuf, sizeof(urlBuf))) {
            m_impl->apiBaseUrl = urlBuf;
            m_aiClient->setBaseUrl(m_impl->apiBaseUrl);
        }

        if (ImGui::Combo("Model", &m_impl->apiModelIdx, modelNames, modelCount)) {
            m_impl->apiModel = modelValues[m_impl->apiModelIdx];
            m_aiClient->setModel(m_impl->apiModel);
        }

        ImGui::Separator();
        ImGui::Text("Test Connection");

        if (m_impl->testBusy) {
            ImGui::BeginDisabled();
            ImGui::Button("Testing...", ImVec2(120, 0));
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Test API", ImVec2(120, 0))) {
                m_impl->testBusy = true;
                m_impl->testLog.clear();
                m_aiClient->setApiKey(m_impl->apiKey);
                m_aiClient->setBaseUrl(m_impl->apiBaseUrl);
                m_aiClient->setModel(m_impl->apiModel);

                std::string model, bal;
                model = m_aiClient->testConnection();
                m_impl->testLog += model + "\n";

                if (model.rfind("[OK]", 0) == 0) {
                    auto balInfo = m_aiClient->checkBalance();
                    if (balInfo.isAvailable) {
                        m_impl->testLog += "[OK] Account available\n";
                        m_impl->testLog += "      Currency: " + balInfo.currency + "\n";
                        m_impl->testLog += "      Total balance: " + balInfo.totalBalance + "\n";
                        m_impl->testLog += "      Granted balance: " + balInfo.grantedBalance + "\n";
                        m_impl->testLog += "      Topped-up balance: " + balInfo.toppedUpBalance + "\n";
                    } else {
                        m_impl->testLog += "[WARN] Balance query returned unavailable\n";
                    }
                }
                m_impl->testBusy = false;
            }
        }

        if (!m_impl->testLog.empty()) {
            ImGui::Separator();
            ImGui::Text("Test Log:");
            ImGui::BeginChild("TestLog", ImVec2(420, 120), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextWrapped("%s", m_impl->testLog.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }

        ImGui::Separator();
        if (ImGui::Button("Save")) {
            m_database->saveConfig("api_key", m_impl->apiKey);
            m_database->saveConfig("api_base_url", m_impl->apiBaseUrl);
            m_database->saveConfig("api_model", m_impl->apiModel);
            ImGui::CloseCurrentPopup();
            m_impl->showConfigDialog = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            m_impl->showConfigDialog = false;
        }

        ImGui::EndPopup();
    }
}

void App::scanPath(const std::wstring& path) {
    m_impl->scanning = true;
    m_impl->selectedNode = nullptr;
    m_impl->aiResponse.clear();

    // Resolve Auto mode
    int effectiveMode = m_impl->scanMode;
    if (effectiveMode == 0) {
        effectiveMode = isVolumeRoot(path) ? 2 : 1;
    }
    m_impl->actualScanMode = effectiveMode;

    if (effectiveMode == 1) {
        // Legacy scanner
        Scanner scanner;
        scanner.setRootPath(path);
        m_impl->scanRoot = scanner.scan();
        m_impl->scanBytes = scanner.totalBytes();
        m_impl->scanBytesOnDisk = 0; // legacy scanner does not compute physical size
        m_impl->scanFiles = scanner.totalFiles();
        m_impl->scanDirs = scanner.totalDirs();
        m_impl->scanTimeMs = scanner.elapsedMs();
        m_impl->scanIoMs = scanner.scanTimeMs();
        m_impl->scanBuildMs = scanner.buildTimeMs();

        storeScanResult("Legacy", scanner.elapsedMs(), scanner.scanTimeMs(), scanner.buildTimeMs(),
                        scanner.totalBytes(), 0,
                        scanner.totalFiles(), scanner.totalDirs());
    } else {
        // NTFS Fast scanner
        NtfsScanner nscanner;
        nscanner.setRootPath(path);
        m_impl->scanRoot = nscanner.scan();

        if (m_impl->scanRoot) {
            m_impl->scanBytes = nscanner.totalBytes();
            m_impl->scanBytesOnDisk = nscanner.totalBytesOnDisk();
            m_impl->scanFiles = nscanner.totalFiles();
            m_impl->scanDirs = nscanner.totalDirs();
            m_impl->scanTimeMs = nscanner.elapsedMs();
            m_impl->scanIoMs = nscanner.scanTimeMs();
            m_impl->scanBuildMs = nscanner.buildTimeMs();
            m_impl->scanError.clear();
        } else {
            m_impl->scanError = nscanner.lastError();
            if (m_impl->scanError.empty())
                m_impl->scanError = "NTFS scan failed for unknown reason.";
        }

        storeScanResult("NTFS Fast", nscanner.elapsedMs(), nscanner.scanTimeMs(), nscanner.buildTimeMs(),
                        nscanner.totalBytes(),
                        nscanner.totalBytesOnDisk(), nscanner.totalFiles(), nscanner.totalDirs());
    }

    m_impl->scanning = false;
}

void App::storeScanResult(const char* mode, double ms, double scanMs, double buildMs,
                          int64_t bytes, int64_t bytesOnDisk, size_t files, size_t dirs) {
    int idx = (m_impl->actualScanMode == 1) ? 0 : 1;
    m_impl->results[idx].mode = mode;
    m_impl->results[idx].elapsedMs = ms;
    m_impl->results[idx].scanMs = scanMs;
    m_impl->results[idx].buildMs = buildMs;
    m_impl->results[idx].bytes = bytes;
    m_impl->results[idx].bytesOnDisk = bytesOnDisk;
    m_impl->results[idx].files = files;
    m_impl->results[idx].dirs = dirs;
}

void App::openFolderPicker() {
    IFileOpenDialog* pFolderDlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&pFolderDlg));
    if (FAILED(hr)) return;

    DWORD dwOptions;
    pFolderDlg->GetOptions(&dwOptions);
    pFolderDlg->SetOptions(dwOptions | FOS_PICKFOLDERS);

    hr = pFolderDlg->Show(m_impl->hwnd);
    if (SUCCEEDED(hr)) {
        IShellItem* pItem = nullptr;
        hr = pFolderDlg->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
            PWSTR pszPath = nullptr;
            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
            if (SUCCEEDED(hr)) {
                int len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, nullptr, 0, nullptr, nullptr);
                if (len > 0 && len <= (int)sizeof(m_impl->pathInput)) {
                    WideCharToMultiByte(CP_UTF8, 0, pszPath, -1,
                                        m_impl->pathInput, sizeof(m_impl->pathInput), nullptr, nullptr);
                }
                CoTaskMemFree(pszPath);
            }
            pItem->Release();
        }
    }
    pFolderDlg->Release();
}

static std::vector<std::pair<std::string, int64_t>> collectExtensionStats(const FileNode* node) {
    std::unordered_map<std::string, int64_t> extMap;
    std::function<void(const FileNode*)> walk = [&](const FileNode* n) {
        if (n->isFile()) {
            std::string ext = n->extension();
            if (!ext.empty()) {
                extMap[ext] += n->sizeBytes;
            } else {
                extMap["(no extension)"] += n->sizeBytes;
            }
        }
        for (const auto& child : n->children) {
            walk(&child);
        }
    };
    walk(node);
    std::vector<std::pair<std::string, int64_t>> sorted(extMap.begin(), extMap.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (sorted.size() > 20) sorted.resize(20);
    return sorted;
}

static std::string categorizeAiError(const std::string& errorMsg) {
    if (errorMsg.find("HTTP 401") != std::string::npos || errorMsg.find("Authentication") != std::string::npos) {
        return "认证失败：API Key 无效，请在 工具 → 配置 中检查 API Key 设置。";
    }
    if (errorMsg.find("HTTP 402") != std::string::npos) {
        return "余额不足：账户已欠费，请充值后重试。";
    }
    if (errorMsg.find("HTTP 429") != std::string::npos) {
        return "请求过于频繁：请稍后重试。";
    }
    if (errorMsg.find("HTTP 5") != std::string::npos) {
        return "AI 服务端错误：服务暂时不可用，请稍后重试。";
    }
    if (errorMsg.find("NetworkError") != std::string::npos || errorMsg.find("Timeout") != std::string::npos || errorMsg.find("timed out") != std::string::npos) {
        return "网络连接失败：无法连接到 AI 服务，请检查网络连接和 API Base URL 配置。";
    }
    if (errorMsg.find("parse") != std::string::npos || errorMsg.find("json") != std::string::npos) {
        return "AI 响应解析失败：返回格式异常，请重试。";
    }
    return "分析失败：" + errorMsg;
}

void App::analyzeSelectedNode() {
    if (!m_impl->selectedNode || m_impl->aiBusy) return;

    // Snapshot context on UI thread (FileNode tree may change later)
    DirectoryContext ctx;
    ctx.path = m_impl->selectedNode->fullPath;
    ctx.sizeBytes = m_impl->selectedNode->sizeBytes;
    ctx.fileCount = m_impl->selectedNode->fileCount;
    ctx.dirCount = m_impl->selectedNode->dirCount;
    ctx.topExtensions = collectExtensionStats(m_impl->selectedNode);

    std::string narrowPath = m_impl->selectedNode->narrowPath();

    // Fast cache check on UI thread (unless force refresh)
    if (!m_impl->aiForceRefresh) {
        auto cached = m_database->loadAnalysis(narrowPath);
        if (cached && !cached->empty()) {
            m_impl->aiResponse = cached->dump(2);
            m_impl->aiFromCache = true;
            return;
        }
    }
    m_impl->aiForceRefresh = false;

    // Prepare async launch
    m_impl->aiBusy = true;
    m_impl->aiFromCache = false;
    m_impl->aiResponse.clear();
    {
        std::lock_guard<std::mutex> lock(m_impl->aiMutex);
        m_impl->aiStreamBuffer.clear();
    }

    PromptBuilder builder;
    auto messages = builder.buildChatMessages(ctx);
    std::string model = m_impl->apiModel;

    // Launch AI call in background thread
    m_impl->aiFuture = std::async(std::launch::async, [
        this,
        messages = std::move(messages),
        model,
        narrowPath
    ]() {
        auto doRetry = false;

        // -- First attempt: streaming --
        {
            ChatRequest req;
            req.messages = messages;
            req.model = model;
            req.stream = true;

            bool ok = m_aiClient->chatStream(req, [this](const std::string& chunk) {
                std::lock_guard<std::mutex> lock(m_impl->aiMutex);
                m_impl->aiStreamBuffer += chunk;
            });

            if (ok) {
                std::lock_guard<std::mutex> lock(m_impl->aiMutex);
                m_impl->aiResponse = m_impl->aiStreamBuffer;
                return;
            }
            doRetry = true;
        }

        // -- Retry: non-streaming fallback (handles timeout/auth errors better) --
        if (doRetry) {
            ChatRequest req;
            req.messages = messages;
            req.model = model;
            req.stream = false;

            auto resp = m_aiClient->chat(req);
            if (resp.success) {
                m_impl->aiResponse = resp.content;
                {
                    std::lock_guard<std::mutex> lock(m_impl->aiMutex);
                    m_impl->aiStreamBuffer = resp.content;
                }
            } else {
                m_impl->aiResponse = categorizeAiError(resp.errorMsg);
                return;
            }
        }

        // Cache result on success
        if (!m_impl->aiResponse.empty()) {
            try {
                json j = json::parse(m_impl->aiResponse, nullptr, false);
                if (!j.is_discarded()) {
                    m_database->saveAnalysis(narrowPath, j);
                } else {
                    m_database->saveAnalysis(narrowPath, m_impl->aiResponse);
                }
            } catch (...) {
                m_database->saveAnalysis(narrowPath, m_impl->aiResponse);
            }
        }
    });
}
