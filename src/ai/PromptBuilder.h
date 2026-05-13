#pragma once
#include <string>
#include <vector>
#include "core/FileNode.h"
#include "ai/AIClient.h"

struct DirectoryContext {
    std::wstring   path;
    int64_t        sizeBytes = 0;
    size_t         fileCount = 0;
    size_t         dirCount  = 0;
    std::vector<std::pair<std::string, int64_t>> topExtensions;
    std::vector<std::pair<std::wstring, int64_t>> largestSubdirs;
};

class PromptBuilder {
public:
    PromptBuilder();
    ~PromptBuilder();

    void setLanguage(const std::string& lang);

    std::string buildSystemPrompt() const;
    std::string buildContextPrompt(const DirectoryContext& ctx) const;
    std::string buildAnalysisTask(const DirectoryContext& ctx) const;
    std::string buildFullPrompt(const DirectoryContext& ctx) const;

    std::vector<ChatMessage> buildChatMessages(const DirectoryContext& ctx) const;

private:
    std::string m_language = "zh-CN";
};
