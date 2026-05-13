#pragma once
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ChatMessage {
    std::string role;
    std::string content;
};

struct ChatRequest {
    std::string              model;
    std::vector<ChatMessage> messages;
    double                   temperature = 0.7;
    int                      maxTokens = 4096;
    bool                     stream = false;
};

struct ChatResponse {
    std::string content;
    int         promptTokens = 0;
    int         completionTokens = 0;
    bool        success = false;
    std::string errorMsg;
};

struct BalanceInfo {
    bool        isAvailable = false;
    std::string currency;
    std::string totalBalance;
    std::string grantedBalance;
    std::string toppedUpBalance;
};

using StreamCallback = std::function<void(const std::string& chunk)>;

class AIClient {
public:
    AIClient();
    virtual ~AIClient();

    void setApiKey(const std::string& key);
    void setBaseUrl(const std::string& url);
    void setModel(const std::string& model);
    void setTimeout(int seconds);

    const std::string& apiKey()  const { return m_apiKey; }
    const std::string& baseUrl() const { return m_baseUrl; }
    const std::string& model()   const { return m_model; }

    virtual ChatResponse chat(const ChatRequest& req) = 0;
    virtual bool chatStream(const ChatRequest& req, StreamCallback cb) = 0;

    virtual std::string testConnection();
    virtual BalanceInfo checkBalance();

protected:
    std::string m_apiKey;
    std::string m_baseUrl;
    std::string m_model;
    int         m_timeoutSec = 60;

    json buildChatPayload(const ChatRequest& req) const;
    ChatResponse parseChatResponse(const json& j) const;
};

class AIClientFactory {
public:
    enum Provider { OpenAI, DeepSeek, Ollama, Custom };

    static std::unique_ptr<AIClient> create(Provider provider);
    static std::unique_ptr<AIClient> createFromUrl(const std::string& baseUrl);
};
