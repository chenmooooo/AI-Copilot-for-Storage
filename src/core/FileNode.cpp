#include "core/FileNode.h"
#include <windows.h>
#include <algorithm>

std::string FileNode::extension() const {
    auto pos = name.rfind(L'.');
    if (pos == std::wstring::npos) return "";
    std::wstring ext = name.substr(pos);
    int len = WideCharToMultiByte(CP_UTF8, 0, ext.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, ext.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::string FileNode::narrowName() const {
    int len = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::string FileNode::narrowPath() const {
    int len = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}
