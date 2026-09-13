#pragma once

#include "px_ui/layout_metrics.h"

namespace px::panel::ui::layout {

inline float NavigationWidth() noexcept {
    return px::ui::Scale(200.0F);
}

inline float NavigationGap() noexcept {
    return px::ui::Scale(27.0F);
}

inline float PageBottomInset() noexcept {
    return px::ui::Scale(15.0F);
}

inline float SectionGap() noexcept {
    return px::ui::Scale(14.0F);
}

inline float CardGap() noexcept {
    return px::ui::Scale(10.0F);
}

inline float CompactCardHeight() noexcept {
    return px::ui::Scale(60.0F);
}

} // namespace px::panel::ui::layout
