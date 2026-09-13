#include "client_local_file_system.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace px::client::imgui {
namespace {

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("pixels-local-files-" + std::to_string(stamp));
        std::filesystem::create_directories(path_ / "folder");
        std::ofstream{path_ / "sample.txt"} << "pixels";
    }
    ~TemporaryDirectory() {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    [[nodiscard]] const std::filesystem::path& Path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_{};
};

TEST(ClientLocalFileSystemTest, NavigatesRefreshesAndReturnsToPreviousDirectory) {
    TemporaryDirectory temporary{};
    ClientLocalFileSystem files{};
    ASSERT_TRUE(files.Navigate(temporary.Path().string()));
    ASSERT_EQ(files.Entries().size(), 2U);
    EXPECT_TRUE(files.Entries().front().directory);
    EXPECT_EQ(files.Entries().front().name, "folder");
    ASSERT_TRUE(files.Navigate((temporary.Path() / "folder").string()));
    EXPECT_TRUE(files.Entries().empty());
    ASSERT_TRUE(files.NavigateBack());
    EXPECT_EQ(std::filesystem::path{files.Path()}, temporary.Path());
    EXPECT_TRUE(files.Refresh());
    EXPECT_TRUE(files.Error().empty());
}

TEST(ClientLocalFileSystemTest, RejectsFilesAndPreservesCurrentDirectory) {
    TemporaryDirectory temporary{};
    ClientLocalFileSystem files{};
    ASSERT_TRUE(files.Navigate(temporary.Path().string()));
    const std::string before{files.Path()};
    EXPECT_FALSE(files.Navigate((temporary.Path() / "sample.txt").string()));
    EXPECT_EQ(files.Path(), before);
    EXPECT_FALSE(files.Error().empty());
}

TEST(ClientLocalFileSystemTest, CreatesRenamesAndRemovesItemsWithinCurrentDirectory) {
    TemporaryDirectory temporary{};
    ClientLocalFileSystem files{};
    ASSERT_TRUE(files.Navigate(temporary.Path().string()));
    ASSERT_TRUE(files.CreateDirectory("created"));
    ASSERT_TRUE(std::filesystem::is_directory(temporary.Path() / "created"));
    ASSERT_TRUE(files.Rename((temporary.Path() / "created").string(), "renamed"));
    EXPECT_FALSE(std::filesystem::exists(temporary.Path() / "created"));
    ASSERT_TRUE(std::filesystem::is_directory(temporary.Path() / "renamed"));
    ASSERT_TRUE(files.Remove((temporary.Path() / "renamed").string()));
    EXPECT_FALSE(std::filesystem::exists(temporary.Path() / "renamed"));
}

TEST(ClientLocalFileSystemTest, RejectsTraversalNamesAndItemsOutsideCurrentDirectory) {
    TemporaryDirectory temporary{};
    ClientLocalFileSystem files{};
    ASSERT_TRUE(files.Navigate((temporary.Path() / "folder").string()));
    EXPECT_FALSE(files.CreateDirectory("../escape"));
    EXPECT_FALSE(files.Remove((temporary.Path() / "sample.txt").string()));
    EXPECT_TRUE(std::filesystem::exists(temporary.Path() / "sample.txt"));
}

} // namespace
} // namespace px::client::imgui
