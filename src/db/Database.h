#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Database {
public:
    explicit Database(const std::string& dbPath);
    ~Database();

    bool open();
    void close();
    bool isOpen() const;

    bool migrate();

    bool saveAnalysis(const std::string& path, const json& result);
    std::optional<json> loadAnalysis(const std::string& path);

    bool saveConfig(const std::string& key, const std::string& value);
    std::optional<std::string> loadConfig(const std::string& key);

    bool saveScanHistory(const std::string& path, int64_t totalBytes, size_t totalFiles);
    std::vector<json> loadScanHistory(int limit = 20);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool exec(const std::string& sql);
    bool prepareAndExec(const std::string& sql, const std::vector<std::string>& params = {});
};
