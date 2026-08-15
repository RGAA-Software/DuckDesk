//
// Test for folder_util
//

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "../folder_util.h"
#include "../file_util.h"
#include "../string_util.h"

using namespace tc;



class FolderUtilTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "tc_folder_util_test";
        std::filesystem::remove_all(temp_dir_);
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }
};

TEST_F(FolderUtilTest, CreateDir) {
    auto dir = temp_dir_ / "new_folder";
    ASSERT_FALSE(std::filesystem::exists(dir));
    FolderUtil::CreateDir(dir);
    EXPECT_TRUE(std::filesystem::exists(dir));
    EXPECT_TRUE(std::filesystem::is_directory(dir));

    // Creating again should be fine
    FolderUtil::CreateDir(dir);
    EXPECT_TRUE(std::filesystem::exists(dir));
}

TEST_F(FolderUtilTest, DeleteDir) {
    auto dir = temp_dir_ / "delete_me";
    std::filesystem::create_directories(dir);
    auto file = dir / "file.txt";
    {
        std::ofstream ofs(file);
        ofs << "data";
    }
    ASSERT_TRUE(std::filesystem::exists(dir));

    EXPECT_TRUE(FolderUtil::DeleteDir(dir));
    EXPECT_FALSE(std::filesystem::exists(dir));

    // Deleting non-existing should return true (matches original behaviour)
    EXPECT_TRUE(FolderUtil::DeleteDir(dir));
}

TEST_F(FolderUtilTest, VisitFiles) {
    auto dir = temp_dir_ / "visit";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "a.txt") << "a";
    std::ofstream(dir / "b.cpp") << "b";
    std::ofstream(dir / "c.txt") << "c";

    int count = 0;
    int txt_count = 0;
    FolderUtil::VisitFiles(dir, [&](VisitResult&& result) {
        count++;
        auto u8name = StringUtil::ToUTF8(result.name_);
        if (FileUtil::GetFileSuffix(u8name) == "txt") {
            txt_count++;
        }
    });
    EXPECT_EQ(count, 3);
    EXPECT_EQ(txt_count, 2);

    // With filter
    count = 0;
    FolderUtil::VisitFiles(dir, [&](VisitResult&& result) {
        count++;
    }, "cpp");
    EXPECT_EQ(count, 1);
}

TEST_F(FolderUtilTest, CopyDir) {
    auto src = temp_dir_ / "src";
    auto dst = temp_dir_ / "dst";
    std::filesystem::create_directories(src);
    std::ofstream(src / "file1.txt") << "hello";
    std::ofstream(src / "file2.log") << "world";
    auto sub = src / "sub";
    std::filesystem::create_directories(sub);
    std::ofstream(sub / "file3.txt") << "nested";

    EXPECT_TRUE(FolderUtil::CopyDir(src, dst));
    EXPECT_TRUE(std::filesystem::exists(dst / "file1.txt"));
    EXPECT_TRUE(std::filesystem::exists(dst / "file2.log"));
    EXPECT_TRUE(std::filesystem::exists(dst / "sub" / "file3.txt"));

    // Test ignore suffix
    std::filesystem::remove_all(dst);
    EXPECT_TRUE(FolderUtil::CopyDir(src, dst, {".log"}));
    EXPECT_TRUE(std::filesystem::exists(dst / "file1.txt"));
    EXPECT_FALSE(std::filesystem::exists(dst / "file2.log"));
    EXPECT_TRUE(std::filesystem::exists(dst / "sub" / "file3.txt"));
}

TEST_F(FolderUtilTest, VisitRecursiveFiles) {
    auto root = temp_dir_ / "recursive";
    std::filesystem::create_directories(root / "a" / "b");
    std::ofstream(root / "1.txt") << "1";
    std::ofstream(root / "a" / "2.txt") << "2";
    std::ofstream(root / "a" / "b" / "3.cpp") << "3";

    int count = 0;
    FolderUtil::VisitRecursiveFiles(root, 0, 10, [&](VisitResult&& result) {
        count++;
    });
    EXPECT_EQ(count, 3);

    count = 0;
    FolderUtil::VisitRecursiveFiles(root, 0, 10, [&](VisitResult&& result) {
        count++;
    }, "txt");
    EXPECT_EQ(count, 2);
}

TEST_F(FolderUtilTest, CreateDirChinese) {
    auto dir = temp_dir_ / U8Path(U8S(u8"中文目录"));
    ASSERT_FALSE(std::filesystem::exists(dir));
    FolderUtil::CreateDir(dir);
    EXPECT_TRUE(std::filesystem::exists(dir));
    EXPECT_TRUE(std::filesystem::is_directory(dir));

    // Nested chinese dir
    auto nested = dir / U8Path(U8S(u8"嵌套目录"));
    FolderUtil::CreateDir(nested);
    EXPECT_TRUE(std::filesystem::exists(nested));
}

TEST_F(FolderUtilTest, DeleteDirChinese) {
    auto dir = temp_dir_ / U8Path(U8S(u8"删除目录"));
    std::filesystem::create_directories(dir / U8Path(U8S(u8"子目录")));
    auto file = dir / U8Path(U8S(u8"文件.txt"));
    {
        std::ofstream ofs(file);
        ofs << "data";
    }
    ASSERT_TRUE(std::filesystem::exists(dir));

    EXPECT_TRUE(FolderUtil::DeleteDir(dir));
    EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST_F(FolderUtilTest, VisitFilesChinese) {
    auto dir = temp_dir_ / U8Path(U8S(u8"遍历目录"));
    std::filesystem::create_directories(dir);
    std::ofstream(dir / U8Path(U8S(u8"文件甲.txt"))) << "a";
    std::ofstream(dir / U8Path(U8S(u8"文件乙.cpp"))) << "b";

    int count = 0;
    FolderUtil::VisitFiles(dir, [&](VisitResult&& result) {
        count++;
    });
    EXPECT_EQ(count, 2);

    count = 0;
    FolderUtil::VisitFiles(dir, [&](VisitResult&& result) {
        auto u8name = StringUtil::ToUTF8(result.name_);
        if (FileUtil::GetFileSuffix(u8name) == "txt") {
            count++;
        }
    });
    EXPECT_EQ(count, 1);
}

TEST_F(FolderUtilTest, CopyDirChinese) {
    auto src = temp_dir_ / U8Path(U8S(u8"源目录"));
    auto dst = temp_dir_ / U8Path(U8S(u8"目标目录"));
    std::filesystem::create_directories(src / U8Path(U8S(u8"子目录")));
    std::ofstream(src / U8Path(U8S(u8"文件甲.txt"))) << "hello";
    std::ofstream(src / U8Path(U8S(u8"文件乙.log"))) << "world";
    std::ofstream(src / U8Path(U8S(u8"子目录")) / U8Path(U8S(u8"嵌套.txt"))) << "nested";

    EXPECT_TRUE(FolderUtil::CopyDir(src, dst));
    EXPECT_TRUE(std::filesystem::exists(dst / U8Path(U8S(u8"文件甲.txt"))));
    EXPECT_TRUE(std::filesystem::exists(dst / U8Path(U8S(u8"文件乙.log"))));
    EXPECT_TRUE(std::filesystem::exists(dst / U8Path(U8S(u8"子目录")) / U8Path(U8S(u8"嵌套.txt"))));

    // Verify content
    std::ifstream ifs(dst / U8Path(U8S(u8"文件甲.txt")));
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "hello");
}
