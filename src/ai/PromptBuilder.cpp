#include "ai/PromptBuilder.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <windows.h>

using json = nlohmann::json;

static std::string wstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

PromptBuilder::PromptBuilder() = default;
PromptBuilder::~PromptBuilder() = default;

void PromptBuilder::setLanguage(const std::string& lang) {
    m_language = lang;
}

std::string PromptBuilder::buildSystemPrompt() const {
    return R"(You are a disk storage semantic analysis assistant. Your role is to analyze directories and explain their purpose, risk, and cleanup recommendations.

Rules:
1. Never recommend deleting system directories or user personal data without clear risk warnings.
2. Always output in the user's language.
3. Always provide:
   - What this directory is
   - Why it exists
   - Risk level (Safe / Low / Medium / High / Critical)
   - Whether it can be safely cleaned
   - Impact if deleted
   - Estimated reclaimable space
4. Be concise but informative.
5. If unsure about a directory, mark risk as High and suggest user research.)";
}

std::string PromptBuilder::buildContextPrompt(const DirectoryContext& ctx) const {
    json j;
    j["path"] = wstringToUtf8(ctx.path);
    j["size_bytes"] = ctx.sizeBytes;
    j["size_gb"] = ctx.sizeBytes / (1024.0 * 1024.0 * 1024.0);
    j["file_count"] = ctx.fileCount;
    j["dir_count"] = ctx.dirCount;

    json exts = json::array();
    for (const auto& [ext, size] : ctx.topExtensions) {
        exts.push_back({{"extension", ext}, {"total_bytes", size}});
    }
    j["top_extensions"] = exts;
    return j.dump(2);
}

static std::string formatBytes(int64_t bytes) {
    constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return os.str();
}

std::string PromptBuilder::buildAnalysisTask(const DirectoryContext& ctx) const {
    std::ostringstream os;
    os << "Analyze this directory:\n";
    os << "Path: " << wstringToUtf8(ctx.path) << "\n";
    os << "Size: " << formatBytes(ctx.sizeBytes) << "\n";
    os << "Files: " << ctx.fileCount << ", Subdirectories: " << ctx.dirCount << "\n";
    os << "\nProvide structured analysis in JSON format:\n";
    os << "{\n";
    os << "  \"category\": \"...\",\n";
    os << "  \"risk_level\": \"Safe|Low|Medium|High|Critical\",\n";
    os << "  \"can_clean\": true|false,\n";
    os << "  \"reclaimable_bytes\": 0,\n";
    os << "  \"impact\": \"...\",\n";
    os << "  \"recommendation\": \"...\",\n";
    os << "  \"explanation\": \"...\"\n";
    os << "}\n";
    os << "Ensure your response contains ONLY the JSON object, no other text.";
    return os.str();
}

std::string PromptBuilder::buildFullPrompt(const DirectoryContext& ctx) const {
    return buildSystemPrompt() + "\n\n"
         + buildContextPrompt(ctx) + "\n\n"
         + buildAnalysisTask(ctx);
}

std::vector<ChatMessage> PromptBuilder::buildChatMessages(const DirectoryContext& ctx) const {
    std::vector<ChatMessage> msgs;
    msgs.push_back({"system", buildSystemPrompt()});

    std::string userMsg = buildAnalysisTask(ctx);
    if (!ctx.topExtensions.empty()) {
        userMsg += "\n\nFile extension distribution (top by size):\n";
        for (const auto& [ext, size] : ctx.topExtensions) {
            userMsg += "  " + ext + ": " + formatBytes(size) + "\n";
        }
    }
    msgs.push_back({"user", userMsg});
    return msgs;
}
