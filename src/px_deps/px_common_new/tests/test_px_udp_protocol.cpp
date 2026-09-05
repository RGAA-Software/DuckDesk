//
// Created by RGAA on 12/08/2026.
// Unit tests for px_udp_protocol.h (shard / reassemble / ctrl packets)
//

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

#include "px_common_new/px_udp_protocol.h"

using namespace px;

static std::string MakeFrameBytes(size_t size) {
    std::string s(size, '\0');
    for (size_t i = 0; i < size; i++) s[i] = (char)(i % 251);
    return s;
}

static PxUdpProtocol::VideoFrameMeta MakeMeta(uint32_t frame_index, bool key) {
    PxUdpProtocol::VideoFrameMeta meta;
    meta.frame_index_ = frame_index;
    meta.timestamp_ms_ = 123456;
    meta.key_ = key;
    meta.codec_ = PxUdpProtocol::kCodecH264;
    meta.frame_width_ = 1920;
    meta.frame_height_ = 1080;
    meta.mon_slot_ = 1;
    meta.mon_name_ = R"(\\.\DISPLAY1)";
    return meta;
}

TEST(PxUdpProtocol, CommonHeader) {
    char buf[4];
    PxUdpProtocol::WriteCommon(buf, PxUdpProtocol::kPktVideo);
    EXPECT_EQ(PxUdpProtocol::ParseCommon(std::span<const char>{buf}), PxUdpProtocol::kPktVideo);
    buf[0] = 0; // break magic
    EXPECT_EQ(PxUdpProtocol::ParseCommon(std::span<const char>{buf}), 0);
    EXPECT_EQ(PxUdpProtocol::ParseCommon(std::span<const char>{buf}.first(2)), 0);
}

TEST(PxUdpProtocol, ShardSmallFrameSinglePacket) {
    auto frame = MakeFrameBytes(500);
    auto meta = MakeMeta(7, true);
    auto pkts = PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame});
    ASSERT_EQ(pkts.size(), 1u);

    PxUdpProtocol::VideoShardInfo shard;
    ASSERT_TRUE(PxUdpProtocol::ParseVideoShard(pkts[0]->Bytes(), shard));
    EXPECT_EQ(shard.frame_index_, 7u);
    EXPECT_EQ(shard.data_shards_, 1u);
    EXPECT_EQ(shard.shard_index_, 0u);
    EXPECT_TRUE(shard.flags_ & PxUdpProtocol::kFlagSof);
    EXPECT_TRUE(shard.flags_ & PxUdpProtocol::kFlagEof);
    EXPECT_TRUE(shard.flags_ & PxUdpProtocol::kFlagKey);
    EXPECT_EQ(shard.mon_name_, meta.mon_name_);
    EXPECT_EQ(shard.frame_width_, 1920);
    EXPECT_EQ(shard.frame_size_, 500u);
    EXPECT_EQ(shard.codec_, PxUdpProtocol::kCodecH264);
    ASSERT_EQ(shard.payload_len_, 500u);
    EXPECT_TRUE(std::ranges::equal(shard.payload_, std::span<const char>{frame}.first(500)));
}

TEST(PxUdpProtocol, ShardLargeFrameMultiplePackets) {
    auto frame = MakeFrameBytes(10000);
    auto meta = MakeMeta(9, false);
    auto pkts = PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame});
    ASSERT_GT(pkts.size(), 1u);
    for (size_t i = 0; i < pkts.size(); i++) {
        EXPECT_LE(pkts[i]->Size(), PxUdpProtocol::kDefaultMtu);
        PxUdpProtocol::VideoShardInfo shard;
        ASSERT_TRUE(PxUdpProtocol::ParseVideoShard(pkts[i]->Bytes(), shard));
        EXPECT_EQ(shard.data_shards_, pkts.size());
        EXPECT_EQ(shard.shard_index_, i);
        EXPECT_EQ(shard.flags_ & PxUdpProtocol::kFlagSof, i == 0 ? PxUdpProtocol::kFlagSof : 0);
        // mon_name only on SOF
        if (i == 0) EXPECT_EQ(shard.mon_name_, meta.mon_name_);
        else EXPECT_TRUE(shard.mon_name_.empty());
        // frame_size only on SOF
        if (i == 0) EXPECT_EQ(shard.frame_size_, 10000u);
        else EXPECT_EQ(shard.frame_size_, 0u);
    }
}

TEST(PxUdpProtocol, ReassembleInOrder) {
    auto frame = MakeFrameBytes(8000);
    auto meta = MakeMeta(3, true);
    auto pkts = PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame});
    ASSERT_GT(pkts.size(), 1u);

    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) { FAIL() << "unexpected loss"; };

    for (auto& p : pkts) reasm.AddPacket(p->Bytes());
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].frame_index_, 3u);
    EXPECT_TRUE(frames[0].key_);
    EXPECT_EQ(frames[0].mon_name_, meta.mon_name_);
    ASSERT_EQ(frames[0].data_->Size(), frame.size());
    EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0);
}

TEST(PxUdpProtocol, ReassembleOutOfOrder) {
    auto frame = MakeFrameBytes(8000);
    auto meta = MakeMeta(4, false);
    auto pkts = PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame});
    ASSERT_GT(pkts.size(), 2u);

    // P frame as the very first frame of the stream is decodable (no prior loss)
    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };

    // deliver: SOF first, then reverse the rest
    reasm.AddPacket(pkts[0]->Bytes());
    for (size_t i = pkts.size() - 1; i >= 1; i--) reasm.AddPacket(pkts[i]->Bytes());
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].data_->Size(), frame.size());
    EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0);
}

TEST(PxUdpProtocol, ReassembleLateSofKeepsPreSofCounters) {
    // 回归:UDP 乱序下 SOF 包可能晚于部分数据/parity 包到达。旧实现收到 SOF 时把
    // received_ 清 0,导致 SOF 前已收到的块不再计入 distinct 块数;即使 parity 足够
    // 恢复,也会在下一帧到来时被误判丢帧,画面卡顿。正确行为是 SOF 只补元信息,
    // 不清空已经收到的数据/parity 计数。
    auto frame = MakeFrameBytes(20000);
    auto meta = MakeMeta(20, false);
    auto pkts = PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame},
                                               PxUdpProtocol::kDefaultMtu, 20);
    ASSERT_GT(pkts.size(), 2u);

    PxUdpProtocol::VideoShardInfo info0;
    ASSERT_TRUE(PxUdpProtocol::ParseVideoShard(pkts[0]->Bytes(), info0));
    const int data_shards = info0.data_shards_;
    const int parity_shards = info0.parity_shards_;
    ASSERT_GE(data_shards, 4);
    ASSERT_GE(parity_shards, 1);
    ASSERT_EQ((int)pkts.size(), data_shards + parity_shards);

    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) {
        frames.push_back(f);
    };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) {
        FAIL() << "enough parity exists; late SOF must not cause frame loss";
    };

    // 先到:一个数据块(非 SOF)+ 一个 parity 块;随后 SOF 才到。
    reasm.AddPacket(pkts[1]->Bytes());
    reasm.AddPacket(pkts[data_shards]->Bytes());
    reasm.AddPacket(pkts[0]->Bytes());

    // 补齐除 shard 2 外的所有数据块:数据块少 1 个,但已有 1 个 parity,足够 RS 恢复。
    for (int i = 2; i < data_shards; i++) {
        if (i == 2) {
            continue;
        }
        reasm.AddPacket(pkts[i]->Bytes());
    }

    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].data_->Size(), frame.size());
    EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0);
}

TEST(PxUdpProtocol, LossDeclaresAndDropsPUntilKey) {
    auto frame_n = MakeFrameBytes(8000);
    auto meta_n = MakeMeta(10, false);
    auto pkts_n = PxUdpProtocol::ShardVideoFrame(meta_n, std::span<const char>{frame_n});
    ASSERT_GT(pkts_n.size(), 1u);

    auto frame_n1 = MakeFrameBytes(600);
    auto meta_n1 = MakeMeta(11, false);
    auto pkts_n1 = PxUdpProtocol::ShardVideoFrame(meta_n1, std::span<const char>{frame_n1});

    auto frame_key = MakeFrameBytes(700);
    auto meta_key = MakeMeta(12, true);
    auto pkts_key = PxUdpProtocol::ShardVideoFrame(meta_key, std::span<const char>{frame_key});

    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    std::vector<uint32_t> lost;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [&](uint8_t, uint32_t idx) { lost.push_back(idx); };

    // frame 10: deliver all but the last shard -> stuck incomplete
    for (size_t i = 0; i + 1 < pkts_n.size(); i++) reasm.AddPacket(pkts_n[i]->Bytes());
    EXPECT_TRUE(frames.empty());

    // frame 11 (newer) arrives -> frame 10 declared lost
    for (auto& p : pkts_n1) reasm.AddPacket(p->Bytes());
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost[0], 10u);
    // frame 11 is a P frame after a loss -> completed but dropped
    EXPECT_TRUE(frames.empty());

    // key frame 12 -> delivered, stream recovered
    for (auto& p : pkts_key) reasm.AddPacket(p->Bytes());
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(frames[0].key_);
    EXPECT_EQ(frames[0].frame_index_, 12u);
}

TEST(PxUdpProtocol, JoinMidFrameDeclaresLoss) {
    auto frame = MakeFrameBytes(8000);
    auto meta = MakeMeta(20, false);
    auto pkts = PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame});
    ASSERT_GT(pkts.size(), 1u);

    PxUdpFrameReassembler reasm;
    std::vector<uint32_t> lost;
    reasm.on_frame_lost_ = [&](uint8_t, uint32_t idx) { lost.push_back(idx); };
    reasm.on_frame_ = [](const PxUdpFrameReassembler::CompleteFrame&) { FAIL() << "should not complete"; };

    // first seen shard is NOT a SOF -> mid-frame join
    reasm.AddPacket(pkts[1]->Bytes());
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost[0], 20u);
}

TEST(PxUdpProtocol, CtrlRoundtrip) {
    std::string s1, s2;

    auto hello = PxUdpProtocol::BuildHello("dev-123", "stream-abc");
    ASSERT_EQ(PxUdpProtocol::ParseCtrl(hello->Bytes(), s1, s2), PxUdpProtocol::kCtrlHello);
    EXPECT_EQ(s1, "dev-123");
    EXPECT_EQ(s2, "stream-abc");

    auto hb = PxUdpProtocol::BuildHeartbeat("stream-abc");
    ASSERT_EQ(PxUdpProtocol::ParseCtrl(hb->Bytes(), s1, s2), PxUdpProtocol::kCtrlHeartbeat);
    EXPECT_EQ(s1, "stream-abc");

    auto idr = PxUdpProtocol::BuildIdrRequest(R"(\\.\DISPLAY2)");
    ASSERT_EQ(PxUdpProtocol::ParseCtrl(idr->Bytes(), s1, s2), PxUdpProtocol::kCtrlIdrRequest);
    EXPECT_EQ(s1, R"(\\.\DISPLAY2)");

    auto kick = PxUdpProtocol::BuildKick("taken over");
    ASSERT_EQ(PxUdpProtocol::ParseCtrl(kick->Bytes(), s1, s2), PxUdpProtocol::kCtrlKick);
    EXPECT_EQ(s1, "taken over");

    // truncated packet rejected
    ASSERT_EQ(PxUdpProtocol::ParseCtrl(hello->Bytes().first(hello->Size() - 3), s1, s2), 0);
}

// ---------------- FEC (Reed-Solomon, P2) ----------------

// 带 FEC 切帧:返回全部包(数据包在前、parity 包随后)
static std::vector<std::shared_ptr<Data>> ShardFec(const PxUdpProtocol::VideoFrameMeta& meta,
                                                   const std::string& frame, int fec_percent) {
    return PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame},
                                          PxUdpProtocol::kDefaultMtu, fec_percent);
}

static uint16_t DataShardsOf(const std::vector<std::shared_ptr<Data>>& pkts) {
    PxUdpProtocol::VideoShardInfo shard;
    EXPECT_TRUE(PxUdpProtocol::ParseVideoShard(pkts[0]->Bytes(), shard));
    return shard.data_shards_;
}

TEST(PxUdpProtocol, FecShardLayout) {
    auto frame = MakeFrameBytes(12000);
    auto meta = MakeMeta(30, true);
    auto pkts = ShardFec(meta, frame, 20);
    uint16_t d = DataShardsOf(pkts);
    ASSERT_GT(d, 1u);
    uint16_t parity = (uint16_t)(pkts.size() - d);
    EXPECT_EQ(parity, (d * 20 + 99) / 100); // ceil(D*20%)
    for (size_t i = 0; i < pkts.size(); i++) {
        PxUdpProtocol::VideoShardInfo shard;
        ASSERT_TRUE(PxUdpProtocol::ParseVideoShard(pkts[i]->Bytes(), shard));
        EXPECT_EQ(shard.data_shards_, d);
        EXPECT_EQ(shard.parity_shards_, parity);
        EXPECT_EQ(shard.fec_block_, 0);
        if (i < d) {
            EXPECT_FALSE(shard.flags_ & PxUdpProtocol::kFlagParity);
            EXPECT_EQ(shard.shard_index_, i);
            EXPECT_LE(pkts[i]->Size(), PxUdpProtocol::kDefaultMtu);
        }
        else {
            // parity 包:kFlagParity,shard_index = D+j,整包正好 mtu,无 SOF 扩展
            EXPECT_TRUE(shard.flags_ & PxUdpProtocol::kFlagParity);
            EXPECT_TRUE(shard.flags_ & PxUdpProtocol::kFlagKey); // key 帧的 parity 也带 key 标记
            EXPECT_EQ(shard.shard_index_, d + (i - d));
            EXPECT_EQ(pkts[i]->Size(), (size_t)PxUdpProtocol::kDefaultMtu);
            EXPECT_FALSE(shard.flags_ & PxUdpProtocol::kFlagSof);
        }
    }
}

TEST(PxUdpProtocol, FecRecoversOneDataShard) {
    auto frame = MakeFrameBytes(12000);
    auto meta = MakeMeta(31, true);
    auto pkts = ShardFec(meta, frame, 20);
    uint16_t d = DataShardsOf(pkts);
    ASSERT_GT(pkts.size(), (size_t)d);

    // 丢一个中间数据 shard
    const size_t dropped = d / 2;
    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) { FAIL() << "unexpected loss"; };
    for (size_t i = 0; i < pkts.size(); i++) {
        if (i == dropped) continue;
        reasm.AddPacket(pkts[i]->Bytes());
    }
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].frame_index_, 31u);
    ASSERT_EQ(frames[0].data_->Size(), frame.size());
    EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0);
}

TEST(PxUdpProtocol, FecRecoversShardZero) {
    auto frame = MakeFrameBytes(12000);
    auto meta = MakeMeta(32, true);
    auto pkts = ShardFec(meta, frame, 20);
    uint16_t d = DataShardsOf(pkts);

    // 丢 shard 0(SOF):mon_name/分辨率/frame_size 都要从恢复块里取
    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) { FAIL() << "unexpected loss"; };
    for (size_t i = 1; i < pkts.size(); i++) {
        reasm.AddPacket(pkts[i]->Bytes());
    }
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].mon_name_, meta.mon_name_);
    EXPECT_EQ(frames[0].frame_width_, 1920);
    EXPECT_EQ(frames[0].frame_height_, 1080);
    EXPECT_TRUE(frames[0].key_);
    ASSERT_EQ(frames[0].data_->Size(), frame.size());
    EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0);
}

TEST(PxUdpProtocol, FecRecoversUpToParityCount) {
    auto frame = MakeFrameBytes(12000);
    auto meta = MakeMeta(33, false);
    auto pkts = ShardFec(meta, frame, 20);
    uint16_t d = DataShardsOf(pkts);
    size_t parity = pkts.size() - d;
    ASSERT_GE(parity, 2u);

    // 丢 parity_count 个数据 shard -> 仍可恢复
    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) { FAIL() << "unexpected loss"; };
    for (size_t i = parity; i < pkts.size(); i++) { // 跳过前 parity 个数据 shard
        reasm.AddPacket(pkts[i]->Bytes());
    }
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].data_->Size(), frame.size());
    EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0);
}

TEST(PxUdpProtocol, FecLossBeyondParityDeclaresLossAtEof) {
    auto frame_n = MakeFrameBytes(12000);
    auto meta_n = MakeMeta(40, false);
    auto pkts_n = ShardFec(meta_n, frame_n, 20);
    uint16_t d = DataShardsOf(pkts_n);
    size_t parity = pkts_n.size() - d;

    auto frame_key = MakeFrameBytes(700);
    auto meta_key = MakeMeta(41, true);
    auto pkts_key = ShardFec(meta_key, frame_key, 20);

    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    std::vector<uint32_t> lost;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [&](uint8_t, uint32_t idx) { lost.push_back(idx); };

    // 丢 parity+1 个数据 shard。EOF 到达说明发送端数据 shard 已发完，
    // 即使后续全部 parity 到达也不够恢复，因此不等待下一帧就必须判丢。
    for (size_t i = parity + 1; i < pkts_n.size(); i++) {
        reasm.AddPacket(pkts_n[i]->Bytes());
    }
    EXPECT_TRUE(frames.empty());
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost[0], 40u);

    // 迟到的旧帧包不得重复判丢；后续 key 帧正常交付，流恢复。
    for (auto& p : pkts_key) reasm.AddPacket(p->Bytes());
    EXPECT_EQ(lost.size(), 1u);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(frames[0].key_);
    EXPECT_EQ(frames[0].frame_index_, 41u);
}

TEST(PxUdpProtocol, FecDisabledBehavesLikeBefore) {
    auto frame = MakeFrameBytes(12000);
    auto meta = MakeMeta(50, true);
    auto pkts = ShardFec(meta, frame, 0);
    uint16_t d = DataShardsOf(pkts);
    // 无 parity 包,所有包 parity_shards = 0
    EXPECT_EQ(pkts.size(), (size_t)d);
    for (auto& p : pkts) {
        PxUdpProtocol::VideoShardInfo shard;
        ASSERT_TRUE(PxUdpProtocol::ParseVideoShard(p->Bytes(), shard));
        EXPECT_EQ(shard.parity_shards_, 0);
        EXPECT_FALSE(shard.flags_ & PxUdpProtocol::kFlagParity);
    }

    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) { FAIL() << "unexpected loss"; };
    for (auto& p : pkts) reasm.AddPacket(p->Bytes());
    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].data_->Size(), frame.size());
    EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0);
}

// FEC_VALIDATION 风格:遍历每个 shard 位置单独丢弃,都能恢复出原帧
TEST(PxUdpProtocol, FecRecoverEverySingleShardPosition) {
    auto frame = MakeFrameBytes(12000);
    auto meta = MakeMeta(60, true);
    auto pkts = ShardFec(meta, frame, 20);
    uint16_t d = DataShardsOf(pkts);
    ASSERT_GT(d, 1u);

    for (size_t dropped = 0; dropped < (size_t)d; dropped++) {
        PxUdpFrameReassembler reasm;
        std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
        reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
        reasm.on_frame_lost_ = [&](uint8_t, uint32_t idx) {
            FAIL() << "unexpected loss, dropped shard " << dropped << ", frame " << idx;
        };
        // 乱序投递:先 parity 再数据(跳过被丢的),覆盖 parity 先到的路径
        for (size_t i = d; i < pkts.size(); i++) reasm.AddPacket(pkts[i]->Bytes());
        for (size_t i = 0; i < (size_t)d; i++) {
            if (i == dropped) continue;
            reasm.AddPacket(pkts[i]->Bytes());
        }
        ASSERT_EQ(frames.size(), 1u) << "dropped shard " << dropped;
        ASSERT_EQ(frames[0].data_->Size(), frame.size()) << "dropped shard " << dropped;
        EXPECT_EQ(std::memcmp(frames[0].data_->Bytes().data(), frame.data(), frame.size()), 0)
            << "dropped shard " << dropped;
        EXPECT_EQ(frames[0].mon_name_, meta.mon_name_) << "dropped shard " << dropped;
    }
}

// ---------------- FRAME_STATUS 反馈 ----------------

TEST(PxUdpProtocol, FrameStatusRoundtrip) {
    auto pkt = PxUdpProtocol::BuildFrameStatus(12345, 8, 2);
    uint32_t frame_index = 0;
    uint16_t received = 0, lost = 0;
    ASSERT_TRUE(PxUdpProtocol::ParseFrameStatus(pkt->Bytes(), frame_index, received, lost));
    EXPECT_EQ(frame_index, 12345u);
    EXPECT_EQ(received, 8u);
    EXPECT_EQ(lost, 2u);

    // 其它 ctrl 包/截断包不应被解析成 FrameStatus
    auto hb = PxUdpProtocol::BuildHeartbeat("stream-abc");
    ASSERT_FALSE(PxUdpProtocol::ParseFrameStatus(hb->Bytes(), frame_index, received, lost));
    ASSERT_FALSE(PxUdpProtocol::ParseFrameStatus(pkt->Bytes().first(pkt->Size() - 1), frame_index, received, lost));
    // FrameStatus 不走 ParseCtrl 字符串路径
    std::string s1, s2;
    EXPECT_EQ(PxUdpProtocol::ParseCtrl(pkt->Bytes(), s1, s2), 0);
}

TEST(PxUdpProtocol, FrameStatusOnCleanComplete) {
    auto frame = MakeFrameBytes(8000);
    auto meta = MakeMeta(70, true);
    auto pkts = PxUdpProtocol::ShardVideoFrame(meta, std::span<const char>{frame});
    const uint16_t d = DataShardsOf(pkts);

    PxUdpFrameReassembler reasm;
    struct Status { uint32_t frame; uint16_t received; uint16_t lost; };
    std::vector<Status> statuses;
    reasm.on_frame_status_ = [&](uint8_t, uint32_t f, uint16_t r, uint16_t l) {
        statuses.push_back({f, r, l});
    };
    for (auto& p : pkts) reasm.AddPacket(p->Bytes());
    // 干净完成:received = 全部数据 shard,lost = 0,恰好一次
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].frame, 70u);
    EXPECT_EQ(statuses[0].received, d);
    EXPECT_EQ(statuses[0].lost, 0u);
}

TEST(PxUdpProtocol, FrameStatusOnFecRecovery) {
    auto frame = MakeFrameBytes(12000);
    auto meta = MakeMeta(71, true);
    auto pkts = ShardFec(meta, frame, 20);
    const uint16_t d = DataShardsOf(pkts);
    ASSERT_GT(pkts.size(), (size_t)d);

    PxUdpFrameReassembler reasm;
    struct Status { uint32_t frame; uint16_t received; uint16_t lost; };
    std::vector<Status> statuses;
    reasm.on_frame_status_ = [&](uint8_t, uint32_t f, uint16_t r, uint16_t l) {
        statuses.push_back({f, r, l});
    };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) { FAIL() << "unexpected loss"; };
    // 丢 1 个数据 shard,FEC 恢复:received = D-1,lost = 1,恰好一次
    for (size_t i = 1; i < pkts.size(); i++) {
        reasm.AddPacket(pkts[i]->Bytes());
    }
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].frame, 71u);
    EXPECT_EQ(statuses[0].received, d - 1);
    EXPECT_EQ(statuses[0].lost, 1u);
}

TEST(PxUdpProtocol, FrameStatusOnDeclareLoss) {
    auto frame_n = MakeFrameBytes(8000);
    auto meta_n = MakeMeta(80, false);
    auto pkts_n = PxUdpProtocol::ShardVideoFrame(meta_n, std::span<const char>{frame_n});
    const uint16_t d = DataShardsOf(pkts_n);
    ASSERT_GT(d, 1u);

    auto frame_key = MakeFrameBytes(700);
    auto meta_key = MakeMeta(81, true);
    auto pkts_key = PxUdpProtocol::ShardVideoFrame(meta_key, std::span<const char>{frame_key});

    PxUdpFrameReassembler reasm;
    struct Status { uint32_t frame; uint16_t received; uint16_t lost; };
    std::vector<Status> statuses;
    reasm.on_frame_status_ = [&](uint8_t, uint32_t f, uint16_t r, uint16_t l) {
        statuses.push_back({f, r, l});
    };

    // 帧 80 丢最后一个 shard 卡住;帧 81 到达 -> 80 判丢(状态一次:received=D-1, lost=1)
    for (size_t i = 0; i + 1 < pkts_n.size(); i++) reasm.AddPacket(pkts_n[i]->Bytes());
    EXPECT_TRUE(statuses.empty());
    for (auto& p : pkts_key) reasm.AddPacket(p->Bytes());
    // 帧 80 判丢一次 + 帧 81 完成一次
    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].frame, 80u);
    EXPECT_EQ(statuses[0].received, d - 1);
    EXPECT_EQ(statuses[0].lost, 1u);
    EXPECT_EQ(statuses[1].frame, 81u);
    EXPECT_EQ(statuses[1].lost, 0u);

    // 帧 80 的迟到包不得再次触发状态(finished_ 去重)
    reasm.AddPacket(pkts_n.back()->Bytes());
    EXPECT_EQ(statuses.size(), 2u);
}

// ---------------- 整帧丢失 gap 检测 + 恢复校验 ----------------

TEST(PxUdpProtocol, WholeFrameLossDetectedOnNextSof) {
    // 帧 90(key) 完整 -> emit
    auto frame90 = MakeFrameBytes(700);
    auto pkts90 = PxUdpProtocol::ShardVideoFrame(MakeMeta(90, true), std::span<const char>{frame90});
    // 帧 91 整帧蒸发:生成但一个包都不投
    auto frame91 = MakeFrameBytes(700);
    auto pkts91 = PxUdpProtocol::ShardVideoFrame(MakeMeta(91, false), std::span<const char>{frame91});
    (void)pkts91;
    // 帧 92(P) 全到:参考链已断,必须被 need_key_ 丢掉
    auto frame92 = MakeFrameBytes(700);
    auto pkts92 = PxUdpProtocol::ShardVideoFrame(MakeMeta(92, false), std::span<const char>{frame92});
    // 帧 93(key):流恢复
    auto frame93 = MakeFrameBytes(700);
    auto pkts93 = PxUdpProtocol::ShardVideoFrame(MakeMeta(93, true), std::span<const char>{frame93});

    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    std::vector<uint32_t> lost;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [&](uint8_t, uint32_t idx) { lost.push_back(idx); };

    for (auto& p : pkts90) reasm.AddPacket(p->Bytes());
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_TRUE(lost.empty()); // 首连后连续帧,不误判

    // 跳过 91,直接喂 92 -> 检测出 [91] 整帧丢失,DeclareLoss 恰好一次
    for (auto& p : pkts92) reasm.AddPacket(p->Bytes());
    ASSERT_EQ(lost.size(), 1u);
    EXPECT_EQ(lost[0], 91u);
    // 92 是 P 帧,need_key_ 置位 -> 完成但不进解码器
    EXPECT_EQ(frames.size(), 1u);

    // key 帧 93 正常 emit,流恢复
    for (auto& p : pkts93) reasm.AddPacket(p->Bytes());
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[1].frame_index_, 93u);
    EXPECT_TRUE(frames[1].key_);
    // 没有重复判丢
    EXPECT_EQ(lost.size(), 1u);
}

TEST(PxUdpProtocol, ContiguousFramesNoFalseGap) {
    PxUdpFrameReassembler reasm;
    std::vector<PxUdpFrameReassembler::CompleteFrame> frames;
    reasm.on_frame_ = [&](const PxUdpFrameReassembler::CompleteFrame& f) { frames.push_back(f); };
    reasm.on_frame_lost_ = [](uint8_t, uint32_t) { FAIL() << "unexpected loss"; };

    // gap = 0 的连续帧:100(key) -> 101(P) -> 102(P),全部正常 emit
    for (uint32_t idx = 100; idx <= 102; idx++) {
        auto frame = MakeFrameBytes(700 + idx);
        auto pkts = PxUdpProtocol::ShardVideoFrame(MakeMeta(idx, idx == 100), std::span<const char>{frame});
        for (auto& p : pkts) reasm.AddPacket(p->Bytes());
    }
    EXPECT_EQ(frames.size(), 3u);
}

TEST(PxUdpProtocol, ValidateRecoveredShard0Check) {
    const int d = 9;
    const size_t p = 1376; // mtu 1400 - 24
    // 合法块:ext(9+4) + frame_size 在容量内
    std::string b0(p, '\0');
    PxUdpProtocol::W16(std::span<char>{b0}, 0, 1920);
    PxUdpProtocol::W16(std::span<char>{b0}, 2, 1080);
    PxUdpProtocol::W32(std::span<char>{b0}, 4, 12000);
    b0[8] = 4;
    std::ranges::copy(std::string_view{"mon1"}, b0.begin() + 9);
    EXPECT_TRUE(PxUdpFrameReassembler::ValidateRecoveredShard0(b0, d, p));

    // mon_name_len 超上限
    auto bad_nl = b0;
    bad_nl[8] = (char)200;
    EXPECT_FALSE(PxUdpFrameReassembler::ValidateRecoveredShard0(bad_nl, d, p));

    // frame_size 超容量:sof_ext(13) + frame_size > D * P (12384)
    auto bad_size = b0;
    PxUdpProtocol::W32(std::span<char>{bad_size}, 4, 20000);
    EXPECT_FALSE(PxUdpFrameReassembler::ValidateRecoveredShard0(bad_size, d, p));

    // 块太短
    EXPECT_FALSE(PxUdpFrameReassembler::ValidateRecoveredShard0(std::string(5, '\0'), d, p));
}

// ---------------- 音频包 + jitter buffer (P2) ----------------

TEST(PxUdpProtocol, AudioPacketRoundtrip) {
    auto payload = MakeFrameBytes(240); // 一帧 Opus 大约这个量级
    auto pkt = PxUdpProtocol::BuildAudioPacket(1234, 567890, std::span<const char>{payload});
    ASSERT_TRUE(pkt != nullptr);
    EXPECT_EQ(pkt->Size(), (size_t)(PxUdpProtocol::kCommonHeaderSize + PxUdpProtocol::kAudioHeaderSize + payload.size()));

    PxUdpProtocol::AudioPacketInfo info;
    ASSERT_TRUE(PxUdpProtocol::ParseAudioPacket(pkt->Bytes(), info));
    EXPECT_EQ(info.seq_, 1234u);
    EXPECT_EQ(info.timestamp_ms_, 567890u);
    ASSERT_EQ(info.payload_len_, 240u);
    EXPECT_TRUE(std::ranges::equal(info.payload_, std::span<const char>{payload}));

    // 截断/错类型/空载荷都拒绝
    EXPECT_FALSE(PxUdpProtocol::ParseAudioPacket(pkt->Bytes().first(pkt->Size() - 1), info));
    auto hb = PxUdpProtocol::BuildHeartbeat("s");
    EXPECT_FALSE(PxUdpProtocol::ParseAudioPacket(hb->Bytes(), info));
    EXPECT_EQ(PxUdpProtocol::BuildAudioPacket(1, 1, {}), nullptr);
}

TEST(PxUdpProtocol, AudioJitterInOrder) {
    PxUdpAudioJitterBuffer jb;
    std::vector<uint32_t> delivered;
    jb.on_frame_ = [&](uint32_t seq, uint32_t ts, std::span<const char> payload) {
        EXPECT_EQ(ts, seq * 20);
        EXPECT_EQ(payload.size(), 100u);
        delivered.push_back(seq);
    };
    jb.on_lost_ = [](uint32_t) { FAIL() << "unexpected loss"; };

    auto payload = MakeFrameBytes(100);
    for (uint32_t s = 0; s < 5; s++) jb.AddPacket(s, s * 20, std::span<const char>{payload});
    EXPECT_EQ(delivered, (std::vector<uint32_t>{0, 1, 2, 3, 4}));
}

TEST(PxUdpProtocol, AudioJitterOutOfOrder) {
    PxUdpAudioJitterBuffer jb;
    std::vector<uint32_t> delivered;
    jb.on_frame_ = [&](uint32_t seq, uint32_t, std::span<const char>) { delivered.push_back(seq); };
    jb.on_lost_ = [](uint32_t) { FAIL() << "unexpected loss"; };

    auto payload = MakeFrameBytes(100);
    for (uint32_t s : {0u, 2u, 1u, 4u, 3u}) jb.AddPacket(s, s * 20, std::span<const char>{payload});
    EXPECT_EQ(delivered, (std::vector<uint32_t>{0, 1, 2, 3, 4}));
}

TEST(PxUdpProtocol, AudioJitterGapTriggersLostThenContinues) {
    PxUdpAudioJitterBuffer jb;
    std::vector<uint32_t> delivered, lost;
    jb.on_frame_ = [&](uint32_t seq, uint32_t, std::span<const char>) { delivered.push_back(seq); };
    jb.on_lost_ = [&](uint32_t seq) { lost.push_back(seq); };

    auto payload = MakeFrameBytes(100);
    jb.AddPacket(0, 0, std::span<const char>{payload});
    // seq 1 缺失:最老缓冲包领先 expected_ 超过 2 帧时判丢
    jb.AddPacket(4, 80, std::span<const char>{payload});
    EXPECT_EQ(lost, (std::vector<uint32_t>{1u}));
    // 缺口之后的包补上仍能按序交付
    jb.AddPacket(2, 40, std::span<const char>{payload});
    jb.AddPacket(3, 60, std::span<const char>{payload});
    EXPECT_EQ(delivered, (std::vector<uint32_t>{0, 2, 3, 4}));
    // 迟到的 seq 1 直接丢弃,不重复判丢
    jb.AddPacket(1, 20, std::span<const char>{payload});
    EXPECT_EQ(lost.size(), 1u);
    EXPECT_EQ(delivered.size(), 4u);
}

TEST(PxUdpProtocol, AudioJitterJoinMidStream) {
    PxUdpAudioJitterBuffer jb;
    std::vector<uint32_t> delivered;
    jb.on_frame_ = [&](uint32_t seq, uint32_t, std::span<const char>) { delivered.push_back(seq); };
    jb.on_lost_ = [](uint32_t) { FAIL() << "mid-stream join must not backfill history"; };

    auto payload = MakeFrameBytes(100);
    // 首包 seq 100:中途加入不补历史,立即从 100 开始交付
    jb.AddPacket(100, 2000, std::span<const char>{payload});
    jb.AddPacket(101, 2020, std::span<const char>{payload});
    EXPECT_EQ(delivered, (std::vector<uint32_t>{100, 101}));
}

TEST(PxUdpProtocol, AudioJitterPermanentGapHeals) {
    // 单个 seq 永久缺失:判丢看最新缓冲包,等够 3 帧窗口后立即收口并继续按序交付
    PxUdpAudioJitterBuffer jb;
    std::vector<uint32_t> delivered, lost;
    jb.on_frame_ = [&](uint32_t seq, uint32_t, std::span<const char>) { delivered.push_back(seq); };
    jb.on_lost_ = [&](uint32_t seq) { lost.push_back(seq); };

    auto payload = MakeFrameBytes(100);
    jb.AddPacket(0, 0, std::span<const char>{payload});
    // seq 1 永远不到:灌 2..21,seq 4 到达时(领先 expected_ 3 帧)判丢 1 并立即追平
    for (uint32_t s = 2; s <= 21; s++) jb.AddPacket(s, s * 20, std::span<const char>{payload});
    EXPECT_EQ(lost, (std::vector<uint32_t>{1u}));
    std::vector<uint32_t> expect{0u};
    for (uint32_t s = 2; s <= 21; s++) expect.push_back(s);
    EXPECT_EQ(delivered, expect);
    // 迟到的 seq 1 直接丢弃,不重复判丢
    jb.AddPacket(1, 20, std::span<const char>{payload});
    EXPECT_EQ(lost.size(), 1u);
}

TEST(PxUdpProtocol, AudioJitterBurstBehindNeverSpirals) {
    // 回归:接收线程 stall 后 expected_ 落后、突发包一次性涌入。
    // 旧实现(判丢看最老 + 淘汰最老)在此进入永久判丢死循环(真机 2026-08-13 踩过:
    // 每个包判丢一次、日志 50/s 刷盘、接收线程被拖垮、视频跟着卡死);
    // 新实现必须快速追平,1..60 每个 seq 要么恰好交付一次,要么恰好判丢一次
    PxUdpAudioJitterBuffer jb;
    std::vector<uint32_t> delivered, lost;
    jb.on_frame_ = [&](uint32_t seq, uint32_t, std::span<const char>) { delivered.push_back(seq); };
    jb.on_lost_ = [&](uint32_t seq) { lost.push_back(seq); };

    auto payload = MakeFrameBytes(100);
    jb.AddPacket(0, 0, std::span<const char>{payload});
    // 突发:1/2 丢失,3..60 一次性涌入
    for (uint32_t s = 3; s <= 60; s++) jb.AddPacket(s, s * 20, std::span<const char>{payload});
    EXPECT_EQ(delivered.front(), 0u);
    EXPECT_EQ(delivered.back(), 60u);
    EXPECT_GE(delivered.size(), 50u);
    for (size_t i = 1; i < delivered.size(); i++) {
        EXPECT_GT(delivered[i], delivered[i - 1]); // 严格递增,无重复
    }
    std::vector<char> seen(61, 0);
    for (auto s : delivered) seen[s] = 1;
    for (auto s : lost) {
        EXPECT_EQ(seen[s], 0) << "seq " << s << " both delivered and lost";
        seen[s] = 1;
    }
    for (uint32_t s = 1; s <= 60; s++) {
        EXPECT_EQ(seen[s], 1) << "seq " << s << " neither delivered nor lost";
    }
}

TEST(PxUdpProtocol, AudioJitterResyncOnPeerRestart) {
    // 对端重启 seq 归零重来:大幅回退视为新流,重置后从新 seq 继续交付
    PxUdpAudioJitterBuffer jb;
    std::vector<uint32_t> delivered;
    jb.on_frame_ = [&](uint32_t seq, uint32_t, std::span<const char>) { delivered.push_back(seq); };
    jb.on_lost_ = [](uint32_t) {};

    auto payload = MakeFrameBytes(100);
    for (uint32_t s = 10000; s < 10005; s++) jb.AddPacket(s, s * 20, std::span<const char>{payload});
    // 对端重启,seq 从 0 重新开始
    for (uint32_t s = 0; s < 3; s++) jb.AddPacket(s, s * 20, std::span<const char>{payload});
    EXPECT_EQ(delivered, (std::vector<uint32_t>{10000u, 10001u, 10002u, 10003u, 10004u, 0u, 1u, 2u}));
}
