//
// Created by RGAA on 2023-12-17.
//

#include <gtest/gtest.h>

#include "../uuid.h"
#include "../data.h"
#include "../md5.h"
#include "../random.h"
#include "../task_runtime.h"
#include "../log.h"
#include "../base64.h"
#include "../file.h"
#include "../tc_aes.h"
#include "test_network_adapter.cpp"
#include "test_qr_generator.cpp"

using namespace tc;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}

TEST(Test_GenUUID, gen_uuid) {
    auto uuid = tc::GetUUID();
    std::cout << "UUID: " << uuid << std::endl;
}

TEST(Test_Data_String, data_string) {
//    auto data = Data::Make(nullptr, 100);
//    memcpy(data->DataAddr(), "Jack", 4);
//    auto str = data->AsString();
//    LOGI("DataString: {}", str);
}

TEST(Test_MD5, md5) {
    auto str = tc::MD5::Hex("jack");
    std::cout << "MD5: " << str << std::endl;
}

TEST(Test_Random, random) {
    for (int i = 0; i < 10; i++) {
        std::cout << "Random: " << Random::RandT(1, 10) << std::endl;
    }
}

TEST(Test_TR, tr_create) {
    TaskRuntime runtime(4);
    runtime.Exit();
}

TEST(Test_TR, tr_post_many_task) {
    TaskRuntime runtime(4);
    for (int i = 0; i < 16; i++) {
        auto duration = Random::RandT(100, 200);
        auto task = SimpleThreadTask::Make([=](){
            std::this_thread::sleep_for(std::chrono::milliseconds(duration));
            LOGI("Task index: {} exec success, sleep : {}", i, duration);
        });
        runtime.Post(task);
    }

    std::cout << runtime.Dump() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    runtime.Exit();
}

TEST(Test_TR, tr_remove_task) {
    TaskRuntime runtime(1);
    uint64_t the_second_task_id = 0;
    for (int i = 0; i < 6; i++) {
        auto duration = Random::RandT(100, 200);
        auto task = SimpleThreadTask::Make([=](){
            std::this_thread::sleep_for(std::chrono::milliseconds(duration));
        });
        runtime.Post(task);

        if (i == 1) {
            the_second_task_id = task->task_id_;
        }
    }

    std::cout << "--------Before Remove--------" << std::endl;
    std::cout << runtime.Dump() << std::endl;

    std::cout << "--------After Remove--------" << std::endl;
    runtime.RemoveTask(the_second_task_id);
    std::cout << runtime.Dump() << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    runtime.Exit();
}

TEST(Test_b64, enc_dec) {
    auto enc = Base64::Base64Encode("Jack Sparrow");
    LOGI("b64 enc: {}", enc);
    auto dec = Base64::Base64Decode(enc);
    LOGI("b64 dec: {}", dec);
}

TEST(Test_file, create) {
    auto file = File::OpenForWrite(U8Path("jack_test.txt"));
    file->Write(0, "hello...");
}

TEST(Test_file, read) {
    auto file = File::OpenForRead(U8Path("jack_test.txt"));
    LOGI("file exists: {}", file->Exists());
    auto data = file->ReadAllAsString();
    LOGI("data : {}", data);
}

TEST(Test_file, append) {
    File::Delete(U8Path("jack_test.txt"));
    auto file = File::OpenForAppendB(U8Path("jack_test.txt"));
    file->Append("1111111111111111111");
    file->Append("3333333333333333333");
    file->Close();
    LOGI("After write");

    file = File::OpenForRead(U8Path("jack_test.txt"));
    LOGI("Data: {}", file->ReadAll()->AsString());
}

TEST(Test_file, read_all) {
    auto file = File::OpenForReadB(U8Path("vc_redist.x64.exe"));
    auto file_cpy = File::OpenForWriteB(U8Path("vc_redist.x64_cpy.exe"));
    file->ReadAll([=](auto offset, DataPtr&& data)->bool {
        //LOGI("offset: {}, data size: {}", offset, data->Size());
        file_cpy->Write(offset, data);
        return false;
    }, 1024);

}

TEST(Test_AES_enc, AES_enc) {
    std::string input = "this is a test string@333";
    const std::string user_key = MD5::Hex("123@321");
    std::string key = user_key.substr(0, 16);
    std::string iv = user_key.substr(user_key.length() - 16);
    std::vector<uint8_t> output;
    auto r = AesEncryptPcks7Cbc128((uint8_t*)input.c_str(), input.size(), (uint8_t*)key.c_str(), (uint8_t*)iv.c_str(), output);
    auto output_b64 = Base64::Base64Encode(output.data(), output.size());
    LOGI("AES Enc, input: {}", input);
    LOGI("user key: {}", user_key);
    LOGI("key: {}", key);
    LOGI("iv: {}", iv);
    LOGI("output b64: {}", output_b64);
    ASSERT_EQ(r, true);
}

TEST(Test_AES_enc, AES_dec) {
    std::string input = "Ac9AB55203wqg4AYKb7alxeKlh6DArEDBZF0d1HV+eM=";
    const std::string user_key = MD5::Hex("123@321");
    std::string key = user_key.substr(0, 16);
    std::string iv = user_key.substr(user_key.length() - 16);
    std::vector<uint8_t> output;
    auto input_raw = Base64::Base64Decode(input);
    auto r = AesDecryptPcks7Cbc128((uint8_t*)input_raw.c_str(), input_raw.size(), (uint8_t*)key.c_str(), (uint8_t*)iv.c_str(), output);
    LOGI("AES Enc, input: {}", input);
    LOGI("user key: {}", user_key);
    LOGI("key: {}", key);
    LOGI("iv: {}", iv);
    std::string output_raw((char*)output.data(), output.size());
    LOGI("output b64: {}", output_raw);
    ASSERT_EQ(r, true);
    ASSERT_EQ(std::string("this is a test string@333") == output_raw, true);
}