//
// Created by RGAA on 6/08/2025.
//

#include "panel_companion_impl.h"

void* GetInstance() {
    static tc::PanelCompanionImpl impl;
    return (void*)&impl;
}

namespace tc
{

    PanelCompanionImpl::~PanelCompanionImpl() {

    }

}