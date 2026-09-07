//
// Created by RGAA on 26/07/2025.
//

#ifndef GAMMARAYPREMIUM_GR_RENDER_MSG_PROCESSOR_H
#define GAMMARAYPREMIUM_GR_RENDER_MSG_PROCESSOR_H

#include <memory>

namespace px
{
    class PxContext;
    class Message;

    class PxRenderMsgProcessor {
    public:
        explicit PxRenderMsgProcessor(const std::shared_ptr<PxContext>& ctx);
        void OnMessage(std::shared_ptr<px::Message> msg) const;

    private:
        std::weak_ptr<PxContext> context_;
    };

}

#endif //GAMMARAYPREMIUM_GR_RENDER_MSG_PROCESSOR_H
