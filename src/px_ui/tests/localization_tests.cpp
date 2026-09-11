#include "px_ui/localization.h"

int main() {
    if (!px::ui::CatalogsAreComplete()) {
        return 1;
    }

    px::ui::Localizer localizer{};
    if (localizer.Text(px::ui::TextId::Settings).empty()) {
        return 2;
    }
    localizer.SetLanguage(px::ui::Language::English);
    if (localizer.Text(px::ui::TextId::Settings) != "Settings") {
        return 3;
    }
    return 0;
}
