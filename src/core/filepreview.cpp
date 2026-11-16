#include "core/filepreview.h"

#include "core/textextractor.h"

#include <QBuffer>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#if OKAPI_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
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

// Draws the white page with folded corner shared by the page-like tiles.
void drawPage(QPainter &painter, int size, qreal &x, qreal &y, qreal &pageWidth, qreal &pageHeight,
              qreal &fold);

// Draws a white page with a folded corner plus a colored badge with the
// category letter, used for file types without a real page preview.
QImage iconTile(FileCategory category, int size) {
    QImage tile(size, size, QImage::Format_ARGB32);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);

    qreal x = 0, y = 0, pageWidth = 0, pageHeight = 0, fold = 0;
    drawPage(painter, size, x, y, pageWidth, pageHeight, fold);

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

// Draws the white page with folded corner shared by the page-like tiles.
// Returns the page geometry (x, y, pageWidth, pageHeight, fold) via out-params.
void drawPage(QPainter &painter, int size, qreal &x, qreal &y, qreal &pageWidth, qreal &pageHeight,
              qreal &fold) {
    const qreal marginX = size * 0.18;
    const qreal marginY = size * 0.12;
    pageWidth = size - (2 * marginX);
    pageHeight = qMin(pageWidth * 1.3, size - (2 * marginY));
    fold = pageWidth * 0.22;
    x = (size - pageWidth) / 2.0;
    y = (size - pageHeight) / 2.0;

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
}

// Draws a text file tile in the style of the Breeze "text-plain" icon: white
// page, folded corner and a few thin gray text lines, all contained within
// the page (no overflowing content text).
QImage textTile(int size) {
    QImage tile(size, size, QImage::Format_ARGB32);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);

    qreal x = 0, y = 0, pageWidth = 0, pageHeight = 0, fold = 0;
    drawPage(painter, size, x, y, pageWidth, pageHeight, fold);

    // Text lines strictly inside the page bounds.
    const qreal lineLeft = x + (pageWidth * 0.20);
    const qreal lineRight = x + (pageWidth * 0.80);
    constexpr int kLineCount = 5;
    const qreal top = y + (pageHeight * 0.30);
    const qreal bottom = y + (pageHeight * 0.85);
    const qreal spacing = (bottom - top) / (kLineCount - 1);
    painter.setPen(
        QPen(QColor(0x71, 0x79, 0x7f), qMax(1.0, size * 0.018), Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < kLineCount; ++i) {
        const qreal lineY = top + (i * spacing);
        painter.drawLine(QPointF(lineLeft, lineY), QPointF(lineRight, lineY));
    }
    painter.end();
    return tile;
}

// Renders the icon theme's icon for a file type (like Dolphin does), e.g.
// "text-plain" for text files. Returns a null image when no icon theme
// provides the icon (e.g. headless test environments).
QImage themeIconTile(const QStringList &names, int size) {
    for (const auto &name : names) {
        const QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull())
            continue;
        const QImage image = icon.pixmap(size, size).toImage();
        if (!image.isNull())
            return image;
    }
    return {};
}

// Composes a rendered page (e.g. a PDF's first page) into a square tile the
// way file managers do: the page keeps its aspect ratio, is centered with a
// margin, and gets a soft drop shadow.
QImage pageTileWithPreview(const QImage &page, int size) {
    QImage tile(size, size, QImage::Format_ARGB32);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const qreal margin = size * 0.12;
    const qreal avail = size - (2 * margin);
    const qreal scale = qMin(avail / page.width(), avail / page.height());
    const qreal pageWidth = page.width() * scale;
    const qreal pageHeight = page.height() * scale;
    const qreal x = (size - pageWidth) / 2.0;
    const qreal y = (size - pageHeight) / 2.0;

    // Soft shadow in two layers.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 45));
    painter.drawRoundedRect(QRectF(x + 2.0, y + 3.0, pageWidth, pageHeight), 2, 2);
    painter.setBrush(QColor(0, 0, 0, 70));
    painter.drawRoundedRect(QRectF(x + 1.0, y + 1.5, pageWidth, pageHeight), 2, 2);
    // White page behind the render.
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(x, y, pageWidth, pageHeight), 2, 2);
    painter.drawImage(QRectF(x, y, pageWidth, pageHeight), page);
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
    // Output edge: PDFs are rendered at least at imageThumbnailEdge() so the
    // page preview is exactly as sharp as an image thumbnail in the chat,
    // even when the caller only needs a small tile.
    int outEdge = maxEdge;
    if (category == FileCategory::Pdf) {
        outEdge = qMax(maxEdge, imageThumbnailEdge());
        // Render the page slightly smaller than the tile, then compose it
        // into a page tile with margins and a soft shadow (Dolphin-style).
        const int pageEdge = qMax(1, outEdge - qRound(outEdge * 0.24));
        const QImage page = TextExtractor::renderFirstPdfPage(path, pageEdge);
        if (!page.isNull())
            preview = pageTileWithPreview(page, outEdge);
    } else if (category == FileCategory::Word || category == FileCategory::Spreadsheet ||
               category == FileCategory::Presentation) {
#if OKAPI_HAVE_LIBARCHIVE
        preview = embeddedPreviewFromOffice(path);
#endif
    } else if (category == FileCategory::Text) {
        // Text files get the icon theme's text-plain icon (like Dolphin);
        // the drawn page tile is the fallback when no icon theme is present.
        preview = themeIconTile({QStringLiteral("text-plain"), QStringLiteral("text-x-generic")},
                                maxEdge);
    }
    if (preview.isNull()) {
        if (category == FileCategory::Text)
            preview = textTile(maxEdge);
        else
            preview = iconTile(category, maxEdge);
    }
    if (preview.isNull())
        return {};
    if (qMax(preview.width(), preview.height()) > outEdge)
        preview = preview.scaled(outEdge, outEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return encodePng(preview);
}

} // namespace okapi
