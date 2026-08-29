#ifndef PX_SQLITE_STORAGE_CONFIG_H
#define PX_SQLITE_STORAGE_CONFIG_H

#include <functional>

extern "C" {
#include <sqlite3.h>
}

namespace px
{
    inline constexpr int kSqliteBusyTimeoutMs = 3000;

    template <typename Storage>
    void ConfigureSqliteStorage(Storage& storage) {
        // sqlite_orm copies on_open into every storage copy. This gives each
        // lazily-opened SQLite connection the same bounded lock-wait policy.
        storage.on_open = std::bind(
            sqlite3_busy_timeout, std::placeholders::_1, kSqliteBusyTimeoutMs);
    }
}

#endif // PX_SQLITE_STORAGE_CONFIG_H
