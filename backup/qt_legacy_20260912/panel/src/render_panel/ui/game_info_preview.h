//
// Created by RGAA  on 2024/5/20.
//

#ifndef PX_GAME_INFO_PREVIEW_H
#define PX_GAME_INFO_PREVIEW_H

#include <QWidget>
#include <QDialog>

namespace px
{

    class TcDBGame;
    class PxApplication;

    class GameInfoPreview : public QDialog {
    public:

        GameInfoPreview(const std::shared_ptr<PxApplication>& app, const std::shared_ptr<TcDBGame>& game, QWidget* parent = nullptr);
        ~GameInfoPreview() = default;

    };

}

#endif //PX_GAME_INFO_PREVIEW_H
