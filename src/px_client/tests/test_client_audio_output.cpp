#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <future>
#include <memory>
#include <span>
#include <thread>

#include "client_audio_output.h"
#include "px_common/data.h"

namespace px::client::imgui {
namespace {

class ExistingAudioReference final {
public:
    ExistingAudioReference() = default;
    ExistingAudioReference(const ExistingAudioReference&) = delete;
    ExistingAudioReference& operator=(const ExistingAudioReference&) = delete;
    ~ExistingAudioReference() { Stop(); }

    bool Start() {
        if (initialized_) return true;
        initialized_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
        return initialized_;
    }

    void Stop() {
        if (initialized_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_ = false;
    }

private:
    bool initialized_{};
};

std::shared_ptr<px::Data> SilenceFrame() {
    const std::array<char, 3840> silence{};
    return px::Data::Copy(std::span<const char>{silence});
}

TEST(ClientAudioOutputTest, StartsAudioAndResumesAfterRepeatedStop) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    ASSERT_EQ(SDL_WasInit(SDL_INIT_AUDIO), 0U);
    {
        ClientAudioOutput output{};
        EXPECT_FALSE(output.Write(SilenceFrame(), 48000, 2, 16));
        ASSERT_TRUE(output.Start());
        ASSERT_TRUE(output.Start());
        EXPECT_NE(SDL_WasInit(SDL_INIT_AUDIO), 0U);
        EXPECT_FALSE(output.Write(SilenceFrame(), 48000, 2, 32));
        ASSERT_TRUE(output.Write(SilenceFrame(), 48000, 2, 16));
        output.Stop();
        output.Stop();
        ASSERT_TRUE(output.Write(SilenceFrame(), 48000, 2, 16));
    }
    EXPECT_EQ(SDL_WasInit(SDL_INIT_AUDIO), 0U);
}

TEST(ClientAudioOutputTest, ReleasesOnlyItsOwnAudioSubsystemReference) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    ExistingAudioReference existingReference{};
    ASSERT_TRUE(existingReference.Start());
    {
        ClientAudioOutput output{};
        ASSERT_TRUE(output.Start());
        ASSERT_TRUE(output.Write(SilenceFrame(), 48000, 2, 16));
    }
    EXPECT_NE(SDL_WasInit(SDL_INIT_AUDIO), 0U);
    existingReference.Stop();
    EXPECT_EQ(SDL_WasInit(SDL_INIT_AUDIO), 0U);
}

TEST(ClientAudioOutputTest, QueuedWriterCannotReviveDestroyedOutput) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    auto output = std::make_shared<ClientAudioOutput>();
    ASSERT_TRUE(output->Start());
    const std::weak_ptr<ClientAudioOutput> observer{output};
    auto releaseWriter = std::make_shared<std::promise<void>>();
    auto writerResult = std::make_shared<std::promise<bool>>();
    auto result = writerResult->get_future();
    std::jthread writer{[observer, releaseWriter, writerResult] {
        releaseWriter->get_future().wait();
        const auto lockedOutput = observer.lock();
        writerResult->set_value(lockedOutput && lockedOutput->Write(SilenceFrame(), 48000, 2, 16));
    }};
    output.reset();
    releaseWriter->set_value();
    EXPECT_FALSE(result.get());
    EXPECT_EQ(SDL_WasInit(SDL_INIT_AUDIO), 0U);
}

TEST(ClientAudioOutputTest, FailedInitializationCanRetryWithoutLeakingAReference) {
    ASSERT_EQ(SDL_WasInit(SDL_INIT_AUDIO), 0U);
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "pixels-unavailable-test-driver"));
    {
        ClientAudioOutput output{};
        EXPECT_FALSE(output.Start());
        EXPECT_EQ(SDL_WasInit(SDL_INIT_AUDIO), 0U);
        output.Stop();
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
        ASSERT_TRUE(output.Start());
        EXPECT_TRUE(output.Write(SilenceFrame(), 48000, 2, 16));
    }
    EXPECT_EQ(SDL_WasInit(SDL_INIT_AUDIO), 0U);
}

}  // namespace
}  // namespace px::client::imgui
