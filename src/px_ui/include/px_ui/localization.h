#pragma once

#include <cstdint>
#include <string_view>

namespace px::ui {

enum class Language : std::uint8_t {
    English,
    SimplifiedChinese,
};

enum class TextId : std::uint8_t {
    RenderNodeConsole,
    RemoteControl,
    CloudApplications,
    ServerStatus,
    Security,
    Settings,
    Hardware,
    ExitPrograms,
    SettingsNetwork,
    ConnectionAddresses,
    Authorization,
    ResolvedControlEndpoints,
    Supervisor,
    NodeManagement,
    Relay,
    ReliableRoutedConnection,
    NodePublicAddress,
    OptionalPublicAddress,
    PublicAddressHint,
    NodeListeningPorts,
    ServiceManagementPort,
    ServiceManagementPurpose,
    DesktopConnectionPort,
    DesktopConnectionPurpose,
    ApplicationPortPool,
    ApplicationPortPurpose,
    RtcMediaPool,
    RtcPortPurpose,
    PanelListeningPort,
    PanelListeningPurpose,
    Save,
    PreviewInitialStatus,
    PreviewSavedStatus,
    English,
    SimplifiedChinese,
    DarkTheme,
    LightTheme,
    Count,
};

class Localizer final {
  public:
    explicit Localizer(Language language = Language::SimplifiedChinese) noexcept;

    void SetLanguage(Language language) noexcept;
    Language CurrentLanguage() const noexcept;
    std::string_view Text(TextId id) const noexcept;

  private:
    Language language_{Language::SimplifiedChinese};
};

bool CatalogsAreComplete() noexcept;

} // namespace px::ui
