//
// Created by RGAA on 28/07/2025.
//
#ifdef WIN32
#include "auto_start.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <comutil.h>
#include <atlcomcli.h>
#include "tc_common_new/log.h"
#include "tc_common_new/string_util.h"

namespace tc
{

    static void SetAutoStartInternal(HKEY hRootKey, const wchar_t* subKey, const std::wstring& exe_path, bool enabled) {
        HKEY hKey = nullptr;
        LONG result = RegCreateKeyExW(hRootKey, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
        if (result != ERROR_SUCCESS) {
            LOGE("RegCreateKeyExW failed: {}", result);
            return;
        }

        std::filesystem::path fs_path(exe_path);
        std::wstring name = fs_path.stem().wstring();
        std::wstring newPath = exe_path;
        std::replace(newPath.begin(), newPath.end(), L'/', L'\\');

        if (enabled) {
            wchar_t oldPathBuf[4096] = {0};
            DWORD oldSize = sizeof(oldPathBuf);
            DWORD type = 0;
            result = RegQueryValueExW(hKey, name.c_str(), nullptr, &type, (LPBYTE)oldPathBuf, &oldSize);
            std::wstring oldPath = (result == ERROR_SUCCESS && type == REG_SZ) ? oldPathBuf : L"";

            if (oldPath != newPath) {
                result = RegSetValueExW(hKey, name.c_str(), 0, REG_SZ, (const BYTE*)newPath.c_str(),
                                        static_cast<DWORD>((newPath.length() + 1) * sizeof(wchar_t)));
                if (result != ERROR_SUCCESS) {
                    LOGE("RegSetValueExW failed: {}", result);
                }
            }
        } else {
            result = RegDeleteValueW(hKey, name.c_str());
            if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
                LOGE("RegDeleteValueW failed: {}", result);
            }
        }

        RegCloseKey(hKey);
    }

    void AutoStart::SetAutoStart(const std::wstring& exe_path, bool enabled) {
        SetAutoStartInternal(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", exe_path, enabled);
    }

    void AutoStart::SetAutoStartAdmin(const std::wstring& exe_path, bool enabled) {
        SetAutoStartInternal(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", exe_path, enabled);
    }

    AutoStart::AutoStart() {
        m_lpITS = NULL;
        m_lpRootFolder = NULL;

        //HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        HRESULT hr = CoInitialize(NULL);
        if (FAILED(hr)) {
            LOGE("AutoStart CoInitializeEx failed!");
            return;
        }

        //创建任务服务对象
        hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (LPVOID*)(&m_lpITS));
        if (FAILED(hr)) {
            LOGE("AutoStart CoCreateInstance failed!");
            return;
        }

        //连接到任务服务
        hr = m_lpITS->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) {
            LOGI("AutoStart Connect failed!");
            return;
        }

        //获取根任务的指针
        //获取Root Task Folder 的指针 ，这个指针指向的是新注册的任务
        hr = m_lpITS->GetFolder(_bstr_t("\\"), &m_lpRootFolder);
        if (FAILED(hr)) {
            LOGI("AutoStart GetFolder failed!");
            return;
        }
    }

    AutoStart::~AutoStart() {
        if (m_lpITS) {
            m_lpITS->Release();
        }
        if (m_lpRootFolder) {
            m_lpRootFolder->Release();
        }
        CoUninitialize();
    }

    //创建任务计划
    BOOL AutoStart::NewLogonTask(char* lpszTaskName, char* lpszProgramPath, char* lpszParameters, char* lpszAuthor) {
        if (NULL == m_lpRootFolder) {
            LOGE("AutoStart, NewLogonTask failed, root folder is empty.");
            return FALSE;
        }

        //如果存在相同的计划任务，则删除
        LOGI("AutoStart, will delete: {}", lpszTaskName);
        Delete(lpszTaskName);

        //创建任务定义对象来创建任务
        ITaskDefinition *pTaskDefinition = NULL;
        HRESULT hr = m_lpITS->NewTask(0, &pTaskDefinition);
        if (FAILED(hr)) {
            LOGE("AutoStart NewTask failed!");
            return FALSE;
        }
        LOGI("AutoStart, NewTask success, will register info");

        //设置注册信息
        IRegistrationInfo *pRegInfo = NULL;
        CComVariant variantAuthor(NULL);
        variantAuthor = lpszAuthor;
        hr = pTaskDefinition->get_RegistrationInfo(&pRegInfo);
        if (FAILED(hr)) {
            LOGE("AutoStart get_RegistrationInfo failed!");
            return FALSE;
        }
        // 设置作者信息
        hr = pRegInfo->put_Author(variantAuthor.bstrVal);
        if (FAILED(hr)) {
            LOGE("AutoStart putAuthor failed.");
            return FALSE;
        }
        pRegInfo->Release();

        // 设置登录类型和运行权限
        LOGI("AutoStart, will set Principal");
        IPrincipal *pPrincipal = NULL;
        hr = pTaskDefinition->get_Principal(&pPrincipal);
        if (FAILED(hr)) {
            LOGE("get_Principal failed!");
            return FALSE;
        }
        // 设置登录类型
        hr = pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
        // 设置运行权限
        // 最高权限
        hr = pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
        pPrincipal->Release();

        // 设置其他信息
        LOGI("AutoStart, will set info");
        ITaskSettings *pSettting = NULL;
        hr = pTaskDefinition->get_Settings(&pSettting);
        if (FAILED(hr)) {
            LOGE("AutoStart 设置其他信息失败!");
            return FALSE;
        }
        // 设置其他信息
        hr = pSettting->put_StopIfGoingOnBatteries(VARIANT_FALSE);
        hr = pSettting->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
        hr = pSettting->put_AllowDemandStart(VARIANT_TRUE);
        hr = pSettting->put_StartWhenAvailable(VARIANT_FALSE);
        hr = pSettting->put_MultipleInstances(TASK_INSTANCES_PARALLEL);

        //
//        IIdleSettings* pIdleSettings = NULL;
//        hr = pSettting->get_IdleSettings(&pIdleSettings);
//        if (SUCCEEDED(hr)) {
//            pIdleSettings->Release();
//        }
//
//        pSettting->put_StartWhenAvailable(VARIANT_TRUE);
//        pSettting->put_RestartInterval(_bstr_t(L"PT1M"));
//        pSettting->put_RestartCount(9999);

        pSettting->Release();

        // 创建执行动作
        IActionCollection *pActionCollect = NULL;
        hr = pTaskDefinition->get_Actions(&pActionCollect);
        if (FAILED(hr)) {
            LOGE("AutoStart 创建执行动作失败!");
            return FALSE;
        }

        // 创建执行操作
        IAction *pAction = NULL;
        hr = pActionCollect->Create(TASK_ACTION_EXEC, &pAction);
        if (FAILED(hr)) {
            LOGE("AutoStart create action failed.");
            return FALSE;
        }
        pActionCollect->Release();

        // 设置执行程序路径和参数
        CComVariant variantProgramPath(NULL);
        CComVariant variantParameters(NULL);
        IExecAction *pExecAction = NULL;
        hr = pAction->QueryInterface(IID_IExecAction, (PVOID *)(&pExecAction));
        if (FAILED(hr)) {
            pAction->Release();
            LOGE("AutoStart QueryInterface IExecAction failed!");
            return FALSE;
        }
        pAction->Release();

        // 设置程序路径和参数
        variantProgramPath = lpszProgramPath;
        variantParameters = lpszParameters;
        pExecAction->put_Path(variantProgramPath.bstrVal);
        pExecAction->put_Arguments(variantParameters.bstrVal);
        pExecAction->Release();
        // 设置触发器信息，包括用户登录时触发
        ITriggerCollection *pTriggers = NULL;
        hr = pTaskDefinition->get_Triggers(&pTriggers);
        if (FAILED(hr)) {
            LOGE("AutoStart get_Triggers failed!");
            return FALSE;
        }
        LOGI("AutoStart, get_Triggers success");

        // 创建触发器
        ITrigger *pTrigger = NULL;
        hr = pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger);
        if (FAILED(hr)) {
            LOGE("AutoStart TASK_TRIGGER_LOGON failed!");
            return FALSE;
        }
        // 注册任务计划
        IRegisteredTask *pRegisteredTask = NULL;
        CComVariant variantTaskName(NULL);
        variantTaskName = lpszTaskName;
        hr = m_lpRootFolder->RegisterTaskDefinition(variantTaskName.bstrVal,
                                                    pTaskDefinition,
                                                    TASK_CREATE_OR_UPDATE,
                                                    _variant_t(),
                                                    _variant_t(),
                                                    TASK_LOGON_INTERACTIVE_TOKEN,
                                                    _variant_t(""),
                                                    &pRegisteredTask);
        if (FAILED(hr)) {
            pTaskDefinition->Release();
            LOGE("AutoStart RegisterTaskDefinition TASK_TRIGGER_LOGON failed!");
            return FALSE;
        }
        LOGI("AutoStart RegisterTaskDefinition TASK_TRIGGER_LOGON Success!");
        pTaskDefinition->Release();
        pRegisteredTask->Release();
        return TRUE;
    }

    //删除计划任务
    BOOL AutoStart::Delete(char* lpszTaskName) {
        if(NULL == m_lpRootFolder) {
            return FALSE;
        }
        CComVariant variantTaskName(NULL);
        variantTaskName = lpszTaskName;
        HRESULT hr = m_lpRootFolder->DeleteTask(variantTaskName.bstrVal, 0);
        if (FAILED(hr)) {
            return FALSE;
        }
        return TRUE;
    }

    //创建任务计划
    BOOL AutoStart::NewTimeTask(char* lpszTaskName, char* lpszProgramPath, char* lpszParameters, char* lpszAuthor) {
        if (NULL == m_lpRootFolder) {
            LOGE("RootFolder is empty.");
            return FALSE;
        }

        //如果存在相同的计划任务，则删除
        LOGI("AutoStart, Will delete: {}", lpszTaskName);
        Delete(lpszTaskName);

        //  Create the task definition object to create the task.
        ITaskDefinition *pTask = NULL;
        auto hr = m_lpITS->NewTask( 0, &pTask );

        //pService->Release();  // COM clean up.  Pointer is no longer used.
        if (FAILED(hr)) {
            printf("Failed to CoCreate an instance of the TaskService class: %x", hr);
            CoUninitialize();
            return 1;
        }

        //  ------------------------------------------------------
        //  Get the registration info for setting the identification.
        IRegistrationInfo *pRegInfo= NULL;
        hr = pTask->get_RegistrationInfo( &pRegInfo );
        if( FAILED(hr) )
        {
            printf("\nCannot get identification pointer: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        hr = pRegInfo->put_Author( BSTR(L"Author Name") );
        pRegInfo->Release();
        if( FAILED(hr) )
        {
            printf("\nCannot put identification info: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  ------------------------------------------------------
        //  Create the principal for the task - these credentials
        //  are overwritten with the credentials passed to RegisterTaskDefinition
        IPrincipal *pPrincipal = NULL;
        hr = pTask->get_Principal( &pPrincipal );
        if( FAILED(hr) )
        {
            printf("\nCannot get principal pointer: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  Set up principal logon type to interactive logon
        hr = pPrincipal->put_LogonType( TASK_LOGON_INTERACTIVE_TOKEN );
        pPrincipal->Release();
        if( FAILED(hr) )
        {
            printf("\nCannot put principal info: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  ------------------------------------------------------
        //  Create the settings for the task
        ITaskSettings *pSettings = NULL;
        hr = pTask->get_Settings( &pSettings );
        if( FAILED(hr) )
        {
            printf("\nCannot get settings pointer: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  Set setting values for the task.  
        hr = pSettings->put_StartWhenAvailable(VARIANT_TRUE);
        pSettings->Release();
        if( FAILED(hr) )
        {
            printf("\nCannot put setting information: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        // Set the idle settings for the task.
        IIdleSettings *pIdleSettings = NULL;
        hr = pSettings->get_IdleSettings( &pIdleSettings );
        if( FAILED(hr) )
        {
            printf("\nCannot get idle setting information: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        hr = pIdleSettings->put_WaitTimeout(BSTR(L"PT5M"));
        pIdleSettings->Release();
        if( FAILED(hr) )
        {
            printf("\nCannot put idle setting information: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }


        //  ------------------------------------------------------
        //  Get the trigger collection to insert the time trigger.
        ITriggerCollection *pTriggerCollection = NULL;
        hr = pTask->get_Triggers( &pTriggerCollection );
        if( FAILED(hr) )
        {
            printf("\nCannot get trigger collection: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  Add the time trigger to the task.
        ITrigger *pTrigger = NULL;
        hr = pTriggerCollection->Create( TASK_TRIGGER_TIME, &pTrigger );
        pTriggerCollection->Release();
        if( FAILED(hr) )
        {
            printf("\nCannot create trigger: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        ITimeTrigger *pTimeTrigger = NULL;
        hr = pTrigger->QueryInterface(
                IID_ITimeTrigger, (void**) &pTimeTrigger );
        pTrigger->Release();
        if( FAILED(hr) )
        {
            printf("\nQueryInterface call failed for ITimeTrigger: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        hr = pTimeTrigger->put_Id( _bstr_t( L"Trigger1" ) );
        if( FAILED(hr) )
            printf("\nCannot put trigger ID: %x", hr);

        //////
        SYSTEMTIME st;
        GetLocalTime(&st);  // 获取当前时间

        // start
        {
            st.wMinute += 1;
            if (st.wMinute >= 60) {
                st.wMinute -= 60;
                st.wHour += 1;
            }

            // 转换为 BSTR 格式（ISO 8601 时间格式）
            WCHAR wszStartTime[64];
            swprintf_s(wszStartTime, L"%04d-%02d-%02dT%02d:%02d:%02d",
                       st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond);
            LOGI("AutoStart, start time: {}", StringUtil::ToUTF8(wszStartTime));

            // 设置触发器开始时间
            hr = pTimeTrigger->put_StartBoundary(_bstr_t(wszStartTime));
            if (FAILED(hr)) {
                LOGE("AutoStart put_StartBoundary failed!");
                pTimeTrigger->Release();
                pTrigger->Release();
                return FALSE;
            }
        }

        // end
        {
            // 计算 1 分钟后的时间
            st.wMinute += 2;
            if (st.wMinute >= 60) {
                st.wMinute -= 60;
                st.wHour += 1;
            }

            // 转换为 BSTR 格式（ISO 8601 时间格式）
            WCHAR wszStartTime[64];
            swprintf_s(wszStartTime, L"%04d-%02d-%02dT%02d:%02d:%02d",
                       st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond);
            LOGI("AutoStart, end time: {}", StringUtil::ToUTF8(wszStartTime));

            // 设置触发器开始时间
            hr = pTimeTrigger->put_EndBoundary(_bstr_t(wszStartTime));
            if (FAILED(hr)) {
                LOGE("AutoStart put_EndBoundary failed!");
                pTimeTrigger->Release();
                pTrigger->Release();
                return FALSE;
            }
        }
        //////


        //  ------------------------------------------------------
        //  Add an action to the task. This task will execute notepad.exe.     
        IActionCollection *pActionCollection = NULL;

        //  Get the task action collection pointer.
        hr = pTask->get_Actions( &pActionCollection );
        if( FAILED(hr) )
        {
            printf("\nCannot get Task collection pointer: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  Create the action, specifying that it is an executable action.
        IAction *pAction = NULL;
        hr = pActionCollection->Create( TASK_ACTION_EXEC, &pAction );
        pActionCollection->Release();
        if( FAILED(hr) )
        {
            printf("\nCannot create the action: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        IExecAction *pExecAction = NULL;
        //  QI for the executable task pointer.
        hr = pAction->QueryInterface(
                IID_IExecAction, (void**) &pExecAction );
        pAction->Release();
        if( FAILED(hr) )
        {
            printf("\nQueryInterface call failed for IExecAction: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  Set the path of the executable to notepad.exe.
        hr = pExecAction->put_Path( _bstr_t( StringUtil::ToWString(lpszProgramPath).c_str() ) );
        pExecAction->Release();
        if( FAILED(hr) )
        {
            printf("\nCannot put action path: %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        //  ------------------------------------------------------
        //  Save the task in the root folder.
        IRegisteredTask *pRegisteredTask = NULL;
        hr = m_lpRootFolder->RegisterTaskDefinition(
                _bstr_t( lpszTaskName ),
                pTask,
                TASK_CREATE_OR_UPDATE,
                _variant_t(),
                _variant_t(),
                TASK_LOGON_INTERACTIVE_TOKEN,
                _variant_t(L""),
                &pRegisteredTask);
        if( FAILED(hr) )
        {
            printf("\nError saving the Task : %x", hr );
            pTask->Release();
            CoUninitialize();
            return 1;
        }

        printf("\n Success! Task successfully registered. " );

        //  Clean up.
        pTask->Release();
        pRegisteredTask->Release();

        return true;
    }
}
#endif
