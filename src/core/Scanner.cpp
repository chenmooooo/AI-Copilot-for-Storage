#include "core/Scanner.h"
#include <windows.h>
#include <chrono>

struct Scanner::Impl {
    std::wstring rootPath;
    ProgressCallback callback;
    std::unique_ptr<FileNode> rootNode;
    volatile bool cancelled = false;
    std::chrono::steady_clock::time_point startTime;

    int64_t scanBytes = 0;
    size_t scanFiles = 0;
    size_t scanDirs = 0;

    void scanDirectory(FileNode* parent, const std::wstring& dirPath) {
        if (cancelled) return;

        std::wstring searchPath = dirPath + L"\\*";
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileExW(searchPath.c_str(), FindExInfoBasic, &findData,
                                         FindExSearchNameMatch, nullptr, 0);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (cancelled) break;

            std::wstring name = findData.cFileName;
            if (name == L"." || name == L"..") continue;

            std::wstring fullPath = dirPath + L"\\" + name;

            if (callback) {
                callback(fullPath, 0);
            }

            FileNode node;
            node.name = name;
            node.fullPath = fullPath;
            node.lastModified = filetimeToChrono(findData.ftLastWriteTime);
            node.lastAccessed = filetimeToChrono(findData.ftLastAccessTime);
            node.created = filetimeToChrono(findData.ftCreationTime);

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                node.type = FileType::Symlink;
                node.sizeBytes = (static_cast<int64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
                parent->children.push_back(std::move(node));
                continue;
            }

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                node.type = FileType::Directory;
                scanDirs++;
                scanDirectory(&node, fullPath);
                for (auto& child : node.children) {
                    node.sizeBytes += child.sizeBytes;
                    node.fileCount += child.fileCount;
                    node.dirCount += child.dirCount;
                }
                node.dirCount++;
                node.childCount = (int)node.children.size();
            } else {
                node.type = FileType::File;
                node.sizeBytes = (static_cast<int64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
                scanFiles++;
                scanBytes += node.sizeBytes;
                node.fileCount = 1;
            }

            parent->children.push_back(std::move(node));

        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
    }

    static std::chrono::system_clock::time_point filetimeToChrono(const FILETIME& ft) {
        ULARGE_INTEGER ull;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        const uint64_t EPOCH_OFFSET = 116444736000000000ULL;
        if (ull.QuadPart >= EPOCH_OFFSET) {
            uint64_t sinceEpoch = ull.QuadPart - EPOCH_OFFSET;
            auto dur = std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::microseconds(sinceEpoch / 10));
            return std::chrono::system_clock::time_point(dur);
        }
        return std::chrono::system_clock::time_point{};
    }
};

Scanner::Scanner() : m_impl(std::make_unique<Impl>()) {}

Scanner::~Scanner() = default;

void Scanner::setRootPath(const std::wstring& path) {
    m_impl->rootPath = path;
}

void Scanner::setProgressCallback(ProgressCallback cb) {
    m_impl->callback = std::move(cb);
}

std::unique_ptr<FileNode> Scanner::scan() {
    m_impl->startTime = std::chrono::steady_clock::now();
    m_impl->scanBytes = 0;
    m_impl->scanFiles = 0;
    m_impl->scanDirs = 0;
    m_impl->cancelled = false;

    m_impl->rootNode = std::make_unique<FileNode>();
    m_impl->rootNode->fullPath = m_impl->rootPath;
    m_impl->rootNode->type = FileType::Directory;

    auto pos = m_impl->rootPath.rfind(L'\\');
    if (pos == std::wstring::npos) pos = m_impl->rootPath.rfind(L'/');
    if (pos != std::wstring::npos)
        m_impl->rootNode->name = m_impl->rootPath.substr(pos + 1);
    else
        m_impl->rootNode->name = m_impl->rootPath;

    m_impl->scanDirectory(m_impl->rootNode.get(), m_impl->rootPath);

    m_impl->rootNode->fileCount = m_impl->scanFiles;
    m_impl->rootNode->dirCount = m_impl->scanDirs + 1;
    m_impl->rootNode->childCount = (int)m_impl->rootNode->children.size();
    m_impl->rootNode->sizeBytes = 0;
    for (auto& child : m_impl->rootNode->children) {
        m_impl->rootNode->sizeBytes += child.sizeBytes;
    }

    m_totalBytes = m_impl->rootNode->sizeBytes;
    m_totalFiles = m_impl->scanFiles;
    m_totalDirs = m_impl->scanDirs + 1;
    m_elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - m_impl->startTime).count();

    return std::move(m_impl->rootNode);
}

void Scanner::cancel() {
    m_impl->cancelled = true;
}
