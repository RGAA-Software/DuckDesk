//
// Created by RGAA on 20/03/2025.
//

#ifndef GAMMARAY_MAINWINDOW_WRAPPER_H
#define GAMMARAY_MAINWINDOW_WRAPPER_H

#include <QMainWindow>

namespace tc
{

    class MessageNotifier;

    class MainWindowWrapper : public QObject {
    public:
        explicit MainWindowWrapper(const std::shared_ptr<MessageNotifier>& notifier, QMainWindow* window);
        void Setup(const QString& title);

        MainWindowWrapper* ShowProfile(bool show = true);
        MainWindowWrapper* ShowSettings(bool show = true);
        MainWindowWrapper* ShowPinTop(bool show = true);
        MainWindowWrapper* ShowMin(bool show = true);
        MainWindowWrapper* ShowMax(bool show = true);
        MainWindowWrapper* ShowClose(bool show = true);

    private:
        QMainWindow* window_ = nullptr;
        std::shared_ptr<MessageNotifier> notifier_ = nullptr;
        bool show_profile_ = true;
        bool show_settings_ = true;
        bool show_pin_top_ = true;
        bool show_max_ = true;
        bool show_min_ = true;
        bool show_close_ = true;
    };

}

#endif //GAMMARAY_MAINWINDOW_WRAPPER_H
