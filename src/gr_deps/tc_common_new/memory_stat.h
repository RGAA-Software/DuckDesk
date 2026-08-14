//
// Created by RGAA on 14/09/2025.
//

#ifndef GAMMARAYPREMIUM_MEMORY_STAT_H
#define GAMMARAYPREMIUM_MEMORY_STAT_H

#include <vector>
#include "concurrent_hashmap.h"

namespace tc
{
    class Thread;

    class MemoryInfo {
    public:
        uint64_t id_ = 0;
        uint64_t size_ = 0;
        std::string module_;
        std::string name_;
    };

    class MemoryStatInfo {
    public:
        std::string Dump();

    public:
        uint64_t alloc_size_ = 0;
        uint64_t total_count_ = 0;
        uint64_t total_memory_size_ = 0;
        uint64_t total_memory_size_KB_ = 0;
        uint64_t total_memory_size_MB_ = 0;
        std::string total_readable_memory_size_;
        uint32_t thread_count_ = 0;
        std::vector<std::weak_ptr<Thread>> threads_;
    };

    class MemoryStat {
    public:
        static MemoryStat* Instance() {
            static MemoryStat stat;
            return &stat;
        }

        void AddMemInfo(uint64_t id, const std::shared_ptr<MemoryInfo>& info);
        void RemoveMemInfo(uint64_t id);
        void AddThread(uint32_t id, const std::shared_ptr<Thread>& thread);
        void RemoveThread(uint32_t id);
        MemoryStatInfo GetStatInfo();

    private:
        tc::ConcurrentHashMap<uint64_t, std::shared_ptr<MemoryInfo>> mem_info_;
        std::atomic_uint64_t alloc_size_ = 0;
        tc::ConcurrentHashMap<uint32_t, std::weak_ptr<Thread>> thread_info_;
    };

}

#endif //GAMMARAYPREMIUM_MEMORY_STAT_H
