#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#endif

#include "ft_sha256.h"
#include "transfer_job.h"

namespace px::ft {
namespace {

class TestTempDir final {
public:
    explicit TestTempDir(const std::string& prefix) {
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
        path_ = std::filesystem::temp_directory_path() /
                (prefix + "_" + std::to_string(_getpid()) + "_" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TestTempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] std::filesystem::path Join(const std::string& name) const {
        return path_ / ToFsPath(name);
    }

private:
    std::filesystem::path path_;
};

TransferJob MakeWriter(const std::filesystem::path& destination,
                       std::uint64_t capabilities = kFtCurrentCapabilities) {
    auto writer = TransferJob::NewWrite(7, JobType::Generic, "source",
                                        DataSource{destination}, 0, false, true, true);
    px::FileEntry entry;
    entry.set_entry_type(px::RegularFile);
    entry.set_size(0);
    entry.set_modified_time(1);
    writer.SetFiles({entry});
    writer.set_digest(0, 1);
    writer.set_peer_capabilities(capabilities);
    return writer;
}

px::FileTransferBlock MakeBlock(std::uint32_t block_id, std::string data) {
    px::FileTransferBlock block;
    block.set_id(7);
    block.set_file_num(0);
    block.set_blk_id(block_id);
    block.set_data(std::move(data));
    return block;
}

std::string Hash(std::string_view value) {
    Sha256Hasher hasher;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(value.size());
    for (const auto character : value) {
        bytes.push_back(static_cast<std::uint8_t>(character));
    }
    hasher.Update(bytes);
    return Sha256Bytes(hasher.Finalize());
}

void WriteFile(const std::filesystem::path& path, std::string_view value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

TEST(FtIntegrity, Sha256KnownVectors) {
    EXPECT_EQ(Hash(""), std::string("\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24"
                                    "\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55", 32));
    EXPECT_EQ(Hash("abc"), std::string("\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23"
                                       "\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad", 32));
}

TEST(FtIntegrity, SenderAdvertisesCapabilitiesAndEmitsSequencedHashedEof) {
    TestTempDir temp("ft_integrity_sender");
    const auto source = temp.Join("source.bin");
    WriteFile(source, "abc");
    auto reader = TransferJob::NewRead(7, JobType::Generic, "destination",
                                       DataSource{source}, 0, false, false, true);

    std::vector<px::Message> messages;
    ASSERT_TRUE(reader.InitDataStream([&messages](const px::Message& message) {
        messages.push_back(message);
        return true;
    }));
    ASSERT_EQ(messages.size(), 1U);
    ASSERT_TRUE(messages.front().file_response().has_digest());
    EXPECT_EQ(messages.front().file_response().digest().capabilities(),
              kFtCurrentCapabilities);

    px::FileTransferSendConfirmRequest confirmation;
    confirmation.set_id(7);
    confirmation.set_file_num(0);
    confirmation.set_offset_blk(0);
    ASSERT_TRUE(reader.Confirm(confirmation));

    const auto payload = reader.Read();
    ASSERT_TRUE(payload);
    EXPECT_EQ(payload->blk_id(), 1U);
    EXPECT_EQ(payload->data(), "abc");
    EXPECT_TRUE(payload->file_hash().empty());

    const auto eof = reader.Read();
    ASSERT_TRUE(eof);
    EXPECT_EQ(eof->blk_id(), 2U);
    EXPECT_TRUE(eof->data().empty());
    EXPECT_EQ(eof->file_hash(), Hash("abc"));
}

TEST(FtIntegrity, AcceptsOneThousandStrictlyOrderedBlocksAndSha256) {
    TestTempDir temp("ft_integrity_1000");
    auto writer = MakeWriter(temp.Join("received.bin"));
    Sha256Hasher expected;
    for (std::uint32_t block_id = 1; block_id <= 1000; ++block_id) {
        const auto value = static_cast<char>('a' + (block_id % 26));
        const std::string data(1, value);
        const std::array<std::uint8_t, 1> byte{static_cast<std::uint8_t>(value)};
        expected.Update(byte);
        writer.Write(MakeBlock(block_id, data));
    }
    auto eof = MakeBlock(1001, {});
    eof.set_file_hash(Sha256Bytes(expected.Finalize()));
    EXPECT_NO_THROW(writer.Write(eof));
    EXPECT_NO_THROW(writer.ModifyTime());
    EXPECT_TRUE(std::filesystem::exists(temp.Join("received.bin")));
    EXPECT_FALSE(std::filesystem::exists(temp.Join("received.bin.download")));
}

TEST(FtIntegrity, RejectsDuplicateMissingAndOutOfOrderBlockBeforeWrite) {
    for (const std::uint32_t invalid_id : {1U, 3U, 99U}) {
        TestTempDir temp("ft_integrity_order");
        auto writer = MakeWriter(temp.Join("received.bin"));
        writer.Write(MakeBlock(1, "first"));
        EXPECT_THROW(writer.Write(MakeBlock(invalid_id, "invalid")), std::runtime_error);
        EXPECT_FALSE(std::filesystem::exists(temp.Join("received.bin")));
        EXPECT_TRUE(std::filesystem::exists(temp.Join("received.bin.download")));
    }
}

TEST(FtIntegrity, RejectsCorruptedPayloadAtSha256GateAndKeepsTemporaryFile) {
    TestTempDir temp("ft_integrity_corrupt");
    auto writer = MakeWriter(temp.Join("received.bin"));
    writer.Write(MakeBlock(1, "corrupted"));
    auto eof = MakeBlock(2, {});
    eof.set_file_hash(Hash("expected"));
    EXPECT_THROW(writer.Write(eof), std::runtime_error);
    EXPECT_THROW(writer.ModifyTime(), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(temp.Join("received.bin")));
    EXPECT_TRUE(std::filesystem::exists(temp.Join("received.bin.download")));
    EXPECT_TRUE(std::filesystem::exists(temp.Join("received.bin.digest")));
}

TEST(FtIntegrity, RejectsMissingFinalHashWhenCapabilityWasNegotiated) {
    TestTempDir temp("ft_integrity_missing_hash");
    auto writer = MakeWriter(temp.Join("received.bin"));
    writer.Write(MakeBlock(1, "payload"));
    EXPECT_THROW(writer.Write(MakeBlock(2, {})), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(temp.Join("received.bin")));
}

TEST(FtIntegrity, LegacyPeerWithoutCapabilitiesRemainsCompatible) {
    TestTempDir temp("ft_integrity_legacy");
    auto writer = MakeWriter(temp.Join("received.bin"), 0);
    writer.Write(MakeBlock(0, "legacy"));
    writer.Write(MakeBlock(0, {}));
    EXPECT_NO_THROW(writer.ModifyTime());
    EXPECT_TRUE(std::filesystem::exists(temp.Join("received.bin")));
}

TEST(FtIntegrity, ResetsBlockSequenceAndHashForEveryFile) {
    TestTempDir temp("ft_integrity_multiple_files");
    const auto source = temp.Join("source");
    const auto destination = temp.Join("destination");
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(destination);
    WriteFile(source / "first.txt", "first payload");
    WriteFile(source / "second.txt", "second payload");

    auto reader = TransferJob::NewRead(7, JobType::Generic, ToUtf8(destination),
                                       DataSource{source}, 0, false, false, true);
    auto writer = TransferJob::NewWrite(7, JobType::Generic, ToUtf8(source),
                                        DataSource{destination}, 0, false, true, true);
    writer.SetFiles(reader.files());
    writer.set_peer_capabilities(kFtCurrentCapabilities);

    std::vector<std::uint32_t> first_block_ids;
    int current_file = -1;
    while (reader.InitDataStream([&reader](const px::Message& message) {
        const auto& digest = message.file_response().digest();
        px::FileTransferSendConfirmRequest confirmation;
        confirmation.set_id(digest.id());
        confirmation.set_file_num(digest.file_num());
        confirmation.set_offset_blk(0);
        reader.Confirm(confirmation);
        return true;
    })) {
        const auto block = reader.Read();
        if (!block) continue;
        if (block->file_num() != current_file) {
            current_file = block->file_num();
            first_block_ids.push_back(block->blk_id());
        }
        writer.Write(*block);
    }
    EXPECT_NO_THROW(writer.ModifyTime());

    ASSERT_EQ(first_block_ids.size(), 2U);
    EXPECT_EQ(first_block_ids[0], 1U);
    EXPECT_EQ(first_block_ids[1], 1U);
    EXPECT_EQ(ReadFile(destination / "first.txt"), "first payload");
    EXPECT_EQ(ReadFile(destination / "second.txt"), "second payload");
}

TEST(FtIntegrity, ResumeHashIncludesExistingPrefix) {
    TestTempDir temp("ft_integrity_resume");
    const auto source = temp.Join("source.bin");
    const auto destination = temp.Join("received.bin");
    constexpr std::string_view prefix = "already transferred ";
    constexpr std::string_view suffix = "remaining payload";
    WriteFile(source, std::string(prefix) + std::string(suffix));
    WriteFile(ToFsPath(ToUtf8(destination) + ".download"), prefix);
    WriteFile(ToFsPath(ToUtf8(destination) + ".digest"),
              R"({"size":35,"modified":1})");

    auto reader = TransferJob::NewRead(7, JobType::Generic, ToUtf8(destination),
                                       DataSource{source}, 0, false, false, true);
    ASSERT_TRUE(reader.InitDataStream([&reader](const px::Message& message) {
        px::FileTransferSendConfirmRequest confirmation;
        confirmation.set_id(message.file_response().digest().id());
        confirmation.set_file_num(message.file_response().digest().file_num());
        confirmation.set_offset_blk(prefix.size());
        reader.Confirm(confirmation);
        return true;
    }));

    auto writer = MakeWriter(destination);
    writer.SetStreamOffset(0, prefix.size());
    const auto payload = reader.Read();
    ASSERT_TRUE(payload);
    EXPECT_EQ(payload->blk_id(), 1U);
    EXPECT_EQ(payload->data(), suffix);
    writer.Write(*payload);
    const auto eof = reader.Read();
    ASSERT_TRUE(eof);
    EXPECT_EQ(eof->blk_id(), 2U);
    writer.Write(*eof);
    EXPECT_NO_THROW(writer.ModifyTime());
    EXPECT_EQ(ReadFile(destination), std::string(prefix) + std::string(suffix));
}

} // namespace
} // namespace px::ft
