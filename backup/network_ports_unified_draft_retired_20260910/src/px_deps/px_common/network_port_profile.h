#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "folder_util.h"

namespace px {

struct NetworkPortRange final {
    int start{};
    int end{};
};

struct NetworkPortProfile final {
    int console{};
    int desktop_render{};
    int turn{};
    int service{};
    int discovery{};
    int legacy_relay{};
    int media_http{};
    int media_rtmp{};
    NetworkPortRange apps{};
    NetworkPortRange rtc{};
    NetworkPortRange turn_relay{};

    [[nodiscard]] static NetworkPortProfile Parse(const nlohmann::json& value) {
        if (!value.is_object() || value.size() != 12) {
            throw std::runtime_error("network_ports.json: expected 12 profile fields");
        }
        const auto port = [](const nlohmann::json& item) -> int {
            if (!item.is_number_integer() || item < 1 || item > 65535) {
                throw std::runtime_error("network_ports.json: invalid port");
            }
            return item.get<int>();
        };
        const auto range = [&port](const nlohmann::json& item) -> NetworkPortRange {
            if (!item.is_object() || item.size() != 2) {
                throw std::runtime_error("network_ports.json: invalid range fields");
            }
            const NetworkPortRange result{port(item.at("start")), port(item.at("end"))};
            if (result.start > result.end) {
                throw std::runtime_error("network_ports.json: reversed range");
            }
            return result;
        };
        const NetworkPortProfile result{
            port(value.at("console")), port(value.at("desktop_render")), port(value.at("turn")), port(value.at("service")),
            port(value.at("discovery")), port(value.at("legacy_relay")), port(value.at("media_http")), port(value.at("media_rtmp")),
            range(value.at("apps")), range(value.at("rtc")), range(value.at("turn_relay"))};
        std::set<int> occupied{};
        const auto reserve = [&occupied](int number) {
            if (!occupied.insert(number).second) {
                throw std::runtime_error("network_ports.json: overlapping port");
            }
        };
        for (const int number : std::array{result.console, result.desktop_render, result.turn, result.service, result.discovery,
                                          result.legacy_relay, result.media_http, result.media_rtmp}) {
            reserve(number);
        }
        const auto& reserved = value.at("reserved");
        if (!reserved.is_array()) {
            throw std::runtime_error("network_ports.json: reserved must be an array");
        }
        for (const auto& number : reserved) {
            reserve(port(number));
        }
        for (const auto pool : std::array{result.apps, result.rtc, result.turn_relay}) {
            for (int number = pool.start; number <= pool.end; ++number) {
                reserve(number);
            }
        }
        return result;
    }

    [[nodiscard]] static std::optional<NetworkPortProfile> Load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return std::nullopt;
        }
        std::ifstream input{path};
        if (!input) {
            throw std::runtime_error("cannot read network_ports.json");
        }
        return Parse(nlohmann::json::parse(input));
    }

    [[nodiscard]] static std::optional<NetworkPortProfile> LoadBesideExecutable() {
        return Load(std::filesystem::path{FolderUtil::GetCurrentFolderPath()} / "network_ports.json");
    }
};

} // namespace px
