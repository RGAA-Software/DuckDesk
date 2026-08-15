#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <cstdint>
#include <functional>
#include <mutex>
#include <leveldb/db.h>

namespace px
{

    typedef std::function<void(const std::string& key, const std::string& val)> IVisitListener;

    // 存储Key - Value值
    class SharedPreference {
    public:

        static SharedPreference* Instance() {
            static SharedPreference instance;
            return &instance;
        }

        SharedPreference() = default;

        bool Init(const std::wstring& path, const std::string& name);
        void Release() const;
        bool IsReady() const;
        bool IsReadOnly() const;
        std::string GetLastError() const;

        bool Put(const std::string& key, const std::string& value) const;
        bool PutInt(const std::string& key, int value) const;
        bool PutInt64(const std::string& key, int64_t value) const;
        std::string Get(const std::string& key) const;
        std::string Get(const std::string& key, const std::string& def) const;
        int GetInt(const std::string& key, int def = 0) const;
        int64_t GetInt64(const std::string& key, int64_t def = 0) const;
        bool Remove(const std::string& key) const;

        void Visit(IVisitListener&& listener) const;

    private:

        leveldb::DB* db_ = nullptr;
        bool initialized_ = false;
        bool read_only_ = false;
        std::string last_error_;
        mutable std::mutex mtx_;
};

}

#endif // DATABASE_H
