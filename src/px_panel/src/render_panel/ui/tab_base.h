//
// Created by RGAA on 2024/4/9.
//

#ifndef TC_SERVER_STEAM_BASE_TAB_H
#define TC_SERVER_STEAM_BASE_TAB_H

#include <QWidget>
#include <memory>

namespace px
{
    constexpr int kTabContentMarginTop = 3;

    class PxContext;
    class PxSettings;
    class PxApplication;
    class MessageListener;
    class PxStatistics;

    class TabBase : public QWidget {
    public:
        explicit TabBase(const std::shared_ptr<PxApplication>& app, QWidget* parent);
        ~TabBase() override;
        virtual void OnTabShow();
        virtual void OnTabHide();
        virtual void OnTranslate();

        void SetAttach(QObject* at) {attach_ = at;}
        QObject* GetAttach() {return attach_;}

    protected:
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        QObject* attach_ = nullptr;
        PxSettings* settings_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<PxStatistics> statistics_ = nullptr;
    };
}

#endif //TC_SERVER_STEAM_BASE_TAB_H
