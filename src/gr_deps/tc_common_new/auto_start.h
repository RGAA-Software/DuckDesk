//
// Created by RGAA on 28/07/2025.
//

#ifndef GAMMARAYPREMIUM_AUTO_START_H
#define GAMMARAYPREMIUM_AUTO_START_H
#ifdef WIN32
#include <string>
#include <taskschd.h>
#pragma comment(lib, "taskschd.lib")

namespace tc
{

    class AutoStart {
    public:
        // reg
        static void SetAutoStart(const std::wstring& exe_path, bool enabled);
        static void SetAutoStartAdmin(const std::wstring& exe_path, bool enabled);

        AutoStart();
        ~AutoStart();

        // task
        //************************************
        // 函数名:  CMyTaskSchedule::NewTask
        // 返回类型:   BOOL
        // 功能: 创建计划任务
        // 参数1: char * lpszTaskName    计划任务名
        // 参数2: char * lpszProgramPath    计划任务路径
        // 参数3: char * lpszParameters        计划任务参数
        // 参数4: char * lpszAuthor            计划任务作者
        //************************************
        BOOL NewLogonTask(char* lpszTaskName, char* lpszProgramPath, char* lpszParameters, char* lpszAuthor);

        BOOL NewTimeTask(char* lpszTaskName, char* lpszProgramPath, char* lpszParameters, char* lpszAuthor);

        //************************************
        // 函数名:  CMyTaskSchedule::Delete
        // 返回类型:   BOOL
        // 功能: 删除计划任务
        // 参数1: char * lpszTaskName    计划任务名
        //************************************
        BOOL Delete(char* lpszTaskName);

    private:
        ITaskService *m_lpITS = nullptr;
        ITaskFolder *m_lpRootFolder = nullptr;
    };

}
#endif // WIN32
#endif //GAMMARAYPREMIUM_AUTO_START_H
