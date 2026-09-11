#include "stream_settings_dialog.h"
#include <QFormLayout>
#include "px_qt_widget/no_margin_layout.h"
#include "px_qt_widget/px_label.h"
#include "px_qt_widget/px_pushbutton.h"
#include "render_panel/px_context.h"
#include "render_panel/database/stream_db_operator.h"

namespace px {
namespace {
QPointer<QCheckBox> AddOption(QPointer<QWidget> parent, QPointer<QFormLayout> layout, const QString& text_id, bool checked) {
    QPointer<TcLabel> label{new TcLabel(parent.data())}; // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns this label.
    label->SetTextId(text_id);
    QPointer<QCheckBox> control{new QCheckBox(parent.data())}; // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns this control.
    control->setObjectName(text_id);
    control->setChecked(checked);
    layout->addRow(label.data(), control.data());
    return control;
}
} // namespace

StreamSettingsDialog::StreamSettingsDialog(const std::shared_ptr<PxContext>& context, const std::shared_ptr<px_console::ConsoleStream>& item,
                                           QPointer<QWidget> parent)
    : TcCustomTitleBarDialog("", parent.data()), database_(context->GetStreamDBManager()), item_(item) {
    setWindowTitle(tcTr("id_device_settings"));
    setFixedSize(410, 450);
    CreateLayout();
}

void StreamSettingsDialog::CreateLayout() {
    QPointer<QWidget> body{new QWidget(this)};                // NOLINT(gammaray-raw-pointer-boundary) Qt dialog owns this widget.
    QPointer<QFormLayout> form{new QFormLayout(body.data())}; // NOLINT(gammaray-raw-pointer-boundary) Qt widget owns this layout.
    form->setContentsMargins(28, 16, 28, 16);
    form->setVerticalSpacing(14);
    audio_ = AddOption(body, form, "id_enable_audio", item_->audio_enabled_);
    clipboard_ = AddOption(body, form, "id_enable_clipboard", item_->clipboard_enabled_);
    only_viewing_ = AddOption(body, form, "id_only_viewing", item_->only_viewing_);
    split_windows_ = AddOption(body, form, "id_split_windows", item_->split_windows_);
    software_ = AddOption(body, form, "id_force_software", item_->force_software_);
    tcp_ = AddOption(body, form, "id_force_tcp", item_->force_tcp_);
    wait_debug_ = AddOption(body, form, "id_wait_debug", item_->wait_debug_);
    gdi_ = AddOption(body, form, "id_force_gdi_capture", item_->force_gdi_capture_);
    disable_vulkan_ = AddOption(body, form, "id_disable_vulkan_render", item_->disable_vulkan_render_);
    QPointer<TcPushButton> save{new TcPushButton(body.data())}; // NOLINT(gammaray-raw-pointer-boundary) Qt widget owns this button.
    save->SetTextId("id_ok");
    save->setFixedHeight(35);
    form->addRow(save.data());
    root_layout_->addWidget(body.data());
    const QPointer<StreamSettingsDialog> self{this}; // Observe the dialog; no asynchronous raw capture.
    connect(save.data(), &QPushButton::clicked, this, [self]() {
        if (self)
            self->Save();
    });
}

void StreamSettingsDialog::Save() {
    item_->audio_enabled_ = audio_->isChecked();
    item_->clipboard_enabled_ = clipboard_->isChecked();
    item_->only_viewing_ = only_viewing_->isChecked();
    item_->split_windows_ = split_windows_->isChecked();
    item_->force_software_ = software_->isChecked();
    item_->force_tcp_ = tcp_->isChecked();
    item_->wait_debug_ = wait_debug_->isChecked();
    item_->force_gdi_capture_ = gdi_->isChecked();
    item_->disable_vulkan_render_ = disable_vulkan_->isChecked();
    database_->UpdateStream(item_);
    close();
}
} // namespace px
