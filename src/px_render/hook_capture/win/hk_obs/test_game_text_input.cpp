#include "app/win/ipc_peer_identity.h"
#include "app/win/game_text_write_permit.h"
#include "game_text_input.h"
#include "window_message_key.h"
#include <gtest/gtest.h>

namespace px {
namespace {
TEST(GameWindowKeyboardState, QueuedSnapshotsDoNotReadTheLaterModifierRelease) {
    const auto pressed = WindowKeyboardSnapshot{.key = 'A', .down = true, .control = true}.Pack();
    const auto released = WindowKeyboardSnapshot{.key = VK_LCONTROL, .down = false}.Pack();
    EXPECT_TRUE(WindowKeyboardSnapshot::Unpack(pressed).control);
    EXPECT_FALSE(WindowKeyboardSnapshot::Unpack(released).control);
    std::array<BYTE, 256> original{};
    ASSERT_TRUE(GetKeyboardState(original.data()));
    for (int iteration{}; iteration < 20; ++iteration) {
        {
            const ScopedWindowKeyboardState outer{WindowKeyboardSnapshot::Unpack(pressed)};
            ASSERT_TRUE(outer.Active());
            std::array<BYTE, 256> state{};
            ASSERT_TRUE(GetKeyboardState(state.data()));
            EXPECT_NE(state[VK_LCONTROL] & 0x80, 0);
            {
                const ScopedWindowKeyboardState inner{WindowKeyboardSnapshot::Unpack(released)};
                ASSERT_TRUE(inner.Active());
                ASSERT_TRUE(GetKeyboardState(state.data()));
                EXPECT_EQ(state[VK_LCONTROL] & 0x80, 0);
            }
            ASSERT_TRUE(GetKeyboardState(state.data()));
            EXPECT_NE(state[VK_LCONTROL] & 0x80, 0);
        }
        std::array<BYTE, 256> restored{};
        ASSERT_TRUE(GetKeyboardState(restored.data()));
        EXPECT_EQ(restored, original);
    }
}

TEST(GameWindowMessageKey, NormalizesOnlySidedModifiersForWindowMessages) {
    EXPECT_EQ(WindowMessageKey(VK_LCONTROL), VK_CONTROL);
    EXPECT_EQ(WindowMessageKey(VK_RCONTROL), VK_CONTROL);
    EXPECT_EQ(WindowMessageKey(VK_LSHIFT), VK_SHIFT);
    EXPECT_EQ(WindowMessageKey(VK_RSHIFT), VK_SHIFT);
    EXPECT_EQ(WindowMessageKey(VK_LMENU), VK_MENU);
    EXPECT_EQ(WindowMessageKey(VK_RMENU), VK_MENU);
    EXPECT_EQ(WindowMessageKey(VK_CONTROL), VK_CONTROL);
    EXPECT_EQ(WindowMessageKey('A'), 'A');
    EXPECT_EQ(WindowMessageKey(VK_LEFT), VK_LEFT);
    EXPECT_EQ(WindowMessageKey(VK_LWIN), VK_LWIN);
}

struct TestWindowDeleter final {
    void operator()(HWND window) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) Owns only this test's Win32 window.
        if (window) {
            DestroyWindow(window);
        }
    }
};
using TestWindow = std::unique_ptr<HWND__, TestWindowDeleter>;

class GameTextInputTest : public testing::Test {
  protected:
    void SetUp() override {
        root_.reset(CreateWindowExW(0, L"STATIC", L"GammaRay isolated text fixture", WS_POPUP, 0, 0, 320, 200, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr));
        ASSERT_TRUE(root_);
        edit_.reset(CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_WANTRETURN, 0, 0, 320, 200, root_.get(), nullptr,
                                    GetModuleHandleW(nullptr), nullptr));
        ASSERT_TRUE(edit_);
        SetFocus(edit_.get());
        query_.request_id = 17;
        query_.target_pid = GetCurrentProcessId();
        query_.root_window = reinterpret_cast<std::uint64_t>(root_.get());
    }
    void TearDown() override {
        backend_.Reset();
    }
    CaptureTextReply Submit(std::string text) {
        const auto queried = backend_.Execute(query_);
        auto submit = query_;
        submit.operation = CaptureTextOperation::kSubmit;
        submit.expected_generation = queried.generation;
        submit.text = std::move(text);
        return backend_.Execute(submit);
    }
    std::wstring ReadText() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
        std::wstring text(32768, L'\0');
        const auto size = GetWindowTextW(edit_.get(), text.data(), static_cast<int>(text.size()));
        text.resize(static_cast<std::size_t>(size));
        return text;
    }
    std::wstring DrainQueuedCharacters() {
        MSG message{};
        std::wstring characters{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.hwnd == edit_.get() && message.message == WM_CHAR) {
                characters.push_back(static_cast<wchar_t>(message.wParam));
            }
            DispatchMessageW(&message);
        }
        return characters;
    }
    TestWindow root_{};
    TestWindow edit_{};
    HookGameTextInput backend_{};
    CaptureTextCommand query_{};
};

TEST_F(GameTextInputTest, UnicodeSurrogatePairAndSelectionReplacement) {
    const auto reply = Submit("\xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80");
    ASSERT_EQ(reply.status, CaptureTextStatus::kSubmitted);
    EXPECT_EQ(ReadText(), L"\x4e2d\x6587 \xd83d\xde00");
    SendMessageW(edit_.get(), EM_SETSEL, 0, -1);
    EXPECT_EQ(Submit("replacement").status, CaptureTextStatus::kSubmitted);
    EXPECT_EQ(ReadText(), L"replacement");
}

TEST_F(GameTextInputTest, RejectsForeignPidAndForeignRoot) {
    query_.target_pid ^= 0x80000000;
    EXPECT_EQ(backend_.Execute(query_).status, CaptureTextStatus::kUnavailable);
    query_.target_pid = GetCurrentProcessId();
    query_.root_window = reinterpret_cast<std::uint64_t>(GetDesktopWindow());
    EXPECT_EQ(backend_.Execute(query_).status, CaptureTextStatus::kUnavailable);
    EXPECT_TRUE(ReadText().empty());
}

TEST_F(GameTextInputTest, PreservesTabCrLfWithoutAppendingEnter) {
    EXPECT_EQ(Submit("A\tB\r\nC\nD").status, CaptureTextStatus::kSubmitted);
    EXPECT_EQ(DrainQueuedCharacters(), L"A\tB\r\nC\nD");
}

TEST_F(GameTextInputTest, RejectsDestroyedTargetWithoutWindowDiscovery) {
    ASSERT_EQ(backend_.Execute(query_).status, CaptureTextStatus::kReady);
    edit_.reset();
    EXPECT_EQ(backend_.Execute(query_).status, CaptureTextStatus::kUnavailable);
}

TEST_F(GameTextInputTest, TargetGenerationChangesWhenWindowRegistrationIsLost) {
    const auto first = backend_.Execute(query_);
    ASSERT_EQ(first.status, CaptureTextStatus::kReady);
    RemovePropW(edit_.get(), L"GammaRay.TextInput.TargetGeneration.v1");
    auto submit = query_;
    submit.operation = CaptureTextOperation::kSubmit;
    submit.expected_generation = first.generation;
    submit.text = "must not appear";
    EXPECT_EQ(backend_.Execute(submit).status, CaptureTextStatus::kTargetChanged);
    EXPECT_TRUE(ReadText().empty());
}

TEST_F(GameTextInputTest, RejectsInvalidUtf8NulAndControls) {
    for (const auto& text : {std::string{}, std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("a\0b", 3), std::string("\x1b")}) {
        EXPECT_EQ(Submit(text).status, CaptureTextStatus::kInvalidText);
    }
    EXPECT_EQ(Submit(std::string(kCaptureTextInputMaxBytes + 1, 'a')).status, CaptureTextStatus::kInvalidText);
    EXPECT_TRUE(ReadText().empty());
}

TEST_F(GameTextInputTest, LostFocusDoesNotFallbackToRoot) {
    ASSERT_EQ(backend_.Execute(query_).status, CaptureTextStatus::kReady);
    SetFocus(nullptr);
    EXPECT_EQ(backend_.Execute(query_).status, CaptureTextStatus::kUnavailable);
}

TEST_F(GameTextInputTest, RepeatedResetDoesNotLeaveOldGenerationAuthorized) {
    for (int attempt{}; attempt < 50; ++attempt) {
        const auto first = backend_.Execute(query_);
        backend_.Reset();
        backend_.Reset();
        auto submit = query_;
        submit.operation = CaptureTextOperation::kSubmit;
        submit.expected_generation = first.generation;
        submit.text = "old";
        EXPECT_EQ(backend_.Execute(submit).status, CaptureTextStatus::kTargetChanged);
    }
    EXPECT_TRUE(ReadText().empty());
}

TEST(CaptureTextWire, RoundTripAndRejectTruncatedOversizedAndWrongVersion) {
    const CaptureTextCommand command{CaptureTextOperation::kSubmit, 321, 654, 987, 11, "\xe4\xb8\xad\xe6\x96\x87"};
    const auto encoded = EncodeCaptureTextCommand(command);
    const auto decoded = DecodeCaptureTextCommand(encoded);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->text, command.text);
    EXPECT_EQ(decoded->root_window, command.root_window);
    EXPECT_EQ(decoded->expected_generation, command.expected_generation);
    for (std::size_t length{}; length < encoded.size(); ++length) {
        EXPECT_FALSE(DecodeCaptureTextCommand(std::string_view(encoded).substr(0, length)));
    }
    auto altered = encoded;
    altered[8] = 2;
    EXPECT_FALSE(DecodeCaptureTextCommand(altered));
    altered = encoded + "x";
    EXPECT_FALSE(DecodeCaptureTextCommand(altered));
    const CaptureTextReply reply{123, 456, CaptureTextStatus::kOutcomeUnknown, CaptureTextEditability::kUnknown, 0};
    const auto result = DecodeCaptureTextReply(EncodeCaptureTextReply(reply));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, reply.status);
    EXPECT_EQ(result->generation, reply.generation);
}

class TestSocket final {
  public:
    TestSocket() : socket_(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {}
    ~TestSocket() {
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
        }
    }
    TestSocket(const TestSocket&) = delete;
    TestSocket& operator=(const TestSocket&) = delete;
    [[nodiscard]] SOCKET Get() const {
        return socket_;
    }

  private:
    SOCKET socket_{INVALID_SOCKET};
};

TEST(CaptureTextPeerIdentity, UsesKernelTcpOwnerAndExactPorts) {
    WSADATA winsock{};
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &winsock), 0);
    struct WinsockCleanup final {
        ~WinsockCleanup() {
            WSACleanup();
        }
    } cleanup{};
    TestSocket server{};
    TestSocket client{};
    ASSERT_NE(server.Get(), INVALID_SOCKET);
    ASSERT_NE(client.Get(), INVALID_SOCKET);
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(server.Get(), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)), 0);
    ASSERT_EQ(listen(server.Get(), 1), 0);
    int size{sizeof(endpoint)};
    ASSERT_EQ(getsockname(server.Get(), reinterpret_cast<sockaddr*>(&endpoint), &size), 0);
    ASSERT_EQ(connect(client.Get(), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)), 0);
    sockaddr_in peer{};
    size = sizeof(peer);
    ASSERT_EQ(getsockname(client.Get(), reinterpret_cast<sockaddr*>(&peer), &size), 0);
    const auto pid = FindLoopbackTcpClientPid(ntohs(peer.sin_port), ntohs(endpoint.sin_port));
    ASSERT_TRUE(pid);
    EXPECT_EQ(*pid, GetCurrentProcessId());
    EXPECT_FALSE(FindLoopbackTcpClientPid(ntohs(peer.sin_port), 0));
}

TEST(GameTextWritePermit, QueuedWriteRechecksLeaseAndCancellation) {
    const auto authorized = std::make_shared<std::atomic_bool>(true);
    const auto permit = std::make_shared<GameTextWritePermit>([authorized] { return authorized->load(); });
    const auto queued_write = GameTextWritePermit::Observe(permit);
    ASSERT_TRUE(queued_write());
    authorized->store(false);
    EXPECT_FALSE(queued_write());
    authorized->store(true);
    EXPECT_TRUE(queued_write());
    permit->Cancel();
    EXPECT_FALSE(queued_write());
}

TEST(GameTextWritePermit, DestructionAndCallbackCancellationFailClosed) {
    auto permit = std::make_shared<GameTextWritePermit>([] { return true; });
    const auto queued_write = GameTextWritePermit::Observe(permit);
    permit.reset();
    EXPECT_FALSE(queued_write());
    const auto observed = std::make_shared<std::weak_ptr<GameTextWritePermit>>();
    permit = std::make_shared<GameTextWritePermit>([observed] {
        if (const auto owner = observed->lock()) {
            owner->Cancel();
        }
        return true;
    });
    *observed = permit;
    EXPECT_FALSE(GameTextWritePermit::Observe(permit)());
}
} // namespace
} // namespace px
