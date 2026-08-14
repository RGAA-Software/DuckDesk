#include "tc_translator.h"
#include <QFile>
#include <QIODevice>
#include <QDebug>
#include <QLocale>
#include <QApplication>
#include <iostream>
#include <nlohmann/json.hpp>
#include "tc_common_new/log.h"
#include "tc_common_new/shared_preference.h"

using namespace nlohmann;

namespace tc
{

    const std::string kUsingLanguage = "using_language";

    TcTranslator::TcTranslator(QObject *self_, TranslateUIType type) {
        this->self_ = self_;
        this->type_ = type;
        TcTranslatorManager::Instance()->AddUIObject(this->self_);
    }

    TcTranslator::~TcTranslator() {
        TcTranslatorManager::Instance()->RemoveUIObject(self_);
    }

    QWidget *TcTranslator::AsWidget() {
        return (QWidget *) self_;
    }

    QLayout *TcTranslator::AsLayout() {
        return (QLayout *) self_;
    }

    TranslateUIType TcTranslator::GetUIType() {
        return type_;
    }

    void TcTranslator::SetTextId(const QString &id) {
        text_id_ = id;
    }

    QString TcTranslator::GetTextId() {
        return text_id_;
    }

    void TcTranslator::OnTranslate(LanguageKind kind) {

    }

//

    void TcTranslatorManager::InitLanguage(LanguageKind kind) {
        auto sys_name = QLocale::system().bcp47Name();
        LOGI("system.bcp47 name: {}", sys_name.toStdString());
//        sys_name = QLocale(QLocale::Chinese, QLocale::China).bcp47Name();    // zh
//        LOGI("system.bcp47 name: {}", sys_name.toStdString());
//        sys_name = QLocale(QLocale::Chinese, QLocale::Taiwan).bcp47Name();   // zh-TW
//        LOGI("system.bcp47 name: {}", sys_name.toStdString());
//        sys_name = QLocale(QLocale::Chinese, QLocale::HongKong).bcp47Name();   // zh-HK
//        LOGI("system.bcp47 name: {}", sys_name.toStdString());
//        sys_name = QLocale(QLocale::Chinese, QLocale::Macao).bcp47Name();   // zh-MO
//        LOGI("system.bcp47 name: {}", sys_name.toStdString());
//        sys_name = QLocale(QLocale::English, QLocale::UnitedStates).bcp47Name(); // en
//        LOGI("system.bcp47 name: {}", sys_name.toStdString());
//        sys_name =  QLocale(QLocale::Japanese, QLocale::Japan).bcp47Name();   // ja
//        LOGI("system.bcp47 name: {}", sys_name.toStdString());

        LanguageKind target_kind = kind;
        auto sp = SharedPreference::Instance();
        if (kind == LanguageKind::kDefaultLang) {
            target_kind = (LanguageKind)sp->GetInt(kUsingLanguage, kDefaultLang);
            LOGI("using language: {}", (int)target_kind);
        }

        if (target_kind == kDefaultLang) {
            if (sys_name == "zh") {
                this->LoadLanguage(kSimpleCN);
                sp->PutInt(kUsingLanguage, kSimpleCN);
            }
            else if (sys_name == "zh-TW" || sys_name == "zh-HK" || sys_name == "zh-MO") {
                this->LoadLanguage(kTraditionalCN);
                sp->PutInt(kUsingLanguage, kTraditionalCN);
            }
            else if (sys_name == "ja") {
                //
            }
            else {
                this->LoadLanguage(kEnglish);
                sp->PutInt(kUsingLanguage, kEnglish);
            }
        }
        else {
            this->LoadLanguage((LanguageKind)target_kind);
        }
    }

    bool TcTranslatorManager::LoadLanguage(LanguageKind kind) {
        QString path;
        if (kind == LanguageKind::kSimpleCN) {
            path = "./resources/language/simple_cn.json";
        } else if (kind == LanguageKind::kTraditionalCN) {
            path = "./resources/language/traditional_cn.json";
        } else if (kind == LanguageKind::kEnglish) {
            path = "./resources/language/english.json";
        } else {
            path = "./resources/language/english.json";
        }
        this->kind_ = kind;

        auto base_dir = QApplication::applicationDirPath();
        return LoadLanguage(base_dir + "/" + path);
    }

    bool TcTranslatorManager::LoadLanguage(const QString &path) {
        LOGI("Load language at: {}", path.toStdString());
        if (path.isEmpty()) {
            return false;
        }
        QFile file(path);
        file.open(QIODevice::ReadOnly);
        if (!file.isOpen()) {
            LOGI("Open: {} failed", path.toStdString());
            return false;
        }

        auto content = file.readAll().toStdString();
        try {
            auto parsed_content = json::parse(content);
            contents_.clear();
            for (json::iterator it = parsed_content.begin(); it != parsed_content.end(); it++) {
                contents_[it.key().c_str()] = it.value().get<std::string>().c_str();
                //LOGI("{} => {}", it.key(), it.value());
            }
            return true;
        }
        catch (json::exception &e) {
            std::cout << "error : " << e.what() << std::endl;
        }
        return false;
    }

    QString TcTranslatorManager::GetTrString(const QString &id) {
        if (contents_.find(id) != contents_.end()) {
            return contents_[id];
        }
        return "not find";
    }

    void TcTranslatorManager::Translate() {
        std::lock_guard<std::mutex> guard(ui_objs_mtx_);
        for (auto &pair: ui_objects_) {
            auto translator = dynamic_cast<TcTranslator *>(pair.first);
            translator->OnTranslate(kind_);
        }
        auto sp = SharedPreference::Instance();
        sp->PutInt(kUsingLanguage, this->kind_);
    }

    void TcTranslatorManager::AddUIObject(QObject *obj, QObject *val) {
        std::lock_guard<std::mutex> guard(ui_objs_mtx_);
        ui_objects_[obj] = val;
    }

    void TcTranslatorManager::RemoveUIObject(QObject *obj) {
        std::lock_guard<std::mutex> guard(ui_objs_mtx_);
        if (ui_objects_.find(obj) != ui_objects_.end()) {
            ui_objects_.erase(obj);
        }
    }

    LanguageKind TcTranslatorManager::GetSelectedLanguage() {
        auto sp = SharedPreference::Instance();
        auto using_lang = sp->GetInt(kUsingLanguage, kDefaultLang);
        return static_cast<LanguageKind>(using_lang);
    }

}