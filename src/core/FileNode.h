#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

enum class FileType {
    Unknown,
    File,
    Directory,
    Symlink
};

enum class RiskLevel {
    Safe,
    Low,
    Medium,
    High,
    Critical
};

struct FileNode {
    std::wstring  name;
    std::wstring  fullPath;
    FileType      type = FileType::Unknown;
    int64_t       sizeBytes = 0;
    int64_t       sizeOnDisk = 0;
    bool          isAllocated = false;
    std::chrono::system_clock::time_point lastModified;
    std::chrono::system_clock::time_point lastAccessed;
    std::chrono::system_clock::time_point created;

    std::vector<FileNode> children;
    size_t                childCount = 0;
    size_t                fileCount = 0;
    size_t                dirCount = 0;
    int                   depth = 0;

    std::string  category;
    std::string  description;
    RiskLevel    riskLevel = RiskLevel::Safe;
    bool         analyzed = false;

    bool isDirectory() const { return type == FileType::Directory; }
    bool isFile()      const { return type == FileType::File; }

    std::string extension() const;
    std::string narrowName() const;
    std::string narrowPath() const;
};
