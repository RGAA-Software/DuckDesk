//
// Created by RGAA on 17/08/2024.
//

#ifndef GAMMARAYPC_FLOAT_3RD_SCALE_PANEL_H
#define GAMMARAYPC_FLOAT_3RD_SCALE_PANEL_H

#include "float_overlay_window.h"
#include <QPainter>
#include "px_client/ct_settings.h"

namespace px
{

    class Settings;
    class SingleSelectedList;

    class ThirdScalePanel : public FloatOverlayWindow {
    public:
        explicit ThirdScalePanel(const std::shared_ptr<ClientContext>& ctx, QWidget* parent = nullptr);
        void paintEvent(QPaintEvent *event) override;

    private:
        void UpdateScaleMode(ScaleMode mode);
        void UpdateStatus(const MsgClientFloatControllerPanelUpdate& msg) override;

    private:
        Settings* settings_ = nullptr;
        SingleSelectedList* listview_ = nullptr;
    };

}

#endif
