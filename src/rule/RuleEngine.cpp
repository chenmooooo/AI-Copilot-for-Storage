#include "rule/RuleEngine.h"
#include <algorithm>
#include <unordered_map>

struct RuleEngine::Impl {
    std::vector<Rule> rules;

    std::vector<RuleMatch> matchNode(const FileNode& node) const {
        std::vector<RuleMatch> matches;
        std::wstring name = node.name;
        for (const auto& rule : rules) {
            if (name.find(rule.pattern) != std::wstring::npos) {
                RuleMatch m;
                m.rule = &rule;
                m.matchedPath = node.fullPath;
                m.score = 1.0;
                matches.push_back(m);
            }

            for (const auto& child : node.children) {
                auto childMatches = matchNode(child);
                matches.insert(matches.end(), childMatches.begin(), childMatches.end());
            }
        }
        return matches;
    }
};

RuleEngine::RuleEngine() : m_impl(std::make_unique<Impl>()) {}
RuleEngine::~RuleEngine() = default;

void RuleEngine::loadBuiltinRules() {
    auto& r = m_impl->rules;
    r.push_back({"node_modules", "Development", RiskLevel::Safe, L"node_modules",
        L"Node.js dependencies. Safe to delete and reinstall with npm install.", true});

    r.push_back({"DerivedDataCache", "GameEngine", RiskLevel::Safe, L"DerivedDataCache",
        L"Unreal Engine shader cache. Safe to delete; will be regenerated.", true});

    r.push_back({"HuggingFace", "AI", RiskLevel::Low, L"huggingface",
        L"Downloaded AI models from Hugging Face. Check which models you still need.", false});

    r.push_back({"pip cache", "Cache", RiskLevel::Safe, L"pip",
        L"Python pip cache. Safe to delete with pip cache purge.", true});

    r.push_back({"Docker", "Virtualization", RiskLevel::Medium, L"docker",
        L"Docker containers/images. Check before deleting.", false});

    r.push_back({"ComfyUI", "AI", RiskLevel::Low, L"ComfyUI",
        L"ComfyUI AI models and cache. Knows what models are installed.", false});

    r.push_back({"Windows Cache", "System", RiskLevel::Critical, L"Windows",
        L"Windows system directory. Do NOT delete unless certain.", false});

    r.push_back({"Program Files", "System", RiskLevel::Critical, L"Program Files",
        L"Installed applications directory. Do NOT delete.", false});

    r.push_back({"Steam", "Game", RiskLevel::Medium, L"steam",
        L"Steam games and data. Use Steam interface to manage.", false});

    r.push_back({".git", "Development", RiskLevel::Safe, L".git",
        L"Git repository data. Only delete if you no longer need version history.", false});

    r.push_back({"Android SDK", "Development", RiskLevel::Low, L"Android",
        L"Android SDK and build tools. Check what API levels you need.", false});

    r.push_back({"WSL", "Virtualization", RiskLevel::Medium, L"WSL",
        L"Windows Subsystem for Linux data. Check disk usage with wsl --manage.", false});
}

void RuleEngine::addRule(const Rule& rule) {
    m_impl->rules.push_back(rule);
}

std::vector<RuleMatch> RuleEngine::evaluate(const FileNode& node) const {
    return m_impl->matchNode(node);
}

RuleMatch RuleEngine::bestMatch(const FileNode& node) const {
    auto matches = evaluate(node);
    if (matches.empty()) return {};
    return *std::max_element(matches.begin(), matches.end(),
        [](const RuleMatch& a, const RuleMatch& b) { return a.score < b.score; });
}

void RuleEngine::forEachRule(RuleVisitor visitor) const {
    for (const auto& rule : m_impl->rules) {
        visitor(rule);
    }
}
