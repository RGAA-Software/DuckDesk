#pragma once

#include <string>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace px
{
    void CaptureDump();

    class BreakpadContext {
    public:
        std::string version_;
        std::string app_name_;
    };

    class BreakpadRegistration final {
    public:
        explicit BreakpadRegistration(std::shared_ptr<const BreakpadContext> context);
        ~BreakpadRegistration();

        BreakpadRegistration(const BreakpadRegistration&) = delete;
        BreakpadRegistration& operator=(const BreakpadRegistration&) = delete;

    private:
        class State;
        std::unique_ptr<State> state_;
    };

    [[nodiscard]] std::shared_ptr<BreakpadRegistration> CaptureDumpByBreakpad(std::shared_ptr<const BreakpadContext> context);

    void ClearOldDumps();

    void CleanupDirectory(const fs::path& dir, std::size_t keep_count = 20);
}
