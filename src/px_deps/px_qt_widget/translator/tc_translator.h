#pragma once

#include <QString>
#include <QWidget>
#include <QLayout>

#include <vector>
#include <map>
#include <mutex>

#define tcTr(x) TcTranslatorManager::Instance()->GetTrString(x)
#define tcTrMgr() TcTranslatorManager::Instance()

namespace tc
{

    enum LanguageKind {
        kDefaultLang,
        kSimpleCN,
        kTraditionalCN,
        kEnglish
    };

    enum TranslateUIType {
        kWidget,
        kLayout,
    };

    class TcTranslator {
    public:
        explicit TcTranslator(QObject *self, TranslateUIType type = TranslateUIType::kWidget);

        virtual ~TcTranslator();
        virtual void SetTextId(const QString &id);
        virtual void OnTranslate(LanguageKind kind);
        QWidget *AsWidget();
        QLayout *AsLayout();
        TranslateUIType GetUIType();
        QString GetTextId();

    protected:
        QObject *self_ = nullptr;
        TranslateUIType type_;
        QString text_id_;
    };

//
    class TcTranslatorManager {
    public:
        static TcTranslatorManager *Instance() {
            static TcTranslatorManager mgr;
            return &mgr;
        }

        void InitLanguage(LanguageKind kind = LanguageKind::kDefaultLang);
        bool LoadLanguage(LanguageKind kind);
        bool LoadLanguage(const QString &file);
        QString GetTrString(const QString &id);
        void Translate();
        void AddUIObject(QObject *obj, QObject * = nullptr);
        void RemoveUIObject(QObject *obj);
        LanguageKind GetSelectedLanguage();

    private:
        std::map<QString, QString> contents_;
        std::mutex ui_objs_mtx_;
        std::map<QObject *, QObject *> ui_objects_;
        LanguageKind kind_ = kDefaultLang;
    };

}