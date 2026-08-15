//
// Created by RGAA on 19/05/2025.
//

#ifndef GAMMARAY_SELECT_STREAM_TYPE_DIALOG_H
#define GAMMARAY_SELECT_STREAM_TYPE_DIALOG_H

#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QRadioButton>
#include <QComboBox>
#include <QPainter>

#include "px_cms_client/cms_stream.h"
#include "px_qt_widget/px_custom_titlebar_dialog.h"

namespace px
{

    class GrContext;

    class SelectStreamTypeDialog : public TcCustomTitleBarDialog {
    public:
        explicit SelectStreamTypeDialog(const std::shared_ptr<GrContext>& ctx, QWidget* parent = nullptr);

    private:
    };

}

#endif //GAMMARAY_SELECT_STREAM_TYPE_DIALOG_H
