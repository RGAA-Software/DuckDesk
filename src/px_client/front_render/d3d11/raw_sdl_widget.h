//
// Created by RGAA on 3/09/2025.
//

#ifndef GAMMARAYPREMIUM_RAW_SDL_WIDGET_H
#define GAMMARAYPREMIUM_RAW_SDL_WIDGET_H

#include <SDL2/SDL.h>
#include <SDL_syswm.h>
#include <memory>
#include "px_common/win32/d3d11_wrapper.h"

namespace px
{

    class RawImage;

    class RawSdlWidget {
    public:
        RawSdlWidget();
        void RefreshImage(const std::shared_ptr<RawImage>& image);
        void Init(int frame_width, int frame_height, ComPtr<ID3D11Device>,  ComPtr<ID3D11DeviceContext>);

    private:
        SDL_Window* window_ = nullptr;
        bool init_ = false;
    };

}

#endif //GAMMARAYPREMIUM_RAW_SDL_WIDGET_H
