//
// Created by RGAA on 2024/2/2.
//

#ifndef TC_APPLICATION_DYNAMIC_LIBRARY_H
#define TC_APPLICATION_DYNAMIC_LIBRARY_H

#include <string>
#include <Windows.h>

namespace tc {

class DynamicLibrary {
public:
    explicit DynamicLibrary(const std::wstring& path);
    ~DynamicLibrary();

    bool Load();
    void* GetSymbol(const std::string& name);
    std::string GetErrorString();
    bool IsLoaded() const;

private:
    std::wstring path_;
    HMODULE handle_ = NULL;
};

} // namespace tc

#endif //TC_APPLICATION_DYNAMIC_LIBRARY_H
