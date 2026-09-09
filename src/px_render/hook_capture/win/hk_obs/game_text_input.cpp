#include "game_text_input.h"

#include <limits>
#include <string>
#include <imm.h>
#include <functional>

namespace px {
namespace {
constexpr std::wstring_view kWindowProperty{L"GammaRay.TextInput.TargetGeneration.v1"};
struct ImmContextDeleter final {
    std::reference_wrapper<const TextWindowRegistration> window;
    void operator()(HIMC context) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) Paired ImmGetContext/ImmReleaseContext ABI.
        ImmReleaseContext(window.get().get(), context);
    }
};

bool IsOwnWindow(HWND window) { // NOLINT(gammaray-raw-pointer-boundary) Transient Win32 HWND query; never retained here.
    DWORD process_id{};
    return window && IsWindow(window) && GetWindowThreadProcessId(window, &process_id) != 0 && process_id == GetCurrentProcessId();
}

std::optional<std::wstring> DecodeText(std::string_view text) {
    if (text.empty() || text.size() > kCaptureTextInputMaxBytes) {
        return std::nullopt;
    }
    for (const auto value : text) {
        const auto byte = static_cast<unsigned char>(value);
        if ((byte < 0x20 && byte != '\t' && byte != '\r' && byte != '\n') || byte == 0x7f) {
            return std::nullopt;
        }
    }
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length) != length) {
        return std::nullopt;
    }
    return result;
}
} // namespace

void TextWindowRegistrationDeleter::operator()(HWND window) const noexcept { // NOLINT(gammaray-raw-pointer-boundary) Win32 RAII cleanup.
    if (window) {
        RemovePropW(window, kWindowProperty.data());
    }
}

void HookGameTextInput::Reset() {
    std::lock_guard lock(mutex_);
    target_.reset();
    ++generation_;
    previous_imm_association_.reset();
    imm_hint_ = CaptureTextEditability::kUnknown;
}

CaptureTextReply HookGameTextInput::Execute(const CaptureTextCommand& command) {
    std::lock_guard lock(mutex_);
    CaptureTextReply reply{.request_id = command.request_id, .generation = generation_};
    if (target_ && !IsOwnWindow(target_.get())) {
        target_.reset();
        ++generation_;
        reply.generation = generation_;
        previous_imm_association_.reset();
        imm_hint_ = CaptureTextEditability::kUnknown;
        return reply;
    }
    if (command.target_pid != GetCurrentProcessId() || command.root_window == 0 || !IsOwnWindow(reinterpret_cast<HWND>(command.root_window))) {
        return reply;
    }

    // GUI thread data is transient Windows output. Never scan unrelated windows or
    // use this process's spoofed GetFocus/GetForegroundWindow fallback.
    GUITHREADINFO info{.cbSize = sizeof(GUITHREADINFO)};
    const auto thread_id = GetWindowThreadProcessId(reinterpret_cast<HWND>(command.root_window), nullptr);
    if (!GetGUIThreadInfo(thread_id, &info) || !IsOwnWindow(info.hwndFocus) ||
        (info.hwndFocus != reinterpret_cast<HWND>(command.root_window) && !IsChild(reinterpret_cast<HWND>(command.root_window), info.hwndFocus))) {
        if (target_) {
            target_.reset();
            ++generation_;
        }
        reply.generation = generation_;
        return reply;
    }

    const auto property_generation = reinterpret_cast<std::uintptr_t>(GetPropW(info.hwndFocus, kWindowProperty.data()));
    if (target_.get() != info.hwndFocus || property_generation != generation_) {
        target_.reset();
        ++generation_;
        if (generation_ == 0 || generation_ > std::numeric_limits<std::uintptr_t>::max() ||
            !SetPropW(info.hwndFocus, kWindowProperty.data(), reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(generation_)))) {
            reply.generation = generation_;
            return reply;
        }
        target_.reset(info.hwndFocus);
        previous_imm_association_.reset();
        imm_hint_ = CaptureTextEditability::kUnknown;
    }
    reply.generation = generation_;
    reply.status = CaptureTextStatus::kReady;
    const std::unique_ptr<HIMC__, ImmContextDeleter> imm(ImmGetContext(target_.get()), ImmContextDeleter{std::cref(target_)});
    const bool associated = static_cast<bool>(imm);
    if (previous_imm_association_ && *previous_imm_association_ != associated) {
        imm_hint_ = associated ? CaptureTextEditability::kEditable : CaptureTextEditability::kNotEditable;
    }
    previous_imm_association_ = associated;
    reply.editability = imm_hint_;
    // A real Win32 EDIT advertises text input; arbitrary game HWNDs remain Unknown.
    // Unknown is deliberately not a rejection and is not inferred from default IMM contexts.
    std::wstring window_class(256, L'\0');
    const auto class_length = GetClassNameW(target_.get(), window_class.data(), static_cast<int>(window_class.size()));
    window_class.resize(class_length > 0 ? static_cast<std::size_t>(class_length) : 0);
    if (_wcsicmp(window_class.c_str(), L"Edit") == 0 || window_class.starts_with(L"RICHEDIT") || window_class.starts_with(L"RichEdit")) {
        reply.editability = CaptureTextEditability::kEditable;
    }
    if (command.operation == CaptureTextOperation::kQuery) {
        return reply;
    }
    if (command.expected_generation == 0 || command.expected_generation != generation_) {
        reply.status = CaptureTextStatus::kTargetChanged;
        return reply;
    }
    const auto text = DecodeText(command.text);
    if (!text) {
        reply.status = CaptureTextStatus::kInvalidText;
        return reply;
    }
    std::size_t submitted{};
    for (const auto character : *text) {
        // Revalidate every UTF-16 unit. A partial post is never reported as retryable success.
        GUITHREADINFO current{.cbSize = sizeof(GUITHREADINFO)};
        if (!IsOwnWindow(target_.get()) || !GetGUIThreadInfo(thread_id, &current) || current.hwndFocus != target_.get() ||
            reinterpret_cast<std::uintptr_t>(GetPropW(target_.get(), kWindowProperty.data())) != generation_ ||
            !PostMessageW(target_.get(), WM_CHAR, static_cast<WPARAM>(character), 1)) {
            reply.status = submitted == 0 ? CaptureTextStatus::kUnavailable : CaptureTextStatus::kOutcomeUnknown;
            return reply;
        }
        ++submitted;
    }
    reply.status = CaptureTextStatus::kSubmitted;
    reply.accepted_bytes = static_cast<std::uint32_t>(command.text.size());
    return reply;
}
} // namespace px
