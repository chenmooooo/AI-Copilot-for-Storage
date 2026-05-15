#include "core/NtfsScanner.h"
#include <windows.h>
#include <winioctl.h>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

// ====================================================================
// NTFS on-disk structures (ported from WinDirStat)
// ====================================================================

enum AttributeTypeCode : ULONG {
    AttrStandardInformation = 0x10,
    AttrFileName            = 0x30,
    AttrData                = 0x80,
    AttrReparsePoint        = 0xC0,
    AttrEnd                 = 0xFFFFFFFF,
};

#pragma pack(push, 1)
struct FILE_RECORD {
    ULONG    Signature;
    USHORT   UsaOffset;
    USHORT   UsaCount;
    ULONGLONG Lsn;
    USHORT   SequenceNumber;
    USHORT   LinkCount;
    USHORT   FirstAttributeOffset;
    USHORT   Flags;          // 0x0001 in-use, 0x0002 directory
    ULONG    FirstFreeByte;
    ULONG    BytesAvailable;
    ULONGLONG BaseFileRecordNumber : 48;
    ULONGLONG BaseFileRecordSequence : 16;
    USHORT   NextAttributeNumber;
    USHORT   SegmentNumberHighPart;
    ULONG    SegmentNumberLowPart;

    uint64_t SegmentNumber() const {
        return (static_cast<uint64_t>(SegmentNumberHighPart) << 32) | SegmentNumberLowPart;
    }
    bool IsValid()   const { return Signature == 0x454C4946; }
    bool IsInUse()   const { return Flags & 0x0001; }
    bool IsDir()     const { return Flags & 0x0002; }
};

struct ATTRIBUTE_RECORD {
    AttributeTypeCode TypeCode;
    ULONG   RecordLength;
    UCHAR   FormCode;       // 0=resident, 1=non-resident
    UCHAR   NameLength;
    USHORT  NameOffset;
    USHORT  Flags;
    USHORT  Instance;
    union {
        struct {
            ULONG   ValueLength;
            USHORT  ValueOffset;
            UCHAR   Reserved[2];
        } Resident;
        struct {
            LONGLONG LowestVcn;
            LONGLONG HighestVcn;
            USHORT   DataRunOffset;
            USHORT   CompressionSize;
            UCHAR    Padding[4];
            ULONGLONG AllocatedLength;
            ULONGLONG FileSize;
            ULONGLONG ValidDataLength;
            ULONGLONG Compressed;
        } Nonresident;
    } Form;

    bool IsNonResident() const { return FormCode & 0x0001; }
    bool IsCompressed()  const { return Flags & 0x0001; }
    bool IsSparse()      const { return Flags & 0x8000; }

    ATTRIBUTE_RECORD* Next() const {
        return reinterpret_cast<ATTRIBUTE_RECORD*>(
            reinterpret_cast<UCHAR*>(const_cast<ATTRIBUTE_RECORD*>(this)) + RecordLength);
    }

    static std::pair<ATTRIBUTE_RECORD*, ATTRIBUTE_RECORD*>
    Bounds(FILE_RECORD* fr, ULONG totalLength) {
        auto* begin = reinterpret_cast<ATTRIBUTE_RECORD*>(
            reinterpret_cast<UCHAR*>(fr) + fr->FirstAttributeOffset);
        auto* end   = reinterpret_cast<ATTRIBUTE_RECORD*>(
            reinterpret_cast<UCHAR*>(fr) + fr->FirstAttributeOffset + totalLength);
        return {begin, end};
    }
};

struct FILE_NAME {
    ULONGLONG ParentDirectory : 48;
    ULONGLONG ParentSequence  : 16;
    LONGLONG  CreationTime;
    LONGLONG  LastModificationTime;
    LONGLONG  MftChangeTime;
    LONGLONG  LastAccessTime;
    LONGLONG  AllocatedLength;
    LONGLONG  FileSize;
    ULONG     FileAttributes;
    USHORT    PackedEaSize;
    USHORT    Reserved;
    UCHAR     FileNameLength;
    UCHAR     Flags;        // 0x02 = short (8.3) name
    WCHAR     FileName[1];

    bool IsShortName() const { return Flags == 0x02; }
};

struct STANDARD_INFORMATION {
    FILETIME CreationTime;
    FILETIME LastModificationTime;
    FILETIME MftChangeTime;
    FILETIME LastAccessTime;
    ULONG    FileAttributes;
};
#pragma pack(pop)

// REPARSE_DATA_BUFFER (from <winnt.h>, may not be available with WIN32_LEAN_AND_MEAN)
struct REPARSE_DATA_BUFFER {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    WCHAR  PathBuffer[1];
};

// ====================================================================
// Internal data per MFT record
// ====================================================================
struct MftRecordInfo {
    uint64_t  parentRecord = 0;
    std::wstring name;
    uint64_t  logicalSize = 0;
    uint64_t  physicalSize = 0;
    FILETIME  lastModified = {};
    DWORD     attributes = FILE_ATTRIBUTE_NORMAL;
    DWORD     reparseTag = 0;
    bool      isDirectory = false;
    bool      isShortName = false;
};

// ====================================================================
// Helper
// ====================================================================
static std::chrono::system_clock::time_point filetimeToChrono(const FILETIME& ft) {
    ULARGE_INTEGER ull;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    constexpr uint64_t EPOCH_OFFSET = 116444736000000000ULL;
    if (ull.QuadPart >= EPOCH_OFFSET) {
        uint64_t sinceEpoch = ull.QuadPart - EPOCH_OFFSET;
        auto dur = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::microseconds(sinceEpoch / 10));
        return std::chrono::system_clock::time_point(dur);
    }
    return {};
}

// Split a path like L"C:\Users\Public" into components [L"C:", L"Users", L"Public"]
static std::vector<std::wstring> splitPath(const std::wstring& path) {
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (auto c : path) {
        if (c == L'\\' || c == L'/') {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

// Case-insensitive wide string compare
static bool iequals(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return _wcsnicmp(a.c_str(), b.c_str(), a.size()) == 0;
}

// ====================================================================
// Pimpl
// ====================================================================
struct NtfsScanner::Impl {
    std::wstring rootPath;
    ProgressCallback callback;
    std::atomic<bool> cancelled{false};
    std::chrono::steady_clock::time_point startTime;

    // Maps built from MFT parsing
    // parent record -> list of child (record, displayName)
    std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, std::wstring>>> parentToChild;
    // record -> MftRecordInfo
    std::unordered_map<uint64_t, MftRecordInfo> recordInfo;
    // volume parameters
    ULONG bytesPerCluster = 0;
    ULONG bytesPerSector = 0;
    ULONG bytesPerFrs = 0;       // File Record Segment size
    std::wstring volumePath;     // e.g. L"\\\\.\\C:"

    // stats accumulated during tree building
    int64_t totalBytes = 0;
    int64_t totalBytesOnDisk = 0;
    size_t totalFiles = 0;
    size_t totalDirs = 0;

    // ----------------------------------------------------------------
    // Build a FileNode subtree starting from a given MFT record
    // ----------------------------------------------------------------
    FileNode buildSubtree(uint64_t record, int depth, const std::wstring& parentPath) {
        FileNode node;
        auto it = recordInfo.find(record);
        if (it == recordInfo.end()) {
            node.type = FileType::Directory;
            node.name = L"(unknown)";
            node.fullPath = parentPath + L"\\" + node.name;
            return node;
        }

        auto& info = it->second;
        if (depth == 0) {
            // For the root, name is the volume label; fullPath is what user requested
        }

        if (info.isDirectory) {
            node.type = FileType::Directory;
            node.fileCount = 0;
            node.dirCount = 1;
        } else {
            node.type = FileType::File;
            node.fileCount = 1;
            node.dirCount = 0;
        }

        node.sizeBytes   = info.isDirectory ? 0 : static_cast<int64_t>(info.logicalSize);
        node.sizeOnDisk  = info.isDirectory ? 0 : static_cast<int64_t>(info.physicalSize);
        node.lastModified = filetimeToChrono(info.lastModified);

        if (info.reparseTag != 0) {
            node.type = FileType::Symlink;
        }

        if (info.isDirectory) {
            auto childIt = parentToChild.find(record);
            if (childIt != parentToChild.end()) {
                for (auto& [childRec, childName] : childIt->second) {
                    auto childInfoIt = recordInfo.find(childRec);
                    if (childInfoIt == recordInfo.end()) continue;
                    auto& childInfo = childInfoIt->second;

                    FileNode child;
                    child.name = childName;
                    // Build fullPath
                    std::wstring fp = parentPath;
                    if (!fp.empty() && fp.back() != L'\\') fp.push_back(L'\\');
                    fp += childName;
                    child.fullPath = fp;

                    if (childInfo.isDirectory) {
                        child.type = FileType::Directory;
                        auto subtree = buildSubtree(childRec, depth + 1, fp);
                        child.children = std::move(subtree.children);
                        child.sizeBytes  = subtree.sizeBytes;
                        child.sizeOnDisk = subtree.sizeOnDisk;
                        child.fileCount  = subtree.fileCount;
                        child.dirCount   = subtree.dirCount + 1;
                        child.childCount = child.children.size();
                        child.lastModified = subtree.lastModified;
                    } else {
                        child.type = FileType::File;
                        child.sizeBytes  = static_cast<int64_t>(childInfo.logicalSize);
                        child.sizeOnDisk = static_cast<int64_t>(childInfo.physicalSize);
                        child.fileCount  = 1;
                        child.dirCount   = 0;
                        child.childCount = 0;
                        child.lastModified = filetimeToChrono(childInfo.lastModified);

                        if (childInfo.reparseTag != 0) {
                            child.type = FileType::Symlink;
                        }

                        totalFiles++;
                        totalBytes += childInfo.logicalSize;
                        totalBytesOnDisk += childInfo.physicalSize;
                    }

                    if (childInfo.attributes & FILE_ATTRIBUTE_HIDDEN) {
                        // keep hidden files, but mark them
                    }

                    // Aggregate into current directory
                    if (child.isDirectory()) {
                        node.sizeBytes  += child.sizeBytes;
                        node.sizeOnDisk += child.sizeOnDisk;
                        node.fileCount  += child.fileCount;
                        node.dirCount   += child.dirCount;
                        node.childCount++;
                    } else {
                        node.sizeBytes  += child.sizeBytes;
                        node.sizeOnDisk += child.sizeOnDisk;
                        node.fileCount  += child.fileCount;
                        node.dirCount   += child.dirCount;
                        node.childCount++;
                    }

                    node.children.push_back(std::move(child));
                }
            }

            // Add this directory's own MFT record physical size
            // (the MFT record itself takes space)
            totalDirs++;
        }

        std::sort(node.children.begin(), node.children.end(),
            [](const FileNode& a, const FileNode& b) {
                return a.sizeBytes > b.sizeBytes;
            });

        return node;
    }

    // ----------------------------------------------------------------
    // Find the MFT record number for a given path starting from root (5)
    // ----------------------------------------------------------------
    uint64_t findRecordForPath(const std::vector<std::wstring>& components) {
        if (components.empty()) return 5; // root

        // Walk from root (record 5) through each component
        uint64_t currentRec = 5;
        size_t componentIdx = 0;

        // Skip the first component if it's a drive letter (e.g. "C:")
        if (components.size() >= 1 && components[0].size() == 2 &&
            components[0][1] == L':') {
            componentIdx = 1;
        }

        while (componentIdx < components.size()) {
            auto& target = components[componentIdx];
            auto childIt = parentToChild.find(currentRec);
            if (childIt == parentToChild.end()) return 5; // fallback to root

            bool found = false;
            for (auto& [childRec, childName] : childIt->second) {
                if (iequals(childName, target)) {
                    currentRec = childRec;
                    found = true;
                    break;
                }
            }
            if (!found) return 5; // path component not found, fallback
            componentIdx++;
        }
        return currentRec;
    }
};

// ====================================================================
// Public interface
// ====================================================================
NtfsScanner::NtfsScanner() : m_impl(std::make_unique<Impl>()) {}
NtfsScanner::~NtfsScanner() = default;

void NtfsScanner::setRootPath(const std::wstring& path) {
    m_impl->rootPath = path;
}

void NtfsScanner::setProgressCallback(ProgressCallback cb) {
    m_impl->callback = std::move(cb);
}

void NtfsScanner::cancel() {
    m_impl->cancelled = true;
}

std::unique_ptr<FileNode> NtfsScanner::scan() {
    m_impl->startTime = std::chrono::steady_clock::now();
    m_impl->cancelled = false;
    m_impl->parentToChild.clear();
    m_impl->recordInfo.clear();
    m_impl->totalBytes = 0;
    m_impl->totalBytesOnDisk = 0;
    m_impl->totalFiles = 0;
    m_impl->totalDirs = 0;

    auto setError = [&](const char* msg) {
        m_lastError = msg;
        fprintf(stderr, "[NtfsScanner] ERROR: %s\n", msg);
        OutputDebugStringA(("[NtfsScanner] ERROR: " + std::string(msg) + "\n").c_str());
    };

    fprintf(stderr, "[NtfsScanner] Starting NTFS fast scan: %S\n", m_impl->rootPath.c_str());
    OutputDebugStringA(("[NtfsScanner] Starting scan: " + std::string(m_impl->rootPath.begin(), m_impl->rootPath.end()) + "\n").c_str());

    // Determine volume path
    std::wstring volPath = m_impl->rootPath;
    while (!volPath.empty() && volPath.back() == L'\\') volPath.pop_back();
    if (volPath.size() >= 2 && volPath[1] == L':') {
        volPath = L"\\\\.\\" + volPath.substr(0, 2);
    } else {
        setError("Not a valid drive path (e.g. C:\\)");
        return nullptr;
    }
    m_impl->volumePath = volPath;

    // Check if process is elevated
    bool elevated = false;
    { HANDLE hToken = nullptr;
      if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
          TOKEN_ELEVATION te;
          DWORD size = 0;
          if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &size))
              elevated = te.TokenIsElevated;
          CloseHandle(hToken);
      } }

    // Open volume
    HANDLE hVolume = CreateFileW(volPath.c_str(),
        FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (hVolume == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED && !elevated)
            setError("Access denied. NTFS fast scan requires Administrator privileges. "
                     "Restart the program as Administrator.");
        else if (err == ERROR_ACCESS_DENIED)
            setError("Access denied even with admin rights. The volume may be locked.");
        else
            setError(("Failed to open volume. Error: " + std::to_string(err)).c_str());
        return nullptr;
    }

    // Get NTFS volume data
    NTFS_VOLUME_DATA_BUFFER vdb = {};
    DWORD bytesRet = 0;
    BOOL ok = DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA,
        nullptr, 0, &vdb, sizeof(vdb), &bytesRet, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        setError(("Not an NTFS volume or failed to query volume data. Error: " + std::to_string(err)).c_str());
        CloseHandle(hVolume); return nullptr;
    }

    m_impl->bytesPerSector = vdb.BytesPerSector;
    m_impl->bytesPerCluster = vdb.BytesPerCluster;
    m_impl->bytesPerFrs = vdb.BytesPerFileRecordSegment;

    // Open $MFT::$DATA
    std::wstring mftPath = m_impl->rootPath;
    while (!mftPath.empty() && mftPath.back() == L'\\') mftPath.pop_back();
    if (mftPath.size() >= 2 && mftPath[1] == L':') {
        mftPath = mftPath.substr(0, 2) + L"\\$MFT::$DATA";
    }
    HANDLE hMft = CreateFileW(mftPath.c_str(),
        FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_NO_BUFFERING,
        nullptr);
    if (hMft == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        setError(("Failed to open $MFT. Error: " + std::to_string(err)).c_str());
        CloseHandle(hVolume); return nullptr;
    }

    // Get retrieval pointers
    std::vector<BYTE> rpBuf(sizeof(RETRIEVAL_POINTERS_BUFFER) + 32 * sizeof(LARGE_INTEGER));
    STARTING_VCN_INPUT_BUFFER svib = {};
    DWORD lastErr = 0;
    while (true) {
        ok = DeviceIoControl(hMft, FSCTL_GET_RETRIEVAL_POINTERS,
            &svib, sizeof(svib), rpBuf.data(), static_cast<DWORD>(rpBuf.size()),
            &bytesRet, nullptr);
        if (ok) break;
        lastErr = GetLastError();
        if (lastErr == ERROR_MORE_DATA) {
            rpBuf.resize(rpBuf.size() * 2);
        } else {
            break;
        }
    }
    if (!ok && lastErr != ERROR_SUCCESS && lastErr != ERROR_MORE_DATA) {
        setError(("Failed to get MFT retrieval pointers. Error: " + std::to_string(lastErr)).c_str());
        CloseHandle(hMft); CloseHandle(hVolume);
        return nullptr;
    }

    auto* retrievalBuffer = reinterpret_cast<RETRIEVAL_POINTERS_BUFFER*>(rpBuf.data());
    std::vector<std::pair<ULONGLONG, ULONGLONG>> dataRuns;
    dataRuns.reserve(retrievalBuffer->ExtentCount);
    for (ULONG i = 0; i < retrievalBuffer->ExtentCount; ++i) {
        ULONGLONG prevVcn = (i == 0) ? retrievalBuffer->StartingVcn.QuadPart
                                     : retrievalBuffer->Extents[i - 1].NextVcn.QuadPart;
        ULONGLONG clusterCount = retrievalBuffer->Extents[i].NextVcn.QuadPart - prevVcn;
        dataRuns.emplace_back(retrievalBuffer->Extents[i].Lcn.QuadPart, clusterCount);
    }
    CloseHandle(hMft);

    // Read and parse MFT data runs in parallel
    constexpr size_t READ_SIZE = 4 * 1024 * 1024; // 4 MB read buffer per thread
    const unsigned numThreads = (std::min)(static_cast<unsigned>(dataRuns.size()),
        std::thread::hardware_concurrency());

    // Thread-local result accumulator
    struct MftChunk {
        std::unordered_map<uint64_t, MftRecordInfo> records;
        std::unordered_map<uint64_t, std::vector<std::pair<uint64_t, std::wstring>>> children;
    };
    std::vector<MftChunk> chunks(numThreads);
    std::atomic<bool> anyCancelled{false};

    {
        std::vector<std::thread> workers;
        workers.reserve(numThreads);

        for (unsigned tid = 0; tid < numThreads; ++tid) {
            workers.emplace_back([&, tid]() {
                auto& localRecords = chunks[tid].records;
                auto& localChildren = chunks[tid].children;

                // Each thread opens its OWN volume handle
                HANDLE hThreadVol = CreateFileW(m_impl->volumePath.c_str(),
                    FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_NO_BUFFERING,
                    nullptr);
                if (hThreadVol == INVALID_HANDLE_VALUE) return;

                auto* threadBuf = static_cast<UCHAR*>(
                    _aligned_malloc(READ_SIZE, m_impl->bytesPerSector));
                if (!threadBuf) { CloseHandle(hThreadVol); return; }

                // Distribute data runs round-robin across threads
                for (size_t i = tid; i < dataRuns.size(); i += numThreads) {
                    if (anyCancelled || m_impl->cancelled) break;

                    auto [lcn, clusterCount] = dataRuns[i];
                    ULONGLONG bytesToRead = clusterCount * m_impl->bytesPerCluster;
                    LARGE_INTEGER fileOffset;
                    fileOffset.QuadPart = static_cast<LONGLONG>(lcn * m_impl->bytesPerCluster);

                    while (bytesToRead > 0) {
                        if (anyCancelled || m_impl->cancelled) break;

                        ULONG chunk = static_cast<ULONG>(
                            (std::min)(bytesToRead, static_cast<ULONGLONG>(READ_SIZE)));

                        // Synchronous read with NO_BUFFERING
                        ULONG bytesRead = 0;
                        OVERLAPPED ov = {};
                        ov.Offset     = fileOffset.LowPart;
                        ov.OffsetHigh = static_cast<DWORD>(fileOffset.HighPart);

                        if (!ReadFile(hThreadVol, threadBuf, chunk, &bytesRead, &ov)) {
                            if (GetLastError() == ERROR_IO_PENDING) {
                                WaitForSingleObject(hThreadVol, INFINITE);
                                GetOverlappedResult(hThreadVol, &ov, &bytesRead, FALSE);
                            } else {
                                break;
                            }
                        }
                        if (bytesRead == 0) break;

                        // Parse each FRS in this chunk (no mutex needed — thread-local)
                        for (ULONG off = 0; off + m_impl->bytesPerFrs <= bytesRead; off += m_impl->bytesPerFrs) {
                            UCHAR* frBuf = threadBuf + off;
                            auto* sig = reinterpret_cast<ULONG*>(frBuf);
                            if (*sig != 0x454C4946) continue;
                            auto* flags = reinterpret_cast<USHORT*>(frBuf + 0x16);
                            if (!(*flags & 0x0001)) continue;

                            // Inline parse directly into thread-local maps
                            auto* fr = reinterpret_cast<FILE_RECORD*>(frBuf);

                            // Apply fixup
                            constexpr ULONG MFT_SECTOR = 512;
                            const auto wps = MFT_SECTOR / sizeof(USHORT);
                            if (fr->UsaCount > 0 && fr->UsaOffset + fr->UsaCount * sizeof(USHORT) <= m_impl->bytesPerFrs) {
                                auto* fixup = reinterpret_cast<USHORT*>(frBuf + fr->UsaOffset);
                                USHORT usn = fixup[0];
                                auto* rw = reinterpret_cast<USHORT*>(frBuf);
                                bool corrupt = false;
                                for (USHORT j = 1; j < fr->UsaCount; ++j) {
                                    auto& se = rw[j * wps - 1];
                                    if (se == usn) { se = fixup[j]; }
                                    else { corrupt = true; break; }
                                }
                                if (corrupt) continue;
                            }

                            auto currentRec = fr->SegmentNumber();
                            auto baseIdx = fr->BaseFileRecordNumber > 0
                                ? static_cast<uint64_t>(fr->BaseFileRecordNumber) : currentRec;

                            auto& base = localRecords[baseIdx];
                            if (baseIdx == currentRec) base.isDirectory = fr->IsDir();

                            auto [curAttr, endAttr] = ATTRIBUTE_RECORD::Bounds(fr, m_impl->bytesPerFrs);
                            for (; curAttr < endAttr && curAttr->TypeCode != AttrEnd;
                                   curAttr = curAttr->Next()) {

                                if (curAttr->TypeCode == AttrStandardInformation) {
                                    if (curAttr->IsNonResident()) continue;
                                    auto* si = reinterpret_cast<STANDARD_INFORMATION*>(
                                        reinterpret_cast<UCHAR*>(curAttr) + curAttr->Form.Resident.ValueOffset);
                                    base.lastModified = si->LastModificationTime;
                                    base.attributes   = si->FileAttributes;
                                    if (fr->IsDir()) base.attributes |= FILE_ATTRIBUTE_DIRECTORY;
                                    if (base.attributes == 0) base.attributes = FILE_ATTRIBUTE_NORMAL;

                                } else if (curAttr->TypeCode == AttrFileName) {
                                    if (curAttr->IsNonResident()) continue;
                                    auto* fn = reinterpret_cast<FILE_NAME*>(
                                        reinterpret_cast<UCHAR*>(curAttr) + curAttr->Form.Resident.ValueOffset);
                                    if (fn->IsShortName()) continue;
                                    if ((fn->FileNameLength == 1 && fn->FileName[0] == L'.') ||
                                        (fn->FileNameLength == 2 && fn->FileName[0] == L'.' && fn->FileName[1] == L'.'))
                                        continue;

                                    std::wstring nm(fn->FileName, fn->FileNameLength);
                                    uint64_t parent = fn->ParentDirectory;
                                    localChildren[parent].emplace_back(baseIdx, std::move(nm));
                                    base.parentRecord = parent;

                                } else if (curAttr->TypeCode == AttrData) {
                                    if (curAttr->NameLength > 0) {
                                        auto* sn = reinterpret_cast<WCHAR*>(
                                            reinterpret_cast<UCHAR*>(curAttr) + curAttr->NameOffset);
                                        if (std::wstring_view(sn, curAttr->NameLength) == L"WofCompressedData") {
                                            base.physicalSize = curAttr->IsNonResident()
                                                ? curAttr->Form.Nonresident.AllocatedLength
                                                : (curAttr->Form.Resident.ValueLength + 7) & ~7;
                                        }
                                        continue;
                                    }
                                    if (curAttr->IsNonResident()) {
                                        if (curAttr->Form.Nonresident.LowestVcn != 0) continue;
                                        base.logicalSize  = curAttr->Form.Nonresident.FileSize;
                                        base.physicalSize = (curAttr->IsCompressed() || curAttr->IsSparse())
                                            ? curAttr->Form.Nonresident.Compressed
                                            : curAttr->Form.Nonresident.AllocatedLength;
                                    } else {
                                        base.logicalSize  = curAttr->Form.Resident.ValueLength;
                                        base.physicalSize = (curAttr->Form.Resident.ValueLength + 7) & ~7;
                                    }

                                } else if (curAttr->TypeCode == AttrReparsePoint) {
                                    if (curAttr->IsNonResident()) continue;
                                    auto* rdb = reinterpret_cast<REPARSE_DATA_BUFFER*>(
                                        reinterpret_cast<UCHAR*>(curAttr) + curAttr->Form.Resident.ValueOffset);
                                    base.reparseTag = rdb->ReparseTag;
                                    if (rdb->ReparseTag == IO_REPARSE_TAG_WOF)
                                        base.attributes |= FILE_ATTRIBUTE_COMPRESSED;
                                }
                            }
                        }

                        fileOffset.QuadPart += bytesRead;
                        bytesToRead -= bytesRead;
                    }
                }

                _aligned_free(threadBuf);
                CloseHandle(hThreadVol);
            });
        }

        for (auto& w : workers) w.join();
    }

    // Merge thread-local maps into main maps
    for (auto& chunk : chunks) {
        for (auto& [key, val] : chunk.records) {
            auto& dest = m_impl->recordInfo[key];
            if (dest.name.empty()) {
                dest = std::move(val);
            }
        }
        for (auto& [key, val] : chunk.children) {
            auto& dest = m_impl->parentToChild[key];
            dest.insert(dest.end(),
                std::make_move_iterator(val.begin()),
                std::make_move_iterator(val.end()));
        }
    }

    CloseHandle(hVolume);

    if (m_impl->cancelled) return nullptr;

    // Build tree from MFT maps
    auto components = splitPath(m_impl->rootPath);
    uint64_t targetRecord = m_impl->findRecordForPath(components);

    // Build subtree
    auto rootNode = std::make_unique<FileNode>();
    // Set root name from path
    auto pos = m_impl->rootPath.rfind(L'\\');
    if (pos == std::wstring::npos) pos = m_impl->rootPath.rfind(L'/');
    if (pos != std::wstring::npos)
        rootNode->name = m_impl->rootPath.substr(pos + 1);
    else
        rootNode->name = m_impl->rootPath;
    rootNode->fullPath = m_impl->rootPath;
    rootNode->type = FileType::Directory;

    auto subtree = m_impl->buildSubtree(targetRecord, 0, m_impl->rootPath);
    rootNode->children = std::move(subtree.children);
    rootNode->sizeBytes  = subtree.sizeBytes;
    rootNode->sizeOnDisk = subtree.sizeOnDisk;
    rootNode->fileCount  = subtree.fileCount;
    rootNode->dirCount   = subtree.dirCount + 1;
    rootNode->childCount = rootNode->children.size();

    m_totalBytes       = m_impl->totalBytes;
    m_totalBytesOnDisk = m_impl->totalBytesOnDisk;
    m_totalFiles       = m_impl->totalFiles;
    m_totalDirs        = m_impl->totalDirs;
    m_elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - m_impl->startTime).count();

    return rootNode;
}
