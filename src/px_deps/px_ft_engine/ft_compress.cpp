#include "ft_compress.h"

#include <array>
#include <string_view>

#include "miniz/miniz.h"

namespace px::ft {

namespace {

// fs.rs:445 get_ext
std::string_view GetExt(std::string_view name) {
    size_t i = name.rfind('.');
    if (i != std::string::npos) return name.substr(i + 1);
    return "";
}

} // namespace

bool IsCompressedFile(std::string_view name) {
    static constexpr std::array<std::string_view, 9> kCompressedExts{
        "xz", "gz", "zip", "7z", "rar", "bz2", "tgz", "png", "jpg"};
    const auto ext = GetExt(name);
    for (const auto e : kCompressedExts) {
        if (ext == e) return true;
    }
    return false;
}

std::vector<uint8_t> Compress(std::span<const uint8_t> data) {
    if (data.empty()) return {};
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(data.size()));
    std::vector<uint8_t> out(bound);
    mz_ulong out_len = bound;
    // MZ_DEFAULT_COMPRESSION(6):对齐 zstd 默认级别 3 的压缩率取向
    if (mz_compress2(out.data(), &out_len, data.data(),
                     static_cast<mz_ulong>(data.size()), MZ_DEFAULT_COMPRESSION) != MZ_OK) {
        return {};
    }
    out.resize(out_len);
    return out;
}

std::vector<uint8_t> Decompress(std::span<const uint8_t> data) {
    return DecompressWithLimit(data, kMaxDecompressedSize);
}

std::vector<uint8_t> DecompressWithLimit(std::span<const uint8_t> data, size_t limit) {
    if (data.empty()) return {};
    // 压缩块最大 120KB(见 transfer_job.h kBlockPayloadSize),解压输出以 4 倍起步、翻倍增长,
    // 超 limit 视为压缩炸弹(compress.rs:41 decompress_with_limit)。
    mz_stream strm{};
    if (mz_inflateInit(&strm) != MZ_OK) return {};
    strm.next_in = data.data();
    strm.avail_in = static_cast<unsigned int>(data.size());

    size_t cap = data.size() * 4;
    if (cap < 64 * 1024) cap = 64 * 1024;
    if (cap > limit + 1) cap = limit + 1; // 多开 1 字节探测超限(compress.rs:45 take(limit+1))
    std::vector<uint8_t> out(cap);
    size_t total = 0;
    int status = MZ_OK;
    while (status == MZ_OK) {
        if (total == out.size()) {
            if (out.size() > limit) {
                mz_inflateEnd(&strm);
                return {}; // 超上限
            }
            size_t new_cap = out.size() * 2;
            if (new_cap > limit + 1) new_cap = limit + 1;
            out.resize(new_cap);
        }
        strm.next_out = out.data() + total;
        strm.avail_out = static_cast<unsigned int>(out.size() - total);
        status = mz_inflate(&strm, MZ_NO_FLUSH);
        total = strm.total_out;
    }
    mz_inflateEnd(&strm);
    if (status != MZ_STREAM_END) return {};
    if (total > limit) return {};
    out.resize(total);
    return out;
}

} // namespace px::ft
