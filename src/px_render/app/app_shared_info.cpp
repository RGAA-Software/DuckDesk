//
// Created by RGAA on 2024/3/17.
//
// Boot config for the injected capture DLL. Intentionally NOT shared-memory:
// frame IPC uses plain WebSocket (/ipc). This file only carries port + DXGI
// offsets so the DLL knows where to connect.

#include "app_shared_info.h"

#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include "px_common_new/folder_util.h"
#include "px_common_new/log.h"
#include "px_common_new/string_util.h"

namespace tc
{
    namespace {

        std::filesystem::path HookBootDir() {
            return std::filesystem::path(FolderUtil::GetProgramDataPath()) / L"hook_boot";
        }

        // hook_boot lives under C:\Users\Public — any local user could otherwise read
        // the boot file, steal the /ipc token and forge /ipc connections. Replace the
        // inherited DACL with a protected one: only the current user / SYSTEM /
        // Administrators can access the file.
        void RestrictBootFileAcl(const std::filesystem::path& path) {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                LOGW("RestrictBootFileAcl: OpenProcessToken failed, err={}", GetLastError());
                return;
            }
            DWORD len = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &len);
            std::vector<BYTE> buf(len);
            const bool got_user = len > 0 && GetTokenInformation(token, TokenUser, buf.data(), len, &len);
            CloseHandle(token);
            if (!got_user) {
                LOGW("RestrictBootFileAcl: GetTokenInformation failed, err={}", GetLastError());
                return;
            }
            auto* token_user = reinterpret_cast<TOKEN_USER*>(buf.data());
            LPWSTR sid_str = nullptr;
            if (!ConvertSidToStringSidW(token_user->User.Sid, &sid_str)) {
                LOGW("RestrictBootFileAcl: ConvertSidToStringSid failed, err={}", GetLastError());
                return;
            }
            // D:P = protected DACL (drops inherited ACEs, e.g. Everyone from Public).
            const std::wstring sddl =
                std::format(L"D:P(A;;FA;;;{})(A;;FA;;;SY)(A;;FA;;;BA)", sid_str);
            LocalFree(sid_str);
            PSECURITY_DESCRIPTOR sd = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
                LOGW("RestrictBootFileAcl: SDDL convert failed, err={}", GetLastError());
                return;
            }
            PACL dacl = nullptr;
            BOOL present = FALSE, defaulted = FALSE;
            GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted);
            const DWORD rc = SetNamedSecurityInfoW(
                const_cast<LPWSTR>(path.wstring().c_str()), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                nullptr, nullptr, present ? dacl : nullptr, nullptr);
            LocalFree(sd);
            if (rc != ERROR_SUCCESS) {
                LOGW("RestrictBootFileAcl: SetNamedSecurityInfo failed rc={} path={}",
                     rc, StringUtil::ToUTF8(path.wstring()));
            }
        }

    } // namespace

    std::filesystem::path AppSharedInfo::BootConfigPath(uint32_t pid) {
        return HookBootDir() / std::format(L"application_{}.bin", pid);
    }

    std::shared_ptr<AppSharedInfo> AppSharedInfo::Make(const std::shared_ptr<RdContext>& ctx) {
        return std::make_shared<AppSharedInfo>(ctx);
    }

    AppSharedInfo::AppSharedInfo(const std::shared_ptr<RdContext>& ctx) {
        context_ = ctx;
    }

    void AppSharedInfo::WriteData(const std::string& shm_name, const std::string& data) {
        // shm_name kept for call-site compatibility; expected form: application_shm_{pid}
        uint32_t pid = 0;
        auto pos = shm_name.rfind('_');
        if (pos != std::string::npos) {
            try {
                pid = static_cast<uint32_t>(std::stoul(shm_name.substr(pos + 1)));
            } catch (...) {
                pid = 0;
            }
        }
        if (pid == 0) {
            LOGE("Write hook boot config failed: cannot parse pid from {}", shm_name);
            return;
        }
        if (!WriteBootConfig(pid, data)) {
            LOGE("Write hook boot config failed for pid {}", pid);
        }
    }

    bool AppSharedInfo::WriteBootConfig(uint32_t pid, const std::string& data) {
        try {
            auto dir = HookBootDir();
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            auto path = BootConfigPath(pid);
            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                LOGE("Open hook boot file failed: {}", StringUtil::ToUTF8(path.wstring()));
                return false;
            }
            ofs.write(data.data(), static_cast<std::streamsize>(data.size()));
            ofs.close();
            if (!ofs) {
                LOGE("Write hook boot file failed: {}", StringUtil::ToUTF8(path.wstring()));
                return false;
            }
            RestrictBootFileAcl(path);
            LOGI("Wrote hook boot config (WS IPC bootstrap, not SHM): {} ({} bytes)",
                 StringUtil::ToUTF8(path.wstring()), data.size());
            return true;
        } catch (const std::exception& ex) {
            LOGE("WriteBootConfig exception: {}", ex.what());
            return false;
        }
    }

    void AppSharedInfo::Exit() {
        // Leave boot files for the injected DLL; cleaned up on next write/overwrite.
    }

}
