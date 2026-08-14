//
// Test for file_util
//

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "../file_util.h"
#include "../string_util.h"

using namespace tc;


static std::string PathToUTF8(const std::filesystem::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}


class FileUtilTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "tc_file_util_test";
        std::filesystem::remove_all(temp_dir_);
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
};

TEST_F(FileUtilTest, GetFileNameFromPath) {
    EXPECT_EQ(FileUtil::GetFileNameFromPath("C:/foo/bar.txt"), "bar.txt");
    EXPECT_EQ(FileUtil::GetFileNameFromPath("C:\\foo\\bar.txt"), "bar.txt");
    EXPECT_EQ(FileUtil::GetFileNameFromPath("bar.txt"), "bar.txt");
    EXPECT_EQ(FileUtil::GetFileNameFromPath("/usr/local/bin/"), "bin");
}

TEST_F(FileUtilTest, GetFileNameFromPathNoSuffix) {
    EXPECT_EQ(FileUtil::GetFileNameFromPathNoSuffix("C:/foo/bar.txt"), "bar");
    EXPECT_EQ(FileUtil::GetFileNameFromPathNoSuffix("bar"), "bar");
}

TEST_F(FileUtilTest, GetFileSuffix) {
    EXPECT_EQ(FileUtil::GetFileSuffix("C:/foo/bar.txt"), "txt");
    EXPECT_EQ(FileUtil::GetFileSuffix("bar"), "bar");
    EXPECT_EQ(FileUtil::GetFileSuffix("archive.tar.gz"), "gz");
}

TEST_F(FileUtilTest, GetFileFolder) {
    EXPECT_EQ(FileUtil::GetFileFolder("C:/foo/bar.txt"), "C:/foo");
    EXPECT_EQ(FileUtil::GetFileFolder("C:\\foo\\bar.txt"), "C:/foo");
}

TEST_F(FileUtilTest, GetFolderNameFormAbsFolderPath) {
    EXPECT_EQ(FileUtil::GetFolderNameFormAbsFolderPath("C:/foo/bar/"), "bar");
    EXPECT_EQ(FileUtil::GetFolderNameFormAbsFolderPath("C:/foo/bar"), "bar");
}

TEST_F(FileUtilTest, CopyFileExt) {
    auto src = temp_dir_ / "source.txt";
    auto dst = temp_dir_ / "dest.txt";
    {
        std::ofstream ofs(src);
        ofs << "hello";
    }
    ASSERT_TRUE(std::filesystem::exists(src));

    EXPECT_TRUE(FileUtil::CopyFileExt(src, dst, false));
    EXPECT_TRUE(std::filesystem::exists(dst));

    // Copy again without force should return true (existing file, not force)
    EXPECT_TRUE(FileUtil::CopyFileExt(src, dst, false));

    // Copy with force should replace
    {
        std::ofstream ofs(src);
        ofs << "world";
    }
    EXPECT_TRUE(FileUtil::CopyFileExt(src, dst, true));
    std::ifstream ifs(dst);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "world");
}

TEST_F(FileUtilTest, ReName) {
    auto old_path = temp_dir_ / "old.txt";
    auto new_path = temp_dir_ / "new.txt";
    {
        std::ofstream ofs(old_path);
        ofs << "data";
    }
    ASSERT_TRUE(std::filesystem::exists(old_path));

    EXPECT_TRUE(FileUtil::ReName(old_path, new_path));
    EXPECT_FALSE(std::filesystem::exists(old_path));
    EXPECT_TRUE(std::filesystem::exists(new_path));

    // Rename non-existing should fail
    EXPECT_FALSE(FileUtil::ReName(old_path, new_path));
}

TEST_F(FileUtilTest, CopyFileExtChinese) {
    auto src = temp_dir_ / U8Path(U8S(u8"中文源文件.txt"));
    auto dst = temp_dir_ / U8Path(U8S(u8"中文目标文件.txt"));
    {
        std::ofstream ofs(src);
        ofs << "hello chinese";
    }
    ASSERT_TRUE(std::filesystem::exists(src));

    EXPECT_TRUE(FileUtil::CopyFileExt(src, dst, false));
    EXPECT_TRUE(std::filesystem::exists(dst));

    {
        std::ifstream ifs(dst);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "hello chinese");
    }

    // Copy with force
    {
        std::ofstream ofs(src);
        ofs << "replaced";
    }
    EXPECT_TRUE(FileUtil::CopyFileExt(src, dst, true));
    {
        std::ifstream ifs2(dst);
        std::string content2((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content2, "replaced");
    }
}

TEST_F(FileUtilTest, ReNameChinese) {
    auto old_path = temp_dir_ / U8Path(U8S(u8"旧文件.txt"));
    auto new_path = temp_dir_ / U8Path(U8S(u8"新文件.txt"));
    {
        std::ofstream ofs(old_path);
        ofs << "data";
    }
    ASSERT_TRUE(std::filesystem::exists(old_path));

    EXPECT_TRUE(FileUtil::ReName(old_path, new_path));
    EXPECT_FALSE(std::filesystem::exists(old_path));
    EXPECT_TRUE(std::filesystem::exists(new_path));
}

TEST_F(FileUtilTest, GetFileNameFromPathChinese) {
    EXPECT_EQ(FileUtil::GetFileNameFromPath(U8S(u8"C:/测试/文件.txt")), U8S(u8"文件.txt"));
    EXPECT_EQ(FileUtil::GetFileNameFromPathNoSuffix(U8S(u8"C:/测试/文件.txt")), U8S(u8"文件"));
    EXPECT_EQ(FileUtil::GetFileSuffix(U8S(u8"C:/测试/文件.txt")), "txt");
}
