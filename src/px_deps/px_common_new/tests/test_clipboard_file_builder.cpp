//
// Unit tests for px_common_new/clipboard/clipboard_file_builder.h
//

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "px_common_new/clipboard/clipboard_file_builder.h"
#include "px_common_new/clipboard/clipboard_types.h"
#include "px_common_new/string_util.h"

using namespace tc;
using namespace tc::clipboard;

namespace {
    std::string NormalizeSlashes(std::string path) {
        for (auto& ch : path) {
            if (ch == '\\') {
                ch = '/';
            }
        }
        return path;
    }
}

class ClipboardFileBuilderTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "tc_clipboard_file_builder_test";
        std::filesystem::remove_all(temp_dir_);
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }

    std::string WriteFile(const std::filesystem::path& path, const std::string& content) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream ofs(path, std::ios::binary);
        ofs << content;
        ofs.close();
        return PathToUTF8(path);
    }
};

TEST_F(ClipboardFileBuilderTest, EmptyPathsReturnsNullopt) {
    EXPECT_FALSE(BuildFileEntriesFromPaths({}).has_value());
}

TEST_F(ClipboardFileBuilderTest, SingleFileEntry) {
    const auto full = WriteFile(temp_dir_ / "hello.txt", "hello");
    auto entries = BuildFileEntriesFromPaths({full});
    ASSERT_TRUE(entries.has_value());
    ASSERT_EQ(entries->size(), 1u);
    EXPECT_EQ(entries->at(0).file_name_, "hello.txt");
    EXPECT_EQ(entries->at(0).ref_path_, "hello.txt");
    EXPECT_EQ(entries->at(0).full_path_, full);
    EXPECT_EQ(entries->at(0).total_size_, 5);
}

TEST_F(ClipboardFileBuilderTest, MultipleFilesSameFolder) {
    const auto f1 = WriteFile(temp_dir_ / "a.txt", "aaa");
    const auto f2 = WriteFile(temp_dir_ / "sub" / "b.txt", "bb");
    auto entries = BuildFileEntriesFromPaths({f1, f2});
    ASSERT_TRUE(entries.has_value());
    ASSERT_EQ(entries->size(), 2u);

    bool found_a = false;
    bool found_b = false;
    for (const auto& e : entries.value()) {
        if (e.file_name_ == "a.txt") {
            found_a = true;
            EXPECT_EQ(e.ref_path_, "a.txt");
        }
        if (e.file_name_ == "b.txt") {
            found_b = true;
            EXPECT_EQ(NormalizeSlashes(e.ref_path_), "sub/b.txt");
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST_F(ClipboardFileBuilderTest, DirectoryExpandsRecursively) {
    const auto root = temp_dir_ / "bundle";
    WriteFile(root / "top.txt", "1");
    WriteFile(root / "nested" / "inner.txt", "22");
    const auto root_u8 = PathToUTF8(root);

    auto entries = BuildFileEntriesFromPaths({root_u8});
    ASSERT_TRUE(entries.has_value());
    EXPECT_GE(entries->size(), 2u);

    bool found_top = false;
    bool found_inner = false;
    for (const auto& e : entries.value()) {
        if (e.file_name_ == "top.txt") {
            found_top = true;
            EXPECT_EQ(NormalizeSlashes(e.ref_path_), "bundle/top.txt");
        }
        if (e.file_name_ == "inner.txt") {
            found_inner = true;
            EXPECT_EQ(NormalizeSlashes(e.ref_path_), "bundle/nested/inner.txt");
        }
    }
    EXPECT_TRUE(found_top);
    EXPECT_TRUE(found_inner);
}

TEST_F(ClipboardFileBuilderTest, MixedDirectoryAndSiblingFiles) {
    const auto bundle = temp_dir_ / "bundle";
    WriteFile(bundle / "nested" / "in.txt", "x");
    const auto sibling = WriteFile(temp_dir_ / "sibling.txt", "y");

    auto entries = BuildFileEntriesFromPaths({PathToUTF8(bundle), sibling});
    ASSERT_TRUE(entries.has_value());
    ASSERT_EQ(entries->size(), 2u);

    bool found_in = false;
    bool found_sibling = false;
    for (const auto& e : entries.value()) {
        if (e.file_name_ == "in.txt") {
            found_in = true;
            EXPECT_EQ(NormalizeSlashes(e.ref_path_), "bundle/nested/in.txt");
        }
        if (e.file_name_ == "sibling.txt") {
            found_sibling = true;
            EXPECT_EQ(e.ref_path_, "sibling.txt");
        }
    }
    EXPECT_TRUE(found_in);
    EXPECT_TRUE(found_sibling);
}

TEST_F(ClipboardFileBuilderTest, NonexistentPathReturnsNullopt) {
    const auto missing = PathToUTF8(temp_dir_ / "does-not-exist.txt");
    EXPECT_FALSE(BuildFileEntriesFromPaths({missing}).has_value());
}

TEST_F(ClipboardFileBuilderTest, SkipsInvalidKeepsValid) {
    const auto valid = WriteFile(temp_dir_ / "ok.txt", "ok");
    const auto missing = PathToUTF8(temp_dir_ / "missing.txt");
    auto entries = BuildFileEntriesFromPaths({missing, valid});
    ASSERT_TRUE(entries.has_value());
    ASSERT_EQ(entries->size(), 1u);
    EXPECT_EQ(entries->at(0).file_name_, "ok.txt");
}

TEST(ClipboardContentTest, Flags) {
    Content empty;
    EXPECT_TRUE(empty.Empty());
    EXPECT_FALSE(empty.HasText());
    EXPECT_FALSE(empty.HasFiles());

    Content text_only;
    text_only.text_ = "x";
    EXPECT_FALSE(text_only.Empty());
    EXPECT_TRUE(text_only.HasText());
    EXPECT_FALSE(text_only.HasFiles());

    Content files_only;
    files_only.files_.push_back(FileEntry{.file_name_ = "f"});
    EXPECT_FALSE(files_only.Empty());
    EXPECT_FALSE(files_only.HasText());
    EXPECT_TRUE(files_only.HasFiles());
}
