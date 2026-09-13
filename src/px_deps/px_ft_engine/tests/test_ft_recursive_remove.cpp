#include "ft_engine.h"
#include "ft_path.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace px::ft {
namespace {

class TemporaryTree final {
  public:
    TemporaryTree() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("pixels-recursive-remove-" + std::to_string(stamp));
        std::filesystem::create_directories(path_ / "nested");
        std::ofstream{path_ / "nested" / "file.txt"} << "pixels";
    }
    ~TemporaryTree() {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }
    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_{};
};

TEST(FtRecursiveRemoveTest, RemovesNonEmptyDirectoryAndReturnsDone) {
    TemporaryTree tree{};
    const auto sent = std::make_shared<std::vector<px::Message>>();
    FtEngine engine{[sent](const px::Message& message) {
        sent->push_back(message);
        return true;
    }};
    px::FileAction action{};
    auto& remove = *action.mutable_remove_dir();
    remove.set_id(72);
    remove.set_path(ToUtf8(tree.Path()));
    remove.set_recursive(true);

    engine.HandleFileAction(action);

    EXPECT_FALSE(std::filesystem::exists(tree.Path()));
    ASSERT_EQ(sent->size(), 1U);
    ASSERT_TRUE(sent->front().has_file_response());
    ASSERT_TRUE(sent->front().file_response().has_done());
    EXPECT_EQ(sent->front().file_response().done().id(), 72);
}

} // namespace
} // namespace px::ft
