//
// Created by RGAA on 2024-01-18.
//

#include "hardware.h"

#include <iostream>
#include <format>
#include <Windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "tc_common_new/log.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/num_formatter.h"
#include "tc_common_new/shared_preference.h"

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "IPHLPAPI.lib")

namespace tc
{

    const std::string kKeyCpuName = "key_cpu_name";
    const std::string kKeyCpuCores = "key_cpu_cores";
    const std::string kKeyCpuId = "key_cpu_id";
    const std::string kKeyCpuMaxClock = "key_cpu_max_clock";

    int Hardware::Detect(bool read_cpu_info, bool disk, bool driver) {

        auto sp = SharedPreference::Instance();
        auto cpu_name = sp->Get(kKeyCpuName);
        if (cpu_name.empty()) {
            read_cpu_info = true;
        }

        wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        if (GetComputerNameW(computerName, &size)) {
            desktop_name_ = StringUtil::ToUTF8(computerName);
        }

        MEMORYSTATUS ms;
        ::GlobalMemoryStatus(&ms);
        memory_size_ = ms.dwTotalPhys;

        CoUninitialize();
        HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
        if (FAILED(hres)) {
            LOGE("COM library initialization failed.");
            return -1;
        }

        hres = CoInitializeSecurity(
                NULL,
                -1,
                NULL,
                NULL,
                RPC_C_AUTHN_LEVEL_DEFAULT,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                NULL,
                EOAC_NONE,
                NULL
        );
        if (FAILED(hres)) {
            if (hres == RPC_E_TOO_LATE) {
                LOGE("WQLHelper: CoInitializeSecurity already initialized, continuing... hres: {}",hres);
            }
            else {
                LOGE("WQLHelper: CoInitializeSecurity error hres: {}", hres);
                CoUninitialize();
                return -1;
            }
        }

        IWbemLocator *pLoc = NULL;
        hres = CoCreateInstance(
                CLSID_WbemLocator,
                0,
                CLSCTX_INPROC_SERVER,
                IID_IWbemLocator,
                (LPVOID *) &pLoc
        );
        if (FAILED(hres) || !pLoc) {
            LOGE("Failed to create IWbemLocator object.");
            CoUninitialize();
            return -1;
        }

        IWbemServices *pSvc = NULL;
        hres = pLoc->ConnectServer(
                _bstr_t(L"ROOT\\CIMV2"),
                NULL,
                NULL,
                0,
                NULL,
                0,
                0,
                &pSvc
        );
        if (FAILED(hres)) {
            LOGE("Could not connect to WMI server.");
            pLoc->Release();
            CoUninitialize();
            return -1;
        }

        if (!pSvc) {
            LOGE("IWbemServices is null.");
            pLoc->Release();
            CoUninitialize();
            return -1;
        }

        hres = CoSetProxyBlanket(
                pSvc,
                RPC_C_AUTHN_WINNT,
                RPC_C_AUTHZ_NONE,
                NULL,
                RPC_C_AUTHN_LEVEL_CALL,
                RPC_C_IMP_LEVEL_IMPERSONATE,
                NULL,
                EOAC_NONE
        );
        if (FAILED(hres)) {
            LOGE("Could not set proxy blanket.");
            pSvc->Release();
            pLoc->Release();
            CoUninitialize();
            return -1;
        }

        IEnumWbemClassObject *pEnumerator = NULL;
        IWbemClassObject *pclsObj = NULL;
        ULONG uReturn = 0;

        hw_disks_.clear();
        drivers_.clear();

        // CPU
        if (read_cpu_info) {
            hres = pSvc->ExecQuery(
                    bstr_t("WQL"),
                    bstr_t("SELECT * FROM Win32_Processor"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    NULL,
                    &pEnumerator
            );

            if (FAILED(hres)) {
                LOGE("Query for processor information failed.");
                pSvc->Release();
                pLoc->Release();
                CoUninitialize();
                return -1;
            }

            while (pEnumerator) {
                HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn || FAILED(hr)) {
                    break;
                }

                if (!pclsObj) {
                    break;
                }

                VARIANT vtProp;
                // name
                {
                    hr = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
                    do {
                        if (FAILED(hr)) {
                            break;
                        }
                        if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                            VariantClear(&vtProp);
                            break;
                        }
                        hw_cpu_.name_ = StringUtil::ToUTF8(vtProp.bstrVal);
                        LOGI("CPU NAME: {}", hw_cpu_.name_);
                        sp->Put(kKeyCpuName, hw_cpu_.name_);
                        VariantClear(&vtProp);

                    } while (0);
                }

                // NumberOfLogicalProcessors
                {
                    hr = pclsObj->Get(L"NumberOfCores", 0, &vtProp, 0, 0);
                    do {
                        if (FAILED(hr)) {
                            break;
                        }
                        hw_cpu_.num_cores_ = vtProp.uintVal;
                        LOGI("CPU Cores: {}", hw_cpu_.num_cores_);
                        sp->Put(kKeyCpuCores, std::to_string(hw_cpu_.num_cores_));
                        VariantClear(&vtProp);
                    } while (0);
                }

                // MaxClockSpeed
                {
                    hr = pclsObj->Get(L"MaxClockSpeed", 0, &vtProp, 0, 0);
                    do {
                        if (FAILED(hr)) {
                            break;
                        }
                        hw_cpu_.max_clock_speed_ = vtProp.uintVal;
                        LOGI("CPU clocks: {}", hw_cpu_.max_clock_speed_);
                        sp->Put(kKeyCpuMaxClock, std::to_string(hw_cpu_.max_clock_speed_));
                        VariantClear(&vtProp);

                    } while (0);
                }

                // process id
                {
                    hr = pclsObj->Get(L"ProcessorId", 0, &vtProp, 0, 0);  // "ProcessorId" 是属性名，不同的信息需要查询不同的属性名
                    do {
                        if (FAILED(hr)) {
                            break;
                        }
                        if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                            VariantClear(&vtProp);
                            break;
                        }
                        hw_cpu_.id_ = StringUtil::ToUTF8(vtProp.bstrVal);
                        LOGI("CPU id: {}", hw_cpu_.id_);
                        sp->Put(kKeyCpuId, hw_cpu_.id_);
                        VariantClear(&vtProp);

                    } while (0);
                }

                pclsObj->Release();
            }
        }
        else {
            hw_cpu_.name_ = sp->Get(kKeyCpuName);
            try {
                hw_cpu_.num_cores_ = std::atoll(sp->Get(kKeyCpuName).c_str());
                hw_cpu_.max_clock_speed_ = std::atoll(sp->Get(kKeyCpuMaxClock).c_str());
            }
            catch (std::exception& e) {
                LOGE("error: is {}", e.what());
            }
            hw_cpu_.id_ = sp->Get(kKeyCpuId);
        }

        if (disk) {
            hres = pSvc->ExecQuery(
                    bstr_t("WQL"),
                    bstr_t("SELECT * FROM Win32_DiskDrive"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    NULL,
                    &pEnumerator);

            if (FAILED(hres)) {
                LOGE("Query for operating system name failed, Error code = {}", hres);
                pSvc->Release();
                pLoc->Release();
                CoUninitialize();
                return -1; // Program has failed.
            }

            uReturn = 0;
            while (pEnumerator) {
                HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn || FAILED(hr)) {
                    break;
                }

                HwDisk disk{};

                VARIANT vtProp;

                if (!pclsObj) {
                    break;
                }
                // Get the value of the Name property
                hr = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    disk.name_ = StringUtil::ToUTF8(vtProp.bstrVal);
                    VariantClear(&vtProp);
                } while (0);

                hr = pclsObj->Get(L"Model", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    disk.model_ = StringUtil::ToUTF8(vtProp.bstrVal);
                    VariantClear(&vtProp);
                } while (0);


                hr = pclsObj->Get(L"Status", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    disk.status_ = StringUtil::ToUTF8(vtProp.bstrVal);
                    VariantClear(&vtProp);
                } while (0);



                hr = pclsObj->Get(L"DeviceID", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    // no to do
                    VariantClear(&vtProp);
                } while (0);


                hr = pclsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    disk.serial_number_ = StringUtil::ToUTF8(vtProp.bstrVal);
                    VariantClear(&vtProp);
                } while (0);

                //InterfaceType
                hr = pclsObj->Get(L"InterfaceType", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    disk.interface_type_ = StringUtil::ToUTF8(vtProp.bstrVal);
                    VariantClear(&vtProp);
                } while (0);

                pclsObj->Release();

                if (disk.interface_type_ == "IDE") {
                    hw_disks_.push_back(disk);
                }
            }
        }

        // system driver
        if (driver) {
            hres = pSvc->ExecQuery(
                    bstr_t("WQL"),
                    bstr_t("SELECT * FROM Win32_SystemDriver"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    NULL,
                    &pEnumerator);

            if (FAILED(hres)) {
                LOGE("Query for SystemDriver name failed.  Error code = {}", hres);
                pSvc->Release();
                pLoc->Release();
                CoUninitialize();
                return -1;
            }

            uReturn = 0;
            while (pEnumerator) {
                HRESULT hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
                if (0 == uReturn || FAILED(hr)) {
                    break;
                }

                VARIANT vtProp;
                SysDriver driver;

                if (!pclsObj) {
                    break;
                }

                hr = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    auto name = StringUtil::ToUTF8(vtProp.bstrVal);
                    driver.name_ = name;
                    VariantClear(&vtProp);
                } while (0);



                hr = pclsObj->Get(L"DisplayName", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    auto display_name = StringUtil::ToUTF8(vtProp.bstrVal);
                    driver.display_name_ = display_name;
                    VariantClear(&vtProp);
                } while (0);

                hr = pclsObj->Get(L"State", 0, &vtProp, 0, 0);
                do {
                    if (FAILED(hr)) {
                        break;
                    }
                    if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                        VariantClear(&vtProp);
                        break;
                    }
                    auto state = StringUtil::ToUTF8(vtProp.bstrVal);
                    driver.state_ = state;
                    VariantClear(&vtProp);
                } while (0);

                drivers_.push_back(driver);

                pclsObj->Release();
            }
        }

        // 显卡
        // 执行 WMI 查询（获取显卡信息）
        hres = pSvc->ExecQuery(
                _bstr_t("WQL"),
                _bstr_t("SELECT * FROM Win32_VideoController"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                NULL,
                &pEnumerator
        );
        if (FAILED(hres)) {
            std::cerr << "Failed to execute WMI query: 0x" << std::hex << hres << std::endl;
            pSvc->Release();
            pLoc->Release();
            CoUninitialize();
            return 1;
        }

        int gpuCount = 0;
        while (pEnumerator) {
            hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
            if (uReturn == 0 || FAILED(hres)) break;

            gpuCount++;
            LOGI("GPU: {}", gpuCount);

            HwGPU hw_gpu;

            VARIANT vtProp;

            if (!pclsObj) {
                break;
            }

            // 显卡名称
            auto hr = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
            do {
                if (FAILED(hr)) {
                    break;
                }
                if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                    VariantClear(&vtProp);
                    break;
                }
                hw_gpu.name_ = StringUtil::ToUTF8(vtProp.bstrVal);
                LOGI("GPU NAME: {}", hw_gpu.name_);
                VariantClear(&vtProp);
            } while (0);

            // 显存大小
            hr = pclsObj->Get(L"AdapterRAM", 0, &vtProp, 0, 0);
            do {
                if (FAILED(hr)) {
                    break;
                }
                hw_gpu.size_ = vtProp.ullVal;
                hw_gpu.size_str_ = NumFormatter::FormatStorageSize(vtProp.ullVal);
                LOGI("AdapterRAM: {}Bytes, {}", hw_gpu.size_, hw_gpu.size_str_);
                VariantClear(&vtProp);
            } while (0);

            // 当前分辨率
            if (SUCCEEDED(pclsObj->Get(L"CurrentHorizontalResolution", 0, &vtProp, 0, 0)) &&
                SUCCEEDED(pclsObj->Get(L"CurrentVerticalResolution", 0, &vtProp, 0, 0))) {

                do {
                    VARIANT vtPropH, vtPropV;
                    hr = pclsObj->Get(L"CurrentHorizontalResolution", 0, &vtPropH, 0, 0);
                    if (FAILED(hr)) {
                        break;
                    }

                    hr = pclsObj->Get(L"CurrentVerticalResolution", 0, &vtPropV, 0, 0);
                    if (FAILED(hr)) {
                        VariantClear(&vtPropH);
                        break;
                    }

                    LOGI("Res: {}x{}", vtPropH.intVal, vtPropV.intVal);
                    hw_gpu.res_w_ = vtPropH.intVal;
                    hw_gpu.res_h_ = vtPropV.intVal;
                    VariantClear(&vtPropH);
                    VariantClear(&vtPropV);
                } while (0);
            }

            // 驱动版本
            hr = pclsObj->Get(L"DriverVersion", 0, &vtProp, 0, 0);
            do {
                if (FAILED(hr)) {
                    break;
                }
                if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                    VariantClear(&vtProp);
                    break;
                }
                hw_gpu.driver_version_ = StringUtil::ToUTF8(vtProp.bstrVal);
                LOGI("Driver version: {}", hw_gpu.driver_version_);
                VariantClear(&vtProp);
            } while (0);

            // 显卡PNP设备ID
            hr = pclsObj->Get(L"PNPDeviceID", 0, &vtProp, 0, 0);
            do {
                if (FAILED(hr)) {
                    break;
                }
                if (vtProp.vt != VT_BSTR || vtProp.bstrVal == NULL) {
                    VariantClear(&vtProp);
                    break;
                }
                hw_gpu.pnp_device_id_ = StringUtil::ToUTF8(vtProp.bstrVal);
                LOGI("PNPDeviceID: {}", hw_gpu.pnp_device_id_);
                VariantClear(&vtProp);
            } while (0);


            std::wcout << L"----------------------------------------\n";

            if (hw_gpu.name_.find("Virtual") != std::string::npos || hw_gpu.size_ < 1024 * 1024 * 128) {
                LOGE("THIS is a virtual GPU.");
                continue;
            }

            gpus_.push_back(hw_gpu);

            pclsObj->Release();
        }

        if (gpuCount == 0) {
            LOGI("NO GPU DETECTED");
        }
        // 清理资源
        pSvc->Release();
        pLoc->Release();
        pEnumerator->Release();
        CoUninitialize();

        DetectMac();

        return 0;
    }

    void Hardware::Dump() {
        LOGI("==> Desktop name: {}", desktop_name_);
        LOGI("==> CPU id: {}", hw_cpu_.id_);
        LOGI("==> CPU name: {}", hw_cpu_.name_);
        LOGI("==> CPU core: {}", hw_cpu_.num_cores_);
        LOGI("==> Memory size: {}", memory_size_);
        LOGI("==> Memory size: {}", NumFormatter::FormatStorageSize(memory_size_));

        LOGI("==> Total disks: {}", hw_disks_.size());
        for (auto &disk: hw_disks_) {
            LOGI("  name: {}", disk.name_);
            LOGI("  model: {}", disk.model_);
            LOGI("  status: {}", disk.status_);
            LOGI("  serial number: {}", disk.serial_number_);
            LOGI("  interface type: {}", disk.interface_type_);
            LOGI("-------------------------------------");
        }

        LOGI("==> Total gpus: {}", gpus_.size());
        for (const auto& gpu : gpus_) {
            LOGI("  name: {}", gpu.name_);
            LOGI("  name: {}", gpu.size_str_);
            LOGI("  name: {}", gpu.pnp_device_id_);
        }
        LOGI("Mac address: {}", mac_address_);
    }

    std::string Hardware::GetHardwareDescription() {
        std::stringstream ss;
        for (auto &disk: hw_disks_) {
            ss << disk.serial_number_;
        }
        ss << mac_address_;
        std::string res = ss.str();
        StringUtil::Replace(res, " ", "");
        return res;
    }

    void Hardware::DetectMac() {
        ULONG bufLen = 0;
        DWORD ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufLen);
        if (ret != ERROR_BUFFER_OVERFLOW) {
            LOGE("GetAdaptersAddresses failed to get buffer size");
            return;
        }

        std::vector<uint8_t> buffer(bufLen);
        auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapter, &bufLen);
        if (ret != ERROR_SUCCESS) {
            LOGE("GetAdaptersAddresses failed: {}", ret);
            return;
        }

        for (auto* curr = adapter; curr != nullptr; curr = curr->Next) {
            if (curr->OperStatus != IfOperStatusUp) {
                continue;
            }
            if (curr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }

            if (curr->PhysicalAddressLength > 0) {
                std::string mac;
                for (DWORD i = 0; i < curr->PhysicalAddressLength; i++) {
                    if (i > 0) mac.push_back(':');
                    mac += std::format("{:02X}", static_cast<int>(curr->PhysicalAddress[i]));
                }
                mac_address_ = mac;
                LOGI("Net adapter name: {}, MAC: {}", StringUtil::ToUTF8(curr->FriendlyName), mac_address_);
            }
        }
    }

    std::string Hardware::GetDesktopName() {
        wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        if (GetComputerNameW(computerName, &size)) {
            return StringUtil::ToUTF8(computerName);
        }
        return "";
    }

    void Hardware::LockScreen() {
#ifdef WIN32
        LockWorkStation();
#endif
    }

    bool Hardware::AcquirePermissionForRestartDevice() {
        HANDLE hToken;
        TOKEN_PRIVILEGES tkp;
        // Get a token for this process.
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            LOGE("AcquirePermissionForRestartDevice, OpenProcessToken failed.");
            return false;
        }

        // Get the LUID for the shutdown privilege.
        if (!LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) {
            LOGE("AcquirePermissionForRestartDevice, LookupPrivilegeValueW failed.");
            return false;
        }

        tkp.PrivilegeCount = 1;  // one privilege to set
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // Get the shutdown privilege for this process.
        if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0)) {
            LOGE("AcquirePermissionForRestartDevice, AdjustTokenPrivileges failed.");
            return false;
        }
        return true;
    }

    void Hardware::RestartDevice() {
#ifdef WIN32
        AcquirePermissionForRestartDevice();
        if (ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0) == 0) {
            LOGE("RestartDevice failed: {:x}", GetLastError());
        }
#endif
    }

    void Hardware::ShutdownDevice() {
#ifdef WIN32
        AcquirePermissionForRestartDevice();
        if (ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0) == 0) {
            LOGE("ShutdownDevice failed: {:x}", GetLastError());
        }
#endif
    }


}
