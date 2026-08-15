#pragma once
#include <string>

namespace px {

    enum LanguageKind {
        kDefaultLang,
        kSimpleCN,
        kTraditionalCN,
        kEnglish
    };

    class TcTranslatorManager {
    public:
        static TcTranslatorManager* Instance() {
            static TcTranslatorManager inst;
            return &inst;
        }

        std::string GetTrString(const std::string& id) {
            return id;
        }

        void InitLanguage(LanguageKind kind) {
            // no-op
        }
    };

}

#define tcTr(x) px::TcTranslatorManager::Instance()->GetTrString(x)
#define tcTrMgr() px::TcTranslatorManager::Instance()
