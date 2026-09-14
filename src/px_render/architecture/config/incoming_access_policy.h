#pragma once

namespace px {

enum class IncomingAccessProductKind {
    kDesktop,
    kGame,
    kWebView,
    kRdp,
};

[[nodiscard]] constexpr bool ResolveIncomingAccessEnabled(const IncomingAccessProductKind productKind,
                                                          const bool desktopRemoteAccessEnabled) {
    return productKind != IncomingAccessProductKind::kDesktop || desktopRemoteAccessEnabled;
}

} // namespace px
