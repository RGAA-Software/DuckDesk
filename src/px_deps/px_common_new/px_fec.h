//
// Created by RGAA on 12/08/2026.
// Reed-Solomon FEC thin wrapper (over reedsolomon/rs.c, BSD, same as moonlight).
// Used by px_udp_protocol.h for GameStream-style UDP video FEC (P2).
//

#ifndef GAMMARAY_GR_FEC_H
#define GAMMARAY_GR_FEC_H

#include <cstring>
#include <string>
#include <vector>

#include "reedsolomon/rs.h"

namespace tc
{

    // 所有保护块必须等长(P 字节);Encode 生成 parity 块,Decode 在缺失数 <= parity 数时
    // 原地填回缺失块。D + parity <= DATA_SHARDS_MAX(255) 由调用方保证。
    class GrFec {
    public:
        // blocks: D 个等长数据块(每块 P 字节),返回 parity_count 个 P 字节校验块;失败返回空
        static std::vector<std::string> Encode(const std::vector<std::string>& blocks, int parity_count) {
            std::vector<std::string> parity;
            const int data_shards = (int)blocks.size();
            if (data_shards <= 0 || parity_count <= 0 || data_shards + parity_count > DATA_SHARDS_MAX) return parity;
            const size_t block_size = blocks[0].size();
            if (block_size == 0) return parity;
            for (auto& b : blocks) {
                if (b.size() != block_size) return parity;
            }
            EnsureInit();
            reed_solomon* rs = reed_solomon_new(data_shards, parity_count);
            if (!rs) return parity;

            parity.assign(parity_count, std::string(block_size, '\0'));
            std::vector<unsigned char*> shards(data_shards + parity_count);
            for (int i = 0; i < data_shards; i++) {
                shards[i] = (unsigned char*)blocks[i].data();
            }
            for (int j = 0; j < parity_count; j++) {
                shards[data_shards + j] = (unsigned char*)parity[j].data();
            }
            reed_solomon_encode(rs, shards.data(), data_shards + parity_count, (int)block_size);
            reed_solomon_release(rs);
            return parity;
        }

        // blocks: 长度 D + parity,空串 = 缺失;成功(缺失已填回)返回 true
        static bool Decode(std::vector<std::string>& blocks, int data_shards) {
            const int total = (int)blocks.size();
            const int parity_count = total - data_shards;
            if (data_shards <= 0 || parity_count < 0 || total > DATA_SHARDS_MAX) return false;
            size_t block_size = 0;
            int missing = 0;
            for (auto& b : blocks) {
                if (b.empty()) {
                    missing++;
                }
                else if (block_size == 0) {
                    block_size = b.size();
                }
                else if (b.size() != block_size) {
                    return false;
                }
            }
            if (block_size == 0) return false;
            if (missing == 0) return true;
            if (missing > parity_count) return false;
            EnsureInit();
            reed_solomon* rs = reed_solomon_new(data_shards, parity_count);
            if (!rs) return false;

            std::vector<unsigned char*> shards(total);
            std::vector<unsigned char> marks(total, 0);
            for (int i = 0; i < total; i++) {
                if (blocks[i].empty()) {
                    blocks[i].assign(block_size, '\0'); // 重建结果写到这里
                    marks[i] = 1;
                }
                shards[i] = (unsigned char*)blocks[i].data();
            }
            int err = reed_solomon_reconstruct(rs, shards.data(), marks.data(), total, (int)block_size);
            reed_solomon_release(rs);
            if (err != 0) return false;
            for (int i = 0; i < total; i++) {
                if (marks[i] && blocks[i].size() != block_size) return false;
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

}

#endif //GAMMARAY_GR_FEC_H
