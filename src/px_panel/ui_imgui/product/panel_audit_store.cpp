#include "panel_audit_store.h"

#include "px_common/log.h"
#include "px_common/time_util.h"
#include "px_render_panel_message.pb.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace px::panel::product {
namespace {

constexpr int kMaximumRecords{100};

struct StatementDeleter final {
    void operator()(sqlite3_stmt* statement) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): SQLite ownership adapter
        if (statement)
            sqlite3_finalize(statement);
    }
};
using Statement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

Statement Prepare(sqlite3* database, const char* sql) { // NOLINT(gammaray-raw-pointer-boundary): transient SQLite ABI boundary
    sqlite3_stmt* statement{};                          // NOLINT(gammaray-raw-pointer-boundary): SQLite output parameter, immediately wrapped
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
        return {};
    return Statement{statement};
}

std::string ColumnText(const Statement& statement, const int column) {
    const auto value = sqlite3_column_text(statement.get(), column);
    return value ? reinterpret_cast<const char*>(value) : std::string{};
}

bool Execute(sqlite3* database, const char* sql) { // NOLINT(gammaray-raw-pointer-boundary): transient SQLite ABI boundary
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::string FileName(const std::string& value) {
    return std::filesystem::path{value}.filename().string();
}

} // namespace

void PanelAuditStore::DatabaseDeleter::operator()(sqlite3* database) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
    if (database)
        sqlite3_close(database);
}

std::shared_ptr<PanelAuditStore> PanelAuditStore::Create(const std::filesystem::path& dataDirectory) {
    std::error_code error{};
    std::filesystem::create_directories(dataDirectory, error);
    if (error)
        return {};
    auto result = std::make_shared<PanelAuditStore>(dataDirectory / "px_data.db");
    return result->Ready() ? result : std::shared_ptr<PanelAuditStore>{};
}

PanelAuditStore::PanelAuditStore(const std::filesystem::path& databasePath) {
    sqlite3* opened{}; // NOLINT(gammaray-raw-pointer-boundary): SQLite output parameter, immediately wrapped
    if (sqlite3_open_v2(databasePath.string().c_str(), &opened, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) !=
        SQLITE_OK) {
        if (opened)
            sqlite3_close(opened);
        return;
    }
    database_.reset(opened);
    static_cast<void>(Execute(database_.get(), "PRAGMA journal_mode=WAL;"));
    static_cast<void>(Execute(database_.get(), "PRAGMA busy_timeout=3000;"));
    const bool visits =
        Execute(database_.get(),
                "CREATE TABLE IF NOT EXISTS visit_record (id INTEGER PRIMARY KEY, stream_id TEXT NOT NULL, conn_id TEXT UNIQUE NOT NULL, "
                "conn_type TEXT NOT NULL, begin INTEGER NOT NULL, end INTEGER NOT NULL, duration INTEGER NOT NULL, visitor_device TEXT NOT NULL, "
                "target_device TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'running', end_reason TEXT NOT NULL DEFAULT '', recovered INTEGER NOT "
                "NULL DEFAULT 0);");
    const bool transfers = Execute(
        database_.get(),
        "CREATE TABLE IF NOT EXISTS file_transfer_record (id INTEGER PRIMARY KEY, the_file_id TEXT UNIQUE NOT NULL, begin INTEGER NOT NULL, "
        "end INTEGER NOT NULL, visitor_device TEXT NOT NULL, target_device TEXT NOT NULL, direction TEXT NOT NULL, file_detail TEXT NOT NULL, "
        "success INTEGER NOT NULL, duration INTEGER NOT NULL, status TEXT NOT NULL DEFAULT 'running', end_reason TEXT NOT NULL DEFAULT '', "
        "recovered INTEGER NOT NULL DEFAULT 0);");
    if (!visits || !transfers)
        database_.reset();
}

PanelAuditStore::~PanelAuditStore() = default;
bool PanelAuditStore::Ready() const {
    return database_ != nullptr;
}

void PanelAuditStore::Consume(const pxrp::RpMessage& message, const std::string& targetDeviceId) {
    const std::scoped_lock lock{mutex_};
    if (!database_)
        return;
    if (message.type() == pxrp::kRpClientConnected) {
        const auto& value = message.client_connected();
        auto statement = Prepare(
            database_.get(),
            "INSERT INTO visit_record(stream_id,conn_id,conn_type,begin,end,duration,visitor_device,target_device,status,end_reason,recovered) "
            "VALUES(?,?,?,?,0,0,?,?,'running','',0) ON CONFLICT(conn_id) DO NOTHING;");
        if (!statement)
            return;
        sqlite3_bind_text(statement.get(), 1, value.stream_id().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 2, value.conn_id().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 3, value.conn_type().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement.get(), 4, value.begin_timestamp());
        sqlite3_bind_text(statement.get(), 5, value.visitor_device_id().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 6, targetDeviceId.c_str(), -1, SQLITE_TRANSIENT);
        static_cast<void>(sqlite3_step(statement.get()));
        revision_.fetch_add(1, std::memory_order_acq_rel);
    } else if (message.type() == pxrp::kRpClientDisConnected) {
        const auto& value = message.client_disconnected();
        auto statement = Prepare(database_.get(), "UPDATE visit_record SET end=?,duration=?,status='succeeded',end_reason='' WHERE conn_id=?;");
        if (!statement)
            return;
        sqlite3_bind_int64(statement.get(), 1, value.end_timestamp());
        sqlite3_bind_int64(statement.get(), 2, value.duration());
        sqlite3_bind_text(statement.get(), 3, value.conn_id().c_str(), -1, SQLITE_TRANSIENT);
        static_cast<void>(sqlite3_step(statement.get()));
        revision_.fetch_add(1, std::memory_order_acq_rel);
    } else if (message.type() == pxrp::kRpFileTransferBegin) {
        const auto& value = message.ft_begin();
        auto statement = Prepare(database_.get(), "INSERT INTO "
                                                  "file_transfer_record(the_file_id,begin,end,visitor_device,target_device,direction,file_detail,"
                                                  "success,duration,status,end_reason,recovered) "
                                                  "VALUES(?, ?,0,?,?,?, ?,0,0,'running','',0) ON CONFLICT(the_file_id) DO NOTHING;");
        if (!statement)
            return;
        sqlite3_bind_text(statement.get(), 1, value.the_file_id().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement.get(), 2, value.begin_timestamp());
        sqlite3_bind_text(statement.get(), 3, value.visitor_device_id().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 4, targetDeviceId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 5, value.direction().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 6, value.file_detail().c_str(), -1, SQLITE_TRANSIENT);
        static_cast<void>(sqlite3_step(statement.get()));
        revision_.fetch_add(1, std::memory_order_acq_rel);
    } else if (message.type() == pxrp::kRpFileTransferEnd) {
        const auto& value = message.ft_end();
        auto statement =
            Prepare(database_.get(), "UPDATE file_transfer_record SET end=?,duration=?,success=?,status=?,end_reason=? WHERE the_file_id=?;");
        if (!statement)
            return;
        const std::string status = value.status().empty() ? (value.success() ? "succeeded" : "failed") : value.status();
        sqlite3_bind_int64(statement.get(), 1, value.end_timestamp());
        sqlite3_bind_int64(statement.get(), 2, value.duration());
        sqlite3_bind_int(statement.get(), 3, value.success() ? 1 : 0);
        sqlite3_bind_text(statement.get(), 4, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 5, value.end_reason().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement.get(), 6, value.the_file_id().c_str(), -1, SQLITE_TRANSIENT);
        static_cast<void>(sqlite3_step(statement.get()));
        revision_.fetch_add(1, std::memory_order_acq_rel);
    }
}

std::uint64_t PanelAuditStore::Revision() const {
    return revision_.load(std::memory_order_acquire);
}

std::vector<ui::SecurityRecord> PanelAuditStore::Query(const ui::SecurityRecordKind kind) const {
    const std::scoped_lock lock{mutex_};
    std::vector<ui::SecurityRecord> result{};
    if (!database_)
        return result;
    const std::string_view sql =
        kind == ui::SecurityRecordKind::Visit
            ? "SELECT id,conn_type,begin,end,duration,visitor_device,target_device,status,conn_id,stream_id FROM visit_record ORDER BY id DESC LIMIT "
              "100;"
            : "SELECT id,begin,end,visitor_device,target_device,direction,file_detail,success,duration,status,the_file_id FROM file_transfer_record "
              "ORDER BY id DESC LIMIT 100;";
    auto statement = Prepare(database_.get(), sql.data());
    if (!statement)
        return result;
    while (sqlite3_step(statement.get()) == SQLITE_ROW && result.size() < kMaximumRecords) {
        if (kind == ui::SecurityRecordKind::Visit) {
            const auto begin = sqlite3_column_int64(statement.get(), 2);
            const auto end = sqlite3_column_int64(statement.get(), 3);
            const auto duration = sqlite3_column_int64(statement.get(), 4);
            const std::string type{ColumnText(statement, 1)};
            const std::string visitor{ColumnText(statement, 5)};
            const std::string target{ColumnText(statement, 6)};
            const std::string status{ColumnText(statement, 7)};
            const std::string connectionId{ColumnText(statement, 8)};
            const std::string streamId{ColumnText(statement, 9)};
            const auto json =
                nlohmann::json{{"connection_id", connectionId}, {"stream_id", streamId}, {"type", type},     {"begin", begin},  {"end", end},
                               {"duration", duration},          {"visitor", visitor},    {"target", target}, {"status", status}}
                    .dump();
            result.push_back({.id = sqlite3_column_int(statement.get(), 0),
                              .succeeded = status == "succeeded",
                              .type = type,
                              .startedAt = TimeUtil::FormatTimestamp(begin),
                              .endedAt = end > 0 ? TimeUtil::FormatTimestamp(end) : "- - -",
                              .duration = TimeUtil::FormatSecondsToDHMS(duration / 1000),
                              .visitor = visitor,
                              .target = target,
                              .plainText = std::format("{} {} -> {} ({})", type, visitor, target, status),
                              .json = json});
        } else {
            const auto begin = sqlite3_column_int64(statement.get(), 1);
            const auto end = sqlite3_column_int64(statement.get(), 2);
            const std::string visitor{ColumnText(statement, 3)};
            const std::string target{ColumnText(statement, 4)};
            const std::string direction{ColumnText(statement, 5)};
            const std::string detail{ColumnText(statement, 6)};
            const bool success = sqlite3_column_int(statement.get(), 7) != 0;
            const auto duration = sqlite3_column_int64(statement.get(), 8);
            const std::string status{ColumnText(statement, 9)};
            const std::string fileId{ColumnText(statement, 10)};
            const auto json =
                nlohmann::json{{"file_id", fileId}, {"begin", begin},         {"end", end},     {"duration", duration}, {"visitor", visitor},
                               {"target", target},  {"direction", direction}, {"file", detail}, {"status", status}}
                    .dump();
            result.push_back({.id = sqlite3_column_int(statement.get(), 0),
                              .succeeded = success,
                              .startedAt = TimeUtil::FormatTimestamp(begin),
                              .endedAt = end > 0 ? TimeUtil::FormatTimestamp(end) : "- - -",
                              .duration = TimeUtil::FormatSecondsToDHMS(duration / 1000),
                              .visitor = visitor,
                              .target = target,
                              .direction = direction,
                              .fileName = FileName(detail),
                              .plainText = std::format("{} {} -> {} ({})", FileName(detail), visitor, target, status),
                              .json = json});
        }
    }
    return result;
}

bool PanelAuditStore::Delete(const ui::SecurityRecordKind kind, const int id) {
    const std::scoped_lock lock{mutex_};
    if (!database_)
        return false;
    const std::string_view sql{kind == ui::SecurityRecordKind::Visit ? "DELETE FROM visit_record WHERE id=?;"
                                                                     : "DELETE FROM file_transfer_record WHERE id=?;"};
    auto statement = Prepare(database_.get(), sql.data());
    if (!statement)
        return false;
    sqlite3_bind_int(statement.get(), 1, id);
    const bool deleted{sqlite3_step(statement.get()) == SQLITE_DONE};
    if (deleted)
        revision_.fetch_add(1, std::memory_order_acq_rel);
    return deleted;
}

bool PanelAuditStore::DeleteAll(const ui::SecurityRecordKind kind) {
    const std::scoped_lock lock{mutex_};
    const std::string_view sql{kind == ui::SecurityRecordKind::Visit ? "DELETE FROM visit_record;" : "DELETE FROM file_transfer_record;"};
    const bool deleted = database_ && Execute(database_.get(), sql.data());
    if (deleted)
        revision_.fetch_add(1, std::memory_order_acq_rel);
    return deleted;
}

} // namespace px::panel::product
