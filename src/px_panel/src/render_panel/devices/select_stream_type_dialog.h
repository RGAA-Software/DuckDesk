//
// Created by RGAA on 19/05/2025.
//

#ifndef PX_SELECT_STREAM_TYPE_DIALOG_H
#define PX_SELECT_STREAM_TYPE_DIALOG_H

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

    class PxContext;

    class SelectStreamTypeDialog : public TcCustomTitleBarDialog {
    public:
        explicit SelectStreamTypeDialog(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);

    private:
    };

}

#endif //PX_SELECT_STREAM_TYPE_DIALOG_H
