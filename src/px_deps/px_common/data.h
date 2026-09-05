#ifndef DATA_H
#define DATA_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace px {

class Data {
public:
    static std::shared_ptr<Data> Allocate(std::size_t size);
    static std::shared_ptr<Data> Copy(std::span<const char> bytes);
    static std::shared_ptr<Data> From(std::string_view text);

    explicit Data(std::size_t size);
    explicit Data(std::span<const char> bytes);
    ~Data();

    Data(const Data&) = default;
    Data& operator=(const Data&) = default;
    Data(Data&&) noexcept = default;
    Data& operator=(Data&&) noexcept = default;

    [[nodiscard]] std::span<const char> Bytes() const noexcept;
    [[nodiscard]] std::span<char> MutableBytes() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] char At(std::size_t offset) const noexcept;
    [[nodiscard]] std::string AsString() const;
    void ConvertToStr(std::string& out) const;
    [[nodiscard]] std::shared_ptr<Data> Dup() const;
    bool Append(std::span<const char> bytes);
    [[nodiscard]] std::size_t Offset() const noexcept;
    void Reset() noexcept;
    [[nodiscard]] std::shared_ptr<Data> Clone() const;

private:
    std::vector<char> data_{};
    std::size_t offset_{0};
};

using DataPtr = std::shared_ptr<Data>;

}  // namespace px

#endif  // DATA_H
