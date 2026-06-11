#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace tc
{
    class Module {
    public:
        template<typename T>
        static T GetProcAddress(HMODULE module, LPCSTR proc_name) noexcept {
            return reinterpret_cast<T>(::GetProcAddress(module, proc_name));
        }

    public:
        Module() {}

        ~Module() { Free(); }

        DWORD Get(LPCTSTR name) noexcept {
            module_ = GetModuleHandle(name);
            if (nullptr == module_) {
                return GetLastError();
            }
            return NO_ERROR;
        }

        DWORD GetOrLoad(LPCTSTR name) noexcept {
            module_ = GetModuleHandle(name);
            if (nullptr == module_) {
                module_ = LoadLibrary(name);
                if (nullptr == module_) {
                    return GetLastError();
                }
                load_ = true;
            } else {
            }
            return NO_ERROR;
        }

        DWORD Load(LPCTSTR name) noexcept {
            module_ = LoadLibrary(name);
            if (nullptr == module_) {
                return GetLastError();
            }
            load_ = true;
            return NO_ERROR;
        }

        void Free() noexcept {
            if (nullptr != module_) {
                if (load_) {
                    FreeLibrary(module_);
                    load_ = false;
                }
                module_ = nullptr;
            }
        }

        HMODULE GetHandle() const noexcept {
            return module_;
        }

        operator HMODULE() const noexcept {
            return module_;
        }

        operator HANDLE() const noexcept {
            return module_;
        }

        bool IsLoad() const noexcept {
            return load_;
        }

        template<typename T>
        T GetProcAddress(LPCSTR proc_name) const noexcept {
            return reinterpret_cast<T>(::GetProcAddress(module_, proc_name));
        }

    private:
        bool load_ = false;
        HMODULE module_ = nullptr;
    };

}  // namespace tc