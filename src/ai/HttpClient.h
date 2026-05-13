#pragma once
#include <string>
#include <functional>
#include <windows.h>
#include <winhttp.h>

class HttpClient {
public:
    HttpClient(const std::string& baseUrl, int timeoutSec = 60);
    ~HttpClient();

    void setHeader(const std::string& name, const std::string& value);

    struct Response {
        int         status = 0;
        std::string body;
        bool        error = false;
        std::string errorMsg;
    };

    Response post(const std::string& path, const std::string& body, const std::string& contentType = "application/json");
    Response get(const std::string& path);

private:
    std::string m_host;
    int         m_port = 443;
    bool        m_https = true;
    int         m_timeoutSec;

    std::string m_headers;

    HINTERNET m_session = nullptr;

    Response request(const std::wstring& method, const std::wstring& path,
                     const std::string& body, const std::string& contentType);
};
