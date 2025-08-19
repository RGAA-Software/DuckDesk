//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_PROFILE_CONTEXT_H
#define GAMMARAYPREMIUM_GR_PROFILE_CONTEXT_H

#include <memory>

namespace tc
{

    class GrProfileDatabase;

    class GrProfileContext : public std::enable_shared_from_this<GrProfileContext> {
    public:
        GrProfileContext();
        bool Init();

        std::shared_ptr<GrProfileDatabase> GetDatabase();

    private:
        std::shared_ptr<GrProfileDatabase> db_ = nullptr;

    };

    extern std::shared_ptr<GrProfileContext> grProfileContext;

}

#endif //GAMMARAYPREMIUM_GR_PROFILE_CONTEXT_H
