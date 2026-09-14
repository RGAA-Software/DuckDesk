#pragma once

#include <imgui.h>

#include <string_view>

namespace px::ui {

// Pixels uses the 24 x 24 Lucide icon geometry as its cross-platform icon
// vocabulary. Drawing the vectors directly keeps them crisp on every DPI and
// avoids renderer-specific SVG textures in Dear ImGui.
enum class VectorIcon {
    Minimize,
    Maximize,
    Restore,
    Close,
    Monitor,
    Gamepad,
    Globe,
    Panels,
    Cloud,
    Activity,
    Shield,
    Settings,
    LogOut,
    Eye,
    EyeOff,
    Copy,
    QrCode,
    ExternalLink,
    Refresh,
    Restart,
    Power,
    Connect,
    Play,
    Stop,
    More,
    Pencil,
    User,
    List,
    Search,
    Folder,
    File,
    Home,
    ArrowLeft,
    ArrowUp,
    Upload,
    Download,
    FileTransfer,
    Trash,
    Check,
    ChevronLeft,
    ChevronRight,
    Plus,
    Minus,
    Camera,
    Video,
    Phone,
    PhoneOff,
    Microphone,
    MicrophoneOff,
    Volume,
    VolumeOff,
    Languages,
    Palette,
    CircleCheck,
    Info,
    TriangleAlert,
};

void DrawVectorIcon(VectorIcon icon, ImVec2 topLeft, float size, ImU32 color, float thickness = 1.8F);
void DrawVectorIcon(ImDrawList& draw, VectorIcon icon, ImVec2 topLeft, float size, ImU32 color, float thickness = 1.8F);

bool IconButton(VectorIcon icon, std::string_view text, std::string_view id, ImVec2 size = {});
bool IconOnlyButton(VectorIcon icon, std::string_view id, std::string_view tooltip, ImVec2 size = {});

} // namespace px::ui
