#pragma once

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace tc
{
    void CaptureDump();

    class BreakpadContext {
    public:
        std::string version_;
        std::string app_name_;
    };

    void CaptureDumpByBreakpad(BreakpadContext* bc);

    void ClearOldDumps();

    void CleanupDirectory(const fs::path& dir, std::size_t keep_count = 20);
}