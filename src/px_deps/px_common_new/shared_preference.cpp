#include "shared_preference.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "log.h"
#include "string_util.h"

namespace px 
{

    bool SharedPreference::Init(const std::wstring& path, const std::string& name) {
        std::lock_guard<std::mutex> lock(mtx_);
        db_.reset();
        initialized_ = false;
        read_only_ = false;
        last_error_.clear();

        leveldb::Options options;
        options.create_if_missing = true;
        std::wstring target_path = path;
        if (path.empty()) {
            target_path = L".";
        }

        std::filesystem::path p(target_path);
        p /= name;

        std::u8string u8 = p.u8string();
        std::string str(
            reinterpret_cast<const char*>(u8.data()),
            u8.size()
        );
        LOGI("SharedPreference: {}", str);
        leveldb::DB* opened_db = nullptr; // NOLINT(gammaray-raw-pointer-boundary) LevelDB transfers ownership through DB**.
        auto status = leveldb::DB::Open(options, str, &opened_db);
        if (!status.ok()) {
            read_only_ = true;
            last_error_ = status.ToString();
            LOGE("SharedPreference::Init failed, path: {}, error: {}", str, last_error_);
            return false;
        }

        db_.reset(opened_db);
        initialized_ = true;
        return true;
    }
    
    void SharedPreference::Release() {
        std::lock_guard<std::mutex> lock(mtx_);
        db_.reset();
        initialized_ = false;
        read_only_ = false;
        last_error_.clear();
    }

    bool SharedPreference::IsReady() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return initialized_;
    }

    bool SharedPreference::IsReadOnly() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return read_only_;
    }

    std::string SharedPreference::GetLastError() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return last_error_;
    }
    
    bool SharedPreference::Put(const std::string& key, const std::string& value) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return false;
        }
        auto st = db_->Put(leveldb::WriteOptions(), key, value);
        return st.ok();
    }

    bool SharedPreference::PutInt(const std::string& key, int value) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return false;
        }
        auto st = db_->Put(leveldb::WriteOptions(), key, std::to_string(value));
        return st.ok();
    }

    bool SharedPreference::PutInt64(const std::string& key, int64_t value) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return false;
        }
        auto st = db_->Put(leveldb::WriteOptions(), key, std::to_string(value));
        return st.ok();
    }

    std::string SharedPreference::Get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return "";
        }
        std::string value;
        db_->Get(leveldb::ReadOptions(), key, &value);
        return value;
    }

    std::string SharedPreference::Get(const std::string& key, const std::string& def) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return def;
        }
        std::string value;
        auto status = db_->Get(leveldb::ReadOptions(), key, &value);
        if (status.ok()) {
            return value;
        } else {
            return def;
        }
    }

    int SharedPreference::GetInt(const std::string& key, int def) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return def;
        }
        std::string value;
        auto status = db_->Get(leveldb::ReadOptions(), key, &value);
        if (status.ok()) {
            return std::atoi(value.c_str());
        } else {
            return def;
        }
    }

    int64_t SharedPreference::GetInt64(const std::string& key, int64_t def) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return def;
        }
        std::string value;
        auto status = db_->Get(leveldb::ReadOptions(), key, &value);
        if (status.ok()) {
            return std::atoll(value.c_str());
        } else {
            return def;
        }
    }

    bool SharedPreference::Remove(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!db_) {
            return false;
        }
        auto st = db_->Delete(leveldb::WriteOptions(), key);
        return st.ok();
    }
    
    void SharedPreference::Visit(IVisitListener&& listener) const {
        if (!listener) {
            return;
        }
        std::vector<std::pair<std::string, std::string>> entries;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (!db_) {
                return;
            }
            auto iterator = std::unique_ptr<leveldb::Iterator>(
                db_->NewIterator(leveldb::ReadOptions()));
            for (iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
                entries.emplace_back(
                    iterator->key().ToString(), iterator->value().ToString());
            }
        }

        for (const auto& [key, value] : entries) {
            listener(key, value);
        }
    }

}
