#include "core/filepreview.h"

#include "core/textextractor.h"

#include <QGuiApplication>
#include <QIcon>
#include <QTemporaryDir>
#include <QtTest>

using namespace okapi;

class TestFilePreview : public QObject {
    Q_OBJECT

private slots:
    void categoryForFileMapsExtensions();
    void badgeForCategoryCoversCategories();
    void thumbnailForTextFileIsValidPng();
    void thumbnailForTextFileUsesPageIconWithoutOverflow();
    void pdfThumbnailHasImageResolutionAndPageMargins();
    void thumbnailForOfficeWithEmbeddedPreview();
    void thumbnailForOdfWithEmbeddedPreview();
    void thumbnailForOfficeWithoutEmbeddedPreviewFallsBackToIcon();
    void thumbnailForArchiveFileIsValidPng();
    void thumbnailForEmailFileIsValidPng();
    void thumbnailForGenericFileIsValidPng();
    void thumbnailRespectsMaxEdge();
    void thumbnailForMissingFileFallsBackToIcon();
    void thumbnailWithNonPositiveEdgeIsEmpty();
};

void TestFilePreview::categoryForFileMapsExtensions() {
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("/tmp/report.PDF")), FileCategory::Pdf);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.docx")), FileCategory::Word);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.odt")), FileCategory::Word);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.xlsx")), FileCategory::Spreadsheet);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.ods")), FileCategory::Spreadsheet);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.pptx")), FileCategory::Presentation);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.odp")), FileCategory::Presentation);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.zip")), FileCategory::Archive);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.tar.gz")), FileCategory::Archive);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.7z")), FileCategory::Archive);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.eml")), FileCategory::Email);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.mbox")), FileCategory::Email);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.txt")), FileCategory::Text);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.md")), FileCategory::Text);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.py")), FileCategory::Text);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("a.bin")), FileCategory::Generic);
    QCOMPARE(FilePreview::categoryForFile(QStringLiteral("noextension")), FileCategory::Generic);
}

void TestFilePreview::badgeForCategoryCoversCategories() {
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Pdf), QStringLiteral("PDF"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Word), QStringLiteral("W"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Spreadsheet), QStringLiteral("X"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Presentation), QStringLiteral("P"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Archive), QStringLiteral("ZIP"));
    QCOMPARE(FilePreview::badgeForCategory(FileCategory::Email), QStringLiteral("@"));
    QVERIFY(FilePreview::badgeForCategory(FileCategory::Text).isEmpty());
    QVERIFY(FilePreview::badgeForCategory(FileCategory::Generic).isEmpty());
}

namespace {

QImage decodePng(const QByteArray &bytes) {
    return QImage::fromData(bytes);
}

void writeFile(const QString &path, const QByteArray &content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qWarning("failed to write test file");
    file.write(content);
    file.close();
}

// Minimal ZIP packages with an embedded first-page preview image. The
// embedded image bytes are a PNG (QImage::fromData sniffs the actual
// content, not the entry name); real-world OOXML files embed a JPEG.
// clang-format off
QByteArray docxWithThumbnailZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13HHBc8CAAAAAgAAAATAAAAW0NvbnRlbnRfVHlwZXNdLnhtbDxUeXBlcy8+") +
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13iS0eWSgAAAEoAAAAXAAAAZG9jUHJvcHMvdGh1bWJuYWlsLmpwZWeJUE5H") +
        QByteArrayLiteral("DQoaCgAAAA1JSERSAAAACAAAAAgIAgAAAEttKdwAAAARSURBVHicYzghJ4cVMQwtCQCQv0EBsp8+") +
        QByteArrayLiteral("IgAAAABJRU5ErkJgglBLAQIUAxQAAAAAALcqV13HHBc8CAAAAAgAAAATAAAAAAAAAAAAAACAAQAA") +
        QByteArrayLiteral("AABbQ29udGVudF9UeXBlc10ueG1sUEsBAhQDFAAAAAAAtypXXeJLR5ZKAAAASgAAABcAAAAAAAAA") +
        QByteArrayLiteral("AAAAAIABOQAAAGRvY1Byb3BzL3RodW1ibmFpbC5qcGVnUEsFBgAAAAACAAIAhgAAALgAAAAAAA=="));
}

QByteArray docxWithoutThumbnailZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13HHBc8CAAAAAgAAAATAAAAW0NvbnRlbnRfVHlwZXNdLnhtbDxUeXBlcy8+") +
        QByteArrayLiteral("UEsDBBQAAAAAALcqV13dWoYNDQAAAA0AAAARAAAAd29yZC9kb2N1bWVudC54bWw8dzpkb2N1bWVu") +
        QByteArrayLiteral("dC8+UEsBAhQDFAAAAAAAtypXXcccFzwIAAAACAAAABMAAAAAAAAAAAAAAIABAAAAAFtDb250ZW50") +
        QByteArrayLiteral("X1R5cGVzXS54bWxQSwECFAMUAAAAAAC3Kldd3VqGDQ0AAAANAAAAEQAAAAAAAAAAAAAAgAE5AAAA") +
        QByteArrayLiteral("d29yZC9kb2N1bWVudC54bWxQSwUGAAAAAAIAAgCAAAAAdQAAAAAA"));
}

QByteArray odtWithThumbnailZip() {
    return QByteArray::fromBase64(
        QByteArrayLiteral("UEsDBBQAAAAAALcqV11exjIMJwAAACcAAAAIAAAAbWltZXR5cGVhcHBsaWNhdGlvbi92bmQub2Fz") +
        QByteArrayLiteral("aXMub3BlbmRvY3VtZW50LnRleHRQSwMEFAAAAAAAtypXXeJLR5ZKAAAASgAAABgAAABUaHVtYm5h") +
        QByteArrayLiteral("aWxzL3RodW1ibmFpbC5wbmeJUE5HDQoaCgAAAA1JSERSAAAACAAAAAgIAgAAAEttKdwAAAARSURB") +
        QByteArrayLiteral("VHicYzghJ4cVMQwtCQCQv0EBsp8+IgAAAABJRU5ErkJgglBLAQIUAxQAAAAAALcqV11exjIMJwAA") +
        QByteArrayLiteral("ACcAAAAIAAAAAAAAAAAAAACAAQAAAABtaW1ldHlwZVBLAQIUAxQAAAAAALcqV13iS0eWSgAAAEoA") +
        QByteArrayLiteral("AAAYAAAAAAAAAAAAAACAAU0AAABUaHVtYm5haWxzL3RodW1ibmFpbC5wbmdQSwUGAAAAAAIAAgB8") +
        QByteArrayLiteral("AAAAzQAAAAAA"));
}
// clang-format on

} // namespace

void TestFilePreview::thumbnailForTextFileIsValidPng() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("notes.txt"));
    writeFile(path, "hello world");

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QVERIFY(qMax(image.width(), image.height()) <= 64);
}

void TestFilePreview::thumbnailForTextFileUsesPageIconWithoutOverflow() {
    // Text files show a page icon (icon theme's text-plain when available,
    // otherwise the drawn Breeze-style fallback) — not rendered file content.
    // Even with very long lines, nothing may overflow the page/tile.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("notes.md"));
    writeFile(path, (QString(500, QLatin1Char('x')) + QLatin1Char('\n') +
                     QString(500, QLatin1Char('y')) + QLatin1Char('\n'))
                        .toUtf8());

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QCOMPARE(qMax(image.width(), image.height()), 96);

    // Bounding box of all visible (non-transparent) pixels.
    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;
    int linePixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() <= 0)
                continue;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
            // A text line pixel: visible but not (near-)white page.
            if (c.alpha() > 200 && (c.red() < 200 || c.green() < 200 || c.blue() < 200))
                ++linePixels;
        }
    }
    QVERIFY(maxX >= 0);
    // The icon has text lines...
    QVERIFY(linePixels > 20);

    if (QIcon::fromTheme(QStringLiteral("text-plain")).isNull()) {
        // Drawn fallback (headless CI): the page with its lines must stay
        // inset from all four tile edges (no overflow), and the tile corners
        // are transparent.
        const int inset = 96 / 10;
        QVERIFY(minX >= inset);
        QVERIFY(minY >= inset);
        QVERIFY(maxX < image.width() - inset);
        QVERIFY(maxY < image.height() - inset);
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1).alpha(), 0);
    }
}

void TestFilePreview::thumbnailForOfficeWithEmbeddedPreview() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("report.docx"));
    writeFile(path, docxWithThumbnailZip());

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    // The embedded preview is a small square image; scaled into the tile it
    // keeps its aspect ratio, so the thumbnail is square-ish.
    QVERIFY(qMax(image.width(), image.height()) <= 64);
}

void TestFilePreview::thumbnailForOdfWithEmbeddedPreview() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("report.odt"));
    writeFile(path, odtWithThumbnailZip());

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForOfficeWithoutEmbeddedPreviewFallsBackToIcon() {
    if (!TextExtractor::hasLibArchive())
        QSKIP("libarchive not available in this build");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plain.docx"));
    writeFile(path, docxWithoutThumbnailZip());

    // No embedded preview: falls back to the icon tile, still a valid PNG.
    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForArchiveFileIsValidPng() {
    const QByteArray thumbnail =
        FilePreview::thumbnailForFile(QStringLiteral("/tmp/backup.zip"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForEmailFileIsValidPng() {
    const QByteArray thumbnail = FilePreview::thumbnailForFile(QStringLiteral("mail.eml"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailForGenericFileIsValidPng() {
    const QByteArray thumbnail = FilePreview::thumbnailForFile(QStringLiteral("data.xyz"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailRespectsMaxEdge() {
    const QByteArray thumbnail = FilePreview::thumbnailForFile(QStringLiteral("notes.md"), 48);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QCOMPARE(qMax(image.width(), image.height()), 48);
}

void TestFilePreview::thumbnailForMissingFileFallsBackToIcon() {
    // A missing PDF falls back to the generic icon tile instead of failing.
    const QByteArray thumbnail =
        FilePreview::thumbnailForFile(QStringLiteral("/nonexistent/file.pdf"), 64);
    QVERIFY(!thumbnail.isEmpty());
    QVERIFY(!decodePng(thumbnail).isNull());
}

void TestFilePreview::thumbnailWithNonPositiveEdgeIsEmpty() {
    QVERIFY(FilePreview::thumbnailForFile(QStringLiteral("notes.txt"), 0).isEmpty());
    QVERIFY(FilePreview::thumbnailForFile(QStringLiteral("notes.txt"), -5).isEmpty());
}

void TestFilePreview::pdfThumbnailHasImageResolutionAndPageMargins() {
    if (!TextExtractor::hasPoppler())
        QSKIP("Poppler not available in this build");
    // A minimal but valid single-page PDF.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("%PDF-1.4\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
               "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
               "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] >>\nendobj\n"
               "trailer\n<< /Size 4 /Root 1 0 R >>\n%%EOF\n");
    file.close();

    if (TextExtractor::renderFirstPdfPage(path, 96).isNull())
        QSKIP("Poppler could not render the minimal test PDF");

    // Even when a small tile is requested, the PDF preview is rendered at
    // the same max edge as image attachments in the chat.
    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage thumbImage = decodePng(thumbnail);
    QVERIFY(!thumbImage.isNull());
    QCOMPARE(qMax(thumbImage.width(), thumbImage.height()), FilePreview::imageThumbnailEdge());

    // Dolphin-style page tile: the page is inset with margins, so the tile
    // corners are transparent and the center is the (white) page.
    QCOMPARE(thumbImage.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(thumbImage.pixelColor(thumbImage.width() - 1, 0).alpha(), 0);
    QVERIFY(thumbImage.pixelColor(thumbImage.width() / 2, thumbImage.height() / 2).alpha() > 200);

    // A larger request is honored as well.
    const QImage bigger = decodePng(FilePreview::thumbnailForFile(path, 512));
    QVERIFY(!bigger.isNull());
    QCOMPARE(qMax(bigger.width(), bigger.height()), 512);
}

// QTEST_MAIN equivalent that forces the offscreen platform: the thumbnail
// tests paint with QPainter/QFont, which needs a QGuiApplication, but the
// CI test runner has no display server.
int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestFilePreview test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_filepreview.moc"
