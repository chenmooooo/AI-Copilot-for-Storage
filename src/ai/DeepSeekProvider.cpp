#include "ai/DeepSeekProvider.h"
#include <cpr/cpr.h>
#include <sstream>

DeepSeekProvider::DeepSeekProvider() {
    m_baseUrl = "https://api.deepseek.com";
    m_model = "deepseek-v4-flash";
}

DeepSeekProvider::~DeepSeekProvider() = default;

ChatResponse DeepSeekProvider::chat(const ChatRequest& req) {
    json payload = buildChatPayload(req);
    payload["stream"] = false;

    cpr::Response r = cpr::Post(
        cpr::Url{m_baseUrl + "/v1/chat/completions"},
        cpr::Header{
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + m_apiKey}
        },
        cpr::Body{payload.dump()},
        cpr::Timeout{m_timeoutSec * 1000}
    );

    ChatResponse resp;
    if (r.status_code != 200) {
        resp.success = false;
        if (r.status_code == 0 && !r.error.message.empty()) {
            resp.errorMsg = "NetworkError: " + r.error.message;
        } else {
            resp.errorMsg = "HTTP " + std::to_string(r.status_code) + ": " + r.text;
        }
        return resp;
    }

    try {
        json j = json::parse(r.text);
        resp = parseChatResponse(j);
    } catch (const std::exception& e) {
        resp.success = false;
        resp.errorMsg = e.what();
    }

    return resp;
}

bool DeepSeekProvider::chatStream(const ChatRequest& req, StreamCallback cb) {
    json payload = buildChatPayload(req);
    payload["stream"] = true;

    cpr::Response r = cpr::Post(
        cpr::Url{m_baseUrl + "/v1/chat/completions"},
        cpr::Header{
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + m_apiKey}
        },
        cpr::Body{payload.dump()},
        cpr::Timeout{m_timeoutSec * 1000},
        cpr::WriteCallback{[cb](const std::string_view& data, intptr_t) -> bool {
            std::string dataStr(data);
            std::istringstream stream(dataStr);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.rfind("data: ", 0) == 0) {
                    std::string chunk = line.substr(6);
                    if (chunk == "[DONE]") return true;
                    try {
                        json j = json::parse(chunk);
                        if (j.contains("choices") && !j["choices"].empty()) {
                            auto& delta = j["choices"][0]["delta"];
                            if (delta.contains("content")) {
                                cb(delta["content"].get<std::string>());
                            }
                        }
                    } catch (...) {
                    }
                }
            }
            return true;
        }, 0}
    );

    return r.status_code == 200;
}

std::string DeepSeekProvider::testConnection() {
    if (m_apiKey.empty()) {
        return "[FAIL] API Key is empty";
    }

    json payload;
    payload["model"] = m_model;
    payload["messages"] = json::array({json{
        {"role", "user"},
        {"content", "Hi"}
    }});
    payload["max_tokens"] = 2;
    payload["stream"] = false;

    cpr::Response r = cpr::Post(
        cpr::Url{m_baseUrl + "/v1/chat/completions"},
        cpr::Header{
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + m_apiKey}
        },
        cpr::Body{payload.dump()},
        cpr::Timeout{15000}
    );

    if (r.status_code == 200) {
        try {
            json j = json::parse(r.text);
            std::string model = j.value("model", "unknown");
            int promptTokens = j["usage"].value("prompt_tokens", 0);
            int completionTokens = j["usage"].value("completion_tokens", 0);
            return "[OK] Connection successful (model: " + model +
                   ", prompt: " + std::to_string(promptTokens) +
                   ", completion: " + std::to_string(completionTokens) + " tokens)";
        } catch (...) {
            return "[OK] Connection successful (response parsed)";
        }
    }

    if (r.status_code == 401) {
        return "[FAIL] Authentication failed: Invalid API Key (HTTP 401)";
    }
    if (r.status_code == 402) {
        return "[FAIL] Insufficient balance: " + r.text;
    }
    if (r.status_code == 429) {
        return "[FAIL] Rate limit exceeded (HTTP 429)";
    }
    if (r.status_code >= 500) {
        return "[FAIL] Server error (HTTP " + std::to_string(r.status_code) + ")";
    }

    return "[FAIL] HTTP " + std::to_string(r.status_code) + ": " + r.text;
}

BalanceInfo DeepSeekProvider::checkBalance() {
    BalanceInfo info;

    if (m_apiKey.empty()) {
        return info;
    }

    cpr::Response r = cpr::Get(
        cpr::Url{m_baseUrl + "/user/balance"},
        cpr::Header{
            {"Accept", "application/json"},
            {"Authorization", "Bearer " + m_apiKey}
        },
        cpr::Timeout{15000}
    );

    if (r.status_code != 200) {
        return info;
    }

    try {
        json j = json::parse(r.text);
        info.isAvailable = j.value("is_available", false);
        if (j.contains("balance_infos") && !j["balance_infos"].empty()) {
            auto& bi = j["balance_infos"][0];
            info.currency = bi.value("currency", std::string());
            info.totalBalance = bi.value("total_balance", std::string());
            info.grantedBalance = bi.value("granted_balance", std::string());
            info.toppedUpBalance = bi.value("topped_up_balance", std::string());
        }
    } catch (...) {
    }

    return info;
}
