//
// Created by RGAA on 20/09/2025.
//

#ifndef GAMMARAYPREMIUM_AUTHAES_H
#define GAMMARAYPREMIUM_AUTHAES_H

#include <string>

namespace tc
{

    // [32]
    static uint8_t* AES_DEPLOY_AUTH = (uint8_t *)"cae8ae8CDTDF289437e#$()92cb17540";

    class AuthAes {
    public:
        static std::string AesEncrypt(const std::string &plaintext, const unsigned char key[32]);
        static std::string AesDecrypt(const std::string& encoded, const unsigned char key[32]);
    };

}

#endif //GAMMARAYPREMIUM_AUTHAES_H
