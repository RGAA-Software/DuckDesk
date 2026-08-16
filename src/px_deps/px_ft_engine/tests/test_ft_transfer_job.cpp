// TransferJob 引擎单测 - 读写往返 / 续传 offset / 覆盖决策三分支 / 取消清理 / digest 凭证生命周期
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#endif

#include "ft_compress.h"
#include "transfer_job.h"

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
    std::string line = "file transfer engine test line, compressible text payload.\n";
    while (v.size() < approx) v.insert(v.end(), line.begin(), line.end());
    return v;
}

SendFunc CaptureSend(std::vector<px::Message>* box) {
    return [box](const px::Message& m) {
        box->push_back(m);
        return true;
    };
}

// 完整跑一遍读作业 -> 写作业 的块搬运(读侧需已确认;内部交错 InitDataStream 以切换文件)
void PumpBlocks(TransferJob& reader, TransferJob& writer) {
    std::vector<px::Message> sink;
    while (true) {
        reader.InitDataStream(CaptureSend(&sink)); // 返回 false 表示作业无更多文件
        auto block = reader.Read();
        if (!block) {
            if (static_cast<size_t>(reader.file_num()) >= reader.files().size()) break;
            continue; // 当前文件读完,推进下一文件
        }
        writer.Write(*block);
    }
}

// fs.rs 读写引擎:写盘 -> 读盘往返内容一致(含 digest 握手)
TEST(TransferJob, WriteReadRoundtripContentEqual) {
    TestTempDir tmp("ft_roundtrip");
    auto src = tmp.join("src.bin");
    auto content = MakeText(500 * 1024);
    WriteFile(src, content);
    uint64_t mtime = GetFileMtimeSecs(src);

    auto reader = TransferJob::NewRead(1, JobType::Generic, "dst", DataSource{src}, 0, false,
                                       false, true);
    auto writer = TransferJob::NewWrite(1, JobType::Generic, "src",
                                        DataSource{tmp.join("out")}, 0, false, true, true);
    writer.SetFiles(reader.files());

    // Digest 握手:读侧发 digest -> 写侧 NoSuchFile -> offset 0 确认
    std::vector<px::Message> outbox;
    ASSERT_TRUE(reader.InitDataStream(CaptureSend(&outbox)));
    ASSERT_EQ(outbox.size(), 1u);
    ASSERT_TRUE(outbox[0].file_response().has_digest());
    const auto& digest = outbox[0].file_response().digest();
    EXPECT_TRUE(reader.file_is_waiting());
    EXPECT_FALSE(reader.Read().has_value()); // 等待确认时不读盘

    std::string write_path = ToUtf8(tmp.join("out") / ToFsPath(writer.files()[0].name()));
    // 单文件空名场景:写侧目标即 data_source 本身
    if (writer.files()[0].name().empty()) write_path = ToUtf8(tmp.join("out"));
    writer.set_digest(digest.file_size(), digest.last_modified());
    auto check = IsWriteNeedConfirmation(false, write_path, digest);
    ASSERT_EQ(check.kind, DigestCheckResult::Kind::NoSuchFile);

    px::FileTransferSendConfirmRequest req;
    req.set_id(1);
    req.set_file_num(0);
    req.set_offset_blk(0);
    ASSERT_TRUE(reader.Confirm(req));
    writer.Confirm(req);
    EXPECT_TRUE(reader.file_confirmed());

    PumpBlocks(reader, writer);
    EXPECT_TRUE(reader.job_completed());
    writer.ModifyTime();

    auto written = ReadFile(tmp.join("out")); // 单文件空名:写入 data_source 路径本身
    EXPECT_EQ(written, content);
    // .download/.digest 已清理,mtime 已恢复
    EXPECT_FALSE(std::filesystem::exists(tmp.join("out.download")));
    EXPECT_FALSE(std::filesystem::exists(tmp.join("out.digest")));
    EXPECT_EQ(GetFileMtimeSecs(tmp.join("out")), mtime);
}

// fs.rs 续传:传到一半中断 -> 重新 Digest(is_resume) -> offset 续传 -> 内容一致
TEST(TransferJob, ResumeFromOffset) {
    TestTempDir tmp("ft_resume");
    auto src = tmp.join("src.txt");
    auto content = MakeText(300 * 1024);
    WriteFile(src, content);
    auto dst = tmp.join("dst.txt");
    std::string dst_str = ToUtf8(dst);

    // ---- 第一轮:传一半中断 ----
    // (作业置于内层作用域:中断即销毁,释放 .download 文件句柄,Windows 下 rename 才可行)
    uint64_t transferred_size = 0;
    std::vector<px::Message> outbox;
    {
        auto r1 = TransferJob::NewRead(1, JobType::Generic, "dst", DataSource{src}, 0, false,
                                       false, true);
        auto w1 = TransferJob::NewWrite(1, JobType::Generic, "src", DataSource{dst}, 0, false,
                                        true, true);
        w1.SetFiles(r1.files());
        ASSERT_TRUE(r1.InitDataStream(CaptureSend(&outbox)));
        const auto digest = outbox[0].file_response().digest();
        w1.set_digest(digest.file_size(), digest.last_modified());
        px::FileTransferSendConfirmRequest req0;
        req0.set_id(1);
        req0.set_file_num(0);
        req0.set_offset_blk(0);
        r1.Confirm(req0);
        w1.Confirm(req0);

        // 传到一半(按写侧解压后落盘字节数计,与 .download 大小一致)
        while (true) {
            auto block = r1.Read();
            ASSERT_TRUE(block.has_value());
            w1.Write(*block);
            if (w1.finished_size() >= content.size() / 2) break;
        }
        transferred_size = w1.finished_size(); // = .download 落盘字节数
    }
    // 中断:不做 ModifyTime,保留 .download/.digest
    ASSERT_GT(transferred_size, 0u);
    ASSERT_LT(transferred_size, content.size());
    // 中断:不做 ModifyTime,保留 .download/.digest

    // ---- 第二轮:is_resume 续传 ----
    auto r2 = TransferJob::NewRead(1, JobType::Generic, "dst", DataSource{src}, 0, false, false,
                                   true);
    r2.is_resume = true;
    auto w2 = TransferJob::NewWrite(1, JobType::Generic, "src", DataSource{dst}, 0, false, true,
                                    true);
    w2.is_resume = true;
    w2.SetFiles(r2.files());
    w2.SetFinishedSizeOnResume();

    outbox.clear();
    ASSERT_TRUE(r2.InitDataStream(CaptureSend(&outbox)));
    const auto digest2 = outbox[0].file_response().digest();
    ASSERT_TRUE(digest2.is_resume());
    w2.set_digest(digest2.file_size(), digest2.last_modified());
    auto check = IsWriteNeedConfirmation(true, dst_str, digest2);
    ASSERT_EQ(check.kind, DigestCheckResult::Kind::NeedConfirm);
    EXPECT_TRUE(check.digest.is_identical());
    EXPECT_EQ(check.digest.transferred_size(), transferred_size);

    px::FileTransferSendConfirmRequest req;
    req.set_id(1);
    req.set_file_num(0);
    req.set_offset_blk(static_cast<uint32_t>(check.digest.transferred_size())); // 字节偏移
    w2.Confirm(req); // 写侧:set_stream_offset 打开 .download 定位(fs.rs:1115)
    r2.Confirm(req); // 读侧:定位读流
    EXPECT_EQ(w2.finished_size(), transferred_size); // DEBUG: confirm 后写侧应计入已传偏移
    EXPECT_TRUE(r2.file_confirmed());

    PumpBlocks(r2, w2);
    EXPECT_EQ(w2.finished_size(), content.size()); // DEBUG
    w2.ModifyTime();

    EXPECT_EQ(ReadFile(dst), content);
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.download")));
    EXPECT_FALSE(std::filesystem::exists(tmp.join("dst.txt.digest")));
}

// fs.rs:1449 is_write_need_confirmation 三分支
TEST(TransferJob, OverwriteDecisionThreeBranches) {
    TestTempDir tmp("ft_decision");
    auto dst = tmp.join("file.bin");
    std::string dst_str = ToUtf8(dst);
    auto content = MakeText(64 * 1024);

    px::FileTransferDigest digest;
    digest.set_id(1);
    digest.set_file_num(0);
    digest.set_file_size(content.size());
    digest.set_last_modified(1234567890);

    // 分支 1:NoSuchFile
    auto r1 = IsWriteNeedConfirmation(false, dst_str, digest);
    EXPECT_EQ(r1.kind, DigestCheckResult::Kind::NoSuchFile);

    // 分支 2:同名同 size+mtime -> identical
    WriteFile(dst, content);
    ASSERT_TRUE(SetFileMtimeSecs(dst, 1234567890));
    auto r2 = IsWriteNeedConfirmation(false, dst_str, digest);
    ASSERT_EQ(r2.kind, DigestCheckResult::Kind::NeedConfirm);
    EXPECT_TRUE(r2.digest.is_identical());
    EXPECT_EQ(r2.digest.file_size(), content.size());
    EXPECT_EQ(r2.digest.last_modified(), 1234567890u);

    // 分支 3:同 size 不同 mtime -> 需确认,非 identical
    ASSERT_TRUE(SetFileMtimeSecs(dst, 1234567891));
    auto r3 = IsWriteNeedConfirmation(false, dst_str, digest);
    ASSERT_EQ(r3.kind, DigestCheckResult::Kind::NeedConfirm);
    EXPECT_FALSE(r3.digest.is_identical());

    // 不同 size -> 需确认,非 identical
    WriteFile(dst, MakeText(32 * 1024));
    ASSERT_TRUE(SetFileMtimeSecs(dst, 1234567890));
    auto r4 = IsWriteNeedConfirmation(false, dst_str, digest);
    ASSERT_EQ(r4.kind, DigestCheckResult::Kind::NeedConfirm);
    EXPECT_FALSE(r4.digest.is_identical());

    // 续传分支:.digest+.download 存在且凭证匹配 -> transferred_size
    std::filesystem::remove(dst);
    auto part = MakeText(16 * 1024);
    part.resize(16 * 1024); // 精确 16KB(MakeText 按行生成会超出)
    WriteFile(tmp.join("file.bin.download"), part);
    {
        std::ofstream ofs(tmp.join("file.bin.digest"), std::ios::binary);
        ofs << "{\"size\":" << content.size() << ",\"modified\":1234567890}";
    }
    auto r5 = IsWriteNeedConfirmation(true, dst_str, digest);
    ASSERT_EQ(r5.kind, DigestCheckResult::Kind::NeedConfirm);
    EXPECT_TRUE(r5.digest.is_identical());
    EXPECT_EQ(r5.digest.transferred_size(), 16 * 1024u);

    // is_resume=false 时不消费 .digest(见 proto 注释)-> NoSuchFile(正式文件不存在)
    auto r6 = IsWriteNeedConfirmation(false, dst_str, digest);
    EXPECT_EQ(r6.kind, DigestCheckResult::Kind::NoSuchFile);

    // 凭证不匹配(size 不同)-> 不续传
    px::FileTransferDigest other = digest;
    other.set_file_size(content.size() + 1);
    auto r7 = IsWriteNeedConfirmation(true, dst_str, other);
    EXPECT_EQ(r7.kind, DigestCheckResult::Kind::NoSuchFile);
}

// fs.rs:760/704 .digest 凭证生命周期:写首块时生成,ModifyTime 时清理
TEST(TransferJob, DigestFileLifecycle) {
    TestTempDir tmp("ft_digest_lifecycle");
    auto dst = tmp.join("d") / "file.txt";
    auto content = MakeText(150 * 1024);

    auto writer = TransferJob::NewWrite(1, JobType::Generic, "src", DataSource{tmp.join("d")}, 0,
                                        false, true, true);
    px::FileEntry entry;
    entry.set_name("file.txt");
    entry.set_size(content.size());
    entry.set_modified_time(1234567890);
    writer.SetFiles({entry});
    writer.set_digest(content.size(), 1234567890);

    px::FileTransferBlock block;
    block.set_id(1);
    block.set_file_num(0);
    block.set_data(content.data(), content.size());
    writer.Write(block);

    // 凭证已生成且内容正确
    auto digest_path = tmp.join("d") / "file.txt.digest";
    auto download_path = tmp.join("d") / "file.txt.download";
    ASSERT_TRUE(std::filesystem::exists(digest_path));
    ASSERT_TRUE(std::filesystem::exists(download_path));
    std::string digest_json = [&] {
        std::ifstream ifs(digest_path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    }();
    EXPECT_NE(digest_json.find("\"size\":" + std::to_string(content.size())),
              std::string::npos);
    EXPECT_NE(digest_json.find("\"modified\":1234567890"), std::string::npos);

    writer.ModifyTime();
    EXPECT_FALSE(std::filesystem::exists(digest_path));
    EXPECT_FALSE(std::filesystem::exists(download_path));
    EXPECT_EQ(ReadFile(dst), content);
    EXPECT_EQ(GetFileMtimeSecs(dst), 1234567890u);
}

// fs.rs:728 remove_download_file:显式取消清理
TEST(TransferJob, CancelRemovesDownloadFiles) {
    TestTempDir tmp("ft_cancel");
    auto writer = TransferJob::NewWrite(1, JobType::Generic, "src", DataSource{tmp.join("d")}, 0,
                                        false, true, true);
    px::FileEntry entry;
    entry.set_name("file.txt");
    entry.set_size(100);
    writer.SetFiles({entry});
    writer.set_digest(100, 42);

    px::FileTransferBlock block;
    block.set_id(1);
    block.set_file_num(0);
    std::string data(50, 'x');
    block.set_data(data);
    writer.Write(block);

    auto base = tmp.join("d") / "file.txt";
    ASSERT_TRUE(std::filesystem::exists(ToFsPath(ToUtf8(base) + ".download")));
    ASSERT_TRUE(std::filesystem::exists(ToFsPath(ToUtf8(base) + ".digest")));

    writer.RemoveDownloadFile();
    EXPECT_FALSE(std::filesystem::exists(ToFsPath(ToUtf8(base) + ".download")));
    EXPECT_FALSE(std::filesystem::exists(ToFsPath(ToUtf8(base) + ".digest")));
}

// 错误块校验:错误 id / 错误 file_num
TEST(TransferJob, WriteRejectsWrongIdAndFileNum) {
    TestTempDir tmp("ft_wrong");
    auto writer = TransferJob::NewWrite(1, JobType::Generic, "src", DataSource{tmp.join("d")}, 0,
                                        false, true, true);
    px::FileEntry entry;
    entry.set_name("a.txt");
    entry.set_size(10);
    writer.SetFiles({entry});

    px::FileTransferBlock block;
    block.set_id(2); // wrong id
    block.set_file_num(0);
    std::string data(10, 'y');
    block.set_data(data);
    EXPECT_THROW(writer.Write(block), std::runtime_error);

    block.set_id(1);
    block.set_file_num(9); // wrong file_num
    EXPECT_THROW(writer.Write(block), std::runtime_error);
}

// 多文件递归往返:new_read 递归展开目录 -> 逐文件块搬运 -> 内容逐一一致
TEST(TransferJob, MultiFileRecursiveRoundtrip) {
    TestTempDir tmp("ft_multi");
    auto src_dir = tmp.join("srcdir");
    auto c1 = MakeText(10 * 1024);
    auto c2 = MakeText(250 * 1024);
    WriteFile(src_dir / "a.txt", c1);
    WriteFile(src_dir / "sub" / "b.txt", c2);
    std::filesystem::create_directories(src_dir / "emptydir");

    auto reader = TransferJob::NewRead(1, JobType::Generic, "dst", DataSource{src_dir}, 0, false,
                                       false, false); // 不开覆盖检测:逐文件直通
    ASSERT_EQ(reader.files().size(), 2u);

    auto writer = TransferJob::NewWrite(1, JobType::Generic, "src", DataSource{tmp.join("dstdir")},
                                        0, false, true, false);
    writer.SetFiles(reader.files());

    PumpBlocks(reader, writer);
    // 收尾最后一个文件
    writer.ModifyTime();

    EXPECT_EQ(ReadFile(tmp.join("dstdir") / "a.txt"), c1);
    EXPECT_EQ(ReadFile(tmp.join("dstdir") / "sub" / "b.txt"), c2);
}

// 已压缩后缀文件不走压缩路径(fs.rs:992)
TEST(TransferJob, CompressedExtSentUncompressed) {
    TestTempDir tmp("ft_no_compress");
    auto src = tmp.join("photo.jpg");
    auto content = MakeText(200 * 1024); // 可压缩内容,但后缀 jpg 应跳过压缩
    WriteFile(src, content);

    auto reader = TransferJob::NewRead(1, JobType::Generic, "dst", DataSource{src}, 0, false,
                                       false, false);
    std::vector<px::Message> sink;
    ASSERT_TRUE(reader.InitDataStream(CaptureSend(&sink)));
    auto block = reader.Read();
    ASSERT_TRUE(block.has_value());
    EXPECT_FALSE(block->compressed());
    EXPECT_EQ(block->data().size(), 120 * 1024u); // 未压缩直读

    auto src2 = tmp.join("text.txt");
    WriteFile(src2, content);
    auto reader2 = TransferJob::NewRead(2, JobType::Generic, "dst", DataSource{src2}, 0, false,
                                        false, false);
    ASSERT_TRUE(reader2.InitDataStream(CaptureSend(&sink)));
    auto block2 = reader2.Read();
    ASSERT_TRUE(block2.has_value());
    EXPECT_TRUE(block2->compressed()); // 文本应被压缩
    EXPECT_LT(block2->data().size(), 120 * 1024u);
}

// fs.rs:1095/1070 跳过语义
TEST(TransferJob, SkipSemantics) {
    TestTempDir tmp("ft_skip");
    auto src = tmp.join("a.txt");
    WriteFile(src, MakeText(1024));
    auto reader = TransferJob::NewRead(1, JobType::Generic, "dst", DataSource{src}, 0, false,
                                       false, true);
    std::vector<px::Message> outbox;
    ASSERT_TRUE(reader.InitDataStream(CaptureSend(&outbox)));
    EXPECT_TRUE(reader.file_is_waiting());

    px::FileTransferSendConfirmRequest req;
    req.set_id(1);
    req.set_file_num(0);
    req.set_skip(true);
    reader.Confirm(req);
    EXPECT_TRUE(reader.file_skipped());
    EXPECT_TRUE(reader.job_skipped()); // 单文件 -> 整任务跳过
    ASSERT_TRUE(reader.job_error().has_value());
    EXPECT_EQ(*reader.job_error(), "skipped");
    // 跳过后 read 返回 nullopt 且 job_completed
    EXPECT_FALSE(reader.Read().has_value());
    EXPECT_TRUE(reader.job_completed());
}

} // namespace
} // namespace px::ft
