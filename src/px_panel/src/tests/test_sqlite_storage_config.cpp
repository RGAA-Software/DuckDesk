#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <string>

#include <sqlite_orm/sqlite_orm.h>

#include "../render_panel/database/sqlite_storage_config.h"

namespace px
{
    namespace {
        struct TestRow {
            int id{0};
            std::string value;
        };

        auto MakeTestStorage(const std::string& path) {
            using namespace sqlite_orm;
            auto storage = make_storage(
                path,
                make_table("rows",
                           make_column("id", &TestRow::id, primary_key().autoincrement()),
                           make_column("value", &TestRow::value)));
            ConfigureSqliteStorage(storage);
            return storage;
        }

        class TempDatabaseFile {
        public:
            TempDatabaseFile() {
                const auto unique = std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count());
                path_ = std::filesystem::temp_directory_path() /
                        ("px_sqlite_busy_timeout_" + unique + ".db");
            }

            ~TempDatabaseFile() {
                std::error_code error;
                std::filesystem::remove(path_, error);
                std::filesystem::remove(path_.string() + "-journal", error);
                std::filesystem::remove(path_.string() + "-wal", error);
                std::filesystem::remove(path_.string() + "-shm", error);
            }

            [[nodiscard]] std::string String() const {
                return path_.string();
            }

        private:
            std::filesystem::path path_;
        };
    }

    TEST(SqliteStorageConfig, AppliesBusyTimeoutToCopiedStorage) {
        TempDatabaseFile database;
        auto first = MakeTestStorage(database.String());
        first.sync_schema();
        auto copied = first;

        EXPECT_EQ(copied.pragma.busy_timeout(), kSqliteBusyTimeoutMs);
    }

    TEST(SqliteStorageConfig, ConcurrentWriterWaitsForShortLockAndSucceeds) {
        TempDatabaseFile database;
        auto first = MakeTestStorage(database.String());
        first.sync_schema();
        auto second = first;

        auto transaction = first.transaction_guard();
        first.insert(TestRow{0, "first"});

        auto write_result = std::async(std::launch::async,
            [second = std::move(second)]() mutable -> std::string {
                try {
                    second.insert(TestRow{0, "second"});
                    return {};
                }
                catch (const std::exception& error) {
                    return error.what();
                }
            });

        EXPECT_EQ(write_result.wait_for(std::chrono::milliseconds(100)),
                  std::future_status::timeout);
        transaction.commit();
        EXPECT_EQ(write_result.wait_for(std::chrono::seconds(2)),
                  std::future_status::ready);
        EXPECT_TRUE(write_result.get().empty());
        EXPECT_EQ(first.count<TestRow>(), 2);
    }
}
