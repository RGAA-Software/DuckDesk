#include <gtest/gtest.h>

#include "../render_panel/devices/stream_item_display.h"
#include "../render_panel/devices/console_device_state.h"

TEST(StreamItemDisplay, ShowsManagedDeviceIdWithoutPersistedRelayEndpoint) {
    px_console::ConsoleStream item;
    item.connect_type_ = "console_device_ticket";
    item.remote_device_id_ = "600378210";
    item.stream_name_ = "D-16";

    EXPECT_FALSE(item.HasRelayInfo());
    EXPECT_EQ(px::StreamItemPrimaryText(item), "600 378 210");
}

TEST(StreamItemDisplay, KeepsExplicitDirectHostWhenDeviceIdIsAbsent) {
    px_console::ConsoleStream item;
    item.connect_type_ = "explicit_direct";
    item.stream_host_ = "10.0.0.90";

    EXPECT_EQ(px::StreamItemPrimaryText(item), "10.0.0.90");
}

TEST(StreamItemDisplay, KeepsConsoleApplicationName) {
    px_console::ConsoleStream item;
    item.connect_type_ = "console_app_ticket";
    item.stream_name_ = "Cloud App";
    item.remote_device_id_ = "600378210";

    EXPECT_EQ(px::StreamItemPrimaryText(item), "Cloud App");
}

TEST(StreamItemDisplay, RestoresConsoleOnlineStateAfterDatabaseReload) {
    auto online = std::make_shared<px_console::ConsoleStream>();
    online->connect_type_ = px::connection_policy::kConsoleDeviceTicket;
    online->remote_device_id_ = "600378210";

    auto offline = std::make_shared<px_console::ConsoleStream>();
    offline->connect_type_ = px::connection_policy::kConsoleDeviceTicket;
    offline->remote_device_id_ = "001190520";
    offline->console_online_ = true;

    std::vector<std::shared_ptr<px_console::ConsoleStream>> reloaded {online, offline};
    const px::ConsoleDeviceOnlineStates states {
        {"600378210", true},
        {"001190520", false},
    };

    px::ApplyConsoleDeviceOnlineStates(reloaded, states);

    EXPECT_TRUE(online->console_online_);
    EXPECT_FALSE(offline->console_online_);
}

TEST(StreamItemDisplay, DoesNotApplyConsoleStateToExplicitDirectCard) {
    auto direct = std::make_shared<px_console::ConsoleStream>();
    direct->connect_type_ = px::connection_policy::kExplicitDirect;
    direct->remote_device_id_ = "600378210";

    std::vector<std::shared_ptr<px_console::ConsoleStream>> streams {direct};
    px::ApplyConsoleDeviceOnlineStates(streams, {{"600378210", true}});

    EXPECT_FALSE(direct->console_online_);
}

TEST(StreamItemDisplay, ManagedDeviceOnlineUsesOnlyConsolePresence) {
    px_console::ConsoleStream item;
    item.connect_type_ = px::connection_policy::kConsoleDeviceTicket;
    item.direct_online_ = true;
    item.relay_online_ = true;

    EXPECT_FALSE(px::StreamItemIsOnline(item));
    item.console_online_ = true;
    EXPECT_TRUE(px::StreamItemIsOnline(item));
}

TEST(StreamItemDisplay, ExplicitDirectDeviceOnlineUsesReachabilityProbe) {
    px_console::ConsoleStream item;
    item.connect_type_ = px::connection_policy::kExplicitDirect;

    EXPECT_FALSE(px::StreamItemIsOnline(item));
    item.direct_online_ = true;
    EXPECT_TRUE(px::StreamItemIsOnline(item));
}
