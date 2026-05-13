#pragma once
#include "ai/AIClient.h"

class DeepSeekProvider final : public AIClient {
public:
    DeepSeekProvider();
    ~DeepSeekProvider() override;

    ChatResponse chat(const ChatRequest& req) override;
    bool chatStream(const ChatRequest& req, StreamCallback cb) override;

    std::string testConnection() override;
    BalanceInfo checkBalance() override;

private:
    std::string chatEndpoint() const;
    std::string balanceEndpoint() const;
};
