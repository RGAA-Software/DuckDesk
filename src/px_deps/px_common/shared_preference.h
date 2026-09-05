#ifndef DATABASE_H
#define DATABASE_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <leveldb/db.h>

namespace px {

using IVisitListener = std::function<void(const std::string& key, const std::string& value)>;

class SharedPreference {
  public:
    static std::shared_ptr<SharedPreference> Instance() {
        static auto instance = std::make_shared<SharedPreference>();
        return instance;
    }

    SharedPreference() = default;

    bool Init(std::filesystem::path directory, std::string_view name);
    void Release();
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] bool IsReadOnly() const;
    [[nodiscard]] std::string GetLastError() const;

    bool Put(const std::string& key, const std::string& value) const;
    bool PutInt(const std::string& key, int value) const;
    bool PutInt64(const std::string& key, std::int64_t value) const;
    [[nodiscard]] std::string Get(const std::string& key) const;
    [[nodiscard]] std::string Get(const std::string& key, const std::string& default_value) const;
    [[nodiscard]] int GetInt(const std::string& key, int default_value = 0) const;
    [[nodiscard]] std::int64_t GetInt64(const std::string& key, std::int64_t default_value = 0) const;
    bool Remove(const std::string& key) const;

    void Visit(IVisitListener listener) const;

  private:
    std::unique_ptr<leveldb::DB> db_{};
    bool initialized_{};
    bool read_only_{};
    std::string last_error_{};
    mutable std::mutex mtx_{};
};

} // namespace px

#endif // DATABASE_H
