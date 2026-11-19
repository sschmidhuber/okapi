#include "core/filepreview.h"

#include "core/textextractor.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace okapi;

class TestFilePreview : public QObject {
    Q_OBJECT

private slots:
    void categoryForFileMapsExtensions();
    void badgeForCategoryCoversCategories();
    void thumbnailForTextFileIsValidPng();
    void thumbnailForTextFileShowsContent();
    void thumbnailForTextFileHasPageShape();
    void thumbnailForTextFileWrapsTextInsidePage();
    void thumbnailForTextFileSyntaxHighlighting();
    void thumbnailForOfficeWithEmbeddedPreview();
    void thumbnailForOdfWithEmbeddedPreview();
    void thumbnailForOfficeWithoutEmbeddedPreviewFallsBackToIcon();
    void thumbnailForArchiveFileIsValidPng();
    void thumbnailForEmailFileIsValidPng();
    void thumbnailForGenericFileIsValidPng();
    void thumbnailRespectsMaxEdge();
    void thumbnailForMissingFileFallsBackToIcon();
    void thumbnailWithNonPositiveEdgeIsEmpty();
    void pdfThumbnailMatchesRenderFirstPdfPage();
    void pdfThumbnailHasAntialiasedEdges();
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

void TestFilePreview::thumbnailForTextFileShowsContent() {
    // The text preview paints the file's first lines in dark pixels on a
    // white page; a line-only tile (the fallback) would not have dark text
    // pixels in the upper text area.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("code.py"));
    writeFile(path, "def hello():\n    print('hi')\n");

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    // Scan the page for text pixels. The page is rendered at 2x and smoothly
    // downscaled, so small antialiased glyphs end up mid-gray; the threshold
    // only has to separate them from the white page and the light border.
    int darkPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() > 200 && c.red() < 160 && c.green() < 160 && c.blue() < 160)
                ++darkPixels;
        }
    }
    QVERIFY(darkPixels > 10);
}

void TestFilePreview::thumbnailForTextFileHasPageShape() {
    // Text thumbnails use the same page representation as PDFs: a portrait
    // A4-shaped page whose longest edge is maxEdge, not a square tile with
    // a folded corner.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("notes.txt"));
    writeFile(path, "hello world\nsecond line\n");

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QCOMPARE(image.height(), 96);
    QVERIFY(image.height() > image.width());
    const qreal ratio = static_cast<qreal>(image.height()) / image.width();
    QVERIFY(qAbs(ratio - 1.41421356) < 0.05);
}

void TestFilePreview::thumbnailForTextFileWrapsTextInsidePage() {
    // Even a single very long unbroken line must wrap and stay inside the
    // page margins instead of overflowing the page edge.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("long.txt"));
    writeFile(path, QByteArray(500, 'x')); // one unbroken line

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    // No dark text pixels in the outer 3 px of the page (the page margin is
    // ~9% of the width, i.e. ~6 px at this size).
    int edgeTextPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (x < image.width() - 3 && y < image.height() - 3)
                continue;
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() > 200 && c.red() < 160 && c.green() < 160 && c.blue() < 160)
                ++edgeTextPixels;
        }
    }
    QCOMPARE(edgeTextPixels, 0);
}

void TestFilePreview::thumbnailForTextFileSyntaxHighlighting() {
    if (!FilePreview::hasSyntaxHighlighting())
        QSKIP("KSyntaxHighlighting not available in this build");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("code.py"));
    // The string literal is colored (red) by both KSyntaxHighlighting
    // themes and the built-in fallback highlighter.
    writeFile(path, "def hello():\n    print('hello world, a longer string')\n# a comment\n");

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 128);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    // Syntax highlighting colors tokens: count saturated (non-gray) pixels.
    int coloredPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            const int hi = qMax(c.red(), qMax(c.green(), c.blue()));
            const int lo = qMin(c.red(), qMin(c.green(), c.blue()));
            if (c.alpha() > 200 && hi - lo > 60)
                ++coloredPixels;
        }
    }
    QVERIFY(coloredPixels > 10);
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

void TestFilePreview::pdfThumbnailHasAntialiasedEdges() {
    if (!TextExtractor::hasPoppler())
        QSKIP("Poppler not available in this build");
    // A minimal single-page PDF with a thick black diagonal line.
    const QByteArray content = QByteArrayLiteral("0 0 0 RG 8 w 10 10 m 190 190 l S\n");
    QByteArray pdf =
        QByteArrayLiteral("%PDF-1.4\n"
                          "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
                          "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
                          "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] "
                          "/Contents 4 0 R >>\nendobj\n"
                          "4 0 obj\n<< /Length ");
    pdf += QByteArray::number(content.size());
    pdf += QByteArrayLiteral(" >>\nstream\n");
    pdf += content;
    pdf += QByteArrayLiteral("endstream\nendobj\ntrailer\n<< /Size 5 /Root 1 0 R >>\n%%EOF\n");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("line.pdf"));
    writeFile(path, pdf);

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage image = decodePng(thumbnail);
    QVERIFY(!image.isNull());
    QCOMPARE(qMax(image.width(), image.height()), 96);
    // Antialiased rendering (poppler hints + supersampled downscale) produces
    // intermediate gray pixels along the line's edges; a hard-aliased render
    // would be pure black and white only.
    int grayPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() > 200 && c.red() > 40 && c.red() < 215 && c.red() == c.green() &&
                c.green() == c.blue())
                ++grayPixels;
        }
    }
    QVERIFY(grayPixels > 0);
}

void TestFilePreview::pdfThumbnailMatchesRenderFirstPdfPage() {
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

    const QImage rendered = TextExtractor::renderFirstPdfPage(path, 96);
    if (rendered.isNull())
        QSKIP("Poppler could not render the minimal test PDF");
    QVERIFY(qMax(rendered.width(), rendered.height()) <= 96);

    const QByteArray thumbnail = FilePreview::thumbnailForFile(path, 96);
    QVERIFY(!thumbnail.isEmpty());
    const QImage thumbImage = decodePng(thumbnail);
    QVERIFY(!thumbImage.isNull());
    // The PDF preview should be close to the directly rendered page size.
    QCOMPARE(qMax(thumbImage.width(), thumbImage.height()), 96);
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
