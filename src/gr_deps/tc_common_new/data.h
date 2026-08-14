#ifndef DATA_H
#define DATA_H

#include <memory>
#include <string>
#include <filesystem>
#include "string_util.h"

namespace tc
{

    class Data {
    public:
        static std::shared_ptr<Data> Make(const char* data, int64_t size);
        static std::shared_ptr<Data> From(const std::string& data);

        Data(const char *data, int64_t size);
        ~Data();

        [[nodiscard]] const char* CStr() const;
        [[nodiscard]] char* DataAddr() const;
        [[nodiscard]] int64_t Size() const;
        [[nodiscard]] char At(int64_t offset) const;
        [[nodiscard]] std::string AsString() const;
        void ConvertToStr(std::string& out) const;
        [[nodiscard]] std::shared_ptr<Data> Dup() const;
        bool Append(char* data, int64_t size);
        [[nodiscard]] int64_t Offset() const;
        void Reset();
        void Save(const U8Path& path);
        [[nodiscard]] std::shared_ptr<Data> Clone() const;

    private:
        char* data_{nullptr};
        int64_t size_ = 0;
        int64_t offset_ = 0;
        uint64_t id_ = 0;
};


typedef std::shared_ptr<Data> DataPtr;


}
#endif // DATA_H
