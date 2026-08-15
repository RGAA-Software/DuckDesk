//
// Created by RGAA on 18/09/2025.
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
#include "../px_aes.h"
#include "../http_client.h"

using namespace tc;

int main(int argc, char** argv) {
    ::testing::InitGoogleTest();
    return RUN_ALL_TESTS();
}

TEST(Test_Get, Get_Http_Ping) {
    auto client = HttpClient::Make("127.0.0.1", 30600, "/ping");
    auto resp = client->Request({
        {"appkey", "ff785bd3031bc6cf920a782e50f43dcb"}
    });
    LOGI("URL: {}", client->GetReqPath());
    LOGI("resp code: {}, body: {}", resp.status, resp.body);
    if (resp.status == 0) {
        GTEST_SKIP() << "local HTTP test server is not reachable";
    }
    ASSERT_NE(0, resp.status);
    ASSERT_NE(true, resp.body.empty());
}

TEST(Test_Get, Get_Https) {
    auto client = HttpClient::MakeSSL("127.0.0.1", 30500, "/api/v1/device/control/query/device/by/id");
    auto resp = client->Request({
        {"device_id", "109180986"},
        {"appkey", "ff785bd3031bc6cf920a782e50f43dcb"}
    });
    LOGI("URL: {}", client->GetReqPath());
    LOGI("resp code: {}, body: {}", resp.status, resp.body);
    if (resp.status == 0) {
        GTEST_SKIP() << "local HTTPS test server is not reachable";
    }
    ASSERT_NE(0, resp.status);
    ASSERT_NE(true, resp.body.empty());
}

TEST(Test_Get, Post_Https) {
    auto client = HttpClient::MakeSSL("127.0.0.1", 30500, "/api/v1/device/control/create/new/device");
    auto resp = client->Post({
        {"hw_info", "111222"},
        {"platform", "Windows"},
        {"appkey", "ff785bd3031bc6cf920a782e50f43dcb"}
    });
    LOGI("URL: {}", client->GetReqPath());
    LOGI("resp code: {}, body: {}", resp.status, resp.body);
    if (resp.status == 0) {
        GTEST_SKIP() << "local HTTPS test server is not reachable";
    }
    ASSERT_NE(0, resp.status);
    ASSERT_NE(true, resp.body.empty());
}

TEST(Test_Download, Download) {
    HttpClient::Download("https://github.com/RGAA-Software/GammaRay/releases/download/v2.0.1/GammaRay2.0.1.7z", [](const std::string& d) {

    });
}