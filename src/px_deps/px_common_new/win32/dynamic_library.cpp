//
// Created by RGAA on 2024/2/2.
//

#include "dynamic_library.h"
#include <format>

namespace tc {

DynamicLibrary::DynamicLibrary(const std::wstring& path) : path_(path) {}

DynamicLibrary::~DynamicLibrary() {
    if (handle_) {
        FreeLibrary(handle_);
    }
}

bool DynamicLibrary::Load() {
    handle_ = LoadLibraryW(path_.c_str());
    return handle_ != NULL;
}

void* DynamicLibrary::GetSymbol(const std::string& name) {
    if (!handle_) {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(handle_, name.c_str()));
}

std::string DynamicLibrary::GetErrorString() {
    return std::format("LoadLibrary failed, error: {}", GetLastError());
}

bool DynamicLibrary::IsLoaded() const {
    return handle_ != NULL;
}

} // namespace tc
