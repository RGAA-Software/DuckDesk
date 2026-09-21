#include <Windows.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rdp/rdp_clipboard_channel.h"

namespace px::rdp {
namespace {

struct ClipboardTransportCapture final {
    struct Format final {
        std::uint32_t id{};
        std::string name{};
    };

    std::vector<Format> formats{};
    std::optional<std::uint32_t> dataRequest{};
    std::vector<std::uint8_t> dataResponse{};
    bool dataResponseOk{};
    std::deque<CLIPRDR_FILE_CONTENTS_REQUEST> fileRequests{};
    std::vector<std::uint8_t> fileResponse{};
    bool fileResponseOk{};
};

std::shared_ptr<ClipboardTransportCapture> transportCapture{};

UINT AcceptCapabilities(CliprdrClientContext*, const CLIPRDR_CAPABILITIES*) {  // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    return CHANNEL_RC_OK;
}

UINT AcceptFormatList(CliprdrClientContext*,               // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
                      const CLIPRDR_FORMAT_LIST* value) {  // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    transportCapture->formats.clear();
    for (const auto& format : std::span<const CLIPRDR_FORMAT>{value->formats, value->numFormats}) {
        transportCapture->formats.push_back({format.formatId, format.formatName ? format.formatName : ""});
    }
    return CHANNEL_RC_OK;
}

UINT AcceptFormatListResponse(  // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    CliprdrClientContext*, const CLIPRDR_FORMAT_LIST_RESPONSE*) {
    return CHANNEL_RC_OK;
}

UINT AcceptFormatDataRequest(                                             // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    CliprdrClientContext*, const CLIPRDR_FORMAT_DATA_REQUEST* request) {  // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    transportCapture->dataRequest = request->requestedFormatId;
    return CHANNEL_RC_OK;
}

UINT AcceptFormatDataResponse(                                              // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    CliprdrClientContext*, const CLIPRDR_FORMAT_DATA_RESPONSE* response) {  // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    transportCapture->dataResponseOk = response->common.msgFlags == CB_RESPONSE_OK;
    transportCapture->dataResponse.assign(response->requestedFormatData, response->requestedFormatData + response->common.dataLen);
    return CHANNEL_RC_OK;
}

UINT AcceptFileContentsRequest(                                             // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    CliprdrClientContext*, const CLIPRDR_FILE_CONTENTS_REQUEST* request) {  // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    transportCapture->fileRequests.push_back(*request);
    return CHANNEL_RC_OK;
}

UINT AcceptFileContentsResponse(                                              // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    CliprdrClientContext*, const CLIPRDR_FILE_CONTENTS_RESPONSE* response) {  // NOLINT(pixels-raw-pointer-boundary): FreeRDP test ABI.
    transportCapture->fileResponseOk = response->common.msgFlags == CB_RESPONSE_OK;
    transportCapture->fileResponse.assign(response->requestedData, response->requestedData + response->cbRequested);
    return CHANNEL_RC_OK;
}

std::shared_ptr<CliprdrClientContext> MakeContext() {
    transportCapture = std::make_shared<ClipboardTransportCapture>();
    auto context = std::make_shared<CliprdrClientContext>();
    context->ClientCapabilities = AcceptCapabilities;
    context->ClientFormatList = AcceptFormatList;
    context->ClientFormatListResponse = AcceptFormatListResponse;
    context->ClientFormatDataRequest = AcceptFormatDataRequest;
    context->ClientFormatDataResponse = AcceptFormatDataResponse;
    context->ClientFileContentsRequest = AcceptFileContentsRequest;
    context->ClientFileContentsResponse = AcceptFileContentsResponse;
    return context;
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        root_ = std::filesystem::temp_directory_path() / (L"pixels-rdp-clipboard-test-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(root_);
    }
    ~TemporaryDirectory() {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }
    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return root_; }

private:
    std::filesystem::path root_{};
};

CLIPRDR_FORMAT_DATA_RESPONSE SuccessfulResponse(const std::span<const std::uint8_t> bytes) {
    CLIPRDR_FORMAT_DATA_RESPONSE response{};
    response.common.msgType = CB_FORMAT_DATA_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    response.common.dataLen = static_cast<UINT32>(bytes.size());
    response.requestedFormatData = bytes.data();
    return response;
}

TEST(RdpClipboardChannel, ReceivesTextHtmlAndImageFormatsInOneClipboardGeneration) {
    std::optional<ClipboardContent> published{};
    ClipboardChannel channel{MakeContext(), [&published](ClipboardContent content) { published = std::move(content); }};
    ASSERT_TRUE(channel.Ready());

    std::array<char, 12> htmlName{"HTML Format"};
    std::array<char, 4> pngName{"PNG"};
    std::array<CLIPRDR_FORMAT, 5> formats{
        {{CF_UNICODETEXT, nullptr}, {49153U, htmlName.data()}, {CF_DIB, nullptr}, {CF_DIBV5, nullptr}, {49154U, pngName.data()}}};
    CLIPRDR_FORMAT_LIST formatList{};
    formatList.numFormats = static_cast<UINT32>(formats.size());
    formatList.formats = formats.data();
    ASSERT_TRUE(channel.Formats(formatList));

    const std::wstring text{L"Pixels 文本\0", 10};
    const auto textBytes = std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()), text.size() * sizeof(wchar_t)};
    const std::vector<std::uint8_t> html{'V', 'e', 'r', 's', 'i', 'o', 'n', ':', '1', '.', '0'};
    const std::vector<std::uint8_t> dib{1, 2, 3};
    const std::vector<std::uint8_t> dibV5{4, 5, 6, 7};
    const std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G'};

    ASSERT_TRUE(channel.DataResponse(SuccessfulResponse(textBytes)));
    EXPECT_FALSE(published);
    ASSERT_TRUE(channel.DataResponse(SuccessfulResponse(html)));
    ASSERT_TRUE(channel.DataResponse(SuccessfulResponse(dib)));
    ASSERT_TRUE(channel.DataResponse(SuccessfulResponse(dibV5)));
    ASSERT_TRUE(channel.DataResponse(SuccessfulResponse(png)));
    ASSERT_TRUE(published);
    EXPECT_EQ(published->text, "Pixels 文本");
    EXPECT_EQ(published->html, html);
    EXPECT_EQ(published->dib, dib);
    EXPECT_EQ(published->dibV5, dibV5);
    EXPECT_EQ(published->png, png);
}

TEST(RdpClipboardChannel, UnsupportedRemoteClipboardClearsLocalFormats) {
    std::optional<ClipboardContent> published{};
    ClipboardChannel channel{MakeContext(), [&published](ClipboardContent content) { published = std::move(content); }};
    ASSERT_TRUE(channel.Ready());
    CLIPRDR_FORMAT_LIST formatList{};
    ASSERT_TRUE(channel.Formats(formatList));
    ASSERT_TRUE(published);
    EXPECT_TRUE(published->Empty());
}

TEST(RdpClipboardChannel, ServesLocalFilesByDescriptorSizeAndRange) {
    TemporaryDirectory temporary{};
    const auto source = temporary.Root() / L"payload.txt";
    {
        std::ofstream output{source, std::ios::binary};
        output << "Pixels file clipboard";
    }
    ClipboardChannel channel{MakeContext(), [](ClipboardContent) {}};
    ClipboardContent content{};
    content.files.push_back(source);
    ASSERT_TRUE(channel.SetLocal(std::move(content)));
    ASSERT_TRUE(channel.Ready());
    const auto descriptorFormat = std::ranges::find_if(
        transportCapture->formats, [](const ClipboardTransportCapture::Format& format) { return format.name == "FileGroupDescriptorW"; });
    ASSERT_NE(descriptorFormat, transportCapture->formats.end());

    CLIPRDR_FORMAT_DATA_REQUEST descriptorRequest{};
    descriptorRequest.requestedFormatId = descriptorFormat->id;
    ASSERT_TRUE(channel.DataRequest(descriptorRequest));
    ASSERT_TRUE(transportCapture->dataResponseOk);
    const auto descriptors = ParseRemoteClipboardFiles(transportCapture->dataResponse);
    ASSERT_EQ(descriptors.size(), 1U);
    EXPECT_EQ(descriptors.front().relative, L"payload.txt");

    CLIPRDR_FILE_CONTENTS_REQUEST sizeRequest{};
    sizeRequest.streamId = 11U;
    sizeRequest.listIndex = 0U;
    sizeRequest.dwFlags = FILECONTENTS_SIZE;
    ASSERT_TRUE(channel.FileRequest(sizeRequest));
    ASSERT_TRUE(transportCapture->fileResponseOk);
    ASSERT_EQ(transportCapture->fileResponse.size(), sizeof(std::uint64_t));

    CLIPRDR_FILE_CONTENTS_REQUEST rangeRequest{};
    rangeRequest.streamId = 12U;
    rangeRequest.listIndex = 0U;
    rangeRequest.dwFlags = FILECONTENTS_RANGE;
    rangeRequest.cbRequested = 21U;
    ASSERT_TRUE(channel.FileRequest(rangeRequest));
    ASSERT_TRUE(transportCapture->fileResponseOk);
    EXPECT_EQ(std::string(transportCapture->fileResponse.begin(), transportCapture->fileResponse.end()), "Pixels file clipboard");
}

TEST(RdpClipboardChannel, DownloadsRemoteDirectoryIntoOwnedStagingArea) {
    TemporaryDirectory sourceDirectory{};
    const auto sourceRoot = sourceDirectory.Root() / L"payload";
    std::filesystem::create_directories(sourceRoot / L"nested");
    const std::string expected(100000U, 'P');
    {
        std::ofstream output{sourceRoot / L"nested" / L"data.bin", std::ios::binary};
        output.write(expected.data(), static_cast<std::streamsize>(expected.size()));
    }
    const auto localFiles = InventoryLocalClipboardFiles(std::array<std::filesystem::path, 1>{sourceRoot});
    ASSERT_EQ(localFiles.size(), 3U);
    const auto descriptors = SerializeLocalClipboardFiles(localFiles);
    ASSERT_FALSE(descriptors.empty());

    std::optional<ClipboardContent> published{};
    ClipboardChannel channel{MakeContext(), [&published](ClipboardContent content) { published = std::move(content); }};
    ASSERT_TRUE(channel.Ready());
    std::array<char, 21> descriptorName{"FileGroupDescriptorW"};
    CLIPRDR_FORMAT descriptorFormat{49155U, descriptorName.data()};
    CLIPRDR_FORMAT_LIST formatList{};
    formatList.numFormats = 1U;
    formatList.formats = &descriptorFormat;  // NOLINT(pixels-raw-pointer-boundary): synchronous FreeRDP test boundary.
    ASSERT_TRUE(channel.Formats(formatList));
    ASSERT_EQ(transportCapture->dataRequest, descriptorFormat.formatId);
    ASSERT_TRUE(channel.DataResponse(SuccessfulResponse(descriptors)));

    while (!published) {
        ASSERT_FALSE(transportCapture->fileRequests.empty());
        const auto request = transportCapture->fileRequests.front();
        transportCapture->fileRequests.pop_front();
        std::vector<std::uint8_t> responseBytes{};
        if (request.dwFlags == FILECONTENTS_SIZE) {
            const auto size = localFiles[request.listIndex].size;
            responseBytes.resize(sizeof(size));
            for (std::size_t index{}; index < responseBytes.size(); ++index) {
                responseBytes[index] = static_cast<std::uint8_t>(size >> (index * 8U));
            }
        } else {
            const auto offset = (static_cast<std::uint64_t>(request.nPositionHigh) << 32U) | request.nPositionLow;
            ASSERT_TRUE(ReadClipboardFileRange(localFiles[request.listIndex], offset, request.cbRequested, responseBytes));
        }
        CLIPRDR_FILE_CONTENTS_RESPONSE response{};
        response.common.msgFlags = CB_RESPONSE_OK;
        response.streamId = request.streamId;
        response.cbRequested = static_cast<UINT32>(responseBytes.size());
        response.requestedData = responseBytes.data();
        ASSERT_TRUE(channel.FileResponse(response));
    }

    ASSERT_EQ(published->files.size(), 1U);
    ASSERT_TRUE(published->staging);
    const auto stagedRoot = published->staging->Root();
    const auto stagedFile = published->files.front() / L"nested" / L"data.bin";
    std::ifstream input{stagedFile, std::ios::binary};
    const std::string actual{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    EXPECT_EQ(actual, expected);
    input.close();
    published.reset();
    EXPECT_FALSE(std::filesystem::exists(stagedRoot));
}

TEST(RdpClipboardChannel, RejectsRemotePathTraversal) {
    TemporaryDirectory temporary{};
    const auto source = temporary.Root() / L"safe.txt";
    {
        std::ofstream output{source};
        output << "safe";
    }
    const auto localFiles = InventoryLocalClipboardFiles(std::array<std::filesystem::path, 1>{source});
    auto descriptors = SerializeLocalClipboardFiles(localFiles);
    ASSERT_EQ(descriptors.size(), 596U);
    constexpr std::size_t nameOffset{4U + 72U};
    const std::wstring traversal{L"..\\escape.txt"};
    std::fill(descriptors.begin() + nameOffset, descriptors.end(), 0U);
    std::memcpy(descriptors.data() + nameOffset, traversal.data(), traversal.size() * sizeof(wchar_t));
    EXPECT_TRUE(ParseRemoteClipboardFiles(descriptors).empty());
}

}  // namespace
}  // namespace px::rdp
