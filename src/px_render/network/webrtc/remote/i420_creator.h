#ifndef I420_CREATOR_H_
#define I420_CREATOR_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <thread>

namespace px
{

    class I420Creator {
    public:
        using I420Frame = std::shared_ptr<std::vector<uint8_t>>;
        using I420FrameObserver = std::function<void(I420Frame)>;
    public:
        explicit I420Creator(I420FrameObserver&& observer);

        ~I420Creator();

        void set_resolution(int w, int h) {
            state_->width = w;
            state_->height = h;
        }

        void run(int fps = 30);

    private:
        struct State {
            int width = 0;
            int height = 0;
            I420FrameObserver observer;
            std::atomic_bool running{false};
            I420Frame frame;
        };

        static I420Frame ReadI420File(const std::shared_ptr<State>& state);

        std::shared_ptr<State> state_;
        std::thread thread_;
    };

}

#endif
