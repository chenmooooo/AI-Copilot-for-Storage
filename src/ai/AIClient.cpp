#include "ai/AIClient.h"
#include "ai/DeepSeekProvider.h"

AIClient::AIClient() = default;
AIClient::~AIClient() = default;

void AIClient::setApiKey(const std::string& key)   { m_apiKey = key; }
void AIClient::setBaseUrl(const std::string& url)   { m_baseUrl = url; }
void AIClient::setModel(const std::string& model)   { m_model = model; }
void AIClient::setTimeout(int seconds)               { m_timeoutSec = seconds; }

json AIClient::buildChatPayload(const ChatRequest& req) const {
    json payload;
    payload["model"] = req.model.empty() ? m_model : req.model;
    payload["temperature"] = req.temperature;
    payload["max_tokens"] = req.maxTokens;
    payload["stream"] = req.stream;

    json msgs = json::array();
    for (const auto& m : req.messages) {
        msgs.push_back({{"role", m.role}, {"content", m.content}});
    }
    payload["messages"] = msgs;
    return payload;
}

ChatResponse AIClient::parseChatResponse(const json& j) const {
    ChatResponse resp;
    if (j.contains("choices") && !j["choices"].empty()) {
        auto& choice = j["choices"][0];
        if (choice.contains("message") && choice["message"].contains("content")) {
            resp.content = choice["message"]["content"].get<std::string>();
        }
        resp.success = true;
    }
    if (j.contains("usage")) {
        resp.promptTokens = j["usage"].value("prompt_tokens", 0);
        resp.completionTokens = j["usage"].value("completion_tokens", 0);
    }
    if (j.contains("error")) {
        resp.success = false;
        resp.errorMsg = j["error"].value("message", "unknown error");
    }
    return resp;
}

std::string AIClient::testConnection() {
    return "testConnection() not implemented for this provider";
}

BalanceInfo AIClient::checkBalance() {
    return {};
}

std::unique_ptr<AIClient> AIClientFactory::create(Provider provider) {
    switch (provider) {
    case DeepSeek:
        return std::make_unique<DeepSeekProvider>();
    case OpenAI:
    case Ollama:
    case Custom:
    default:
        return std::make_unique<DeepSeekProvider>();
    }
}

std::unique_ptr<AIClient> AIClientFactory::createFromUrl(const std::string& baseUrl) {
    auto client = std::make_unique<DeepSeekProvider>();
    client->setBaseUrl(baseUrl);
    return client;
}
