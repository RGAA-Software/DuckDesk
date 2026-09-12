//
// Created by RGAA on 20/02/2025.
//

#ifndef PX_CT_MAIN_PROGRESS_H
#define PX_CT_MAIN_PROGRESS_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPointer>
#include <memory>

namespace px
{

    class TcLabel;
    class ThunderSdk;
    class ClientContext;
    class MessageListener;
    class TcPushButton;

    class MainProgress : public QLabel {
    public:
        MainProgress(const std::shared_ptr<ThunderSdk>& sdk, const std::shared_ptr<ClientContext>& ctx, QWidget* parent);
        void ResetProgress();
        void StepForward();
        void CompleteProgress();
        int GetCurrentProgress();
        void paintEvent(QPaintEvent *event) override;

    private:
        std::shared_ptr<ThunderSdk> sdk_ = nullptr;
        std::shared_ptr<ClientContext> context_ = nullptr;
        static constexpr int kConnectionProgressSteps{3};
        QPixmap bg_pixmap_;
        TcLabel* lbl_sub_message_ = nullptr;
        QPointer<QProgressBar> progress_bar_{};
        TcPushButton* retry_btn_ = nullptr;
        std::atomic_int progress_steps_ = { 0 };
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
    };

}

#endif //PX_CT_MAIN_PROGRESS_H
