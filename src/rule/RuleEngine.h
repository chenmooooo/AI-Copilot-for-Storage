#pragma once
#include "core/FileNode.h"
#include <vector>
#include <memory>
#include <functional>

struct Rule {
    std::string  name;
    std::string  category;
    RiskLevel    riskLevel = RiskLevel::Safe;
    std::wstring pattern;       // directory name pattern (substring match)
    std::wstring description;
    bool         canClean = false;
    bool         requiresAdmin = false;
};

struct RuleMatch {
    const Rule* rule = nullptr;
    std::wstring matchedPath;
    double       score = 0.0;
};

class RuleEngine {
public:
    RuleEngine();
    ~RuleEngine();

    void loadBuiltinRules();
    void addRule(const Rule& rule);

    std::vector<RuleMatch> evaluate(const FileNode& node) const;
    RuleMatch              bestMatch(const FileNode& node) const;

    using RuleVisitor = std::function<void(const Rule&)>;
    void forEachRule(RuleVisitor visitor) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
