//
// Created by RGAA on 2024-04-09.
//

#include "tab_game.h"
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QStyledItemDelegate>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>

#include <algorithm>
#include <filesystem>
#include <format>
#include <mutex>
#include <utility>
#include "render_panel/database/db_game.h"
#include "render_panel/database/db_game_operator.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "px_qt_widget/round_img_display.h"
#include "px_qt_widget/cover_widget.h"
#include "px_qt_widget/widget_helper.h"
#include "render_panel/ui/game_catalog_refresh_state.h"
#include "render_panel/ui/qt_lifetime_guard.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_common/string_util.h"
#include "px_steam_manager/steam_manager.h"
#include "px_steam_manager/steam_entities.h"
#include "game_info_preview.h"
#include "px_common/folder_util.h"
#include "render_panel/px_run_game_manager.h"
#include "px_qt_widget/no_margin_layout.h"
#include "add_game_panel.h"
#include "px_common/file_util.h"
#include "px_dialog.h"
#include "px_label.h"
#include "px_pushbutton.h"

namespace px
{

    namespace {

        std::mutex& GameCatalogSteamScanMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::string ResolveGameCoverPath(
            const TcDBGamePtr& game,
            const std::string& steam_image_cache_path,
            const std::string& cover_folder_path) {
            if (!game) {
                return {};
            }
            if (!game->cover_url_.empty()
                && std::filesystem::exists(PathFromUTF8(game->cover_url_))) {
                return game->cover_url_;
            }
            if (game->game_id_ == 0) {
                return {};
            }

            const auto source_path = std::format(
                "{}/{}/library_600x900.jpg",
                steam_image_cache_path,
                game->game_id_);
            const auto target_path = std::format(
                "{}/{}_library_600x900.jpg",
                cover_folder_path,
                game->game_id_);
            FileUtil::CopyFileExt(
                PathFromUTF8(source_path), PathFromUTF8(target_path), false);
            if (std::filesystem::exists(PathFromUTF8(target_path))) {
                return target_path;
            }
            return {};
        }

        std::vector<GameCatalogEntry> BuildCatalogEntries(
            std::vector<TcDBGamePtr> games,
            const std::string& steam_image_cache_path,
            const std::string& cover_folder_path) {
            std::vector<GameCatalogEntry> entries;
            entries.reserve(games.size());
            for (auto& game : games) {
                auto cover_path = ResolveGameCoverPath(
                    game, steam_image_cache_path, cover_folder_path);
                entries.push_back({
                    .game = std::move(game),
                    .cover_path = std::move(cover_path),
                });
            }
            return entries;
        }

        std::vector<TcDBGamePtr> ConvertSteamGames(
            const std::vector<std::shared_ptr<SteamApp>>& steam_games) {
            std::vector<TcDBGamePtr> games;
            games.reserve(steam_games.size());
            for (const auto& steam_game : steam_games) {
                const auto game = std::make_shared<TcDBGame>();
                game->CopyFrom(steam_game);
                games.push_back(game);
            }
            return games;
        }

    }

    class MainItemDelegate : public QStyledItemDelegate {
    public:
        explicit MainItemDelegate(QObject *pParent) : QStyledItemDelegate(pParent) {}
        ~MainItemDelegate() override {}
        void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const override {
            editor->setGeometry(option.rect);
        }
    };

    TabGame::TabGame(const std::shared_ptr<PxApplication>& app, QWidget* parent) : TabBase(app, parent) {
        steam_mgr_ = context_->GetSteamManager();
        refresh_state_ = std::make_shared<GameCatalogRefreshState>();
        const QPointer<TabGame> self(this);
        auto root_layout = new QVBoxLayout();
        WidgetHelper::ClearMargins(root_layout);
        // title margin
        root_layout->addSpacing(kTabContentMarginTop);

        // operators
        auto op_layout = new NoMarginHLayout();
        auto btn_size = QSize(120, 33);
        {
            auto btn = new TcPushButton(this);
            btn->SetTextId("id_add_game");
            btn->setFixedSize(btn_size);
            op_layout->addSpacing(15);
            op_layout->addWidget(btn);
            connect(btn, &QPushButton::clicked, this,
                    MakeQtLifetimeAction(self, [](const QPointer<TabGame>& tab) {
                        tab->ShowAddGamePanel();
                    }));
        }
        {
            auto btn = new TcPushButton(this);
            btn->SetTextId("id_refresh");
            btn->setFixedSize(btn_size);
            op_layout->addSpacing(15);
            op_layout->addWidget(btn);
            connect(btn, &QPushButton::clicked, this,
                    MakeQtLifetimeAction(self, [](const QPointer<TabGame>& tab) {
                        tab->RefreshGames();
                    }));
        }
        op_layout->addStretch();
        root_layout->addLayout(op_layout);

        list_widget_ = new QListWidget(this);
        auto delegate = new MainItemDelegate(this);
        list_widget_->setItemDelegate(delegate);

        root_layout->addWidget(list_widget_);

        list_widget_->setMovement(QListView::Static);
        list_widget_->setViewMode(QListView::IconMode);
        list_widget_->setFlow(QListView::LeftToRight);
        list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list_widget_->setSpacing(10);
        list_widget_->setResizeMode(QListWidget::Adjust);
        list_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(
            list_widget_, &QListWidget::customContextMenuRequested, this,
            MakeQtLifetimeCallback(
                self,
                [](const QPointer<TabGame>& tab, const QPoint& position) {
                    tab->ShowContextMenu(position);
                }));

        list_widget_->setStyleSheet("QListWidget {background-color:none;}");
        list_widget_->show();

        setLayout(root_layout);

        // listeners
        msg_listener_->Listen<MsgRunningGameIds>([self](const MsgRunningGameIds& rgs) {
            if (self) {
                self->UpdateRunningStatus(rgs.game_ids_);
            }
        });

        empty_tip_ = new QLabel(this);
        int empty_size = 64;
        empty_tip_->resize(empty_size, empty_size);
        auto pixmap = QPixmap::fromImage(QImage(":/resources/image/empty.svg"));
        pixmap = pixmap.scaled(empty_size, empty_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        empty_tip_->setPixmap(pixmap);

        ShowEmptyTip();
        ScanInstalledGames();
    }

    TabGame::~TabGame() {
        refresh_state_->Stop();
    }

    void TabGame::OnTabShow() {

    }

    void TabGame::OnTabHide() {

    }

    void TabGame::ScanInstalledGames() {
        RequestCatalog(true);
    }

    void TabGame::RequestCatalog(bool scan_installed_games) {
        const auto request = refresh_state_->Begin();
        if (request.generation == 0 || !request.cancellation) {
            return;
        }
        const auto context = context_;
        const auto database = context->GetDBGameManager();
        const auto steam_manager = steam_mgr_;
        const auto state = refresh_state_;
        const auto steam_image_cache_path = steam_manager
            ? steam_manager->GetSteamImageCachePath() : std::string{};
        const auto cover_folder_path =
            context->GetCurrentExeFolder() + "/resources/steam_covers";
        if (!database) {
            static_cast<void>(refresh_state_->Complete(request.generation));
            return;
        }
        FolderUtil::CreateDir(PathFromUTF8(cover_folder_path));
        const QPointer<TabGame> self(this);

        context->PostTask(
            [context, database, steam_manager, state,
             generation = request.generation,
             cancellation = request.cancellation,
             steam_image_cache_path, cover_folder_path,
             scan_installed_games, self]() {
                auto database_games = database->GetAllGames();
                auto initial_entries = BuildCatalogEntries(
                    std::move(database_games),
                    steam_image_cache_path,
                    cover_folder_path);
                if (!scan_installed_games || !steam_manager) {
                    context->PostUITask(
                        [self, generation,
                         entries = std::move(initial_entries)]() mutable {
                            if (self) {
                                self->ApplyCatalog(
                                    generation, std::move(entries), true);
                            }
                        });
                    return;
                }

                context->PostUITask(
                    [self, state, generation,
                     entries = std::move(initial_entries)]() mutable {
                        if (self && state->IsCurrent(generation)) {
                            self->ApplyCatalog(
                                generation, std::move(entries), false);
                        }
                    });
                if (cancellation->load(std::memory_order_acquire)) {
                    return;
                }

                std::vector<TcDBGamePtr> scanned_games;
                {
                    std::lock_guard lock(GameCatalogSteamScanMutex());
                    if (cancellation->load(std::memory_order_acquire)) {
                        return;
                    }
                    steam_manager->ScanInstalledGames(false);
                    scanned_games = ConvertSteamGames(
                        steam_manager->GetInstalledGames());
                    if (!cancellation->load(std::memory_order_acquire)) {
                        // Preserve the existing process-owned deep executable
                        // discovery without capturing the tab or posting UI.
                        steam_manager->RescanRecursively();
                    }
                }
                if (cancellation->load(std::memory_order_acquire)) {
                    return;
                }
                database->BatchSaveOrUpdateGames(scanned_games);
                auto final_entries = BuildCatalogEntries(
                    database->GetAllGames(),
                    steam_manager->GetSteamImageCachePath(),
                    cover_folder_path);
                context->PostUITask(
                    [self, generation,
                     entries = std::move(final_entries)]() mutable {
                        if (self) {
                            self->ApplyCatalog(
                                generation, std::move(entries), true);
                        }
                    });
            });
    }

    void TabGame::ApplyCatalog(
        std::uint64_t generation,
        std::vector<GameCatalogEntry> entries,
        bool final_result) {
        const bool accepted = final_result
            ? refresh_state_->Complete(generation)
            : refresh_state_->IsCurrent(generation);
        if (!accepted) {
            return;
        }
        ReplaceItems(std::move(entries));
    }

    void TabGame::AddItem(const TcDBGamePtr& game) {
        auto item = new QListWidgetItem(list_widget_);
        int margin = 0;
        auto item_size = GetItemSize();
        int item_width = item_size.width();
        int item_height = item_size.height();
        item->setSizeHint(QSize(item_width, item_height));

        auto widget = new QWidget(this);
        widget->setFixedSize(item_width, item_height);
        widget->setObjectName(std::to_string(game->game_id_).c_str());

        auto layout = new QVBoxLayout();
        WidgetHelper::ClearMargins(layout);
        auto cover = new RoundImageDisplay("", item_width, item_height, 9, widget);
        cover->setObjectName("cover");
        layout->addWidget(cover);

        if (game->cover_pixmap_.has_value()) {
            auto pixmap = std::any_cast<QPixmap>(game->cover_pixmap_);
            cover->UpdatePixmap(pixmap);
        }

        widget->setLayout(layout);
        widget->show();

        auto cover_widget = new CoverWidget(widget, 0);
        cover_widget->setObjectName("cover_mask");
        cover_widget->setFixedSize(item_width, item_height);

        auto name = new QLabel(widget);
        name->setFixedSize(item_width, 32);
        name->setStyleSheet(R"(background-color:#333333; border-radius: 7px; color:#ffffff;)");
        name->setAlignment(Qt::AlignCenter);
        name->setGeometry(0, item_height-name->height(), item_width, name->height());
        name->setText(game->game_name_.c_str());
        name->update();
        name->show();

        LOGI("engine type: {}", game->engine_type_);
        if (game->engine_type_ != "UNKNOWN" && !game->engine_type_.empty()) {
            auto engine = new QLabel(widget);
            engine->setFixedSize(60, 22);
            engine->setText(game->engine_type_.c_str());
            engine->setStyleSheet(R"(background-color:#333333; border-radius: 11px; color:#ffffff; font-size:10px;)");
            engine->setAlignment(Qt::AlignCenter);
            engine->setGeometry(item_width - engine->width() - 5, 5, engine->width(), engine->height());
        }

        list_widget_->setItemWidget(item, widget);
    }

    QSize TabGame::GetItemSize() {
        int item_width = 180;
        int item_height = item_width / (600.0/900.0);
        return {item_width, item_height};
    }

    void TabGame::ReplaceItems(std::vector<GameCatalogEntry> entries) {
        list_widget_->clear();
        games_.clear();
        games_.reserve(entries.size());
        const auto item_size = GetItemSize();
        for (auto& entry : entries) {
            if (!entry.game) {
                continue;
            }
            if (!entry.cover_path.empty()) {
                QImage image;
                if (image.load(QString::fromStdString(entry.cover_path))) {
                    auto pixmap = QPixmap::fromImage(image).scaled(
                        item_size.width(), item_size.height(),
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    if (!pixmap.isNull()) {
                        entry.game->cover_pixmap_ = std::move(pixmap);
                    }
                }
            }
            games_.push_back(entry.game);
            AddItem(entry.game);
        }
        if (games_.empty()) {
            ShowEmptyTip();
        } else {
            HideEmptyTip();
        }
    }

    void TabGame::UpdateRunningStatus(const std::vector<uint64_t>& game_ids) {
        for (int index = 0; index < list_widget_->count(); ++index) {
            const QPointer<QWidget> item_widget(
                list_widget_->itemWidget(list_widget_->item(index)));
            if (!item_widget) {
                continue;
            }
            const QPointer<CoverWidget> cover_widget(
                item_widget->findChild<CoverWidget*>("cover_mask"));
            if (!cover_widget) {
                continue;
            }
            const auto game_id = item_widget->objectName().toStdString();
            const auto running = std::ranges::any_of(
                game_ids,
                [&game_id](std::uint64_t running_game_id) {
                    return std::to_string(running_game_id) == game_id;
                });
            cover_widget->SetRunningStatus(running);
        }
    }

    void TabGame::ShowContextMenu(const QPoint& position) {
        const auto index = list_widget_->indexAt(position).row();
        if (index < 0 || static_cast<std::size_t>(index) >= games_.size()) {
            return;
        }
        const auto game = games_.at(index);
        QMenu menu(this);
        const std::vector<QString> actions{
            tcTr("id_game_info"),
            tcTr("id_start_game"),
            tcTr("id_stop_game"),
            tcTr("id_installed_location"),
        };
        const QPointer<TabGame> self(this);
        for (int action_index = 0;
             action_index < static_cast<int>(actions.size());
             ++action_index) {
            const QPointer<QAction> action(menu.addAction(actions.at(action_index)));
            QObject::connect(
                action, &QAction::triggered, this,
                MakeQtLifetimeAction(
                    self,
                    [game, action_index](const QPointer<TabGame>& tab) {
                        switch (action_index) {
                        case 0: {
                            GameInfoPreview preview(tab->app_, game);
                            preview.setFixedSize(640, 480);
                            preview.exec();
                            break;
                        }
                        case 1:
                            tab->StartGame(game);
                            break;
                        case 2:
                            tab->StopGame(game);
                            break;
                        case 3:
                            FolderUtil::OpenDir(
                                PathFromUTF8(game->game_installed_dir_));
                            break;
                        default:
                            break;
                        }
                    }));
        }
        menu.exec(QCursor::pos());
    }

    void TabGame::StartGame(const TcDBGamePtr& game) {
        if (!game) {
            return;
        }
        if (!game->steam_url_.empty()) {
            if (!QDesktopServices::openUrl(
                    QUrl(QString::fromStdString(game->steam_url_)))) {
                ShowStartError(tcTr("id_start_failed"));
            }
            return;
        }
        if (game->exes_.empty()) {
            ShowStartError(tcTr("id_dont_have_exe"));
            return;
        }
        const auto executable_path = game->exes_.front();
        if (!QFile::exists(QString::fromStdString(executable_path))) {
            ShowStartError(tcTr("id_file_not_exist"));
            return;
        }

        const auto context = context_;
        const auto run_game_manager = context->GetRunGameManager();
        if (!run_game_manager) {
            ShowStartError(tcTr("id_start_failed"));
            return;
        }
        const QPointer<TabGame> self(this);
        context->PostTask(
            [context, run_game_manager, executable_path, self]() {
                LOGI("Will start: {}", executable_path);
                const auto response = run_game_manager->StartGame(
                    executable_path, {});
                if (!response.ok_) {
                    context->PostUITask([self]() {
                        if (self) {
                            self->ShowStartError(tcTr("id_start_failed"));
                        }
                    });
                }
            });
    }

    void TabGame::StopGame(const TcDBGamePtr& game) {
        if (!game) {
            return;
        }
        const auto run_game_manager = context_->GetRunGameManager();
        if (!run_game_manager) {
            return;
        }
        const auto game_id = std::to_string(game->game_id_);
        context_->PostTask([run_game_manager, game_id]() {
            static_cast<void>(run_game_manager->StopGame(game_id));
        });
    }

    void TabGame::ShowStartError(const QString& message) {
        const auto target_message = tcTr("id_start_process_error") + message;
        TcDialog dialog(tcTr("id_error"), target_message, this);
        dialog.exec();
    }

    void TabGame::ShowAddGamePanel() {
        AddGamePanel panel(context_, this);
        panel.exec();
        RefreshGames();
    }

    void TabGame::RefreshGames() {
        RequestCatalog(false);
    }

    void TabGame::resizeEvent(QResizeEvent *event) {
        int width = event->size().width();
        int height = event->size().height();
        empty_tip_->setGeometry((width - empty_tip_->width())/2, (height - empty_tip_->height())/2, empty_tip_->width(), empty_tip_->height());
    }

    void TabGame::ShowEmptyTip() {
        empty_tip_->show();
    }

    void TabGame::HideEmptyTip() {
        empty_tip_->hide();
    }

}
