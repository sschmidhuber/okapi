#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace okapi {

// Categorizes attached non-image files so the chat can render a matching
// thumbnail for every supported file type.
enum class FileCategory {
    Pdf,
    Word,         // .docx / .odt
    Spreadsheet,  // .xlsx / .ods
    Presentation, // .pptx / .odp
    Archive,
    Email,
    Text,
    Generic,
};

// Renders a small PNG preview (thumbnail) of a supported file, the way a
// file manager like Dolphin does: PDFs render their first page via Poppler
// on a white page tile with margins and a soft shadow, text files use the
// icon theme's text-plain icon (drawn fallback when no theme is available),
// office documents use their embedded preview, archives and e-mails get a
// generic icon tile.
class FilePreview {
public:
    // Category of a file, derived from its extension (lower-cased suffix).
    [[nodiscard]] static FileCategory categoryForFile(const QString &path);

    // One-letter badge drawn on generic tiles, empty for page-like tiles.
    [[nodiscard]] static QString badgeForCategory(FileCategory category);

    // Max edge of image attachments as shown in the chat; PDF page previews
    // are rendered at this edge so they are exactly as sharp as image
    // thumbnails.
    [[nodiscard]] static constexpr int imageThumbnailEdge() { return 320; }

    // Generates the PNG-encoded thumbnail for a file, scaled to fit
    // maxEdge. PDF previews are rendered at least at imageThumbnailEdge()
    // regardless of maxEdge, so they stay sharp at every display size.
    // Returns an empty QByteArray when no preview is available.
    [[nodiscard]] static QByteArray thumbnailForFile(const QString &path, int maxEdge);

    [[nodiscard]] static int maxThumbnailEdge() { return 96; }
};

} // namespace okapi
