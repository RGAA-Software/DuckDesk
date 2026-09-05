//
// Created by RGAA on 12/08/2026.
// Reed-Solomon FEC thin wrapper (over reedsolomon/rs.c, BSD, same as moonlight).
// Used by px_udp_protocol.h for GameStream-style UDP video FEC (P2).
//

#ifndef PX_FEC_H
#define PX_FEC_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "reedsolomon/rs.h"

namespace px {

namespace detail {

struct ReedSolomonCloser final {
    void operator()(reed_solomon* codec) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): reed-solomon C ABI.
        reed_solomon_release(codec);
    }
};

using UniqueReedSolomon = std::unique_ptr<reed_solomon, ReedSolomonCloser>;

}  // namespace detail

    // 所有保护块必须等长(P 字节);Encode 生成 parity 块,Decode 在缺失数 <= parity 数时
    // 原地填回缺失块。D + parity <= DATA_SHARDS_MAX(255) 由调用方保证。
    class PxFec {
    public:
        // blocks: D 个等长数据块(每块 P 字节),返回 parity_count 个 P 字节校验块;失败返回空
        static std::vector<std::string> Encode(const std::vector<std::string>& blocks, int parity_count) {
            std::vector<std::string> parity;
            const int data_shards = static_cast<int>(blocks.size());
            if (data_shards <= 0 || parity_count <= 0 || data_shards + parity_count > DATA_SHARDS_MAX) {
                return parity;
            }
            const size_t block_size = blocks[0].size();
            if (block_size == 0) {
                return parity;
            }
            for (const auto& block : blocks) {
                if (block.size() != block_size) {
                    return parity;
                }
            }
            EnsureInit();
            const detail::UniqueReedSolomon codec{reed_solomon_new(data_shards, parity_count)};
            if (!codec) {
                return parity;
            }

            parity.assign(parity_count, std::string(block_size, '\0'));
            static_assert(sizeof(std::uintptr_t) == sizeof(unsigned char*));
            std::vector<std::uintptr_t> shard_addresses(data_shards + parity_count);
            for (int i = 0; i < data_shards; i++) {
                shard_addresses[i] = reinterpret_cast<std::uintptr_t>(const_cast<char*>(blocks[i].data()));
            }
            for (int j = 0; j < parity_count; j++) {
                shard_addresses[data_shards + j] = reinterpret_cast<std::uintptr_t>(parity[j].data());
            }
            const int result = reed_solomon_encode(
                codec.get(),
                reinterpret_cast<unsigned char**>(shard_addresses.data()),  // NOLINT(gammaray-raw-pointer-boundary): C ABI pointer table.
                data_shards + parity_count, static_cast<int>(block_size));
            if (result != 0) {
                return {};
            }
            return parity;
        }

        // blocks: 长度 D + parity,空串 = 缺失;成功(缺失已填回)返回 true
        static bool Decode(std::vector<std::string>& blocks, int data_shards) {
            const int total = static_cast<int>(blocks.size());
            const int parity_count = total - data_shards;
            if (data_shards <= 0 || parity_count < 0 || total > DATA_SHARDS_MAX) {
                return false;
            }
            size_t block_size{};
            int missing{};
            for (const auto& block : blocks) {
                if (block.empty()) {
                    missing++;
                }
                else if (block_size == 0) {
                    block_size = block.size();
                }
                else if (block.size() != block_size) {
                    return false;
                }
            }
            if (block_size == 0) {
                return false;
            }
            if (missing == 0) {
                return true;
            }
            if (missing > parity_count) {
                return false;
            }
            EnsureInit();
            const detail::UniqueReedSolomon codec{reed_solomon_new(data_shards, parity_count)};
            if (!codec) {
                return false;
            }

            std::vector<std::uintptr_t> shard_addresses(total);
            std::vector<unsigned char> marks(total, 0);
            for (int i = 0; i < total; i++) {
                if (blocks[i].empty()) {
                    blocks[i].assign(block_size, '\0'); // 重建结果写到这里
                    marks[i] = 1;
                }
                shard_addresses[i] = reinterpret_cast<std::uintptr_t>(blocks[i].data());
            }
            const int error = reed_solomon_reconstruct(
                codec.get(),
                reinterpret_cast<unsigned char**>(shard_addresses.data()),  // NOLINT(gammaray-raw-pointer-boundary): C ABI pointer table.
                marks.data(), total, static_cast<int>(block_size));
            if (error != 0) {
                return false;
            }
            for (int i = 0; i < total; i++) {
                if (marks[i] && blocks[i].size() != block_size) {
                    return false;
                }
            }
            return true;
        }

    private:
        // reed_solomon_init() 全局只需一次,static 局部变量保证线程安全的一次性初始化
        static void EnsureInit() {
            static const bool inited = []() {
                reed_solomon_init();
                return true;
            }();
            (void)inited;
        }
    };

}  // namespace px

#endif  // PX_FEC_H
