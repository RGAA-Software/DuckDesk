#include "rdp/rdp_clipboard_channel.h"
#include <QtEndian>
#include <QFile>
#include <shlobj.h>
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <type_traits>

namespace px::rdp {
namespace {
// A test-owned ABI record, with only scalar observations. Its first-member
// pointer interconversion is checked; no borrowed callback argument is retained.
struct ChannelProbe final {
    CliprdrClientContext api{};
    std::uint32_t stream{0};
    std::uint32_t flags{0};
    std::uint32_t bytes{0};
    std::uint32_t lock{0};
    std::uint32_t unlock{0};
    std::uint32_t request_lock{0};
};
static_assert(std::is_standard_layout_v<ChannelProbe>);
template <typename Payload> UINT Accept(CliprdrClientContext*, const Payload*) {
    return CHANNEL_RC_OK;
} // NOLINT(gammaray-raw-pointer-boundary): synchronous test ABI.
UINT Contents(CliprdrClientContext* context, const CLIPRDR_FILE_CONTENTS_REQUEST* request) { // NOLINT(gammaray-raw-pointer-boundary): test ABI.
    auto& probe = reinterpret_cast<ChannelProbe&>(*context);
    probe.stream = request->streamId;
    probe.flags = request->dwFlags;
    probe.bytes = request->cbRequested;
    probe.request_lock = request->haveClipDataId ? request->clipDataId : 0;
    return CHANNEL_RC_OK;
}
UINT LockData(CliprdrClientContext* context, const CLIPRDR_LOCK_CLIPBOARD_DATA* request) { // NOLINT(gammaray-raw-pointer-boundary): test ABI.
    reinterpret_cast<ChannelProbe&>(*context).lock = request->clipDataId;
    return CHANNEL_RC_OK;
}
UINT UnlockData(CliprdrClientContext* context, const CLIPRDR_UNLOCK_CLIPBOARD_DATA* request) { // NOLINT(gammaray-raw-pointer-boundary): test ABI.
    reinterpret_cast<ChannelProbe&>(*context).unlock = request->clipDataId;
    return CHANNEL_RC_OK;
}
std::shared_ptr<ChannelProbe> Probe() {
    auto probe = std::make_shared<ChannelProbe>();
    probe->api.ClientCapabilities = Accept<CLIPRDR_CAPABILITIES>;
    probe->api.ClientFormatList = Accept<CLIPRDR_FORMAT_LIST>;
    probe->api.ClientFormatListResponse = Accept<CLIPRDR_FORMAT_LIST_RESPONSE>;
    probe->api.ClientFormatDataRequest = Accept<CLIPRDR_FORMAT_DATA_REQUEST>;
    probe->api.ClientFileContentsRequest = Contents;
    probe->api.ClientLockClipboardData = LockData;
    probe->api.ClientUnlockClipboardData = UnlockData;
    return probe;
}
bool StartFile(ClipboardChannel& channel) {
    CLIPRDR_GENERAL_CAPABILITY_SET general{};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES | CB_STREAM_FILECLIP_ENABLED | CB_CAN_LOCK_CLIPDATA;
    CLIPRDR_CAPABILITIES caps{};
    caps.cCapabilitiesSets = 1;
    caps.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general); // Transient synchronous ABI.
    std::array<char, 21> name{"FileGroupDescriptorW"};
    CLIPRDR_FORMAT format{49000, name.data()};
    CLIPRDR_FORMAT_LIST formats{};
    formats.numFormats = 1;
    formats.formats = &format;
    if (!channel.Capabilities(caps) || !channel.Ready() || !channel.Formats(formats)) {
        return false;
    }
    FILEDESCRIPTORW descriptor{};
    descriptor.dwFlags = FD_ATTRIBUTES | FD_FILESIZE | FD_UNICODE;
    descriptor.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    descriptor.nFileSizeLow = 3;
    const std::wstring filename{L"fixture.txt"};
    std::ranges::copy(filename, std::begin(descriptor.cFileName));
    QByteArray bytes(4 + sizeof(descriptor), '\0');
    qToLittleEndian<quint32>(1, bytes.data());
    std::memcpy(bytes.data() + 4, &descriptor, sizeof(descriptor));
    CLIPRDR_FORMAT_DATA_RESPONSE response{};
    response.common.msgFlags = CB_RESPONSE_OK;
    response.common.dataLen = static_cast<UINT32>(bytes.size());
    response.requestedFormatData = reinterpret_cast<const BYTE*>(bytes.constData()); // Transient serialization ABI.
    return channel.DataResponse(response);
}
TEST(RdpClipboardChannel, LockedSnapshotSizeThenRangePublishesOnlyCompleteFiles) {
    auto probe = Probe();
    auto published = std::make_shared<std::shared_ptr<const ClipboardData>>();
    ClipboardChannel channel{std::shared_ptr<CliprdrClientContext>{probe, &probe->api},
                             [published](std::shared_ptr<const ClipboardData> data) { *published = std::move(data); }};
    ASSERT_TRUE(StartFile(channel));
    ASSERT_NE(probe->lock, 0);
    EXPECT_EQ(probe->flags, FILECONTENTS_SIZE);
    EXPECT_EQ(probe->bytes, 8);
    EXPECT_EQ(probe->request_lock, probe->lock);
    std::array<BYTE, 8> size{};
    qToLittleEndian<quint64>(3, size.data());
    CLIPRDR_FILE_CONTENTS_RESPONSE response{};
    response.common.msgFlags = CB_RESPONSE_OK;
    response.streamId = probe->stream;
    response.cbRequested = 8;
    response.requestedData = size.data();
    ASSERT_TRUE(channel.FileResponse(response));
    EXPECT_EQ(probe->flags, FILECONTENTS_RANGE);
    EXPECT_EQ(probe->bytes, 3);
    EXPECT_FALSE(*published);
    const std::array<BYTE, 3> content{'a', 'b', 'c'};
    response.streamId = probe->stream;
    response.cbRequested = 3;
    response.requestedData = content.data();
    ASSERT_TRUE(channel.FileResponse(response));
    ASSERT_TRUE(*published);
    ASSERT_EQ((*published)->urls.size(), 1);
    QFile received{(*published)->urls.front().toLocalFile()};
    ASSERT_TRUE(received.open(QIODevice::ReadOnly));
    EXPECT_EQ(received.readAll(), QByteArray{"abc"});
    EXPECT_EQ(probe->unlock, probe->lock);
}
TEST(RdpClipboardChannel, CancelUnlocksSnapshotAndDiscardsLateResponse) {
    auto probe = Probe();
    auto published = std::make_shared<int>(0);
    ClipboardChannel channel{std::shared_ptr<CliprdrClientContext>{probe, &probe->api},
                             [published](std::shared_ptr<const ClipboardData>) { ++*published; }};
    ASSERT_TRUE(StartFile(channel));
    const auto old_stream = probe->stream;
    ASSERT_TRUE(channel.SetLocal({}));
    EXPECT_EQ(probe->unlock, probe->lock);
    CLIPRDR_FILE_CONTENTS_RESPONSE late{};
    late.streamId = old_stream;
    EXPECT_TRUE(channel.FileResponse(late));
    EXPECT_EQ(*published, 0);
    EXPECT_TRUE(channel.Tick());
}
} // namespace
} // namespace px::rdp
