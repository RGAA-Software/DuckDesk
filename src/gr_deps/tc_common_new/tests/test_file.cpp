//
// Created by RGAA on 2025.
//

#include <gtest/gtest.h>
#include "../file.h"
#include "../string_util.h"
#include <filesystem>
#include <fstream>

using namespace tc;


class FileTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / std::format("tc_file_test_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path TempPath(const std::string& name) {
        return temp_dir_ / U8Path(name);
    }
};

TEST_F(FileTest, OpenForWriteAndRead) {
    auto path = TempPath("test_write_read.txt");
    {
        auto file = File::OpenForWrite(path);
        ASSERT_TRUE(file->IsOpen());
        EXPECT_EQ(file->Write(0, "hello world"), 11);
    }

    {
        auto file = File::OpenForRead(path);
        ASSERT_TRUE(file->IsOpen());
        auto content = file->ReadAllAsString();
        EXPECT_EQ(content, "hello world");
    }
}

TEST_F(FileTest, OpenForAppend) {
    auto path = TempPath("test_append.txt");
    {
        auto file = File::OpenForAppend(path);
        ASSERT_TRUE(file->IsOpen());
        file->Append("111");
        file->Append("222");
    }

    {
        auto file = File::OpenForRead(path);
        auto content = file->ReadAllAsString();
        EXPECT_EQ(content, "111222");
    }
}

TEST_F(FileTest, ExistsStatic) {
    auto path = TempPath("test_exists.txt");
    EXPECT_FALSE(File::Exists(path));

    std::ofstream ofs(path);
    ofs << "x";
    ofs.close();

    EXPECT_TRUE(File::Exists(path));
}

TEST_F(FileTest, ExistsInstance) {
    auto path = TempPath("test_exists_instance.txt");
    auto file = File::OpenForWrite(path);
    EXPECT_TRUE(file->Exists());
    file->Close();
}

TEST_F(FileTest, DeleteFile) {
    auto path = TempPath("test_delete.txt");
    {
        std::ofstream ofs(path);
        ofs << "x";
    }
    ASSERT_TRUE(File::Exists(path));
    EXPECT_TRUE(File::Delete(path));
    EXPECT_FALSE(File::Exists(path));
}

TEST_F(FileTest, FileName) {
    auto path = TempPath("my_file.txt");
    auto file = File::OpenForWrite(path);
    EXPECT_EQ(file->FileName(), "my_file.txt");
}

TEST_F(FileTest, SizeStatic) {
    auto path = TempPath("test_size.txt");
    {
        std::ofstream ofs(path);
        ofs << "12345";
    }
    EXPECT_EQ(File::Size(path), 5);
}

TEST_F(FileTest, SizeInstance) {
    auto path = TempPath("test_size_instance.txt");
    auto file = File::OpenForWrite(path);
    file->Write(0, "abcdefghij");
    EXPECT_EQ(file->Size(), 10);
}

TEST_F(FileTest, IsFolder) {
    auto dir_path = TempPath("test_dir");
    std::filesystem::create_directories(dir_path);
    EXPECT_TRUE(File::IsFolder(dir_path));

    auto file_path = TempPath("test_file.txt");
    {
        std::ofstream ofs(file_path);
        ofs << "x";
    }
    EXPECT_FALSE(File::IsFolder(file_path));
}

TEST_F(FileTest, ReadWithOffset) {
    auto path = TempPath("test_offset.txt");
    {
        auto file = File::OpenForWrite(path);
        file->Write(0, "0123456789");
    }

    auto file = File::OpenForRead(path);
    uint64_t read_size = 0;
    auto data = file->Read(2, 4, read_size);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(read_size, 4);
    EXPECT_EQ(data->AsString(), "2345");
}

TEST_F(FileTest, ReadAllCallback) {
    auto path = TempPath("test_callback.txt");
    {
        auto file = File::OpenForWrite(path);
        file->Write(0, "abcdef");
    }

    auto file = File::OpenForRead(path);
    std::vector<std::string> chunks;
    file->ReadAll([&chunks](uint64_t offset, DataPtr&& data) -> bool {
        chunks.push_back(data->AsString());
        return false;
    }, 2);

    ASSERT_EQ(chunks.size(), 3);
    EXPECT_EQ(chunks[0], "ab");
    EXPECT_EQ(chunks[1], "cd");
    EXPECT_EQ(chunks[2], "ef");
}

TEST_F(FileTest, BinaryMode) {
    auto path = TempPath("test_binary.bin");
    {
        auto file = File::OpenForWriteB(path);
        char data[] = {0x00, 0x01, 0x02, 0x03};
        file->Write(0, data, 4);
    }

    {
        auto file = File::OpenForReadB(path);
        uint64_t read_size = 0;
        auto data = file->Read(0, 4, read_size);
        ASSERT_NE(data, nullptr);
        EXPECT_EQ(read_size, 4);
        auto ptr = reinterpret_cast<const uint8_t*>(data->CStr());
        EXPECT_EQ(ptr[0], 0x00);
        EXPECT_EQ(ptr[1], 0x01);
        EXPECT_EQ(ptr[2], 0x02);
        EXPECT_EQ(ptr[3], 0x03);
    }
}

TEST_F(FileTest, ChinesePath) {
    auto path = TempPath("中文路径测试.txt");
    {
        auto file = File::OpenForWrite(path);
        ASSERT_TRUE(file->IsOpen());
        file->Write(0, "hello");
    }
    EXPECT_TRUE(File::Exists(path));
}


TEST_F(FileTest, ChinesePathReadWrite) {
    auto path = TempPath(U8S(u8"中文读写测试.txt"));
    {
        auto file = File::OpenForWrite(path);
        ASSERT_TRUE(file->IsOpen());
        EXPECT_EQ(file->Write(0, "中文内容测试"), 18);  // UTF-8 bytes
    }

    {
        auto file = File::OpenForRead(path);
        ASSERT_TRUE(file->IsOpen());
        auto content = file->ReadAllAsString();
        EXPECT_EQ(content, "中文内容测试");
    }
}

TEST_F(FileTest, ChinesePathBinary) {
    auto path = TempPath(U8S(u8"中文二进制.bin"));
    {
        auto file = File::OpenForWriteB(path);
        char data[] = {0x00, 0x01, 0x02, 0x03, 0xFF};
        file->Write(0, data, 5);
    }

    {
        auto file = File::OpenForReadB(path);
        uint64_t read_size = 0;
        auto data = file->Read(0, 5, read_size);
        ASSERT_NE(data, nullptr);
        EXPECT_EQ(read_size, 5);
        auto ptr = reinterpret_cast<const uint8_t*>(data->CStr());
        EXPECT_EQ(ptr[4], 0xFF);
    }
}

TEST_F(FileTest, ChinesePathFileName) {
    auto path = TempPath(U8S(u8"中文文件名.txt"));
    auto file = File::OpenForWrite(path);
    EXPECT_EQ(file->FileName(), U8S(u8"中文文件名.txt"));
}

TEST_F(FileTest, ChinesePathDelete) {
    auto path = TempPath(U8S(u8"待删除文件.txt"));
    {
        auto file = File::OpenForWrite(path);
        file->Write(0, "x");
    }
    ASSERT_TRUE(File::Exists(path));
    EXPECT_TRUE(File::Delete(path));
    EXPECT_FALSE(File::Exists(path));
}
