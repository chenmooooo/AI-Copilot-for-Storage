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

    std::unique_ptr<FileNode> scan();
    void cancel();

    int64_t totalBytes() const { return m_totalBytes; }
    size_t  totalFiles() const { return m_totalFiles; }
    size_t  totalDirs()  const { return m_totalDirs; }
    double  elapsedMs()  const { return m_elapsedMs; }
    double  scanTimeMs()  const { return m_scanTimeMs; }
    double  buildTimeMs() const { return m_buildTimeMs; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    int64_t m_totalBytes = 0;
    size_t m_totalFiles = 0;
    size_t m_totalDirs = 0;
    double m_elapsedMs = 0.0;
    double m_scanTimeMs = 0.0;
    double m_buildTimeMs = 0.0;
};
