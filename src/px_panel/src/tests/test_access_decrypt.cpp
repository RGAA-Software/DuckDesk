// Test: decrypt a cms://access## string and verify the contained appkey.
// Self-contained: inlines the AES + base64 + JSON parsing logic so it only
// depends on OpenSSL and nlohmann_json (both available via vcpkg).
//
// Build (from repo root):
//   cmake --build build_official --target test_access_decrypt
// Run:
//   .\build_official\src\px_panel\src\tests\test_access_decrypt.exe

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include <nlohmann/json.hpp>

#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

// ---- base64 ----
static std::string b64_encode(const std::vector<unsigned char>& data) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bio);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data.data(), (int)data.size());
    BIO_flush(b64);
    BUF_MEM* ptr = nullptr;
    BIO_get_mem_ptr(b64, &ptr);
    std::string result(ptr->data, ptr->length);
    BIO_free_all(b64);
    return result;
}

static std::vector<unsigned char> b64_decode(const std::string& input) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new_mem_buf(input.data(), (int)input.size());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    std::vector<unsigned char> buffer(input.size());
    int len = BIO_read(bio, buffer.data(), (int)input.size());
    buffer.resize(len < 0 ? 0 : len);
    BIO_free_all(bio);
    return buffer;
}

// ---- AES-256-GCM (same layout as auth_aes.cpp) ----
static std::string aes_encrypt(const std::string& plaintext, const unsigned char key[32]) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("ctx new");

    unsigned char nonce[12];
    if (!RAND_bytes(nonce, sizeof(nonce))) { EVP_CIPHER_CTX_free(ctx); throw std::runtime_error("rand"); }

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce);

    std::vector<unsigned char> ct(plaintext.size() + 16);
    int len = 0;
    EVP_EncryptUpdate(ctx, ct.data(), &len,
                      reinterpret_cast<const unsigned char*>(plaintext.data()),
                      (int)plaintext.size());
    int ct_len = len;
    EVP_EncryptFinal_ex(ctx, ct.data() + len, &len);
    ct_len += len;

    unsigned char tag[16];
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag);
    EVP_CIPHER_CTX_free(ctx);

    std::vector<unsigned char> result;
    result.insert(result.end(), nonce, nonce + 12);
    result.insert(result.end(), ct.begin(), ct.begin() + ct_len);
    result.insert(result.end(), tag, tag + 16);
    return b64_encode(result);
}

static std::string aes_decrypt(const std::string& encoded, const unsigned char key[32]) {
    auto data = b64_decode(encoded);
    if (data.size() < 12 + 16) throw std::runtime_error("too short");

    const unsigned char* nonce = data.data();
    size_t ct_len = data.size() - 12 - 16;
    const unsigned char* ct = data.data() + 12;
    const unsigned char* tag = data.data() + 12 + ct_len;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce);

    std::vector<unsigned char> pt(ct_len);
    int len = 0;
    EVP_DecryptUpdate(ctx, pt.data(), &len, ct, (int)ct_len);
    int pt_len = len;

    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag);
    int ret = EVP_DecryptFinal_ex(ctx, pt.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret <= 0) throw std::runtime_error("tag mismatch");
    pt_len += len;

    return std::string(reinterpret_cast<char*>(pt.data()), pt_len);
}

// ---- CmsAccessInfo (mirror of panel_companion.h structs) ----
struct CmsSrvConfig {
    std::string srv_name;
    std::string srv_w3c_ip;
    int srv_cms_port = 0;
    std::string srv_appkey;
    int srv_relay_port = 0;
    bool IsValid() const {
        return !srv_w3c_ip.empty() && srv_cms_port > 0 && !srv_appkey.empty() && srv_relay_port > 0;
    }
};

struct CmsAccessInfo {
    CmsSrvConfig cms_config;
    bool IsValid() const { return cms_config.IsValid(); }
};

static std::shared_ptr<CmsAccessInfo> parse_access_info(const std::string& info) {
    try {
        auto obj = json::parse(info);
        auto cms = obj["cms_srv_config"];
        auto ac = std::make_shared<CmsAccessInfo>();
        ac->cms_config.srv_name      = cms["srv_name"].get<std::string>();
        ac->cms_config.srv_w3c_ip    = cms["srv_w3c_ip"].get<std::string>();
        ac->cms_config.srv_cms_port = cms["srv_cms_port"].get<int>();
        ac->cms_config.srv_relay_port= cms["srv_relay_port"].get<int>();
        ac->cms_config.srv_appkey    = cms["srv_appkey"].get<std::string>();
        return ac;
    } catch (std::exception& e) {
        std::cerr << "parse error: " << e.what() << "\n  info: " << info << "\n";
        return nullptr;
    }
}

int main() {
    // The user's access string.
    std::string access_str =
        "cms://access##"
        "j8bahrdRP33H3ADCPi8JMtBF2ZSU4n1tKc0ek3hDiOOJ3x8oMNjb38yAfU+zeJWNdo0pT4i5k0yp"
        "t2gs6qDuC7Z4uopvzEQqCXSAkAm0ktT8AateSSCC/bbqGS4IzDD3vqH7sge6Nd2MO687QNlEyAu/"
        "l1lr+CY7UKdDy0E9IMpJGyuKFa523DC0J+xP2G2oOgXgnLheTGXZmh/OeFvFHhH9RoNWpwh3M1wr"
        "WYk0F0mZxnH1Z097qZDnWFjv/MfWpCKDUvP/HQhQg3KmdewESsSSaBbz+pLJ";

    // 1. strip prefix
    const std::string prefix = "cms://access##";
    auto pos = access_str.find(prefix);
    if (pos == std::string::npos) {
        std::cerr << "ERROR: prefix not found\n";
        return 1;
    }
    std::string b64 = access_str.substr(pos + prefix.size());
    std::cout << "base64 payload: " << b64.substr(0, 40) << "...\n";

    // 2. decrypt
    unsigned char key[32];
    std::memcpy(key, "cae8ae8CDTDF289437e#$()92cb17540", 32);

    std::string plaintext;
    try {
        plaintext = aes_decrypt(b64, key);
    } catch (std::exception& e) {
        std::cerr << "ERROR: AES decrypt failed: " << e.what() << "\n";
        return 1;
    }

    std::cout << "decrypted JSON:\n" << plaintext << "\n\n";

    // 3. parse
    auto info = parse_access_info(plaintext);
    if (!info || !info->IsValid()) {
        std::cerr << "ERROR: parse/valid failed\n";
        return 1;
    }

    std::cout << "=== CmsAccessInfo ===\n";
    std::cout << "  srv_name       : " << info->cms_config.srv_name << "\n";
    std::cout << "  srv_w3c_ip     : " << info->cms_config.srv_w3c_ip << "\n";
    std::cout << "  srv_cms_port  : " << info->cms_config.srv_cms_port << "\n";
    std::cout << "  srv_relay_port : " << info->cms_config.srv_relay_port << "\n";
    std::cout << "  srv_appkey     : " << info->cms_config.srv_appkey << "\n";
    std::cout << "  valid          : " << (info->IsValid() ? "yes" : "no") << "\n";

    // 4. round-trip
    std::string re_enc = aes_encrypt(plaintext, key);
    std::string re_dec = aes_decrypt(re_enc, key);
    if (re_dec != plaintext) {
        std::cerr << "FAIL: round-trip mismatch\n";
        return 1;
    }
    std::cout << "\nround-trip encrypt/decrypt: OK\n";

    std::cout << "\nALL TESTS PASSED\n";
    return 0;
}
