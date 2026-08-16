// px_ft_engine - 块压缩模块
// 对照 rustdesk/libs/hbb_common/src/compress.rs 与 fs.rs:454 is_compressed_file。
//
// 有意偏离:上游用 zstd,本实现复用仓库已有的 miniz(vcpkg miniz::miniz,zlib 格式
// deflate),不引新依赖。两端(render / Qt)都是本引擎所以自洽;Web 端用 fflate/pako
// 对齐 zlib 格式即可。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace px::ft {

// 解压输出上限(compress.rs:7 MAX_DECOMPRESSED_SIZE)
inline constexpr size_t kMaxDecompressedSize = 256 * 1024 * 1024;

// fs.rs:454 is_compressed_file:已压缩格式后缀跳过压缩(xz/gz/zip/7z/rar/bz2/tgz/png/jpg)
bool IsCompressedFile(const std::string& name);

// compress.rs:17 compress:失败返回空 vector(调用方按未压缩处理)
std::vector<uint8_t> Compress(const void* data, size_t len);
inline std::vector<uint8_t> Compress(const std::vector<uint8_t>& data) {
    return Compress(data.data(), data.size());
}

// compress.rs:37 decompress:带 256MB 上限,失败/超限返回空 vector
std::vector<uint8_t> Decompress(const void* data, size_t len);
inline std::vector<uint8_t> Decompress(const std::vector<uint8_t>& data) {
    return Decompress(data.data(), data.size());
}

// compress.rs:41 decompress_with_limit:自定义上限(测试可用小上限验证拒绝逻辑)
std::vector<uint8_t> DecompressWithLimit(const void* data, size_t len, size_t limit);

} // namespace px::ft
