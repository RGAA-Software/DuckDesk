//
// Created by RGAA on 2024-04-09.
//

#ifndef TC_SERVER_STEAM_TABGAME_H
#define TC_SERVER_STEAM_TABGAME_H

#include "tab_base.h"
#include "render_panel/database/db_game.h"
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPointer>

#include <cstdint>
#include <string>
#include <vector>

namespace px
{

    class SteamApp;
    class SteamManager;
    class GameCatalogRefreshState;

    struct GameCatalogEntry final {
        TcDBGamePtr game;
        std::string cover_path;
    };

    class TabGame : public TabBase {
    public:

        explicit TabGame(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~TabGame() override;

        void OnTabShow() override;
        void OnTabHide() override;

        void resizeEvent(QResizeEvent *event) override;

    private:
        void ScanInstalledGames();
        void RequestCatalog(bool scan_installed_games);
        void ApplyCatalog(
            std::uint64_t generation,
            std::vector<GameCatalogEntry> entries,
            bool final_result);
        void AddItem(const TcDBGamePtr& game);
        static QSize GetItemSize();
        void ReplaceItems(std::vector<GameCatalogEntry> entries);
        void UpdateRunningStatus(const std::vector<uint64_t>& game_ids);
        void ShowContextMenu(const QPoint& position);
        void StartGame(const TcDBGamePtr& game);
        void StopGame(const TcDBGamePtr& game);
        void ShowStartError(const QString& message);
        void ShowAddGamePanel();
        void RefreshGames();
        void ShowEmptyTip();
        void HideEmptyTip();

    private:
        QPointer<QListWidget> list_widget_;
        std::shared_ptr<SteamManager> steam_mgr_ = nullptr;
        std::shared_ptr<GameCatalogRefreshState> refresh_state_;
        std::vector<TcDBGamePtr> games_;
        QPointer<QLabel> empty_tip_;
    };

}

#endif //TC_SERVER_STEAM_TABGAME_H
