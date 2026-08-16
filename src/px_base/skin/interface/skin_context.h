//
// Created by RGAA on 19/11/2024.
//

#ifndef PX_SKIN_CONTEXT_H
#define PX_SKIN_CONTEXT_H

#include <QObject>
#include <functional>
#include <memory>
#include <string>

namespace px
{

    class SkinContext : public QObject {
    public:
        explicit SkinContext(const std::string& plugin_name);
        ~SkinContext() override = default;

        void OnDestroy();

    private:
    };

}

#endif //PX_PLUGIN_CONTEXT_H
