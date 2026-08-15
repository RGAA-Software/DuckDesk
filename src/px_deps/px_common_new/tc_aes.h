#pragma once

#include <stdbool.h>
#include <vector>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/rand.h>
#include <openssl/aes.h>

namespace tc
{
	bool AesEncryptPcks7Cbc128(const unsigned char* plaintext, int plaintext_len,
                               const unsigned char* key, const unsigned char* iv,
                               std::vector<unsigned char>& ciphertext);

    bool AesDecryptPcks7Cbc128(const unsigned char* ciphertext, int ciphertext_len,
                               const unsigned char* key, const unsigned char* iv,
                               std::vector<unsigned char>& plaintext);
}

