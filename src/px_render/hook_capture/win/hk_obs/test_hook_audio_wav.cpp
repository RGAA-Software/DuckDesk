// Hook-path ONLY (二选一之 hook): force enable_hook_audio=1, inject px_graphics.dll,
// collect in-process AudioShare WAV. No PID process-loopback in this test.
//
// Usage (from build_official/dist):
//   test_hook_audio_wav.exe [seconds] [optional_exe_path]

#include <Windows.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "px_capture_new/capture_message.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/process_util.h"
#include "px_common_new/string_util.h"

using namespace tc;

namespace {

std::filesystem::path DistDir() {
    if (const char* env = std::getenv("DIST")) {
        return env;
    }
    wchar_t mod[MAX_PATH]{};
    GetModuleFileNameW(nullptr, mod, MAX_PATH);
    return std::filesystem::path(mod).parent_path();
}

std::wstring DefaultGamePath() {
    return L"D:\\1_test_games\\CarGame  汽车\\CarGame\\Binaries\\Win64\\VehicleGame-Win64-Shipping.exe";
}

bool WriteBootForceAudioHook(uint32_t pid, uint32_t ipc_port) {
    AppSharedMessage msg{};
    msg.type_ = kCaptureHelloMessage;
    msg.ipc_port_ = ipc_port;
    msg.self_size_ = sizeof(AppSharedMessage);
    // Events on (production-like). GetForegroundWindow falls back until hwnd is
    // known so games that mute on focus-loss still produce audio.
    msg.enable_hook_events_ = 1;
    msg.enable_hook_audio_ = 1;  // force in-process WASAPI/XAudio2 hook

    auto dir = std::filesystem::path(FolderUtil::GetProgramDataPath()) / L"hook_boot";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = dir / std::format(L"application_{}.bin", pid);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::wcerr << L"Failed to write boot: " << path.wstring() << L"\n";
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(&msg), sizeof(msg));
    std::wcout << L"Wrote boot (enable_hook_audio=1): " << path.wstring() << L"\n";
    return static_cast<bool>(ofs);
}

struct StartedGame {
    uint32_t pid = 0;
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
};

StartedGame StartGameSuspended(const std::wstring& game_path) {
    StartedGame g{};
    std::wstring cmd = L"\"" + game_path + L"\"";
    std::wstring work = std::filesystem::path(game_path).parent_path().wstring();
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back(L'\0');
    if (!CreateProcessW(game_path.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, work.c_str(), &si, &pi)) {
        std::cerr << "CreateProcessW SUSPENDED failed: " << GetLastError() << "\n";
        return g;
    }
    g.pid = pi.dwProcessId;
    g.process = pi.hProcess;
    g.thread = pi.hThread;
    std::wcout << L"Started SUSPENDED pid=" << g.pid << L"\n";
    return g;
}

bool InjectGraphics(uint32_t pid, const std::filesystem::path& dist) {
    auto injector = dist / "px_graphics_util.exe";
    auto dll = dist / "px_graphics.dll";
    if (!std::filesystem::exists(injector) || !std::filesystem::exists(dll)) {
        std::cerr << "Missing injector/dll under " << dist.string() << "\n";
        return false;
    }
    std::vector<std::string> args{
        StringUtil::ToUTF8(dll.wstring()),
        "0",
        std::to_string(pid),
    };
    std::cout << "Inject: " << injector.string() << " -> pid " << pid << "\n";
    return ProcessUtil::StartProcessAndWait(StringUtil::ToUTF8(injector.wstring()), args);
}

std::filesystem::path WavPath(uint32_t pid) {
    return std::filesystem::path(FolderUtil::GetProgramDataPath()) /
           std::format(L"hook_audio_{}.wav", pid);
}

// Steal OS focus away from the game (multi-open / background scenario).
void StealFocusAwayFromGame(uint32_t game_pid) {
    // Create a tiny top-level window and force it foreground.
    static HWND decoy = nullptr;
    if (!decoy) {
        decoy = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"focus-decoy",
                                WS_POPUP | WS_VISIBLE, 0, 0, 80, 40, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    }
    if (decoy) {
        ShowWindow(decoy, SW_SHOW);
        SetForegroundWindow(decoy);
        SetActiveWindow(decoy);
    }
    // Also try to focus any non-game visible window.
    EnumWindows(
        [](HWND w, LPARAM lp) -> BOOL {
            DWORD wpid = 0;
            GetWindowThreadProcessId(w, &wpid);
            if (wpid == static_cast<DWORD>(lp) || !IsWindowVisible(w)) {
                return TRUE;
            }
            if (GetWindow(w, GW_OWNER)) {
                return TRUE;
            }
            SetForegroundWindow(w);
            return FALSE;
        },
        static_cast<LPARAM>(game_pid));
}

double MeanDbFs(const std::filesystem::path& wav) {
    std::ifstream f(wav, std::ios::binary);
    if (!f) {
        return -120.0;
    }
    f.seekg(0, std::ios::end);
    const auto len = static_cast<std::streamoff>(f.tellg());
    if (len <= 44) {
        return -120.0;
    }
    f.seekg(44);
    const size_t samples = static_cast<size_t>(len - 44) / sizeof(int16_t);
    std::vector<int16_t> pcm(samples);
    f.read(reinterpret_cast<char*>(pcm.data()),
           static_cast<std::streamsize>(samples * sizeof(int16_t)));
    double sum_sq = 0;
    for (size_t i = 0; i < samples; i++) {
        const double s = pcm[i] / 32768.0;
        sum_sq += s * s;
    }
    if (samples == 0) {
        return -120.0;
    }
    const double rms = std::sqrt(sum_sq / static_cast<double>(samples));
    if (rms < 1e-9) {
        return -91.0;
    }
    return 20.0 * std::log10(rms);
}

void PatchWavHeader(const std::filesystem::path& wav) {
    std::fstream f(wav, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) {
        return;
    }
    f.seekg(0, std::ios::end);
    const auto len = static_cast<uint32_t>(f.tellg());
    if (len < 44) {
        return;
    }
    const uint32_t riff = len - 8;
    const uint32_t data = len - 44;
    f.seekp(4, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&riff), 4);
    f.seekp(40, std::ios::beg);
    f.write(reinterpret_cast<const char*>(&data), 4);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    int seconds = 20;
    if (argc >= 2) {
        seconds = std::max(12, _wtoi(argv[1]));
    }
    auto dist = DistDir();
    std::wstring game = DefaultGamePath();
    if (argc >= 3) {
        game = argv[2];
    }
    if (!std::filesystem::exists(game)) {
        std::wcerr << L"Game not found: " << game << L"\n";
        return 1;
    }

    std::cout << "Dist: " << dist.string() << "\n";
    std::wcout << L"Target: " << game << L"\n";
    std::cout << "Mode: HOOK only (enable_hook_audio=1), background focus (no SetForeground)\n";

    system("taskkill /F /IM VehicleGame-Win64-Shipping.exe >nul 2>nul");
    system("taskkill /F /IM px_audio_pid_capture.exe >nul 2>nul");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto g = StartGameSuspended(game);
    if (g.pid == 0) {
        return 2;
    }

    if (!WriteBootForceAudioHook(g.pid, 32000)) {
        TerminateProcess(g.process, 1);
        CloseHandle(g.thread);
        CloseHandle(g.process);
        return 3;
    }

    if (!InjectGraphics(g.pid, dist)) {
        std::cerr << "Inject failed\n";
        TerminateProcess(g.process, 1);
        CloseHandle(g.thread);
        CloseHandle(g.process);
        return 4;
    }

    std::cout << "Waiting 2s for in-process audio hooks...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (ResumeThread(g.thread) == static_cast<DWORD>(-1)) {
        std::cerr << "ResumeThread failed: " << GetLastError() << "\n";
    } else {
        std::cout << "Resumed game\n";
    }

    const auto wav = WavPath(g.pid);
    std::error_code ec;
    std::filesystem::remove(wav, ec);
    std::wcout << L"Recording " << seconds << L"s -> " << wav.wstring() << L"\n";

    for (int i = 0; i < seconds; i++) {
        // Deliberately keep OS focus OFF the game — focus spoof inside the
        // inject must keep audio playing for multi-open / background cases.
        StealFocusAwayFromGame(g.pid);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (std::filesystem::exists(wav)) {
            std::cout << "t=" << i + 1 << "s wav_size=" << std::filesystem::file_size(wav)
                      << " (unfocused)\n";
        } else {
            std::cout << "t=" << i + 1 << "s wav=missing (unfocused)\n";
        }
    }

    TerminateProcess(g.process, 0);
    WaitForSingleObject(g.process, 3000);
    CloseHandle(g.thread);
    CloseHandle(g.process);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (!std::filesystem::exists(wav)) {
        std::cerr << "FAIL: hook wav not created. Check "
                  << (dist / "px_graphics_32000.log").string() << "\n";
        return 5;
    }

    PatchWavHeader(wav);
    const auto sz = std::filesystem::file_size(wav);
    const double db = MeanDbFs(wav);
    std::wcout << L"DONE: " << wav.wstring() << L" bytes=" << sz << L" mean_dbfs=" << db << L"\n";

    auto out = std::filesystem::path(FolderUtil::GetProgramDataPath()) / L"hook_audio_capture.wav";
    std::filesystem::copy_file(wav, out, std::filesystem::copy_options::overwrite_existing, ec);

    if (sz < 44 + 48000) {
        std::cerr << "FAIL: wav too small\n";
        return 6;
    }
    if (db < -55.0) {
        std::cerr << "FAIL: hook audio too quiet (mean_dbfs=" << db << ")\n";
        return 7;
    }
    std::cout << "PASS (hook audio while game NOT OS-focused)\n";
    return 0;
}
