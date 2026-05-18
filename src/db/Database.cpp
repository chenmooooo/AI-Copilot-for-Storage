#include "db/Database.h"
#include <stdexcept>
#include <windows.h>

static void dbLog(const char* msg) {
    OutputDebugStringA(msg);
}

struct Database::Impl {
    sqlite3* db = nullptr;
    std::string path;
};

Database::Database(const std::string& dbPath)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->path = dbPath;
}

Database::~Database() {
    close();
}

bool Database::open() {
    if (sqlite3_open(m_impl->path.c_str(), &m_impl->db) != SQLITE_OK) {
        return false;
    }
    return migrate();
}

void Database::close() {
    if (m_impl->db) {
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
    }
}

bool Database::isOpen() const {
    return m_impl->db != nullptr;
}

bool Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(m_impl->db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string log = "[DB] exec failed: " + std::string(err) + "\n  SQL: " + sql + "\n";
        dbLog(log.c_str());
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool Database::prepareAndExec(const std::string& sql, const std::vector<std::string>& params) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_impl->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::string log = "[DB] prepare failed: " + std::string(sqlite3_errmsg(m_impl->db)) + "\n  SQL: " + sql + "\n";
        dbLog(log.c_str());
        return false;
    }

    for (size_t i = 0; i < params.size(); i++) {
        sqlite3_bind_text(stmt, static_cast<int>(i + 1), params[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!ok) {
        std::string log = "[DB] step failed: " + std::string(sqlite3_errmsg(m_impl->db)) + "\n  SQL: " + sql + "\n";
        dbLog(log.c_str());
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::migrate() {
    return exec(
        "CREATE TABLE IF NOT EXISTS analysis_cache ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path TEXT UNIQUE NOT NULL,"
        "  result TEXT NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS config ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS scan_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path TEXT NOT NULL,"
        "  total_bytes INTEGER NOT NULL,"
        "  total_files INTEGER NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
    );
}

bool Database::saveAnalysis(const std::string& path, const json& result) {
    return prepareAndExec(
        "INSERT OR REPLACE INTO analysis_cache (path, result) VALUES (?, ?)",
        {path, result.dump()}
    );
}

    std::optional<json> Database::loadAnalysis(const std::string& path) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT result FROM analysis_cache WHERE path = ? ORDER BY id DESC LIMIT 1";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::string log = "[DB] loadAnalysis prepare failed: " + std::string(sqlite3_errmsg(m_impl->db)) + "\n";
        dbLog(log.c_str());
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<json> result;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) {
            result = json::parse(text, nullptr, false);
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

bool Database::saveConfig(const std::string& key, const std::string& value) {
    return prepareAndExec(
        "INSERT OR REPLACE INTO config (key, value) VALUES (?, ?)",
        {key, value}
    );
}

std::optional<std::string> Database::loadConfig(const std::string& key) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM config WHERE key = ?";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::string log = "[DB] loadConfig prepare failed: " + std::string(sqlite3_errmsg(m_impl->db)) + "\n";
        dbLog(log.c_str());
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> result;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) {
            result = std::string(text);
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

bool Database::saveScanHistory(const std::string& path, int64_t totalBytes, size_t totalFiles) {
    return prepareAndExec(
        "INSERT INTO scan_history (path, total_bytes, total_files) VALUES (?, ?, ?)",
        {path, std::to_string(totalBytes), std::to_string(totalFiles)}
    );
}

std::vector<json> Database::loadScanHistory(int limit) {
    std::vector<json> results;
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT path, total_bytes, total_files, created_at "
                      "FROM scan_history ORDER BY id DESC LIMIT ?";
    if (sqlite3_prepare_v2(m_impl->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::string log = "[DB] loadScanHistory prepare failed: " + std::string(sqlite3_errmsg(m_impl->db)) + "\n";
        dbLog(log.c_str());
        return results;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json j;
        j["path"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        j["total_bytes"] = sqlite3_column_int64(stmt, 1);
        j["total_files"] = sqlite3_column_int(stmt, 2);
        j["created_at"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        results.push_back(j);
    }

    sqlite3_finalize(stmt);
    return results;
}
