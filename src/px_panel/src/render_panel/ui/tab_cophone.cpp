//
// Created by RGAA on 22/03/2025.
//

#include "tab_cophone.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "no_margin_layout.h"
#include "px_label.h"
#include "px_pushbutton.h"
#include "px_common_new/log.h"
#include "px_qt_widget/clickable_widget.h"
#include "render_panel/px_context.h"
#include <QLabel>
#include <QListWidget>
#include <QStyledItemDelegate>

namespace px
{

    class MyDeviceItemDelegate : public QStyledItemDelegate {
    public:
        explicit MyDeviceItemDelegate(QObject* pParent) {}
        ~MyDeviceItemDelegate() override = default;

        void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            editor->setGeometry(option.rect);
        }
    };

    TabCoPhone::TabCoPhone(const std::shared_ptr<PxApplication>& app, QWidget *parent)
        : TabBase(app, parent) {
    }

    void TabCoPhone::OnTabShow() {

    }

    void TabCoPhone::OnTabHide() {

    }

    void TabCoPhone::dragEnterEvent(QDragEnterEvent *event) {
        event->accept();
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
        LOGI("DragEventEnter....");
    }

    void TabCoPhone::dragMoveEvent(QDragMoveEvent *event) {
        event->accept();
        LOGI("DragEventMove....");
    }

    void TabCoPhone::dropEvent(QDropEvent *event) {
        QList<QUrl> urls = event->mimeData()->urls();
        LOGI("DragEventDrop....{}", urls.size());
        if (urls.isEmpty()) {
            return;
        }
        std::vector<QString> files;
        for (const auto& url : urls) {
            files.push_back(url.toLocalFile());
            LOGI("Drop files: {}", url.toLocalFile().toStdString());
        }
        //if (file_transfer_) {
        //    file_transfer_->SendFiles(files);
        //}
    }

}