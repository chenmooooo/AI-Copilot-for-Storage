#include "ai/HttpClient.h"
#include <sstream>
#include <algorithm>
#include <vector>

#pragma comment(lib, "winhttp.lib")

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &result[0], len);
    return result;
}

static std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

static bool parseUrl(const std::string& url, std::string& scheme, std::string& host, int& port, std::string& path) {
    // Format: [scheme://]host[:port][/path]
    size_t pos = url.find("://");
    if (pos != std::string::npos) {
        scheme = url.substr(0, pos);
        pos += 3;
    } else {
        scheme = "https";
        pos = 0;
    }
    size_t pathPos = url.find('/', pos);
    std::string hostPort;
    if (pathPos != std::string::npos) {
        hostPort = url.substr(pos, pathPos - pos);
        path = url.substr(pathPos);
    } else {
        hostPort = url.substr(pos);
        path = "/";
    }
    size_t colonPos = hostPort.find(':');
    if (colonPos != std::string::npos) {
        host = hostPort.substr(0, colonPos);
        port = std::stoi(hostPort.substr(colonPos + 1));
    } else {
        host = hostPort;
        port = (scheme == "https") ? 443 : 80;
    }
    return true;
}

HttpClient::HttpClient(const std::string& baseUrl, int timeoutSec)
    : m_timeoutSec(timeoutSec) {
    std::string scheme, host, path;
    parseUrl(baseUrl, scheme, host, m_port, path);
    m_host = host;
    m_https = (scheme == "https");
}

HttpClient::~HttpClient() {
    if (m_session) WinHttpCloseHandle(m_session);
}

void HttpClient::setHeader(const std::string& name, const std::string& value) {
    m_headers += name + ": " + value + "\r\n";
}

HttpClient::Response HttpClient::post(const std::string& path, const std::string& body, const std::string& contentType) {
    return request(L"POST", toWide(path), body, contentType);
}

HttpClient::Response HttpClient::get(const std::string& path) {
    return request(L"GET", toWide(path), "", "");
}

HttpClient::Response HttpClient::request(const std::wstring& method, const std::wstring& path,
                                          const std::string& body, const std::string& contentType) {
    Response resp;

    if (!m_session) {
        m_session = WinHttpOpen(L"AI-Copilot/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!m_session) {
            resp.error = true;
            resp.errorMsg = "WinHttpOpen failed";
            return resp;
        }
    }

    std::wstring whost = toWide(m_host);
    HINTERNET connect = WinHttpConnect(m_session, whost.c_str(), m_port, 0);
    if (!connect) {
        resp.error = true;
        resp.errorMsg = "WinHttpConnect failed";
        return resp;
    }

    DWORD flags = m_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        resp.error = true;
        resp.errorMsg = "WinHttpOpenRequest failed";
        return resp;
    }

    // Timeout
    int ms = m_timeoutSec * 1000;
    WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &ms, sizeof(ms));
    WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &ms, sizeof(ms));

    // Headers
    std::wstring wheaders;
    if (!m_headers.empty()) {
        wheaders = toWide(m_headers);
    }
    if (!contentType.empty()) {
        std::string ctHeader = "Content-Type: " + contentType + "\r\n";
        wheaders += toWide(ctHeader);
    }

    // Send
    LPCWSTR headerPtr = wheaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wheaders.c_str();
    DWORD headerLen = wheaders.empty() ? 0 : (DWORD)wheaders.size();

    BOOL sent = WinHttpSendRequest(request, headerPtr, headerLen,
                                    body.empty() ? nullptr : (LPVOID)body.data(),
                                    (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!sent) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        resp.error = true;
        resp.errorMsg = "WinHttpSendRequest failed";
        return resp;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        resp.error = true;
        resp.errorMsg = "WinHttpReceiveResponse failed";
        return resp;
    }

    // Status code
    DWORD statusSize = sizeof(DWORD);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        nullptr, &resp.status, &statusSize, nullptr);

    // Read body
    DWORD bytesRead = 0;
    std::vector<char> buf(4096);
    while (WinHttpReadData(request, buf.data(), (DWORD)buf.size(), &bytesRead) && bytesRead > 0) {
        resp.body.append(buf.data(), bytesRead);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);

    return resp;
}
