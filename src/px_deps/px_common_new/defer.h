//
// Created by RGAA on 18/11/2024.
//

#ifndef GAMMARAY_DEFER_H
#define GAMMARAY_DEFER_H

#include <memory>
#include <functional>

namespace tc
{

    using DeferInvoker = std::function<void()>;

    class Defer {
    public:

        static std::shared_ptr<Defer> Make(DeferInvoker&& invoker) {
            return std::make_shared<Defer>(std::move(invoker));
        }

        Defer(DeferInvoker&& invoker) {
            this->invoker_ = std::move(invoker);
        }

        ~Defer() {
            invoker_();
        }

    private:
        DeferInvoker invoker_;

    };

}

#endif //GAMMARAY_DEFER_H
