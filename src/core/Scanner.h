#pragma once
#include "FileNode.h"
#include <memory>
#include <functional>

class Scanner {
public:
    using ProgressCallback = std::function<void(const std::wstring& path, int64_t bytes)>;

    Scanner();
    ~Scanner();

    void setRootPath(const std::wstring& path);
    void setProgressCallback(ProgressCallback cb);

    FileNode* scan();
    void cancel();

    int64_t totalBytes() const { return m_totalBytes; }
    size_t  totalFiles() const { return m_totalFiles; }
    size_t  totalDirs()  const { return m_totalDirs; }
    double  elapsedMs()  const { return m_elapsedMs; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
