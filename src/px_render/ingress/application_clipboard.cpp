#include "application_clipboard.h"

#include <utility>

namespace px {
namespace {
constexpr std::size_t kMaximumTextBytes{1024U * 1024U};
}

ApplicationClipboard::ApplicationClipboard(std::unique_ptr<clipboard::IPlatform> platform) : platform_{std::move(platform)} {}

bool ApplicationClipboard::WriteRemoteText(const std::string& text) {
    if (text.empty() || text.size() > kMaximumTextBytes) return false;
    const std::scoped_lock lock{mutex_};
    if (!platform_ || !platform_->WriteText(text)) return false;
    observed_text_ = text;
    return true;
}

std::optional<std::string> ApplicationClipboard::ReadChangedText() {
    const std::scoped_lock lock{mutex_};
    clipboard::Content content{};
    if (!platform_ || !platform_->Read(content)) return std::nullopt;
    if (observed_text_ && *observed_text_ == content.text_) return std::nullopt;
    observed_text_ = content.text_;
    if (content.text_.empty() || content.text_.size() > kMaximumTextBytes) return std::nullopt;
    return content.text_;
}

void ApplicationClipboard::Refresh() {
    const std::scoped_lock lock{mutex_};
    observed_text_.reset();
}

}  // namespace px
