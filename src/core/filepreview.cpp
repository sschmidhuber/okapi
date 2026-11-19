#include "core/filepreview.h"

#include "core/textextractor.h"

#include <QBuffer>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextOption>
#include <QVector>
#include <numbers>
#include <utility>

#if OKAPI_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#if OKAPI_HAVE_KSYNTAXHIGHLIGHTING
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif

namespace okapi {

namespace {

// Tile colors per category, matching the free-desktop palette conventions.
QColor tileColor(FileCategory category) {
    switch (category) {
    case FileCategory::Pdf:
        return {0xb7, 0x1c, 0x1c};
    case FileCategory::Word:
        return {0x1a, 0x5f, 0xb4};
    case FileCategory::Spreadsheet:
        return {0x1e, 0x7d, 0x32};
    case FileCategory::Presentation:
        return {0xd3, 0x6b, 0x1c};
    case FileCategory::Archive:
        return {0x6d, 0x4c, 0x41};
    case FileCategory::Email:
        return {0x5e, 0x35, 0xb1};
    case FileCategory::Text:
        return {0x45, 0x5a, 0x64};
    case FileCategory::Generic:
        return {0x75, 0x75, 0x75};
    }
    return {0x75, 0x75, 0x75};
}

// Draws a white page with a folded corner plus a colored badge with the
// category letter, used for file types without a real page preview.
QImage iconTile(FileCategory category, int size) {
    QImage tile(size, size, QImage::Format_ARGB32);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal margin = size * 0.18;
    const qreal pageWidth = size - (2 * margin);
    const qreal pageHeight = pageWidth * 1.3;
    const qreal fold = pageWidth * 0.22;
    const qreal x = (size - pageWidth) / 2.0;
    const qreal y = (size - pageHeight) / 2.0;

    QPainterPath page;
    page.moveTo(x, y);
    page.lineTo(x + pageWidth - fold, y);
    page.lineTo(x + pageWidth, y + fold);
    page.lineTo(x + pageWidth, y + pageHeight);
    page.lineTo(x, y + pageHeight);
    page.closeSubpath();
    painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
    painter.setBrush(Qt::white);
    painter.drawPath(page);
    QPainterPath foldPath;
    foldPath.moveTo(x + pageWidth - fold, y);
    foldPath.lineTo(x + pageWidth, y + fold);
    foldPath.lineTo(x + pageWidth - fold, y + fold);
    foldPath.closeSubpath();
    painter.setBrush(QColor(0xe0, 0xe0, 0xe0));
    painter.drawPath(foldPath);

    const QString badge = FilePreview::badgeForCategory(category);
    if (!badge.isEmpty()) {
        const qreal badgeSize = pageWidth * 0.62;
        const QRectF badgeRect(x + ((pageWidth - badgeSize) / 2.0),
                               y + ((pageHeight - badgeSize) / 2.0), badgeSize, badgeSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tileColor(category));
        painter.drawRoundedRect(badgeRect, badgeSize * 0.18, badgeSize * 0.18);
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(static_cast<int>(badgeSize * 0.52));
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(badgeRect, Qt::AlignCenter, badge);
    }
    painter.end();
    return tile;
}

// Fallback tile for text files whose content cannot be extracted: white
// page with a folded corner and dark text lines.
QImage textTile(int size) {
    QImage tile(size, size, QImage::Format_ARGB32);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal margin = size * 0.18;
    const qreal pageWidth = size - (2 * margin);
    const qreal pageHeight = pageWidth * 1.3;
    const qreal fold = pageWidth * 0.22;
    const qreal x = (size - pageWidth) / 2.0;
    const qreal y = (size - pageHeight) / 2.0;

    QPainterPath page;
    page.moveTo(x, y);
    page.lineTo(x + pageWidth - fold, y);
    page.lineTo(x + pageWidth, y + fold);
    page.lineTo(x + pageWidth, y + pageHeight);
    page.lineTo(x, y + pageHeight);
    page.closeSubpath();
    painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
    painter.setBrush(Qt::white);
    painter.drawPath(page);
    QPainterPath foldPath;
    foldPath.moveTo(x + pageWidth - fold, y);
    foldPath.lineTo(x + pageWidth, y + fold);
    foldPath.lineTo(x + pageWidth - fold, y + fold);
    foldPath.closeSubpath();
    painter.setBrush(QColor(0xe0, 0xe0, 0xe0));
    painter.drawPath(foldPath);

    painter.setPen(QPen(tileColor(FileCategory::Text), qMax(1.0, size * 0.035)));
    const qreal lineLeft = x + (pageWidth * 0.18);
    const qreal lineRight = x + (pageWidth * 0.82);
    const qreal lineSpacing = pageHeight * 0.13;
    qreal lineY = y + (pageHeight * 0.28);
    while (lineY < y + (pageHeight * 0.82)) {
        painter.drawLine(QPointF(lineLeft, lineY), QPointF(lineRight, lineY));
        lineY += lineSpacing;
    }
    painter.end();
    return tile;
}

// Office packages (OOXML .docx/.xlsx/.pptx and ODF .odt/.ods/.odp) are ZIP
// containers that often embed a ready-made preview of the first page:
// OOXML stores it at docProps/thumbnail.jpeg, ODF at Thumbnails/thumbnail.png.
// Returns a null image when the package has no embedded preview.
#if OKAPI_HAVE_LIBARCHIVE
QImage embeddedPreviewFromOffice(const QString &path) {
    archive *arch = archive_read_new();
    if (!arch)
        return {};
    archive_read_support_format_zip(arch);
    archive_read_support_format_empty(arch);
    struct ArchiveGuard {
        archive *handle;
        ~ArchiveGuard() {
            if (handle)
                archive_read_free(handle);
        }
    } guard{arch};

    if (archive_read_open_filename(arch, path.toUtf8().constData(), 10240) != ARCHIVE_OK)
        return {};

    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool odf = suffix == QLatin1String("odt") || suffix == QLatin1String("ods") ||
                     suffix == QLatin1String("odp");
    const QString wanted = odf ? QStringLiteral("Thumbnails/thumbnail.png")
                               : QStringLiteral("docProps/thumbnail.jpeg");

    archive_entry *entry = nullptr;
    while (archive_read_next_header(arch, &entry) == ARCHIVE_OK) {
        const QString entryName = QString::fromUtf8(archive_entry_pathname_utf8(entry));
        if (entryName != wanted)
            continue;
        QByteArray payload;
        payload.reserve(static_cast<int>(archive_entry_size(entry)));
        const void *chunk = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (archive_read_data_block(arch, &chunk, &size, &offset) == ARCHIVE_OK)
            payload.append(static_cast<const char *>(chunk), static_cast<int>(size));
        if (payload.isEmpty())
            return {};
        return QImage::fromData(payload);
    }
    return {};
}
#endif

// Fallback highlighter for builds without KSyntaxHighlighting: line/block
// comments, strings, numbers and a small keyword set per language family,
// in a light editor palette that reads well on the white page.
class GenericHighlighter : public QSyntaxHighlighter {
public:
    explicit GenericHighlighter(const QString &suffix, QTextDocument *parent)
        : QSyntaxHighlighter(parent) {
        QTextCharFormat keywordFormat;
        keywordFormat.setForeground(QColor(0x00, 0x00, 0xcc));
        keywordFormat.setFontWeight(QFont::Bold);
        QTextCharFormat stringFormat;
        stringFormat.setForeground(QColor(0xa3, 0x15, 0x15));
        QTextCharFormat commentFormat;
        commentFormat.setForeground(QColor(0x00, 0x80, 0x00));
        commentFormat.setFontItalic(true);
        QTextCharFormat numberFormat;
        numberFormat.setForeground(QColor(0x09, 0x86, 0x58));

        const QString s = suffix.toLower();
        const auto addRule = [this](const QString &pattern, const QTextCharFormat &format) {
            m_rules.append({.pattern = QRegularExpression(pattern), .format = format});
        };

        const bool cLike =
            s == QLatin1String("c") || s == QLatin1String("cc") || s == QLatin1String("cpp") ||
            s == QLatin1String("cxx") || s == QLatin1String("h") || s == QLatin1String("hpp") ||
            s == QLatin1String("java") || s == QLatin1String("js") || s == QLatin1String("ts") ||
            s == QLatin1String("go") || s == QLatin1String("rs") || s == QLatin1String("swift") ||
            s == QLatin1String("kt") || s == QLatin1String("kts") || s == QLatin1String("scala") ||
            s == QLatin1String("cs") || s == QLatin1String("dart") || s == QLatin1String("php") ||
            s == QLatin1String("css") || s == QLatin1String("scss");
        const bool markup = s == QLatin1String("html") || s == QLatin1String("htm") ||
                            s == QLatin1String("xml") || s == QLatin1String("svg") ||
                            s == QLatin1String("md") || s == QLatin1String("markdown");

        if (s == QLatin1String("py") || s == QLatin1String("sh") || s == QLatin1String("rb") ||
            s == QLatin1String("yaml") || s == QLatin1String("yml") || s == QLatin1String("toml") ||
            s == QLatin1String("pl") || s == QLatin1String("r") || s == QLatin1String("ps1"))
            addRule(QStringLiteral("#[^\\n]*"), commentFormat);
        else if (s == QLatin1String("sql") || s == QLatin1String("lua"))
            addRule(QStringLiteral("--[^\\n]*"), commentFormat);
        else if (cLike)
            addRule(QStringLiteral("//[^\\n]*"), commentFormat);

        if (cLike) {
            m_blockStart = QRegularExpression(QStringLiteral("/\\*"));
            m_blockEnd = QRegularExpression(QStringLiteral("\\*/"));
        } else if (markup) {
            m_blockStart = QRegularExpression(QStringLiteral("<!--"));
            m_blockEnd = QRegularExpression(QStringLiteral("-->"));
        }
        m_commentFormat = std::move(commentFormat);

        addRule(QStringLiteral("\"(?:[^\"\\\\\\n]|\\\\.)*\""), stringFormat);
        addRule(QStringLiteral("'(?:[^'\\\\\\n]|\\\\.)*'"), stringFormat);
        if (s == QLatin1String("js") || s == QLatin1String("ts"))
            addRule(QStringLiteral("`(?:[^`\\\\]|\\\\.)*`"), stringFormat);
        addRule(QStringLiteral("\\b(?:0[xX][0-9a-fA-F]+|\\d+(?:\\.\\d+)?)\\b"), numberFormat);

        QStringList keywords;
        if (s == QLatin1String("py"))
            keywords = QStringLiteral("def class return if elif else for while in import from "
                                      "as with try except finally raise pass break continue "
                                      "lambda yield global nonlocal assert del is not and or "
                                      "None True False")
                           .split(QLatin1Char(' '));
        else if (s == QLatin1String("js") || s == QLatin1String("ts"))
            keywords = QStringLiteral("const let var function return if else for while do switch "
                                      "case break continue class extends new this typeof "
                                      "instanceof in of try catch finally throw async await "
                                      "yield import export from default")
                           .split(QLatin1Char(' '));
        else if (s == QLatin1String("go"))
            keywords = QStringLiteral("func package import const var type struct interface map "
                                      "chan go return if else for range switch case break "
                                      "continue fallthrough defer select")
                           .split(QLatin1Char(' '));
        else if (s == QLatin1String("rs"))
            keywords = QStringLiteral("fn let mut const struct enum impl trait pub use mod "
                                      "return if else for while loop match break continue move "
                                      "ref static where async await")
                           .split(QLatin1Char(' '));
        else if (s == QLatin1String("sh"))
            keywords = QStringLiteral("if then else elif fi for while in do done case esac "
                                      "function return echo exit set local export")
                           .split(QLatin1Char(' '));
        else if (s == QLatin1String("sql"))
            keywords = QStringLiteral("select insert update delete from where join left right "
                                      "inner outer group by order having limit offset create "
                                      "table drop alter index values into set")
                           .split(QLatin1Char(' '));
        else if (cLike)
            keywords = QStringLiteral("int long short char float double void bool struct enum "
                                      "union class public private protected static const "
                                      "virtual override final new return if else for while do "
                                      "switch case break continue typedef namespace using "
                                      "template typename this sizeof throw try catch")
                           .split(QLatin1Char(' '));
        if (!keywords.isEmpty())
            addRule(QStringLiteral("\\b(?:") + keywords.join(QLatin1Char('|')) +
                        QStringLiteral(")\\b"),
                    keywordFormat);
    }

protected:
    void highlightBlock(const QString &text) override {
        for (const auto &rule : m_rules) {
            auto it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                setFormat(static_cast<int>(match.capturedStart()),
                          static_cast<int>(match.capturedLength()), rule.format);
            }
        }
        if (m_blockStart.pattern().isEmpty())
            return;
        const int textLength = static_cast<int>(text.length());
        int startIndex = 0;
        if (previousBlockState() != kInBlockComment)
            startIndex = static_cast<int>(m_blockStart.match(text).capturedStart());
        while (startIndex >= 0) {
            const QRegularExpressionMatch endMatch = m_blockEnd.match(text, startIndex);
            const int endIndex = static_cast<int>(endMatch.capturedStart());
            int length = 0;
            if (endIndex == -1) {
                setCurrentBlockState(kInBlockComment);
                length = textLength - startIndex;
            } else {
                length = endIndex - startIndex + static_cast<int>(endMatch.capturedLength());
            }
            setFormat(startIndex, length, m_commentFormat);
            startIndex =
                static_cast<int>(m_blockStart.match(text, startIndex + length).capturedStart());
        }
    }

private:
    static constexpr int kInBlockComment = 1;
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<Rule> m_rules;
    QRegularExpression m_blockStart;
    QRegularExpression m_blockEnd;
    QTextCharFormat m_commentFormat;
};

// Attaches a syntax highlighter to the document: KSyntaxHighlighting with
// the definition matching the file name when available, otherwise the
// built-in generic highlighter.
void attachHighlighter(QTextDocument *doc, const QString &path) {
#if OKAPI_HAVE_KSYNTAXHIGHLIGHTING
    // Leaked on purpose: the highlighters are parented to the document and
    // outlive this function, so they must never see a deleted repository.
    static KSyntaxHighlighting::Repository *repository = new KSyntaxHighlighting::Repository;
    const KSyntaxHighlighting::Definition definition =
        repository->definitionForFileName(QFileInfo(path).fileName());
    KSyntaxHighlighting::Theme theme = repository->theme(QStringLiteral("Breeze Light"));
    if (!theme.isValid())
        theme = repository->defaultTheme();
    if (definition.isValid() && theme.isValid()) {
        auto *highlighter = new KSyntaxHighlighting::SyntaxHighlighter(doc);
        highlighter->setDefinition(definition);
        highlighter->setTheme(theme);
        return;
    }
#endif
    new GenericHighlighter(QFileInfo(path).suffix(), doc);
}

// Renders the content of a text file onto an A4-shaped page — the same page
// representation PDF thumbnails use: a white portrait page whose longest
// edge is maxEdge. The text is laid out with QTextDocument, so it wraps at
// the page margins and never overflows the page, and it is syntax
// highlighted. Returns a null image when the file has no extractable text.
QImage renderTextPage(const QString &path, int maxEdge) {
    QString error;
    QString text = TextExtractor::extractFromTextFile(path, &error);
    if (text.isEmpty())
        return {};
    // Only the beginning of the file is visible on the page anyway; cap the
    // laid-out text so huge files stay cheap to render.
    constexpr int kPreviewMaxChars = 8000;
    if (text.size() > kPreviewMaxChars) {
        text = text.left(kPreviewMaxChars);
        text += QChar(0x2026); // horizontal ellipsis
    }

    // Render at 2x and downscale, so the small text stays crisp.
    constexpr qreal kSupersample = 2.0;
    constexpr qreal kA4HeightPerWidth = std::numbers::sqrt2;
    const int pageHeight = qMax(8, qRound(maxEdge * kSupersample));
    const int pageWidth = qMax(8, qRound(pageHeight / kA4HeightPerWidth));
    const qreal margin = pageWidth * 0.09;

    QImage page(pageWidth, pageHeight, QImage::Format_ARGB32);
    page.fill(Qt::white);

    QTextDocument doc;
    QFont font(QStringLiteral("Monospace"));
    font.setStyleHint(QFont::TypeWriter);
    font.setPixelSize(qMax(6, qRound(pageWidth * 0.062)));
    doc.setDefaultFont(font);
    doc.setDocumentMargin(0);
    QTextOption textOption = doc.defaultTextOption();
    // WrapAnywhere guarantees that even long unbroken tokens (URLs,
    // minified JSON) stay inside the page margins.
    textOption.setWrapMode(QTextOption::WrapAnywhere);
    textOption.setTabStopDistance(4.0 * QFontMetrics(font).horizontalAdvance(QLatin1Char(' ')));
    doc.setDefaultTextOption(textOption);
    doc.setTextWidth(pageWidth - (2 * margin));
    attachHighlighter(&doc, path);
    doc.setPlainText(text);

    QPainter painter(&page);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    // Hairline page edge, like the border of a rendered PDF page.
    painter.setPen(QPen(QColor(0, 0, 0, 40), 1));
    painter.setBrush(Qt::white);
    painter.drawRect(QRectF(0.5, 0.5, pageWidth - 1.0, pageHeight - 1.0));
    painter.save();
    painter.setClipRect(
        QRectF(margin, margin, pageWidth - (2 * margin), pageHeight - (2 * margin)));
    painter.translate(margin, margin);
    doc.drawContents(&painter);
    painter.restore();
    painter.end();

    const int outHeight = maxEdge;
    const int outWidth = qMax(1, qRound(maxEdge / kA4HeightPerWidth));
    return page.scaled(outWidth, outHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QByteArray encodePng(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return bytes;
}

} // namespace

FileCategory FilePreview::categoryForFile(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return FileCategory::Pdf;
    if (suffix == QLatin1String("docx") || suffix == QLatin1String("odt"))
        return FileCategory::Word;
    if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("ods"))
        return FileCategory::Spreadsheet;
    if (suffix == QLatin1String("pptx") || suffix == QLatin1String("odp"))
        return FileCategory::Presentation;
    if (suffix == QLatin1String("zip") || suffix == QLatin1String("tar") ||
        suffix == QLatin1String("gz") || suffix == QLatin1String("tgz") ||
        suffix == QLatin1String("bz2") || suffix == QLatin1String("xz") ||
        suffix == QLatin1String("7z"))
        return FileCategory::Archive;
    if (suffix == QLatin1String("eml") || suffix == QLatin1String("mbox"))
        return FileCategory::Email;
    if (suffix == QLatin1String("txt") || suffix == QLatin1String("md") ||
        suffix == QLatin1String("markdown") || suffix == QLatin1String("csv") ||
        suffix == QLatin1String("json") || suffix == QLatin1String("xml") ||
        suffix == QLatin1String("yaml") || suffix == QLatin1String("yml") ||
        suffix == QLatin1String("log") || suffix == QLatin1String("html") ||
        suffix == QLatin1String("htm") || suffix == QLatin1String("c") ||
        suffix == QLatin1String("cc") || suffix == QLatin1String("cpp") ||
        suffix == QLatin1String("cxx") || suffix == QLatin1String("h") ||
        suffix == QLatin1String("hpp") || suffix == QLatin1String("py") ||
        suffix == QLatin1String("js") || suffix == QLatin1String("ts") ||
        suffix == QLatin1String("java") || suffix == QLatin1String("rs") ||
        suffix == QLatin1String("go") || suffix == QLatin1String("rb") ||
        suffix == QLatin1String("sh") || suffix == QLatin1String("sql"))
        return FileCategory::Text;
    return FileCategory::Generic;
}

bool FilePreview::hasSyntaxHighlighting() {
#if OKAPI_HAVE_KSYNTAXHIGHLIGHTING
    return true;
#else
    return false;
#endif
}

QString FilePreview::badgeForCategory(FileCategory category) {
    switch (category) {
    case FileCategory::Pdf:
        return QStringLiteral("PDF");
    case FileCategory::Word:
        return QStringLiteral("W");
    case FileCategory::Spreadsheet:
        return QStringLiteral("X");
    case FileCategory::Presentation:
        return QStringLiteral("P");
    case FileCategory::Archive:
        return QStringLiteral("ZIP");
    case FileCategory::Email:
        return QStringLiteral("@");
    case FileCategory::Text:
    case FileCategory::Generic:
        return {};
    }
    return {};
}

QByteArray FilePreview::thumbnailForFile(const QString &path, int maxEdge) {
    if (maxEdge <= 0)
        return {};
    const FileCategory category = categoryForFile(path);

    QImage preview;
    if (category == FileCategory::Pdf) {
        preview = TextExtractor::renderFirstPdfPage(path, maxEdge);
    } else if (category == FileCategory::Word || category == FileCategory::Spreadsheet ||
               category == FileCategory::Presentation) {
#if OKAPI_HAVE_LIBARCHIVE
        preview = embeddedPreviewFromOffice(path);
#endif
    } else if (category == FileCategory::Text) {
        preview = renderTextPage(path, maxEdge);
    }
    if (preview.isNull()) {
        if (category == FileCategory::Text)
            preview = textTile(maxEdge);
        else
            preview = iconTile(category, maxEdge);
    }
    if (preview.isNull())
        return {};
    if (qMax(preview.width(), preview.height()) > maxEdge)
        preview = preview.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return encodePng(preview);
}

} // namespace okapi
