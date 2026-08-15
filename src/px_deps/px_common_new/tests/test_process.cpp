//
// Created by RGAA on 2024/5/24.
//
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <map>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include "../process_util.h"
#include "../string_util.h"

using namespace tc;

static std::string GetHelperExePath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    auto exe_path = std::filesystem::path(path);
    auto helper = exe_path.parent_path() / "test_process_helper.exe";
    return StringUtil::ToUTF8(helper.wstring());
}

TEST(TestProcess, StartProcessAndWait_Success) {
    auto helper = GetHelperExePath();
    auto ret = ProcessUtil::StartProcessAndWait(helper, {"exit-code", "0"});
    EXPECT_TRUE(ret);
}

TEST(TestProcess, StartProcessAndWait_Failure) {
    auto helper = GetHelperExePath();
    auto ret = ProcessUtil::StartProcessAndWait(helper, {"exit-code", "1"});
    EXPECT_FALSE(ret);
}

TEST(TestProcess, StartProcessAndWait_NotExists) {
    auto ret = ProcessUtil::StartProcessAndWait("this_exe_should_not_exist_12345.exe", {});
    EXPECT_FALSE(ret);
}

TEST(TestProcess, StartProcess_Detach) {
    auto helper = GetHelperExePath();
    auto pid = ProcessUtil::StartProcess(helper, {"sleep", "10000"}, true, false);
    EXPECT_GT(pid, 0);
    if (pid > 0) {
        Sleep(100);
        auto killed = ProcessUtil::KillProcess(pid);
        EXPECT_TRUE(killed);
    }
}

TEST(TestProcess, StartProcess_Wait) {
    auto helper = GetHelperExePath();
    auto ret = ProcessUtil::StartProcess(helper, {"exit-code", "0"}, false, true);
    EXPECT_EQ(ret, 0);
}

TEST(TestProcess, StartProcess_NoDetachNoWait) {
    auto helper = GetHelperExePath();
    auto pid = ProcessUtil::StartProcess(helper, {"sleep", "10000"}, false, false);
    EXPECT_GT(pid, 0);
    if (pid > 0) {
        Sleep(100);
        ProcessUtil::KillProcess(pid);
    }
}

TEST(TestProcess, StartProcessAndOutput_CaptureStdout) {
    auto helper = GetHelperExePath();
    auto output = ProcessUtil::StartProcessAndOutput(helper, {"echo", "hello_world_test"});
    EXPECT_FALSE(output.empty());
    bool found = false;
    for (const auto& line : output) {
        if (line.find("hello_world_test") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(TestProcess, StartProcessAndOutput_MultipleLines) {
    auto helper = GetHelperExePath();
    auto output = ProcessUtil::StartProcessAndOutput(helper, {"echo-lines", "line1", "line2"});
    EXPECT_GE(output.size(), 2);
    bool found_line1 = false;
    bool found_line2 = false;
    for (const auto& line : output) {
        if (line.find("line1") != std::string::npos) found_line1 = true;
        if (line.find("line2") != std::string::npos) found_line2 = true;
    }
    EXPECT_TRUE(found_line1);
    EXPECT_TRUE(found_line2);
}

TEST(TestProcess, KillProcess_ValidPid) {
    auto helper = GetHelperExePath();
    auto pid = ProcessUtil::StartProcess(helper, {"sleep", "10000"}, true, false);
    ASSERT_GT(pid, 0);
    auto killed = ProcessUtil::KillProcess(pid);
    EXPECT_TRUE(killed);
}

TEST(TestProcess, KillProcess_InvalidPid) {
    auto killed = ProcessUtil::KillProcess(99999);
    EXPECT_FALSE(killed);
}

TEST(TestProcess, GetPidByExeName_Explorer) {
    auto pid = ProcessUtil::GetPidByExeName("explorer.exe");
    EXPECT_GT(pid, 0);
}

TEST(TestProcess, GetPidByExeName_NotExists) {
    auto pid = ProcessUtil::GetPidByExeName("this_should_not_exist_12345.exe");
    EXPECT_EQ(pid, 0);
}

TEST(TestProcess, GetCurrentSessionId) {
    auto sid = ProcessUtil::GetCurrentSessionId();
    EXPECT_NE(sid, static_cast<uint32_t>(-1));
}

TEST(TestProcess, GetProcessSessionId_Current) {
    auto sid = ProcessUtil::GetProcessSessionId(GetCurrentProcessId());
    EXPECT_NE(sid, static_cast<uint32_t>(-1));
}

TEST(TestProcess, GetProcessSessionId_Invalid) {
    auto sid = ProcessUtil::GetProcessSessionId(99999);
    EXPECT_EQ(sid, static_cast<uint32_t>(-1));
}

TEST(TestProcess, GetThreadCount) {
    auto count = ProcessUtil::GetThreadCount();
    EXPECT_GT(count, 0);
}

TEST(TestProcess, SetProcessInHighLevel) {
    EXPECT_NO_THROW(ProcessUtil::SetProcessInHighLevel());
}

TEST(TestProcess, RunAsAdminWithShell_Notepad) {
    GTEST_SKIP() << "RunAsAdminWithShell requires UAC interaction, skip automated test.";
}

TEST(TestProcess, StartProcessInWorkDir_Cmd) {
    auto helper = GetHelperExePath();
    auto temp_dir = std::filesystem::temp_directory_path().string();
    auto cmdline = std::string("\"") + helper + "\" cwd";
    auto ret = ProcessUtil::StartProcessInWorkDir(temp_dir, cmdline, {});
    EXPECT_TRUE(ret);
}

TEST(TestProcess, StartProcessAndOutput_ChineseOutput) {
    auto helper = GetHelperExePath();
    auto output = ProcessUtil::StartProcessAndOutput(helper, {"echo", "你好世界_测试"});
    EXPECT_FALSE(output.empty());
    bool found = false;
    for (const auto& line : output) {
        if (line.find("你好世界_测试") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Chinese output should be captured correctly";
}

TEST(TestProcess, StartProcessAndWait_ChinesePath) {
    auto helper = GetHelperExePath();
    auto temp_dir = std::filesystem::temp_directory_path() / "测试目录_中文_path";
    std::filesystem::create_directories(temp_dir);
    auto test_file = temp_dir / "中文文件.txt";
    auto ret = ProcessUtil::StartProcessAndWait(helper, {"create-file", StringUtil::ToUTF8(test_file.wstring()), "hello"});
    EXPECT_TRUE(ret);
    EXPECT_TRUE(std::filesystem::exists(test_file)) << "Chinese path file should be created";
    std::filesystem::remove_all(temp_dir);
}

TEST(TestProcess, StartProcessInWorkDir_ChinesePath) {
    auto helper = GetHelperExePath();
    auto temp_dir = std::filesystem::temp_directory_path() / "中文工作目录_work_dir";
    std::filesystem::create_directories(temp_dir);
    // Use relative path to verify lpCurrentDirectory is effective
    auto cmdline = std::string("\"") + helper + "\" create-file marker.txt ok";
    auto ret = ProcessUtil::StartProcessInWorkDir(
        StringUtil::ToUTF8(temp_dir.wstring()),
        cmdline,
        {});
    EXPECT_TRUE(ret) << "Process should succeed in Chinese workdir";
    EXPECT_TRUE(std::filesystem::exists(temp_dir / "marker.txt")) << "Relative path file should be created in workdir";
    std::filesystem::remove_all(temp_dir);
}

TEST(TestProcess, StartProcess_ChineseExeName) {
    auto helper = GetHelperExePath();
    auto temp_dir = std::filesystem::temp_directory_path() / "中文exe测试目录";
    std::filesystem::create_directories(temp_dir);
    auto chinese_helper = temp_dir / "中文助手.exe";
    std::filesystem::copy_file(helper, chinese_helper);
    auto output = ProcessUtil::StartProcessAndOutput(
        StringUtil::ToUTF8(chinese_helper.wstring()),
        {"echo", "中文脚本执行成功"});
    bool found = false;
    for (const auto& line : output) {
        if (line.find("中文脚本执行成功") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Chinese exe should execute and produce expected output";
    std::filesystem::remove_all(temp_dir);
}
