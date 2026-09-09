#include "rdp/rdp_clipboard_data.h"
#include "rdp/rdp_clipboard_files.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <gtest/gtest.h>

namespace px::rdp {
namespace {
std::span<const unsigned char> Bytes(const QByteArray& value) {
    return {reinterpret_cast<const unsigned char*>(value.constData()), static_cast<std::size_t>(value.size())};
}
bool WriteTestFile(const QString& path, const QByteArray& bytes) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
} // namespace
TEST(RdpClipboard, UnicodeHtmlAndImageRoundTrip) {
    const auto text = QString::fromUtf8("中文\r\nRDP 😀");
    EXPECT_EQ(DecodeClipboardText(Bytes(EncodeClipboardText(text))), text);
    EXPECT_EQ(DecodeClipboardText(Bytes(EncodeClipboardText({}))), QString{});
    const auto html = QString::fromUtf8("<b>中文 &amp; café 😀</b>");
    EXPECT_EQ(DecodeClipboardHtml(Bytes(EncodeClipboardHtml(html))), html);
    QImage image{7, 5, QImage::Format_RGB32};
    for (int row{}; row < image.height(); ++row) {
        for (int column{}; column < image.width(); ++column) {
            image.setPixel(column, row, qRgb(column * 30, row * 40, 123));
        }
    }
    EXPECT_EQ(DecodeClipboardDib(Bytes(EncodeClipboardDib(image))), image);
}
TEST(RdpClipboard, MalformedOrOversizedDataIsRejected) {
    EXPECT_FALSE(DecodeClipboardText(Bytes(QByteArray{"x"})));
    EXPECT_FALSE(DecodeClipboardText(Bytes(QByteArray::fromHex("00d80000"))));
    EXPECT_FALSE(DecodeClipboardText(Bytes(QByteArray::fromHex("00dc0000"))));
    EXPECT_FALSE(DecodeClipboardText(Bytes(QByteArray::fromHex("000061000000"))));
    EXPECT_TRUE(EncodeClipboardText(QString(kClipboardDataLimit, 'x')).isEmpty());
    auto html = EncodeClipboardHtml(QStringLiteral("fragment"));
    html.replace("StartFragment:", "StartFragment:9999999999999");
    EXPECT_FALSE(DecodeClipboardHtml(Bytes(html)));
    auto incomplete_utf8 = EncodeClipboardHtml(QStringLiteral("x"));
    incomplete_utf8.replace("<!--StartFragment-->x", QByteArray{"<!--StartFragment-->"} + QByteArray::fromHex("c2"));
    EXPECT_FALSE(DecodeClipboardHtml(Bytes(incomplete_utf8)));
    QImage image{10, 10, QImage::Format_RGB32};
    image.fill(Qt::red);
    auto dib = EncodeClipboardDib(image);
    dib.chop(1);
    EXPECT_TRUE(DecodeClipboardDib(Bytes(dib)).isNull());
    dib = EncodeClipboardDib(image);
    qToLittleEndian<quint32>(0x80000000, dib.data() + 8);
    EXPECT_TRUE(DecodeClipboardDib(Bytes(dib)).isNull());
    dib = EncodeClipboardDib(image);
    qToLittleEndian<quint32>(3, dib.data() + 16);
    EXPECT_TRUE(DecodeClipboardDib(Bytes(dib)).isNull());
}
TEST(RdpClipboard, WindowsBitfieldDibRequiresExactMasksAndBoundedPixels) {
    QImage image{16, 16, QImage::Format_RGB32};
    image.fill(qRgb(173, 97, 31));
    auto dib = EncodeClipboardDib(image);
    QByteArray masks(12, '\0');
    qToLittleEndian<quint32>(0x00ff0000, masks.data());
    qToLittleEndian<quint32>(0x0000ff00, masks.data() + 4);
    qToLittleEndian<quint32>(0x000000ff, masks.data() + 8);
    dib.insert(40, masks);
    qToLittleEndian<quint32>(3, dib.data() + 16);
    EXPECT_EQ(DecodeClipboardDib(Bytes(dib)), image);
    auto truncated = dib;
    truncated.chop(1);
    EXPECT_TRUE(DecodeClipboardDib(Bytes(truncated)).isNull());
    qToLittleEndian<quint32>(0x00ffffff, dib.data() + 40);
    EXPECT_TRUE(DecodeClipboardDib(Bytes(dib)).isNull());
}
TEST(RdpClipboard, PathsRejectTraversalAliasesAndNormalizationCollisions) {
    for (const auto& path : {QStringLiteral(""), QStringLiteral("..\\secret"), QStringLiteral("C:\\secret"), QStringLiteral("\\\\host\\file"),
                             QStringLiteral("dir/secret"), QStringLiteral("file:stream"), QStringLiteral("dir\\NUL.txt"), QStringLiteral("LPT1.txt"),
                             QStringLiteral("COM¹.txt"), QStringLiteral("dir\\.\\file"), QStringLiteral("file."), QStringLiteral("file "),
                             QStringLiteral("file?"), QStringLiteral("dir\\\\file"), QString(260, 'x')}) {
        EXPECT_FALSE(SafeClipboardRelativePath(path));
    }
    EXPECT_TRUE(SafeClipboardRelativePath(QStringLiteral("中文目录\\报告.txt")));
    EXPECT_TRUE(SafeClipboardRelativePath(QStringLiteral("file..name")));
}
TEST(RdpClipboard, FilesDirectoriesEmptyFilesAndCancellationAreOwned) {
    QTemporaryDir source{};
    ASSERT_TRUE(source.isValid());
    ASSERT_TRUE(QDir{source.path()}.mkdir("tree"));
    ASSERT_TRUE(QDir{source.path() + "/tree"}.mkdir("emptydir"));
    ASSERT_TRUE(WriteTestFile(source.path() + "/tree/empty", {}));
    const QByteArray content(150000, 'q');
    ASSERT_TRUE(WriteTestFile(source.path() + "/tree/中文.txt", content));
    auto offer = ClipboardFiles::Offer({QUrl::fromLocalFile(source.path() + "/tree")});
    ASSERT_TRUE(offer);
    const auto descriptor = offer->Descriptors();
    auto receive = ClipboardFiles::Receive(Bytes(descriptor));
    ASSERT_TRUE(receive);
    EXPECT_TRUE(receive->Publish().isEmpty());
    for (std::size_t index{}; index < offer->Entries().size(); ++index) {
        const auto& entry = offer->Entries().at(index);
        if (entry.directory) {
            continue;
        }
        for (std::uint64_t offset{}; offset < entry.size;) {
            const auto bytes = offer->Read(index, offset, 64 * 1024);
            ASSERT_FALSE(bytes.isEmpty());
            ASSERT_TRUE(receive->Write(index, offset, Bytes(bytes)));
            EXPECT_FALSE(receive->Write(index, offset, Bytes(bytes)));
            offset += bytes.size();
        }
    }
    const auto urls = receive->Publish();
    ASSERT_EQ(urls.size(), 1);
    const auto path = urls.front().toLocalFile();
    {
        QFile received{path + "/中文.txt"};
        ASSERT_TRUE(received.open(QIODevice::ReadOnly));
        EXPECT_EQ(received.readAll(), content);
    }
    EXPECT_TRUE(QFileInfo{path + "/empty"}.isFile());
    EXPECT_EQ(QFileInfo{path + "/empty"}.size(), 0);
    EXPECT_TRUE(QFileInfo{path + "/emptydir"}.isDir());
    receive.reset();
    EXPECT_FALSE(QFileInfo::exists(path));
    EXPECT_TRUE(QFileInfo::exists(source.path() + "/tree/中文.txt"));
    auto truncated = descriptor;
    truncated.chop(1);
    EXPECT_FALSE(ClipboardFiles::Receive(Bytes(truncated)));
    auto oversized = descriptor;
    qToLittleEndian<quint32>(1025, oversized.data());
    EXPECT_FALSE(ClipboardFiles::Receive(Bytes(oversized)));
    EXPECT_FALSE(ClipboardFiles::Offer({QUrl::fromLocalFile(source.path() + "/tree"), QUrl::fromLocalFile(source.path() + "/tree")}));
}
TEST(RdpClipboard, SourceReparsePointsAndHardLinksAreRejected) {
    QTemporaryDir source{};
    ASSERT_TRUE(source.isValid());
    const auto original = source.path() + "/original";
    const auto link = source.path() + "/link";
    ASSERT_TRUE(WriteTestFile(original, "private"));
    ASSERT_TRUE(CreateHardLinkW(link.toStdWString().c_str(), original.toStdWString().c_str(), nullptr));
    EXPECT_FALSE(ClipboardFiles::Offer({QUrl::fromLocalFile(link)}));
    EXPECT_FALSE(ClipboardFiles::Offer({QUrl::fromLocalFile(original)}));
    EXPECT_FALSE(ClipboardFiles::Offer({QUrl::fromLocalFile("//invalid.example/share/file")}));
}
} // namespace px::rdp
