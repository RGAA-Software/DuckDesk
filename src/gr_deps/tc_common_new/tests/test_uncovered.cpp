#include <gtest/gtest.h>

#include "tc_common_new/base64.h"
#include "tc_common_new/data.h"
#include "tc_common_new/num_formatter.h"
#include "tc_common_new/url_helper.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/message_looper.h"
#include "tc_common_new/shared_preference.h"
#include "tc_common_new/memory_stat.h"
#include "tc_common_new/zip_util.h"
#include "tc_common_new/tc_aes.h"
#include "tc_common_new/gd_md5.h"
#include "tc_common_new/image.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/key_helper.h"
#include "tc_common_new/math_helper.h"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include "tc_common_new/file.h"

namespace tc {

// ========== Base64 ==========
TEST(TestUncovered, Base64EncodeDecode) {
    std::string original = "Hello, World! 你好世界";
    auto encoded = Base64::Base64Encode(original);
    EXPECT_FALSE(encoded.empty());
    auto decoded = Base64::Base64Decode(encoded);
    EXPECT_EQ(decoded, original);
}

TEST(TestUncovered, Base64DecodeEmpty) {
    auto decoded = Base64::Base64Decode("");
    EXPECT_TRUE(decoded.empty());
}

// ========== Data ==========
TEST(TestUncovered, DataMakeAndAccess) {
    std::string payload = "hello data";
    auto data = Data::Make(payload.data(), payload.size());
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->Size(), payload.size());
    EXPECT_EQ(data->AsString(), payload);
    EXPECT_EQ(data->At(0), 'h');
}

TEST(TestUncovered, DataFromString) {
    std::string payload = "from string";
    auto data = Data::From(payload);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->AsString(), payload);
}

TEST(TestUncovered, DataDupClone) {
    std::string payload = "dup test";
    auto data = Data::Make(payload.data(), payload.size());
    auto dup = data->Dup();
    EXPECT_EQ(dup->AsString(), payload);

    auto cloned = data->Clone();
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->AsString(), payload);
}

TEST(TestUncovered, DataCloneNull) {
    auto empty = Data::Make(nullptr, 0);
    auto cloned = empty->Clone();
    EXPECT_EQ(cloned, nullptr);
}

TEST(TestUncovered, DataAppend) {
    char buf[16] = {};
    auto data = Data::Make(buf, 16);
    std::string part1 = "hello";
    EXPECT_TRUE(data->Append(const_cast<char*>(part1.data()), part1.size()));
    EXPECT_EQ(data->Offset(), 5);

    data->Reset();
    EXPECT_EQ(data->Offset(), 0);
}

TEST(TestUncovered, DataAppendOverflow) {
    char buf[4] = {};
    auto data = Data::Make(buf, 4);
    std::string big = "too big";
    EXPECT_FALSE(data->Append(const_cast<char*>(big.data()), big.size()));
}

TEST(TestUncovered, DataAtOutOfBounds) {
    std::string payload = "abc";
    auto data = Data::Make(payload.data(), payload.size());
    EXPECT_EQ(data->At(-1), 0);
    EXPECT_EQ(data->At(3), 0);
    EXPECT_EQ(data->At(100), 0);
}

TEST(TestUncovered, DataSaveAndLoad) {
    std::string payload = "save me";
    auto data = Data::Make(payload.data(), payload.size());
    auto tmp_path = std::filesystem::temp_directory_path() / "tc_data_save_test.bin";
    data->Save(tmp_path);

    auto file = File::OpenForReadB(tmp_path);
    ASSERT_NE(file, nullptr);
    auto read_data = file->ReadAllAsString();
    EXPECT_EQ(read_data, payload);
    file->Close();
    std::filesystem::remove(tmp_path);
}

// ========== NumFormatter ==========
TEST(TestUncovered, NumFormatterStorageSize) {
    EXPECT_EQ(NumFormatter::FormatStorageSize(0), "0B");
    EXPECT_EQ(NumFormatter::FormatStorageSize(512), "512B");
    EXPECT_EQ(NumFormatter::FormatStorageSize(1024), "1.00KB");
    EXPECT_EQ(NumFormatter::FormatStorageSize(1024 * 1024), "1.00MB");
    EXPECT_EQ(NumFormatter::FormatStorageSize(1024LL * 1024 * 1024), "1.00GB");
}

TEST(TestUncovered, NumFormatterSpeed) {
    EXPECT_EQ(NumFormatter::FormatSpeed(1000), "1.0KB/S");
    EXPECT_EQ(NumFormatter::FormatSpeed(1000 * 1000), "1.0MB/S");
}

TEST(TestUncovered, NumFormatterTime) {
    EXPECT_EQ(NumFormatter::FormatTime(3661000), "01:01:01");
}

TEST(TestUncovered, NumFormatterRound) {
    EXPECT_FLOAT_EQ(NumFormatter::Round2DecimalPlaces(3.14159f), 3.14f);
    EXPECT_FLOAT_EQ(NumFormatter::Round2DecimalPlaces(2.999f), 3.0f);
}

// ========== UrlHelper ==========
TEST(TestUncovered, UrlHelperParseQueryString) {
    auto params = UrlHelper::ParseQueryString("a=1&b=hello%20world&c=%2B");
    EXPECT_EQ(params["a"], "1");
    EXPECT_EQ(params["b"], "hello world");
    EXPECT_EQ(params["c"], "+");
}

TEST(TestUncovered, UrlHelperParseQueryStringEmpty) {
    auto params = UrlHelper::ParseQueryString("");
    EXPECT_TRUE(params.empty());
}

TEST(TestUncovered, UrlHelperParseQueryStringNoValue) {
    auto params = UrlHelper::ParseQueryString("key=");
    EXPECT_EQ(params["key"], "");
}

// ========== MessageNotifier ==========
struct TestMessage {
    int value = 0;
};

TEST(TestUncovered, MessageNotifierSendAndListen) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener();
    int received = 0;
    listener->Listen<TestMessage>([&](const TestMessage& msg) {
        received = msg.value;
    });
    notifier.SendAppMessage(TestMessage{42});
    EXPECT_EQ(received, 42);
}

TEST(TestUncovered, MessageNotifierUnListenAll) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener();
    int received = 0;
    listener->Listen<TestMessage>([&](const TestMessage&) {
        received = 1;
    });
    listener->UnListenAll();
    notifier.SendAppMessage(TestMessage{99});
    EXPECT_EQ(received, 0);
}

// ========== MessageLooper ==========
TEST(TestUncovered, MessageLooperPostAndExecute) {
    MessageLooper looper(10);
    std::atomic<int> count = 0;

    std::thread t([&]() {
        looper.Loop();
    });

    looper.Post([&]() { count.fetch_add(1); });
    looper.Post([&]() { count.fetch_add(10); });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    looper.Exit();
    t.join();

    EXPECT_EQ(count.load(), 11);
}

TEST(TestUncovered, MessageLooperMaxTasks) {
    MessageLooper looper(2);
    std::atomic<int> count = 0;

    std::thread t([&]() {
        looper.Loop();
    });

    looper.Post([&]() { count.store(1); });
    looper.Post([&]() { count.store(2); });
    looper.Post([&]() { count.store(3); }); // should drop oldest

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    looper.Exit();
    t.join();

    // oldest task dropped, only last 2 run
    EXPECT_GE(count.load(), 2);
}

// ========== SharedPreference ==========
TEST(TestUncovered, SharedPreferencePutGetRemove) {
    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path() / "tc_test_sp";
    fs::remove_all(tmp_dir);
    fs::create_directories(tmp_dir);

    auto* sp = SharedPreference::Instance();
    ASSERT_TRUE(sp->Init(tmp_dir.wstring(), "test_db"));
    EXPECT_TRUE(sp->IsReady());

    EXPECT_TRUE(sp->Put("key1", "value1"));
    EXPECT_EQ(sp->Get("key1"), "value1");
    EXPECT_EQ(sp->Get("missing", "def"), "def");

    EXPECT_TRUE(sp->PutInt("int_key", 123));
    EXPECT_EQ(sp->GetInt("int_key"), 123);
    EXPECT_EQ(sp->GetInt("missing_int", 42), 42);

    EXPECT_TRUE(sp->Remove("key1"));
    EXPECT_EQ(sp->Get("key1"), "");

    sp->Release();
    fs::remove_all(tmp_dir);
}

TEST(TestUncovered, SharedPreferenceVisit) {
    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path() / "tc_test_sp_visit";
    fs::remove_all(tmp_dir);
    fs::create_directories(tmp_dir);

    auto* sp = SharedPreference::Instance();
    ASSERT_TRUE(sp->Init(tmp_dir.wstring(), "test_db"));
    sp->Put("a", "1");
    sp->Put("b", "2");

    int count = 0;
    sp->Visit([&](const std::string& key, const std::string& val) {
        count++;
        EXPECT_FALSE(key.empty());
        EXPECT_FALSE(val.empty());
    });
    EXPECT_EQ(count, 2);

    sp->Release();
    fs::remove_all(tmp_dir);
}

// ========== MemoryStat ==========
TEST(TestUncovered, MemoryStatAddRemove) {
    auto* stat = MemoryStat::Instance();
    auto info = std::make_shared<MemoryInfo>();
    info->size_ = 1024;
    stat->AddMemInfo(1001, info);

    auto s = stat->GetStatInfo();
    EXPECT_EQ(s.total_count_, 1);
    EXPECT_EQ(s.total_memory_size_, 1024);
    EXPECT_EQ(s.alloc_size_, 1024);

    stat->RemoveMemInfo(1001);
    s = stat->GetStatInfo();
    EXPECT_EQ(s.total_count_, 0);
    EXPECT_EQ(s.total_memory_size_, 0);
    EXPECT_EQ(s.alloc_size_, 0);
}

// ========== AES ==========
TEST(TestUncovered, AesEncryptDecrypt) {
    std::string plaintext = "Hello AES World!";
    unsigned char key[16] = {0};
    unsigned char iv[16] = {0};
    std::memcpy(key, "my_key_16_bytes!", 16);
    std::memcpy(iv, "my_iv_16_bytes!!", 16);

    std::vector<unsigned char> ciphertext;
    EXPECT_TRUE(AesEncryptPcks7Cbc128(
        reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
        key, iv, ciphertext));
    EXPECT_FALSE(ciphertext.empty());

    std::vector<unsigned char> decrypted;
    EXPECT_TRUE(AesDecryptPcks7Cbc128(ciphertext.data(), ciphertext.size(), key, iv, decrypted));
    std::string result(decrypted.begin(), decrypted.end());
    EXPECT_EQ(result, plaintext);
}

TEST(TestUncovered, AesDecryptInvalidInput) {
    unsigned char key[16] = {0};
    unsigned char iv[16] = {0};
    std::vector<unsigned char> plaintext;
    EXPECT_FALSE(AesDecryptPcks7Cbc128(nullptr, 16, key, iv, plaintext));
    EXPECT_FALSE(AesDecryptPcks7Cbc128(key, 0, iv, key, plaintext));
}

// ========== MD5 ==========
TEST(TestUncovered, MD5String) {
    gd::MD5 md5("hello");
    EXPECT_EQ(md5.toString(), "5d41402abc4b2a76b9719d911017c592");
}

TEST(TestUncovered, MD5Empty) {
    gd::MD5 md5("");
    EXPECT_EQ(md5.toString(), "d41d8cd98f00b204e9800998ecf8427e");
}

// ========== ZipUtil ==========
TEST(TestUncovered, ZipUtilZipFolder) {
    namespace fs = std::filesystem;
    auto src_dir = fs::temp_directory_path() / "tc_zip_src";
    auto zip_path = fs::temp_directory_path() / "tc_test.zip";
    fs::remove_all(src_dir);
    fs::remove(zip_path);
    fs::create_directories(src_dir);

    {
        std::ofstream ofs(src_dir / "file1.txt");
        ofs << "hello zip";
    }
    {
        std::ofstream ofs(src_dir / "file2.txt");
        ofs << "second file";
    }

    bool ok = ZipUtil::ZipFolder(src_dir.wstring(), zip_path.wstring());
    EXPECT_TRUE(ok);
    EXPECT_TRUE(fs::exists(zip_path));
    EXPECT_GT(fs::file_size(zip_path), 0);

    fs::remove_all(src_dir);
    fs::remove(zip_path);
}

// ========== Image ==========
TEST(TestUncovered, ImageMakeAndProperties) {
    std::vector<char> pixels(4 * 4 * 3, static_cast<char>(0xAB));
    auto img = Image::Make(pixels.data(), 4, 4, 3);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->GetWidth(), 4);
    EXPECT_EQ(img->GetHeight(), 4);
    EXPECT_EQ(img->GetChannels(), 3);
    EXPECT_EQ(img->GetInternalFormat(), 0x1907);
}

TEST(TestUncovered, ImageMakeZeroSize) {
    auto data = Data::Make("abcd", 4);
    auto img = Image::Make(data, 0, 4);
    EXPECT_EQ(img->GetChannels(), 0);
    auto img2 = Image::Make(data, 4, 0);
    EXPECT_EQ(img2->GetChannels(), 0);
}

TEST(TestUncovered, ImageDuplicate) {
    std::vector<char> pixels(4 * 4 * 4, static_cast<char>(0xCD));
    auto img = Image::Make(pixels.data(), 4, 4, 4);
    auto dup = img->Duplicate(img);
    ASSERT_NE(dup, nullptr);
    EXPECT_EQ(dup->GetChannels(), 4);
    EXPECT_EQ(dup->GetInternalFormat(), 0x1908);
}

TEST(TestUncovered, ImageDuplicateNull) {
    auto img = Image::Make(nullptr, 1, 1, 1);
    auto dup = img->Duplicate(nullptr);
    EXPECT_EQ(dup, nullptr);
}

TEST(TestUncovered, ImagePath) {
    auto img = Image::Make(nullptr, 1, 1, 1);
    img->SetPath("test.png");
    EXPECT_EQ(img->GetPath(), "test.png");
}

// ========== KeyHelper ==========
TEST(TestUncovered, KeyHelperSmoke) {
    // Just ensure it doesn't crash / throw on basic usage
    auto state = KeyHelper::GetKeyStateInner(0x41); // 'A'
    (void)state;
}

// ========== MathHelper ==========
TEST(TestUncovered, MathHelperSmoke) {
    // Currently empty class, just ensure header compiles
    EXPECT_TRUE(true);
}

} // namespace tc
