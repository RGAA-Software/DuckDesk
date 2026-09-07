#include "upgrade_helper.h"
#include "render_panel/ui/qt_lifetime_guard.h"
#include <qwindow.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <Windows.h>
#include <dwmapi.h>
#include <qboxlayout.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qdir.h>
#include <qtextedit.h>
#include <qfileinfo.h>
#include <qdesktopservices.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qurl.h>
#include <qjsonarray.h>
#include <qstandardpaths.h>
#include <px_common/http_client.h>
#include <px_common/log.h>
#include <px_common/md5.h>
#include <px_common/string_util.h>
#include "px_qt_widget/px_dialog.h"
#include "translator/px_translator.h"
#include "gd_button.h"
#include "gd_custom_progress_bar.h"
#include "version_config.h"
#include "render_panel/px_settings.h"
#include "render_panel/px_application.h"
#include "render_panel/px_context.h"
#include "render_panel/upgrade/upgrade_request_state.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>



namespace px {
	static const int kUpgradeApiOkValue = 200;
    static const std::string kUpgradeBaseUrl = "/api/v1/update";
	static const std::string kUpgradeQueryPath = kUpgradeBaseUrl + "/query_update_info";
	static const std::string kUpgradeDownloadPath = kUpgradeBaseUrl + "/download";

	namespace {
		constexpr int kUpgradeCheckTimeoutMs = 10'000;
		constexpr int kUpgradeDownloadTimeoutMs = 180'000;

		struct UpgradeEndpoint final {
			std::string host;
			int port = 0;
			bool ssl = false;
			std::string root_url;
			std::string appkey;
		};

		enum class UpgradeDownloadError {
			None,
			Network,
			Timeout,
			SaveFile,
			Corrupt,
		};

		struct UpgradeDownloadResult final {
			UpgradeDownloadError error = UpgradeDownloadError::None;
			std::string detail;
		};

		class UpgradeDownloadSink final {
		public:
			explicit UpgradeDownloadSink(const std::filesystem::path& path)
				: stream_(path, std::ios::binary | std::ios::trunc) {}

			[[nodiscard]] bool IsOpen() const {
				return stream_.is_open();
			}

			[[nodiscard]] bool Write(std::string_view chunk) {
				stream_.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
				if (!stream_) {
					write_failed_ = true;
					return false;
				}
				md5_.Update(chunk);
				return true;
			}

			void Close() {
				stream_.close();
				if (stream_.fail()) {
					write_failed_ = true;
				}
			}

			[[nodiscard]] bool WriteFailed() const {
				return write_failed_;
			}

			[[nodiscard]] std::string Md5() {
				return md5_.FinishHex();
			}

		private:
			std::ofstream stream_;
			Md5Hasher md5_{};
			bool write_failed_ = false;
		};

		std::optional<UpgradeEndpoint> GetUpgradeEndpoint() {
			const auto app = grApp;
			const auto settings = PxSettings::Instance();
			if (!app || !settings) {
				return std::nullopt;
			}
			const auto host = settings->GetConsoleServerHost();
			const auto port = settings->GetConsoleServerPort();
			const auto scheme = PxSettings::GetConsoleHttpScheme();
			if (host.empty() || port <= 0) {
				return std::nullopt;
			}
			return UpgradeEndpoint{
				.host = host,
				.port = port,
				.ssl = scheme == "https",
				.root_url = std::format("{}://{}:{}", scheme, host, port),
				.appkey = app->GetAppkey(),
			};
		}

		void RemovePartialDownload(const std::filesystem::path& path) {
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

		UpgradeDownloadResult PublishDownload(
			const std::filesystem::path& partial_path,
			const std::filesystem::path& save_path) {
			// MoveFileExW is the Windows boundary that atomically replaces an old
			// installer only after the new package has passed its digest check.
			if (!::MoveFileExW(
					partial_path.c_str(), save_path.c_str(),
					MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				const auto error = std::error_code(
					static_cast<int>(::GetLastError()), std::system_category());
				return {UpgradeDownloadError::SaveFile, error.message()};
			}
			return {};
		}

		QString DownloadErrorText(UpgradeDownloadError error) {
			switch (error) {
			case UpgradeDownloadError::Timeout:
				return tcTr("id_upgrade_time_out");
			case UpgradeDownloadError::Corrupt:
				return tcTr("id_upgrade_file_corrupted");
			case UpgradeDownloadError::SaveFile:
				return tcTr("id_upgrade_failed_open_file");
			case UpgradeDownloadError::Network:
				return tcTr("id_upgrade_network_error");
			case UpgradeDownloadError::None:
				return {};
			}
			return tcTr("id_upgrade_network_error");
		}
	}

	void UpgradeHelperWidget::paintEvent(QPaintEvent* event) {
		QPainter painter(this);
		// 启用抗锯齿
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true); 
		// 获取窗口尺寸
		int windowHeight = rect().height();
		int oneThirdHeight = windowHeight / 10;
		painter.fillRect(0, 0, rect().width(), oneThirdHeight, QColor(0xe0, 0xf1, 0xff));
		painter.fillRect(0, oneThirdHeight, rect().width(), windowHeight - oneThirdHeight, QColor(0xff, 0xff, 0xff));
		static QString pixmap_path = QStringLiteral(":/resources/image/update/update_bk.png");
		static QPixmap pixmap(pixmap_path);
		pixmap.setDevicePixelRatio(2.0);
		QRect targetRect(0, 0, 310, 150);
		painter.drawPixmap(targetRect, pixmap);
	}

	UpgradeHelperWidget::UpgradeHelperWidget(QWidget* parent) : QDialog(parent) {
		InitUI();
		ApplyWindowsShadow();
		InitSigChannel();
	}

	UpgradeHelperWidget::~UpgradeHelperWidget() {
	
	}

	void UpgradeHelperWidget::SetForced(bool f) {
		forced_ = f;
		if (forced_) {
			stack_widget_->setCurrentWidget(forced_update_widget_);
		}
		else {
			stack_widget_->setCurrentWidget(notify_update_widget_);
		}
	}

	void UpgradeHelperWidget::keyPressEvent(QKeyEvent* event) {
		if (event->key() == Qt::Key_Escape) {
			event->ignore();
			event->accept();
			return;
		}
	}

	void UpgradeHelperWidget::keyReleaseEvent(QKeyEvent* event) {
		if (event->key() == Qt::Key_Escape) {
			event->ignore();
			event->accept();
			if (!forced_) {
				QMetaObject::invokeMethod(
					this,
					MakeQtLifetimeAction(
						QPointer<UpgradeHelperWidget>(this),
						[](const QPointer<UpgradeHelperWidget>& widget) {
							TcDialog dialog(
								tcTr("id_tips"),
								tcTr("id_upgrade_are_you_sure_exit"),
								widget);
							if (QDialog::Rejected != dialog.exec()) {
								widget->done(QDialog::Rejected);
							}
						}));
			}
			return;
		}
	}

	void UpgradeHelperWidget::closeEvent(QCloseEvent* event) {
		if (need_exit_) {
			this->done(QDialog::Rejected);
			return;
		}
		event->ignore();
		if (!forced_) {
			QMetaObject::invokeMethod(
				this,
				MakeQtLifetimeAction(
					QPointer<UpgradeHelperWidget>(this),
					[](const QPointer<UpgradeHelperWidget>& widget) {
						TcDialog dialog(
							tcTr("id_tips"),
							tcTr("id_upgrade_are_you_sure_exit"),
							widget);
						if (QDialog::Rejected != dialog.exec()) {
							widget->done(QDialog::Rejected);
						}
					}));
		}
	}

	void UpgradeHelperWidget::InitUI() {
		setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
		setObjectName("UpgradeHelperWidget");
		setAttribute(Qt::WA_StyledBackground);
		setFixedSize(310, 380);

		QVBoxLayout* main_vbox_layout = new QVBoxLayout(this);
		main_vbox_layout->setSpacing(0);
		main_vbox_layout->setContentsMargins(0, 0, 0, 0);
		main_vbox_layout->setAlignment(Qt::AlignTop);
		main_vbox_layout->addStretch(1);
		{
			QHBoxLayout* hbox_layout = new QHBoxLayout();
			hbox_layout->setSpacing(0);
			hbox_layout->setContentsMargins(0, 0, 0, 0);
			hbox_layout->setAlignment(Qt::AlignLeft);

			remote_version_lab_ = new QLabel(this);
			remote_version_lab_->setFixedHeight(20);
			QString style = R"(
				QLabel {font-size: 15px; font-family: Microsoft YaHei; color: #000000; line-height: 20px; font-weight: 500}
			)";
			remote_version_lab_->setStyleSheet(style);
			remote_version_lab_->setAlignment(Qt::AlignLeft); 

			hbox_layout->addSpacing(20);
			hbox_layout->addWidget(remote_version_lab_);
			hbox_layout->addStretch(1);
			main_vbox_layout->addSpacing(10);
			main_vbox_layout->addLayout(hbox_layout);
		}

		{
			QHBoxLayout* msg_hlayout = new QHBoxLayout();
			msg_hlayout->setContentsMargins(0, 0, 0, 0);
			msg_hlayout->setSpacing(0);
			msg_hlayout->setAlignment(Qt::AlignLeft);

			text_edit_ = new QTextEdit();
			text_edit_->setFixedSize(280, 100);
			text_edit_->setPlaceholderText(QStringLiteral(""));
			QFont font("Microsoft YaHei");
			font.setPixelSize(13);
			text_edit_->setFont(font);

			text_edit_->setReadOnly(true); 
			text_edit_->setFrameStyle(QFrame::NoFrame); 
			text_edit_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
			text_edit_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
			text_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
			QString edit_style = R"(
				QTextEdit {
					background: transparent;
					font-size: 13px;
					font-family: Microsoft YaHei;
					color: #8d8e8f;
					padding: 0px;
					border: 0px;
					font-weight: 400;
				}
			)";
			text_edit_->setStyleSheet(edit_style);

			msg_hlayout->addSpacing(18);
			msg_hlayout->addWidget(text_edit_);
			msg_hlayout->addSpacing(20);

			main_vbox_layout->addSpacing(12);
			main_vbox_layout->addLayout(msg_hlayout);
		}


		stack_widget_ = new QStackedWidget(this);
		stack_widget_->setFixedSize(310, 56);
		{
			notify_update_widget_ = new QWidget(stack_widget_);
			stack_widget_->addWidget(notify_update_widget_);
			notify_update_widget_->setFixedSize(310, 56);
			QHBoxLayout* hlayout = new QHBoxLayout(notify_update_widget_);
			hlayout->setContentsMargins(0, 0, 0, 0);
			hlayout->setSpacing(0);
			hlayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			confirm_btn_ = new GDButton(notify_update_widget_);
			GDButton::BorderInfo btn_border_info;
			GDButton::TextInfo btn_text_info;
			GDButton::IconInfo btn_icon_info;
			GDButton::BackgroundInfo btn_bk_info;
			btn_bk_info.m_background_color_normal = QColor(0x1A, 0x76, 0xF6);
			btn_bk_info.m_background_color_hover = QColor(0x1D, 0x79, 0xF9);
			btn_bk_info.m_background_color_press = QColor(0x1F, 0x7F, 0xFF);
			btn_bk_info.m_background_color_disable = QColor(0xcc, 0xcc, 0xcc);
			btn_text_info.m_blod = false;
			btn_text_info.m_font_color_normal = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_hover = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_press = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_disable = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_text = tcTr("id_upgrade_download_now");
			btn_text_info.m_font_size = 13;
			btn_text_info.m_padding_left = 28;
			btn_text_info.m_padding_top = 21;
			btn_border_info.m_border_radius = 4;
			btn_border_info.m_border_width = 0;
			btn_icon_info.m_have_icon = false;
			confirm_btn_->Init(QSize(110, 32), btn_text_info, btn_bk_info, btn_icon_info,
				btn_border_info);
			confirm_btn_->setEnabled(true);

			btn_text_info.m_text = tcTr("id_upgrade_update_later");
			cancel_btn_ = new GDButton(notify_update_widget_);

			btn_bk_info.m_background_color_normal = QColor(0xeb, 0xeb, 0xeb);
			btn_bk_info.m_background_color_hover = QColor(0xe9, 0xe9, 0xe9);
			btn_bk_info.m_background_color_press = QColor(0xe7, 0xe7, 0xe7);
			btn_bk_info.m_background_color_disable = QColor(0xcc, 0xcc, 0xcc);
			btn_text_info.m_font_color_normal = QColor(0x84, 0x84, 0x84);
			btn_text_info.m_font_color_hover = QColor(0x84, 0x84, 0x84);
			btn_text_info.m_font_color_press = QColor(0x84, 0x84, 0x84);
			btn_text_info.m_font_color_disable = QColor(0x84, 0x84, 0x84);

			cancel_btn_->Init(QSize(110, 32), btn_text_info, btn_bk_info, btn_icon_info,
				btn_border_info);
			cancel_btn_->setEnabled(true);

			hlayout->addStretch(1);
			hlayout->addWidget(cancel_btn_);
			hlayout->addSpacing(26);
			hlayout->addWidget(confirm_btn_);
			hlayout->addStretch(1);
		}

		{
			forced_update_widget_ = new QWidget(stack_widget_);
			stack_widget_->addWidget(forced_update_widget_);
			forced_update_widget_->setFixedSize(310, 56);
			QHBoxLayout* hlayout = new QHBoxLayout(forced_update_widget_);
			hlayout->setContentsMargins(0, 0, 0, 0);
			hlayout->setSpacing(0);
			hlayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			forced_confirm_btn_ = new GDButton(forced_update_widget_);
			GDButton::BorderInfo btn_border_info;
			GDButton::TextInfo btn_text_info;
			GDButton::IconInfo btn_icon_info;
			GDButton::BackgroundInfo btn_bk_info;
			btn_bk_info.m_background_color_normal = QColor(0x1A, 0x76, 0xF6);
			btn_bk_info.m_background_color_hover = QColor(0x1D, 0x79, 0xF9);
			btn_bk_info.m_background_color_press = QColor(0x1F, 0x7F, 0xFF);
			btn_bk_info.m_background_color_disable = QColor(0xcc, 0xcc, 0xcc);
			btn_text_info.m_blod = false;
			btn_text_info.m_font_color_normal = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_hover = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_press = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_disable = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_text = tcTr("id_upgrade_download_now");;
			btn_text_info.m_font_size = 13;
			btn_text_info.m_padding_left = 28;
			btn_text_info.m_padding_top = 21;
			btn_border_info.m_border_radius = 4;
			btn_border_info.m_border_width = 0;
			btn_icon_info.m_have_icon = false;
			forced_confirm_btn_->Init(QSize(110, 32), btn_text_info, btn_bk_info, btn_icon_info,
				btn_border_info);
			forced_confirm_btn_->setEnabled(true);

			btn_text_info.m_text = tcTr("id_upgrade_exit_app");
			exit_app_btn_ = new GDButton(notify_update_widget_);

			btn_bk_info.m_background_color_normal = QColor(0xeb, 0xeb, 0xeb);
			btn_bk_info.m_background_color_hover = QColor(0xe9, 0xe9, 0xe9);
			btn_bk_info.m_background_color_press = QColor(0xe7, 0xe7, 0xe7);
			btn_bk_info.m_background_color_disable = QColor(0xcc, 0xcc, 0xcc);
			btn_text_info.m_font_color_normal = QColor(0x84, 0x84, 0x84);
			btn_text_info.m_font_color_hover = QColor(0x84, 0x84, 0x84);
			btn_text_info.m_font_color_press = QColor(0x84, 0x84, 0x84);
			btn_text_info.m_font_color_disable = QColor(0x84, 0x84, 0x84);

			exit_app_btn_->Init(QSize(110, 32), btn_text_info, btn_bk_info, btn_icon_info,
				btn_border_info);
			exit_app_btn_->setEnabled(true);

			hlayout->addStretch(1);
			hlayout->addWidget(exit_app_btn_);
			hlayout->addSpacing(26);
			hlayout->addWidget(forced_confirm_btn_);
			hlayout->addStretch(1);
		}

		{
			download_widget_ = new QWidget(stack_widget_);
			stack_widget_->addWidget(download_widget_);
			download_widget_->setFixedSize(310, 56);
			QVBoxLayout* main_vlayout = new QVBoxLayout(download_widget_);
			main_vlayout->setSpacing(0);
			main_vlayout->setContentsMargins(0, 0, 0, 0);
			main_vlayout->setAlignment(Qt::AlignTop);
			QHBoxLayout* hlayout = new QHBoxLayout();
			hlayout->setContentsMargins(0, 0, 0, 0);
			hlayout->setSpacing(0);
			hlayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			
			progress_bar_ = new GDCustomProgressBar(download_widget_);
			progress_bar_->setFixedWidth(280);
			progress_bar_->setError(false);
			progress_bar_->setValue(0);
			hlayout->addStretch(1);
			hlayout->addWidget(progress_bar_);
			hlayout->addStretch(1);
			main_vlayout->addLayout(hlayout);

			QHBoxLayout* hint_layout = new QHBoxLayout();
			hint_layout->setContentsMargins(0, 0, 0, 0);
			hint_layout->setSpacing(0);
			hint_layout->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
			download_hint_lab_ = new QLabel(download_widget_);
			download_hint_lab_->setText(tcTr("id_upgrade_downloading"));
			QString style = R"(
				QLabel {font-size: 12px; font-family: Microsoft YaHei; color: #8f8f8f; line-height: 20px; font-weight: 400}
			)";
			download_hint_lab_->setStyleSheet(style);
			download_hint_lab_->adjustSize();
			hint_layout->addWidget(download_hint_lab_);

			retry_btn_ = new QPushButton(download_widget_);
			retry_btn_->setText(tcTr("id_upgrade_retry"));
			retry_btn_->setFixedSize(40, 16);
			retry_btn_->setStyleSheet(R"(
				QPushButton {border: none; font-family:Microsoft YaHei;font-size:12px; color:#4c72ff; text-decoration: underline; background-color:#ffffff;}
			")");
			retry_btn_->setCursor(QCursor(Qt::PointingHandCursor));

			exit_btn_ = new QPushButton(download_widget_);
			exit_btn_->setText(tcTr("id_upgrade_exit"));
			exit_btn_->setFixedSize(90, 16);
			exit_btn_->setStyleSheet(R"(
				QPushButton {border: none; font-family:Microsoft YaHei;font-size:12px; color:#4c72ff; text-decoration: underline; background-color:#ffffff;}
			")");
			exit_btn_->setCursor(QCursor(Qt::PointingHandCursor));
			hint_layout->addWidget(retry_btn_);
			hint_layout->addWidget(exit_btn_);

			retry_btn_->hide();
			exit_btn_->hide();

			main_vlayout->addSpacing(8);
			main_vlayout->addLayout(hint_layout);
		}

		{
			install_widget_ = new QWidget(stack_widget_);
			stack_widget_->addWidget(install_widget_);
			install_widget_->setFixedSize(310, 56);
			QHBoxLayout* hlayout = new QHBoxLayout(install_widget_);
			hlayout->setContentsMargins(0, 0, 0, 0);
			hlayout->setSpacing(0);
			hlayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			install_confirm_btn_ = new GDButton(install_widget_);
			GDButton::BorderInfo btn_border_info;
			GDButton::TextInfo btn_text_info;
			GDButton::IconInfo btn_icon_info;
			GDButton::BackgroundInfo btn_bk_info;
			btn_bk_info.m_background_color_normal = QColor(0x1A, 0x76, 0xF6);
			btn_bk_info.m_background_color_hover = QColor(0x1D, 0x79, 0xF9);
			btn_bk_info.m_background_color_press = QColor(0x1F, 0x7F, 0xFF);
			btn_bk_info.m_background_color_disable = QColor(0xcc, 0xcc, 0xcc);
			btn_text_info.m_blod = false;
			btn_text_info.m_font_color_normal = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_hover = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_press = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_font_color_disable = QColor(0xff, 0xff, 0xff);
			btn_text_info.m_text = tcTr("id_upgrade_install_now");
			btn_text_info.m_font_size = 13;
			btn_text_info.m_padding_left = 112;
			btn_text_info.m_padding_top = 21;
			btn_border_info.m_border_radius = 4;
			btn_border_info.m_border_width = 0;
			btn_icon_info.m_have_icon = false;
			install_confirm_btn_->Init(QSize(280, 36), btn_text_info, btn_bk_info, btn_icon_info,
				btn_border_info);

			hlayout->addStretch(1);
			hlayout->addWidget(install_confirm_btn_);
			hlayout->addStretch(1);
		}

		stack_widget_->setCurrentWidget(notify_update_widget_);
		
		main_vbox_layout->addSpacing(14);
		main_vbox_layout->addWidget(stack_widget_);
		main_vbox_layout->addSpacing(8);
	}

	void UpgradeHelperWidget::SetRemoteVersion(const QString& version) {
		QString msg = tcTr("id_upgrade_find_new_version") + ": " + version;
		remote_version_lab_->setText(msg);
	}

	void UpgradeHelperWidget::SetRemoteUpdateDesc(const QString& desc) {
		text_edit_->setText(desc);
	}

	void UpgradeHelperWidget::InitSigChannel() {
		const QPointer<UpgradeHelperWidget> self(this);
		connect(cancel_btn_, &QPushButton::clicked, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>& widget) {
				widget->done(QDialog::Rejected);
			}));

		connect(exit_app_btn_, &QPushButton::clicked, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>& widget) {
				widget->exit_app_ = true;
				widget->done(QDialog::Rejected);
			}));

		connect(confirm_btn_, &QPushButton::clicked, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>& widget) {
				UpdateManager::Instance().Download();
				widget->stack_widget_->setCurrentWidget(widget->download_widget_);
			}));

		connect(retry_btn_, &QPushButton::clicked, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>& widget) {
				UpdateManager::Instance().Download();
				widget->stack_widget_->setCurrentWidget(widget->download_widget_);
			}));

		connect(exit_btn_, &QPushButton::clicked, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>& widget) {
				widget->done(QDialog::Rejected);
			}));

		connect(install_confirm_btn_, &QPushButton::clicked, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>&) {
				UpdateManager::Instance().OpenInstallFile();
			}));

		connect(forced_confirm_btn_, &QPushButton::clicked, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>& widget) {
				UpdateManager::Instance().Download();
				widget->stack_widget_->setCurrentWidget(widget->download_widget_);
			}));

		connect(&UpdateManager::Instance(), &UpdateManager::SigDownloadProgressValue, this,
			MakeQtLifetimeCallback(self,
				[](const QPointer<UpgradeHelperWidget>& widget, int value) {
					if (widget->stack_widget_->currentWidget() != widget->download_widget_) {
						widget->stack_widget_->setCurrentWidget(widget->download_widget_);
					}
					if (!widget->retry_btn_->isHidden() || !widget->exit_btn_->isHidden()) {
						widget->retry_btn_->hide();
						widget->exit_btn_->hide();
					}
					widget->download_hint_lab_->setText(tcTr("id_upgrade_downloading"));
					widget->progress_bar_->setValue(value);
				}));

		connect(&UpdateManager::Instance(), &UpdateManager::SigDownloadComplete, this,
			MakeQtLifetimeCallback(self,
				[](const QPointer<UpgradeHelperWidget>& widget, bool result, const QString& reason) {
					if (!result) {
						if (widget->stack_widget_->currentWidget() != widget->download_widget_) {
							widget->stack_widget_->setCurrentWidget(widget->download_widget_);
						}
						widget->download_hint_lab_->setText(
							QStringLiteral("id_upgrade_down_error") + reason);
						widget->retry_btn_->show();
						widget->exit_btn_->show();
						return;
					}
					widget->stack_widget_->setCurrentWidget(widget->install_widget_);
				}));

		connect(&UpdateManager::Instance(), &UpdateManager::SigOpenInstallFileError, this,
			MakeQtLifetimeAction(self, [](const QPointer<UpgradeHelperWidget>& widget) {
				TcDialog dialog(
					tcTr("id_tips"), tcTr("id_upgrade_cannot_open_file_for_exit"), widget);
				dialog.exec();
				widget->done(QDialog::Rejected);
			}));
	}

	void UpgradeHelperWidget::mousePressEvent(QMouseEvent* event) {
		if (event->button() == Qt::LeftButton) {
			QWindow* window = windowHandle();
			if (window) {
				window->startSystemMove();
			}
		}
	}

	void UpgradeHelperWidget::ApplyWindowsShadow() {
		HWND hwnd = reinterpret_cast<HWND>(winId());

		if (!hwnd) return;

		const MARGINS shadowMargins = { 1, 1, 1, 1 };
		DwmExtendFrameIntoClientArea(hwnd, &shadowMargins);

		DWM_BLURBEHIND blurBehind = { 0 };
		blurBehind.dwFlags = DWM_BB_ENABLE;
		blurBehind.fEnable = TRUE;
		blurBehind.hRgnBlur = NULL;
		DwmEnableBlurBehindWindow(hwnd, &blurBehind);

		DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
		DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY,
			&policy, sizeof(policy));

		BOOL compositionEnabled = TRUE;
		DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
			&compositionEnabled, sizeof(compositionEnabled));
	}

	UpdateManager::UpdateManager()
		: request_state_(std::make_shared<UpgradeRequestState>()) {}

	UpdateManager::~UpdateManager() {
		Shutdown();
	}

	void UpdateManager::Shutdown() {
		request_state_->Stop();
	}

	void UpdateManager::CheckUpdate(bool need_notify, bool from_user_clicked) {
		const auto endpoint = GetUpgradeEndpoint();
		const auto app = grApp;
		const auto context = app ? app->GetContext() : nullptr;
		if (!endpoint || !context) {
			emit SigGetUpdateConfigError("Upgrade endpoint is unavailable");
			if (from_user_clicked) {
				emit SigUpdateHint(tcTr("id_upgrade_check_error") + tcTr("id_upgrade_network_error"));
			}
			return;
		}

		const auto request = request_state_->BeginCheck();
		if (request.generation == 0 || !request.cancellation) {
			return;
		}
		const auto client = endpoint->ssl
			? HttpClient::MakeSSL(endpoint->host, endpoint->port, kUpgradeQueryPath, kUpgradeCheckTimeoutMs)
			: HttpClient::Make(endpoint->host, endpoint->port, kUpgradeQueryPath, kUpgradeCheckTimeoutMs);
		client->SetHeader("User-Agent", "GammaRay");
		client->SetCancellationSignal(request.cancellation);
		const std::map<std::string, std::string> query{
			{"page", "1"},
			{"page_size", "1"},
			{"sort_time", "-1"},
			{"appkey", endpoint->appkey},
		};
		const auto state = request_state_;
		context->PostNetworkTask(
			[client, context, state, generation = request.generation, query,
			 need_notify, from_user_clicked]() {
				auto response = client->Request(query);
				context->PostUITask(
					[state, generation, response = std::move(response),
					 need_notify, from_user_clicked]() {
						if (!state->CompleteCheck(generation)) {
							return;
						}
						UpdateManager::Instance().OnCheckUpdateResponse(
							response, need_notify, from_user_clicked);
					});
			});
	}

	void UpdateManager::OnCheckUpdateResponse(
		const HttpResponse& response, bool need_notify, bool from_user_clicked) {
		if (response.error_code != 0 || response.status < 200 || response.status >= 300) {
			const auto error = QString::fromStdString(std::format(
				"Network error: status={}, code={}, message={}",
				response.status, response.error_code, response.error_message));
			emit SigGetUpdateConfigError(error);
			LOGE("{}", error.toStdString());
			if (from_user_clicked) {
				emit SigUpdateHint(tcTr("id_upgrade_check_error") + tcTr("id_upgrade_network_error"));
			}
			return;
		}

		const auto data = QByteArray::fromStdString(response.body);

		QJsonParseError err;
		QJsonDocument doc = QJsonDocument::fromJson(data, &err);

		if (err.error != QJsonParseError::NoError) {
			emit SigGetUpdateConfigError("JSON parse error: " + err.errorString());
			LOGE("JSON parse error: {}, json: {}", err.errorString().toStdString(), data.toStdString());
			if (from_user_clicked) {
				emit SigUpdateHint(tcTr("id_upgrade_check_error") + tcTr("id_upgrade_data_format_error") + QString("(%1)").arg(QString::number(1)));
			}
			return;
		}

		if (!doc.isObject()) {
			emit SigGetUpdateConfigError("Invalid JSON format");
			LOGE("Invalid JSON format");
			if (from_user_clicked) {
				emit SigUpdateHint(tcTr("id_upgrade_check_error") + tcTr("id_upgrade_data_format_error") + QString("(%1)").arg(QString::number(2)));
			}
			return;
		}

		QJsonObject obj = doc.object();
		// --- Safe extract fields with default values ---
		const int resp_code = obj.value("code").toInt();
		if (resp_code != kUpgradeApiOkValue) {
			QString msg = "check upgrade error, code: " + QString::number(resp_code) + "; msg: " + obj.value("message").toString();
			emit SigGetUpdateConfigError(msg);
			if (from_user_clicked) {
				emit SigUpdateHint(msg);
			}
			return;
		}

		QString version;
		QString down_path;
		QString file_md5;
		QString file_name;
		QString desc;
		bool forced = false;

		if (obj.contains("data") && obj.value("data").isArray()) {
			QJsonArray data_array = obj.value("data").toArray();

			if (data_array.isEmpty() && from_user_clicked) {
				emit SigUpdateHint(tcTr("id_upgrade_latest") + QString("(%1)").arg(QString::number(4)));
				return;
			}

			for (const QJsonValue& value : data_array) {
				if (value.isObject()) {
					QJsonObject obj = value.toObject();
					desc = obj.value("desc").toString("");
					version = obj.value("version").toString("");
					down_path = obj.value("down_addr").toString("");
					file_md5 = obj.value("file_md5").toString("");
					file_name = obj.value("file_name").toString("");
					forced = obj.value("forced").toBool(false);
					file_size_ = obj.value("file_size").toVariant().toULongLong();
					break;
				}
			}
		}

		// --- Check required fields ---
		if (version.isEmpty() || down_path.isEmpty() || file_md5.isEmpty()) {
			emit SigGetUpdateConfigError("Invalid json: missing required fields");
			LOGE("Invalid json: missing required fields, json: {}", data.toStdString());
			if (from_user_clicked) {
				emit SigUpdateHint(tcTr("id_upgrade_check_error") + tcTr("id_upgrade_data_format_error") + QString("(%1)").arg(QString::number(3)));
			}
			return;
		}

		auto desc_list = desc.split("###");
		QString update_desc;
		for (auto info : desc_list) {
			update_desc = update_desc + info + "\n";
		}

		// Build a map and emit
		QVariantMap result;
		result["version"] = version;
		result["down_path"] = down_path;
		result["down_file_md5"] = file_md5;
		result["desc"] = update_desc;
		result["forced"] = forced;
		qDebug() << "UpdateChecker::onReplyFinished: " << result;

		const auto endpoint = GetUpgradeEndpoint();
		if (!endpoint) {
			emit SigGetUpdateConfigError("Upgrade endpoint is unavailable");
			return;
		}
		download_url_ = QString::fromStdString(endpoint->root_url + kUpgradeDownloadPath);
		remote_file_md5_ = file_md5;
		file_name_ = QFileInfo(file_name).fileName();
		if (file_name_.isEmpty()) {
			emit SigGetUpdateConfigError("Invalid json: missing download file name");
			return;
		}

		if (get_remote_update_version_callback_func_) {
			get_remote_update_version_callback_func_(version);
		}

		ParseUpdateConfig(result, need_notify, from_user_clicked);
	}

	// version1 == version2 return 0;  version1 > version2 return 1; version1 < version2 return -1;
	int UpdateManager::CompareVersion(const QString& version1, const QString& version2)
	{
		QStringList parts1 = version1.split('.');
		QStringList parts2 = version2.split('.');
		int numParts = qMax(parts1.size(), parts2.size());
		for (int i = 0; i < numParts; ++i) {
			int part1 = (i < parts1.size()) ? parts1[i].toInt() : 0;
			int part2 = (i < parts2.size()) ? parts2[i].toInt() : 0;

			if (part1 < part2) {
				return -1;
			}
			else if (part1 > part2) {
				return 1;
			}
		}
		return 0;
	}

	void UpdateManager::ParseUpdateConfig(const QVariantMap& data, bool need_notify, bool from_user_clicked) {
		int res = CompareVersion(data["version"].toString(), PROJECT_VERSION);
		if (res <= 0) {
			if (from_user_clicked) {
				emit SigUpdateHint(tcTr("id_upgrade_latest"));
			}
			return;
		}
		if (need_notify) {
			emit SigFindUpdate(data); 
		}
	}

	void UpdateManager::Download() {
		if (download_url_.isEmpty()) {
			return;
		}

		const auto endpoint = GetUpgradeEndpoint();
		const auto app = grApp;
		const auto context = app ? app->GetContext() : nullptr;
		if (!endpoint || !context) {
			emit SigDownloadComplete(false, tcTr("id_upgrade_network_error"));
			return;
		}

		QString dir_path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
		QDir dir{ dir_path };
        if (!dir.exists()) {
			if (!dir.mkpath(".")) {
				qWarning() << "Failed to create directories for file: " << dir_path;
				LOGE("Failed to create directories for file:", dir_path.toStdString());
				emit SigDownloadComplete(false, tcTr("id_upgrade_failed_create_dir"));
				return;
			}
		}
		QString save_path = dir_path + "/" + file_name_;
		save_path_ = save_path;

		const auto request = request_state_->BeginDownload();
		if (request.generation == 0 || !request.cancellation) {
			return;
		}
		const auto state = request_state_;
		const auto url = download_url_.toStdString();
		const auto file_name = file_name_.toStdString();
		const auto remote_md5 = StringUtil::ToLowerCpy(remote_file_md5_.toStdString());
		const auto expected_size = file_size_;
		const auto save_file_path = std::filesystem::path(save_path.toStdWString());
		const auto partial_path = std::filesystem::path(
			save_path.toStdWString() + L".download." + std::to_wstring(request.generation));
		const auto last_progress = std::make_shared<std::atomic_int>(-1);

		context->PostNetworkTask(
			[context, state, generation = request.generation,
			 cancellation = request.cancellation, url, file_name,
			 appkey = endpoint->appkey, remote_md5, expected_size,
			 save_file_path, partial_path, last_progress]() {
				const auto sink = std::make_shared<UpgradeDownloadSink>(partial_path);
				if (!sink->IsOpen()) {
					context->PostUITask([state, generation]() {
						if (state->CompleteDownload(generation)) {
							emit UpdateManager::Instance().SigDownloadComplete(
								false, tcTr("id_upgrade_failed_open_file"));
						}
					});
					return;
				}

				HttpDownloadOptions options;
				options.timeout_ms = kUpgradeDownloadTimeoutMs;
				options.verify_ssl = false;
				options.query = {{"down_file", file_name}, {"appkey", appkey}};
				options.headers = {{"User-Agent", "GammaRay"}};
				options.cancellation_signal = cancellation;
				options.write_callback = [sink](std::string_view chunk) {
					return sink->Write(chunk);
				};
				options.progress_callback =
					[context, state, generation, expected_size, last_progress](
						std::uint64_t total, std::uint64_t current) {
						const auto effective_total = total > 0 ? total : expected_size;
						if (effective_total == 0) {
							return state->IsDownloadCurrent(generation);
						}
						const auto progress = static_cast<int>(std::min<std::uint64_t>(
							100, current * 100 / effective_total));
						if (last_progress->exchange(progress, std::memory_order_acq_rel) != progress) {
							context->PostUITask([state, generation, progress]() {
								if (state->IsDownloadCurrent(generation)) {
									emit UpdateManager::Instance().SigDownloadProgressValue(progress);
								}
							});
						}
						return state->IsDownloadCurrent(generation);
					};

				const auto response = HttpClient::Download(url, std::move(options));
				sink->Close();
				UpgradeDownloadResult result;
				if (cancellation->load(std::memory_order_acquire)) {
					result = {UpgradeDownloadError::Network, "cancelled"};
				} else if (sink->WriteFailed()) {
					result = {UpgradeDownloadError::SaveFile, "write failed"};
				} else if (response.error_code == static_cast<int>(cpr::ErrorCode::OPERATION_TIMEDOUT)) {
					result = {UpgradeDownloadError::Timeout, response.error_message};
				} else if (response.error_code != 0 || response.status < 200 || response.status >= 300) {
					result = {UpgradeDownloadError::Network, std::format(
						"status={}, code={}, message={}", response.status,
						response.error_code, response.error_message)};
				} else if (StringUtil::ToLowerCpy(sink->Md5()) != remote_md5) {
					result = {UpgradeDownloadError::Corrupt, "md5 mismatch"};
				} else {
					result = PublishDownload(partial_path, save_file_path);
				}

				if (result.error != UpgradeDownloadError::None) {
					RemovePartialDownload(partial_path);
					LOGE("Upgrade download failed: {}", result.detail);
				}
				context->PostUITask(
					[state, generation, result = std::move(result)]() {
						if (!state->CompleteDownload(generation)) {
							return;
						}
						const bool success = result.error == UpgradeDownloadError::None;
						emit UpdateManager::Instance().SigDownloadComplete(
							success, DownloadErrorText(result.error));
					});
			});
	}

	void UpdateManager::OpenInstallFile() {
		const bool res = QDesktopServices::openUrl(QUrl::fromLocalFile(save_path_));
		if (!res) {
			emit SigOpenInstallFileError();
		}
	}
}
