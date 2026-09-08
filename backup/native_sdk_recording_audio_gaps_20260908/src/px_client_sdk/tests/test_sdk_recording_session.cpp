#include "sdk_recording_session.h"
#include "px_message.pb.h"
#include "px_common/uuid.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>

namespace px {
namespace {
using namespace std::chrono_literals;

RecordingSessionConfig Config() {
    // Lifecycle-only cases never deliver video, so no output file is created.
    return {.writer = {.dir = "unused-recording-lifecycle-directory"}};
}

std::shared_ptr<Message> Audio() {
    auto message = std::make_shared<Message>();
    message->set_type(kAudioFrame);
    auto& audio = *message->mutable_audio_frame();
    audio.set_samples(48000);
    audio.set_channels(2);
    audio.set_bits(16);
    audio.set_frame_size(960);
    audio.set_data("opus-test");
    return message;
}

TEST(SdkRecordingSession, InvalidConfigAndStopBeforeStartAreSafe) {
    EXPECT_FALSE(RecordingSession::Create({}));
    auto config = Config();
    config.monitor_count = 0;
    EXPECT_FALSE(RecordingSession::Create(config));
    const auto session = RecordingSession::Create(Config());
    session->Stop();
    session->Stop();
    EXPECT_TRUE(session->WaitFor(100ms));
    EXPECT_TRUE(session->Completion().get().error.empty());
    EXPECT_FALSE(session->Start());
}

TEST(SdkRecordingSession, StopDrainsAcceptedQueueBeforeCompleting) {
    const auto ready = std::make_shared<std::promise<void>>();
    const auto ready_future = ready->get_future();
    const auto release = std::make_shared<std::promise<void>>();
    const auto release_future = std::make_shared<std::shared_future<void>>(release->get_future().share());
    const auto session = RecordingSession::Create(Config(), {.started = [ready, release_future] {
                                                      ready->set_value();
                                                      static_cast<void>(release_future->wait_for(2s));
                                                  }});
    ASSERT_TRUE(session->Start());
    EXPECT_EQ(ready_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(session->Submit(Audio()), RecordingSubmitResult::kAccepted);
    EXPECT_EQ(session->Submit(Audio()), RecordingSubmitResult::kAccepted);
    session->Stop();
    EXPECT_EQ(session->Submit(Audio()), RecordingSubmitResult::kStopped);
    release->set_value();
    ASSERT_TRUE(session->WaitFor(2s));
    const auto result = session->Completion().get();
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.processed_packets, 2U);
    EXPECT_TRUE(result.directories.empty()); // No fake success directory before any video.
}

TEST(SdkRecordingSession, LateWorkCannotEnterTheNextRunAcrossRepeatedStarts) {
    for (int round{}; round < 10; ++round) {
        const auto first = RecordingSession::Create(Config());
        ASSERT_TRUE(first->Start());
        EXPECT_EQ(first->Submit(Audio()), RecordingSubmitResult::kAccepted);
        first->Stop();
        const auto next = RecordingSession::Create(Config());
        ASSERT_TRUE(next->Start());
        EXPECT_FALSE(first->Start());
        EXPECT_EQ(first->Submit(Audio()), RecordingSubmitResult::kStopped);
        EXPECT_EQ(next->Submit(Audio()), RecordingSubmitResult::kAccepted);
        next->Stop();
        ASSERT_TRUE(first->WaitFor(2s));
        ASSERT_TRUE(next->WaitFor(2s));
        EXPECT_EQ(first->Completion().get().processed_packets, 1U);
        EXPECT_EQ(next->Completion().get().processed_packets, 1U);
    }
}

TEST(SdkRecordingSession, UnknownVideoCodecDoesNotBecomeH264) {
    auto message = std::make_shared<Message>();
    message->set_type(kVideoFrame);
    auto& video = *message->mutable_video_frame();
    video.set_type(static_cast<VideoType>(99));
    video.set_data("invalid-codec-data");
    video.set_frame_width(64);
    video.set_frame_height(64);
    const auto session = RecordingSession::Create(Config());
    ASSERT_TRUE(session->Start());
    EXPECT_EQ(session->Submit(message), RecordingSubmitResult::kInvalid);
    ASSERT_TRUE(session->WaitFor(2s));
    EXPECT_EQ(session->Completion().get().error, "unsupported_recording_video");
}

TEST(SdkRecordingSession, AudioLayoutIsValidatedAndLossMarkersAreNotMuxed) {
    const auto session = RecordingSession::Create(Config());
    ASSERT_TRUE(session->Start());
    auto lost = Audio();
    lost->mutable_audio_frame()->clear_data();
    EXPECT_EQ(session->Submit(lost), RecordingSubmitResult::kIgnored);
    auto mono = Audio();
    mono->mutable_audio_frame()->set_channels(1);
    EXPECT_EQ(session->Submit(mono), RecordingSubmitResult::kInvalid);
    ASSERT_TRUE(session->WaitFor(2s));
    EXPECT_EQ(session->Completion().get().error, "unsupported_recording_audio");
}

TEST(SdkRecordingSession, QueueOverflowFailsInsteadOfSilentlyLosingEncodedVideo) {
    auto config = Config();
    config.queue_byte_limit = 1;
    const auto session = RecordingSession::Create(config);
    ASSERT_TRUE(session->Start());
    EXPECT_EQ(session->Submit(Audio()), RecordingSubmitResult::kQueueFull);
    ASSERT_TRUE(session->WaitFor(2s));
    EXPECT_EQ(session->Completion().get().error, "recording_queue_full");
}

TEST(SdkRecordingSession, OwnerCanStopAndDestroyItselfInsideStartedCallback) {
    struct Owner final {
        std::mutex mutex{};
        std::shared_ptr<RecordingSession> session{};
    };
    const auto owner = std::make_shared<Owner>();
    const auto finished = std::make_shared<std::promise<void>>();
    const auto done = finished->get_future();
    const auto callbacks = RecordingSessionCallbacks{
        .started =
            [weak_owner = std::weak_ptr<Owner>(owner)] {
                const auto locked = weak_owner.lock();
                if (!locked)
                    return;
                std::shared_ptr<RecordingSession> retiring{};
                {
                    std::lock_guard lock(locked->mutex);
                    retiring = std::move(locked->session);
                }
                if (retiring) {
                    retiring->Stop();
                    EXPECT_FALSE(retiring->WaitFor(1ms));
                    retiring.reset(); // Destruction on the worker must defer joining itself.
                }
            },
        .finished =
            [finished](const RecordingSessionResult& result) {
                EXPECT_TRUE(result.error.empty());
                finished->set_value();
            },
    };
    {
        std::lock_guard lock(owner->mutex);
        owner->session = RecordingSession::Create(Config(), callbacks);
        ASSERT_TRUE(owner->session->Start());
    }
    EXPECT_EQ(done.wait_for(2s), std::future_status::ready);
}

TEST(SdkRecordingSession, ThrowingCompletionStillResolvesAndDestructorDrains) {
    const auto callback_count = std::make_shared<std::atomic_int>(0);
    auto session = RecordingSession::Create(Config(), {.finished = [callback_count](const RecordingSessionResult&) {
                                                ++*callback_count;
                                                throw std::runtime_error("test callback");
                                            }});
    ASSERT_TRUE(session->Start());
    EXPECT_EQ(session->Submit(Audio()), RecordingSubmitResult::kAccepted);
    const auto completion = session->Completion();
    session.reset();
    ASSERT_EQ(completion.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(completion.get().processed_packets, 1U);
    EXPECT_EQ(callback_count->load(), 1);
}
struct RecordingTestDirectory final {
    const std::filesystem::path path{std::filesystem::temp_directory_path() / ("pixels-recording-test-" + GetUUID())};
    RecordingTestDirectory() {
        std::filesystem::create_directories(path);
    }
    ~RecordingTestDirectory() {
        std::error_code error{};
        std::filesystem::remove_all(path, error);
    }
};

std::shared_ptr<Message> Keyframe() {
    // Generated black 64x64 H.264 keyframe (libx264); the informational SEI is omitted.
    constexpr std::array<std::uint8_t, 59> bytes{0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xda, 0x10, 0x9b, 0x01, 0x10, 0x00, 0x00,
                                                 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x03, 0xc8, 0xf1, 0x22, 0x6a, 0x00, 0x00, 0x00, 0x01,
                                                 0x68, 0xce, 0x0f, 0xc8, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x3a, 0x26, 0x28, 0x00, 0x09,
                                                 0x02, 0xc9, 0xc9, 0xc9, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x75, 0xe0};
    auto message = std::make_shared<Message>();
    message->set_type(kVideoFrame);
    auto& video = *message->mutable_video_frame();
    video.set_type(kNetH264);
    video.set_frame_width(64);
    video.set_frame_height(64);
    video.set_key(true);
    video.set_mon_index(2);                     // A single selected remote monitor need not be mon0.
    video.set_data(bytes.data(), bytes.size()); // NOLINT(gammaray-raw-pointer-boundary): synchronous protobuf copy.
    return message;
}

TEST(SdkRecordingSession, ConcurrentRunsFinalizeDifferentRealMp4Files) {
    const RecordingTestDirectory directory{};
    auto config = Config();
    config.writer.dir = directory.path.string();
    config.writer.max_file_count = 0;
    const auto first = RecordingSession::Create(config);
    const auto second = RecordingSession::Create(config);
    ASSERT_TRUE(first->Start());
    ASSERT_TRUE(second->Start());
    EXPECT_EQ(first->Submit(Keyframe()), RecordingSubmitResult::kAccepted);
    EXPECT_EQ(second->Submit(Keyframe()), RecordingSubmitResult::kAccepted);
    first->Stop();
    second->Stop();
    ASSERT_TRUE(first->WaitFor(2s));
    ASSERT_TRUE(second->WaitFor(2s));
    EXPECT_TRUE(first->Completion().get().error.empty()) << first->Completion().get().error;
    EXPECT_TRUE(second->Completion().get().error.empty()) << second->Completion().get().error;
    EXPECT_EQ(first->Completion().get().directories.size(), 1U);
    EXPECT_EQ(second->Completion().get().directories.size(), 1U);
    std::size_t count{};
    for (const auto& entry : std::filesystem::directory_iterator(directory.path)) {
        EXPECT_EQ(entry.path().extension(), ".mp4");
        EXPECT_GT(entry.file_size(), 500U);
        ++count;
    }
    EXPECT_EQ(count, 2U);
}

TEST(SdkRecordingSession, UnwritableOutputCannotReportSuccess) {
    const RecordingTestDirectory directory{};
    const auto blocker = directory.path / "not-a-directory";
    std::ofstream(blocker).close();
    auto config = Config();
    config.writer.dir = blocker.string();
    const auto session = RecordingSession::Create(config);
    ASSERT_TRUE(session->Start());
    EXPECT_EQ(session->Submit(Keyframe()), RecordingSubmitResult::kAccepted);
    session->Stop();
    ASSERT_TRUE(session->WaitFor(2s));
    EXPECT_EQ(session->Completion().get().error, "recording_file_open_failed");
    EXPECT_TRUE(session->Completion().get().directories.empty());
}

TEST(SdkRecordingSession, WaitingForKeyframeCannotReportAPlayableRecording) {
    const RecordingTestDirectory directory{};
    auto config = Config();
    config.writer.dir = directory.path.string();
    auto video = Keyframe();
    video->mutable_video_frame()->set_key(false);
    const auto session = RecordingSession::Create(config);
    ASSERT_TRUE(session->Start());
    EXPECT_EQ(session->Submit(video), RecordingSubmitResult::kAccepted);
    session->Stop();
    ASSERT_TRUE(session->WaitFor(2s));
    EXPECT_EQ(session->Completion().get().error, "recording_no_keyframe");
    EXPECT_TRUE(session->Completion().get().directories.empty());
    EXPECT_TRUE(std::filesystem::is_empty(directory.path));
}
} // namespace
} // namespace px
