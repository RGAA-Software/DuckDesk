// FtEngine 外壳单测 - 双引擎 loopback:全量传输 / 覆盖决策 / 取消 / 反压 / 限速 / 调度
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ft_engine.h"

#ifdef _WIN32
#include <process.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace px::ft {
namespace {

class TestTempDir {
public:
    explicit TestTempDir(const std::string& prefix) {
        auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                (prefix + "_" + std::to_string(_getpid()) + "_" + std::to_string(ts) + "_" +
                 std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }
    ~TestTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    std::filesystem::path join(const std::string& p) const { return path_ / ToFsPath(p); }
    const std::filesystem::path& path() const { return path_; }
    std::string str(const std::string& p) const { return ToUtf8(join(p)); }

private:
    std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& p, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> MakeText(size_t approx) {
    std::vector<uint8_t> v;
    std::string line = "ft engine loopback test line, compressible text payload.\n";
    while (v.size() < approx) v.insert(v.end(), line.begin(), line.end());
    return v;
}

// 双引擎直连:A 产出的消息直接喂给 B(busy 标志模拟通道水位)
struct Loopback {
    std::unique_ptr<FtEngine> a;
    std::unique_ptr<FtEngine> b;
    bool a_to_b_busy = false;
    bool b_to_a_busy = false;
    int a_sent_messages = 0;
    int b_sent_messages = 0;
    std::vector<std::pair<int32_t, std::string>> a_done; // (id, error)
    std::vector<std::pair<int32_t, std::string>> b_done;
    std::vector<std::tuple<int32_t, int32_t, std::string, bool, bool>> a_confirm_reqs;
    std::vector<std::tuple<int32_t, int32_t, std::string, bool, bool>> b_confirm_reqs;
    int blocks_to_b = 0; // A->B 方向数据块计数(续传断言用)
    int blocks_to_a = 0;
    std::vector<uint32_t> confirm_offsets_to_b; // A 收到的 send_confirm 偏移(来自 B)

    Loopback() {
        a = std::make_unique<FtEngine>([this](const px::Message& m) {
            if (a_to_b_busy) return false;
            ++a_sent_messages;
            if (m.has_file_response() && m.file_response().has_block()) ++blocks_to_b;
            Deliver(m, b.get());
            return true;
        });
        b = std::make_unique<FtEngine>([this](const px::Message& m) {
            if (b_to_a_busy) return false;
            ++b_sent_messages;
            if (m.has_file_response() && m.file_response().has_block()) ++blocks_to_a;
            if (m.has_file_action() && m.file_action().has_send_confirm() &&
                m.file_action().send_confirm().has_offset_blk()) {
                confirm_offsets_to_b.push_back(m.file_action().send_confirm().offset_blk());
            }
            Deliver(m, a.get());
            return true;
        });
        a->SetJobDoneCallback([this](int32_t id, int32_t, const std::string& err) {
            a_done.emplace_back(id, err);
        });
        b->SetJobDoneCallback([this](int32_t id, int32_t, const std::string& err) {
            b_done.emplace_back(id, err);
        });
        a->SetOverwriteConfirmCallback(
            [this](int32_t id, int32_t fn, const std::string& p, bool up, bool identical) {
                a_confirm_reqs.emplace_back(id, fn, p, up, identical);
            });
        b->SetOverwriteConfirmCallback(
            [this](int32_t id, int32_t fn, const std::string& p, bool up, bool identical) {
                b_confirm_reqs.emplace_back(id, fn, p, up, identical);
            });
    }

    static void Deliver(const px::Message& m, FtEngine* peer) {
        if (m.has_file_action()) peer->HandleFileAction(m.file_action());
        if (m.has_file_response()) peer->HandleFileResponse(m.file_response());
    }

    // 驱动两侧 tick 直到空闲(无读写作业)或超时
    void Pump(int max_ticks = 2000) {
        for (int i = 0; i < max_ticks; ++i) {
            a->Tick();
            b->Tick();
            if (a->read_jobs().empty() && a->write_jobs().empty() && b->read_jobs().empty() &&
                b->write_jobs().empty()) {
                return;
            }
        }
        FAIL() << "Pump: not idle after " << max_ticks << " ticks";
    }
};

// 全量上传:A.SendFiles -> B 接收写盘,内容一致,作业双向清空
TEST(FtEngine, FullUploadRoundtrip) {
    TestTempDir tmp("fte_upload");
    auto content = MakeText(400 * 1024);
    WriteFile(tmp.join("src.txt"), content);

    Loopback lb;
    int32_t id = lb.a->SendFiles(tmp.str("src.txt"), false, tmp.str("dst"));
    lb.Pump();

    // 单文件空名:B 侧写到 dst 本身
    EXPECT_EQ(ReadFile(tmp.join("dst")), content);
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.download")));
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.digest")));
    ASSERT_EQ(lb.b_done.size(), 1u);
    EXPECT_EQ(lb.b_done[0].first, id);
    EXPECT_TRUE(lb.b_done[0].second.empty());
}

// 目录上传(多文件递归)
TEST(FtEngine, DirectoryUploadRoundtrip) {
    TestTempDir tmp("fte_dir");
    auto c1 = MakeText(10 * 1024);
    auto c2 = MakeText(130 * 1024);
    WriteFile(tmp.join("srcdir") / "a.txt", c1);
    WriteFile(tmp.join("srcdir") / "sub" / "b.txt", c2);

    Loopback lb;
    lb.a->SendFiles(tmp.str("srcdir"), false, tmp.str("dstdir"));
    lb.Pump();

    EXPECT_EQ(ReadFile(tmp.join("dstdir") / "a.txt"), c1);
    EXPECT_EQ(ReadFile(tmp.join("dstdir") / "sub" / "b.txt"), c2);
}

// 下载方向:B.ReceiveFiles(向 A 要文件)
TEST(FtEngine, DownloadRoundtrip) {
    TestTempDir tmp("fte_download");
    auto content = MakeText(300 * 1024);
    WriteFile(tmp.join("remote.txt"), content);

    Loopback lb;
    int32_t id = lb.b->ReceiveFiles(tmp.str("remote.txt"), false, tmp.str("local.txt"));
    lb.Pump();

    EXPECT_EQ(ReadFile(tmp.join("local.txt")), content);
    ASSERT_EQ(lb.b_done.size(), 1u);
    EXPECT_EQ(lb.b_done[0].first, id);
}

// 覆盖决策:同名同内容(size+mtime 一致)-> 仍回调 UI(上游语义:弹框带 identical 提示),
// 上层回"跳过"后文件不变,读侧整任务跳过
TEST(FtEngine, OverwriteIdenticalPromptsWithIdenticalFlag) {
    TestTempDir tmp("fte_identical");
    auto content = MakeText(50 * 1024);
    WriteFile(tmp.join("src.txt"), content);
    uint64_t mt = GetFileMtimeSecs(tmp.join("src.txt"));
    WriteFile(tmp.join("dst.txt"), content);
    ASSERT_TRUE(SetFileMtimeSecs(tmp.join("dst.txt"), mt));

    Loopback lb;
    lb.b->ReceiveFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));
    // 驱动到收到覆盖确认请求
    for (int i = 0; i < 100 && lb.b_confirm_reqs.empty(); ++i) {
        lb.a->Tick();
        lb.b->Tick();
    }
    ASSERT_EQ(lb.b_confirm_reqs.size(), 1u);
    EXPECT_TRUE(std::get<4>(lb.b_confirm_reqs[0])); // is_identical
    // 上层回"跳过"
    lb.b->ConfirmFile(std::get<0>(lb.b_confirm_reqs[0]), std::get<1>(lb.b_confirm_reqs[0]), false);
    lb.Pump();

    EXPECT_EQ(ReadFile(tmp.join("dst.txt")), content);
    ASSERT_EQ(lb.a_done.size(), 1u);
    EXPECT_EQ(lb.a_done[0].second, "skipped");
}

// 覆盖决策:同名不同内容 -> 回调 UI;上层回"覆盖"
TEST(FtEngine, OverwriteConfirmCallbackThenOverwrite) {
    TestTempDir tmp("fte_confirm");
    auto src_content = MakeText(50 * 1024);
    WriteFile(tmp.join("src.txt"), src_content);
    WriteFile(tmp.join("dst.txt"), MakeText(60 * 1024)); // 不同 size

    Loopback lb;
    // B 收到覆盖确认请求时自动回"覆盖"
    lb.b->SetOverwriteConfirmCallback(
        [this_b = lb.b.get()](int32_t id, int32_t fn, const std::string&, bool, bool) {
            this_b->ConfirmFile(id, fn, true, 0);
        });
    lb.b->ReceiveFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));
    lb.Pump();

    EXPECT_EQ(ReadFile(tmp.join("dst.txt")), src_content);
}

// 覆盖决策:上层回"跳过" -> 目标文件不变
TEST(FtEngine, OverwriteConfirmCallbackThenSkip) {
    TestTempDir tmp("fte_skip");
    auto src_content = MakeText(50 * 1024);
    auto dst_content = MakeText(60 * 1024);
    WriteFile(tmp.join("src.txt"), src_content);
    WriteFile(tmp.join("dst.txt"), dst_content);

    Loopback lb;
    lb.b->SetOverwriteConfirmCallback(
        [this_b = lb.b.get()](int32_t id, int32_t fn, const std::string&, bool, bool) {
            this_b->ConfirmFile(id, fn, false, 0);
        });
    lb.b->ReceiveFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));
    lb.Pump();

    EXPECT_EQ(ReadFile(tmp.join("dst.txt")), dst_content);
}

// "应用到全部":主控读侧设置 default_overwrite_strategy 后,后续冲突不再回调 UI
// (上传方向:冲突决策点在被控回发 digest 后的主控读侧,io_loop.rs:1578)
TEST(FtEngine, DefaultOverwriteStrategySkipsConfirm) {
    TestTempDir tmp("fte_strategy");
    WriteFile(tmp.join("srcdir") / "a.txt", MakeText(20 * 1024));
    WriteFile(tmp.join("srcdir") / "b.txt", MakeText(30 * 1024));
    WriteFile(tmp.join("dstdir") / "a.txt", MakeText(21 * 1024)); // 冲突
    WriteFile(tmp.join("dstdir") / "b.txt", MakeText(31 * 1024)); // 冲突

    Loopback lb;
    // 被控 B 本地不决策(回发 digest)
    lb.b->SetOverwriteConfirmCallback(nullptr);
    lb.a->SendFiles(tmp.str("srcdir"), false, tmp.str("dstdir"));
    // 主控 A 第一个冲突回调到达后设置"全部覆盖"
    lb.a->SetOverwriteConfirmCallback([&](int32_t aid, int32_t fn, const std::string& p, bool up,
                                          bool ident) {
        lb.a_confirm_reqs.emplace_back(aid, fn, p, up, ident);
        lb.a->SetOverwriteStrategy(aid, true);
        lb.a->ConfirmFile(aid, fn, true, 0);
    });
    lb.Pump();

    EXPECT_EQ(lb.a_confirm_reqs.size(), 1u); // 只弹第一次
    EXPECT_EQ(ReadFile(tmp.join("dstdir") / "a.txt"), MakeText(20 * 1024));
    EXPECT_EQ(ReadFile(tmp.join("dstdir") / "b.txt"), MakeText(30 * 1024));
}

// 取消:传输中取消 -> .download/.digest 清理,对端收到 cancel
TEST(FtEngine, CancelCleansDownloadFiles) {
    TestTempDir tmp("fte_cancel");
    auto content = MakeText(2 * 1024 * 1024); // 足够大,保证取消时在途中
    WriteFile(tmp.join("src.txt"), content);

    Loopback lb;
    // B 收到首块后立刻取消
    int32_t id = lb.b->ReceiveFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));
    // 逐 tick 驱动,B 写完第一块后取消
    for (int i = 0; i < 200; ++i) {
        lb.a->Tick();
        lb.b->Tick();
        if (std::filesystem::exists(tmp.join("dst.txt.download"))) break;
    }
    ASSERT_TRUE(std::filesystem::exists(tmp.join("dst.txt.download")));
    lb.b->CancelJob(id);
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.download")));
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.digest")));
    lb.Pump();
    // A 侧读作业也被移除
    EXPECT_TRUE(lb.a->read_jobs().empty());
}

// 反压:通道忙时不读盘、不推进;恢复后继续
TEST(FtEngine, BackpressureStopsDiskRead) {
    TestTempDir tmp("fte_bp");
    auto content = MakeText(500 * 1024);
    WriteFile(tmp.join("src.txt"), content);

    Loopback lb;
    lb.b->SetOverwriteConfirmCallback(
        [this_b = lb.b.get()](int32_t id, int32_t fn, const std::string&, bool, bool) {
            this_b->ConfirmFile(id, fn, true, 0);
        });
    WriteFile(tmp.join("dst.txt"), MakeText(60 * 1024)); // 制造冲突 -> digest/confirm 流程
    lb.b->ReceiveFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));
    // 驱动到读作业被确认(尚未读完)
    for (int i = 0; i < 100; ++i) {
        lb.a->Tick();
        lb.b->Tick();
        if (!lb.a->read_jobs().empty() && lb.a->read_jobs()[0].file_confirmed()) break;
    }
    ASSERT_FALSE(lb.a->read_jobs().empty());
    ASSERT_TRUE(lb.a->read_jobs()[0].file_confirmed());

    // 通道打满:A 侧至多再多读一块(发现忙的那个 tick 会先读后发),之后不再读盘
    lb.a_to_b_busy = true;
    uint64_t before = lb.a->read_jobs()[0].finished_size();
    for (int i = 0; i < 10; ++i) lb.a->Tick();
    EXPECT_LE(lb.a->read_jobs()[0].finished_size(), before + kBlockPayloadSize);

    // 恢复后传输完成
    lb.a_to_b_busy = false;
    lb.Pump();
    EXPECT_EQ(ReadFile(tmp.join("dst.txt")), content);
}

// 限速:令牌桶为空时 tick 不读盘
TEST(FtEngine, RateLimitBlocksRead) {
    TestTempDir tmp("fte_rl");
    auto content = MakeText(500 * 1024);
    WriteFile(tmp.join("src.txt"), content);

    Loopback lb;
    lb.a->SendFiles(tmp.str("src.txt"), false, tmp.str("dst"));
    // 先让 digest/confirm 走通(NoSuchFile -> 自动 confirm offset 0),且尚未读完
    for (int i = 0; i < 100; ++i) {
        lb.a->Tick();
        lb.b->Tick();
        if (!lb.a->read_jobs().empty() && lb.a->read_jobs()[0].file_confirmed()) break;
    }
    ASSERT_FALSE(lb.a->read_jobs().empty());
    ASSERT_TRUE(lb.a->read_jobs()[0].file_confirmed());

    // 限速 60KB/s:初始桶 60KB < 120KB 块 -> 不读盘
    lb.a->SetRateLimitBytesPerSec(60 * 1024);
    lb.a->Tick();
    uint64_t before = lb.a->read_jobs()[0].finished_size();
    lb.a->Tick();
    EXPECT_EQ(lb.a->read_jobs()[0].finished_size(), before);

    // 等 1s+ 桶满一块后放行
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    lb.a->Tick();
    EXPECT_GT(lb.a->read_jobs()[0].finished_size(), before);

    lb.a->SetRateLimitBytesPerSec(0); // 解除限速
    lb.Pump();
    EXPECT_EQ(ReadFile(tmp.join("dst")), content);
}

// 调度:每 tick 只推进一个作业一块(fs.rs:1376 break 语义)
TEST(FtEngine, OneJobOneBlockPerTick) {
    TestTempDir tmp("fte_sched");
    WriteFile(tmp.join("f1.txt"), MakeText(400 * 1024));
    WriteFile(tmp.join("f2.txt"), MakeText(400 * 1024));

    int block_count = 0;
    FtEngine engine([&block_count](const px::Message& m) {
        if (m.file_response().has_block()) ++block_count;
        return true;
    });
    engine.AddReadJob(TransferJob::NewRead(1, JobType::Generic, "d", DataSource{tmp.join("f1.txt")},
                                           0, false, false, false));
    engine.AddReadJob(TransferJob::NewRead(2, JobType::Generic, "d", DataSource{tmp.join("f2.txt")},
                                           0, false, false, false));

    engine.Tick();
    EXPECT_EQ(block_count, 1); // 一个 tick 至多一块
    EXPECT_EQ(engine.read_jobs()[0].finished_size(), 120 * 1024u);
    EXPECT_EQ(engine.read_jobs()[1].finished_size(), 0u); // 第二个作业本 tick 未推进

    engine.Tick();
    EXPECT_EQ(block_count, 2); // 仍是第一个作业(break 语义:每 tick 只处理队首)

    // 等待确认的作业不占 tick:覆盖检测开启且未确认时不产块
    int block_count2 = 0;
    FtEngine engine2([&block_count2](const px::Message& m) {
        if (m.file_response().has_block()) ++block_count2;
        return true;
    });
    engine2.AddReadJob(TransferJob::NewRead(3, JobType::Generic, "d",
                                            DataSource{tmp.join("f1.txt")}, 0, false, false, true));
    engine2.Tick(); // 发 digest,进入等待
    engine2.Tick();
    engine2.Tick();
    EXPECT_EQ(block_count2, 0);
    EXPECT_TRUE(engine2.read_jobs()[0].file_is_waiting());
}

// 回归:上传冲突时被控侧回发 is_upload=true digest,主控 UI 决策后覆盖生效
// (ui_cm_interface.rs:1116-1124 CheckDigest NeedConfirm 回发语义)
TEST(FtEngine, UploadConflictBouncesDigestToController) {
    TestTempDir tmp("fte_bounce");
    auto src_content = MakeText(50 * 1024);
    WriteFile(tmp.join("src.txt"), src_content);
    WriteFile(tmp.join("dst.txt"), MakeText(60 * 1024)); // 冲突:不同 size

    Loopback lb;
    // B 模拟被控:不设本地覆盖决策回调(写侧作业 is_remote=false -> 回发 digest)
    lb.b->SetOverwriteConfirmCallback(nullptr);
    lb.a->SendFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));

    // 驱动到主控 A 收到回发的覆盖确认请求
    for (int i = 0; i < 100 && lb.a_confirm_reqs.empty(); ++i) {
        lb.a->Tick();
        lb.b->Tick();
    }
    ASSERT_EQ(lb.a_confirm_reqs.size(), 1u);
    EXPECT_TRUE(std::get<3>(lb.a_confirm_reqs[0]));  // is_upload=true
    EXPECT_FALSE(std::get<4>(lb.a_confirm_reqs[0])); // 内容不同 -> 非 identical
    EXPECT_TRUE(lb.b_confirm_reqs.empty());          // 被控侧本地不弹

    // 主控 UI 决策:覆盖
    lb.a->ConfirmFile(std::get<0>(lb.a_confirm_reqs[0]), std::get<1>(lb.a_confirm_reqs[0]), true,
                      0);
    lb.Pump();
    EXPECT_EQ(ReadFile(tmp.join("dst.txt")), src_content);
}

// 回归:上传断点续传——digest.is_resume 驱动被控侧消费 .digest 凭证,按偏移续传
TEST(FtEngine, UploadResumeFromDigestIsResume) {
    TestTempDir tmp("fte_upload_resume");
    auto content = MakeText(300 * 1024);
    WriteFile(tmp.join("src.txt"), content);

    Loopback lb;
    // 第一轮:传到一半"断线"
    lb.a->SendFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));
    uint64_t partial = 0;
    for (int i = 0; i < 100; ++i) {
        lb.a->Tick();
        lb.b->Tick();
        std::error_code ec;
        auto sz = std::filesystem::file_size(tmp.join("dst.txt.download"), ec);
        if (!ec && sz > 0) {
            partial = sz;
            if (partial >= content.size() / 2) break;
        }
    }
    ASSERT_GT(partial, 0u);
    ASSERT_LT(partial, content.size());
    ASSERT_TRUE(std::filesystem::exists(tmp.join("dst.txt.digest")));
    // 断线:两侧作业清空,被控侧保留 .download/.digest
    lb.a->DisconnectCleanup();
    lb.b->DisconnectCleanup();

    // 第二轮:is_resume=true 续传
    int blocks_before = lb.blocks_to_b;
    lb.a->SendFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"), 0, true);
    lb.Pump();

    EXPECT_EQ(ReadFile(tmp.join("dst.txt")), content);
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.download")));
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.digest")));
    // 续传证据:被控回了一个 >0 的字节偏移,且第二轮只补传剩余块
    ASSERT_FALSE(lb.confirm_offsets_to_b.empty());
    EXPECT_EQ(lb.confirm_offsets_to_b.back(), partial);
    int resume_blocks = lb.blocks_to_b - blocks_before;
    int full_blocks =
        static_cast<int>((content.size() + kBlockPayloadSize - 1) / kBlockPayloadSize);
    EXPECT_LT(resume_blocks, full_blocks);
}

// 回归:收尾 rename 失败(目标被独占占用)时不得静默成功——
// 保留 .download/.digest 供续传,作业以错误终结并回 new_error 给主控;
// 解除占用后按 digest.is_resume 续传可正常完成。
// (上游 fs.rs:717-718 先删 .digest 再 .ok() 吞 rename 失败,会产生无凭证孤儿)
TEST(FtEngine, FinalizeRenameFailureKeepsResumeState) {
#ifndef _WIN32
    GTEST_SKIP() << "独占文件锁构造依赖 Win32 CreateFile";
#else
    TestTempDir tmp("fte_rename_fail");
    auto content = MakeText(200 * 1024);
    WriteFile(tmp.join("src.txt"), content);
    WriteFile(tmp.join("dst.txt"), MakeText(16 * 1024)); // 旧内容,待覆盖

    // 独占打开 dst.txt(share=0),使 B 侧收尾 rename(.download -> dst.txt)失败
    HANDLE lock = CreateFileW(tmp.join("dst.txt").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE);

    Loopback lb;
    lb.a->SendFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"));
    // 冲突确认:B 回发 is_upload=true digest,A 决策覆盖
    for (int i = 0; i < 100 && lb.a_confirm_reqs.empty(); ++i) {
        lb.a->Tick();
        lb.b->Tick();
    }
    ASSERT_EQ(lb.a_confirm_reqs.size(), 1u);
    lb.a->ConfirmFile(std::get<0>(lb.a_confirm_reqs[0]), std::get<1>(lb.a_confirm_reqs[0]), true,
                      0);
    lb.Pump();

    // 块全部写完,但收尾 rename 失败:作业以错误终结,凭证保留
    ASSERT_EQ(lb.b_done.size(), 1u);
    EXPECT_NE(lb.b_done[0].second.find("rename"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(tmp.join("dst.txt.download")));
    EXPECT_TRUE(std::filesystem::exists(tmp.join("dst.txt.digest")));
    EXPECT_EQ(std::filesystem::file_size(tmp.join("dst.txt.download")), content.size());
    EXPECT_NE(ReadFile(tmp.join("dst.txt")), content); // 旧内容未被覆盖

    // 解除占用,续传:.digest 凭证在,按偏移(full size)续传后收尾成功
    CloseHandle(lock);
    lb.a->SendFiles(tmp.str("src.txt"), false, tmp.str("dst.txt"), 0, true);
    lb.Pump();
    EXPECT_EQ(ReadFile(tmp.join("dst.txt")), content);
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.download")));
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.digest")));
    ASSERT_EQ(lb.b_done.size(), 2u);
    EXPECT_TRUE(lb.b_done[1].second.empty());
#endif
}

// 回归:断线按连接清理——DisconnectCleanup(conn_id) 只清该连接的作业,
// 不影响其他连接在途作业(迟到的断线事件不误杀新会话/其他会话)。
TEST(FtEngine, DisconnectCleanupScopedByConnection) {
    TestTempDir tmp("fte_conn_scope");
    auto content = MakeText(64 * 1024);
    WriteFile(tmp.join("src_a.txt"), content);
    WriteFile(tmp.join("src_b.txt"), content);

    std::vector<px::Message> sent;
    FtEngine e([&](const px::Message& m) {
        sent.push_back(m);
        return true;
    });

    auto make_send_action = [](int32_t id, const std::string& path) {
        px::FileAction action;
        auto* s = action.mutable_send();
        s->set_id(id);
        s->set_path(path);
        s->set_include_hidden(false);
        s->set_file_num(0);
        return action;
    };

    // 两个连接各自建读作业;conn_a 另有一个本端写作业
    e.HandleFileAction(make_send_action(1, tmp.str("src_a.txt")), "conn_a");
    e.HandleFileAction(make_send_action(2, tmp.str("src_b.txt")), "conn_b");
    int32_t wid = e.ReceiveFiles(tmp.str("remote_x.txt"), false, tmp.str("local_x.txt"),
                                 0, false, "conn_a");
    (void)wid;
    ASSERT_EQ(e.read_jobs().size(), 2u);
    ASSERT_EQ(e.write_jobs().size(), 1u);

    // 只断 conn_a:它的读/写作业都移除,conn_b 的读作业保留
    e.DisconnectCleanup("conn_a");
    ASSERT_EQ(e.read_jobs().size(), 1u);
    EXPECT_EQ(e.read_jobs()[0].id(), 2);
    EXPECT_EQ(e.read_jobs()[0].conn_id(), "conn_b");
    EXPECT_TRUE(e.write_jobs().empty());

    // conn_b 的作业仍在正常推进:tick 后发出它自己的 digest
    sent.clear();
    e.Tick();
    bool saw_digest_for_job2 = false;
    for (const auto& m : sent) {
        if (m.has_file_response() && m.file_response().has_digest() &&
            m.file_response().digest().id() == 2) {
            saw_digest_for_job2 = true;
        }
    }
    EXPECT_TRUE(saw_digest_for_job2);

    // 空 conn_id = 清全部(旧语义,单连接/插件停止场景)
    e.DisconnectCleanup("");
    EXPECT_TRUE(e.read_jobs().empty());
    EXPECT_TRUE(e.write_jobs().empty());
}

} // namespace
} // namespace px::ft
