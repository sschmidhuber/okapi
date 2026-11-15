#include "ui/sidebar.h"
#include "core/chatsession.h"
#include "core/sessiongrouper.h"
#include "ui/theme.h"

#include <QApplication>
#include <QHeaderView>
#include <QIcon>
#include <QIconEngine>
#include <QKeySequence>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace okapi {

namespace {

constexpr auto kSessionIdRole = Qt::UserRole;
constexpr auto kIsGroupRole = Qt::UserRole + 1;
constexpr auto kSectionRole = Qt::UserRole + 2; // "pinned" / "date" / "deleted" / "time"
constexpr auto kPinnedRole = Qt::UserRole + 3;

// Vector icon: paints its callback at whatever pixel size Qt requests, so
// the badge stays sharp on high-DPI screens (Qt asks the engine for
// size * devicePixelRatio) instead of upscaling a fixed low-resolution
// bitmap.
class VectorIconEngine : public QIconEngine {
public:
    using Painter = std::function<void(QPainter &, const QRectF &)>;
    explicit VectorIconEngine(Painter painter) : m_painter(std::move(painter)) {}
    [[nodiscard]] QIconEngine *clone() const override { return new VectorIconEngine(*this); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State state) override {
        Q_UNUSED(mode);
        Q_UNUSED(state);
        painter->setRenderHint(QPainter::Antialiasing, true);
        m_painter(*painter, QRectF(rect));
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

private:
    Painter m_painter;
};

// Sessions are expected newest-first; group them by last activity and
// return the group keys in chronological order (newest group first).
QStringList orderedGroupKeys(const QVector<ChatSession *> &sessions) {
    QMap<QString, QString> keyToSort; // group key -> ISO sort key
    for (auto *session : sessions) {
        const QString key = SessionGrouper::groupKey(session->lastActivityAt());
        if (key.isEmpty())
            continue;
        // ISO dates sort chronologically; "today"/"yesterday" sort newest.
        QString sortKey;
        if (key == QLatin1String("today"))
            sortKey = QStringLiteral("9999");
        else if (key == QLatin1String("yesterday"))
            sortKey = QStringLiteral("9998");
        else
            sortKey = key.mid(key.indexOf(QLatin1Char(':')) + 1);
        if (!keyToSort.contains(key) || sortKey > keyToSort.value(key))
            keyToSort.insert(key, sortKey);
    }
    const QStringList keys(keyToSort.keyBegin(), keyToSort.keyEnd());
    QStringList sorted = keys;
    std::ranges::sort(sorted, [&keyToSort](const QString &a, const QString &b) {
        return keyToSort.value(a) > keyToSort.value(b);
    });
    return sorted;
}

} // namespace

Sidebar::Sidebar(QWidget *parent) : QWidget(parent), m_tree(new QTreeWidget(this)) {
    // The spinner of running chats rotates continuously; started lazily in
    // setIndicators() so an idle sidebar keeps no timer running.
    m_spinnerTimer = new QTimer(this);
    m_spinnerTimer->setInterval(120);
    connect(m_spinnerTimer, &QTimer::timeout, this, &Sidebar::advanceSpinner);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_tree, 1);

    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(12);
    // Multi-selection: Ctrl+click adds items, Shift+click selects a range,
    // group headers are selectable like chats.
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!item || item->data(0, kIsGroupRole).toBool())
            return;
        // Ctrl/Shift+click only extends the selection (for multi-delete); it
        // must not switch the visible chat.
        if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
            return;
        emit sessionSelected(QUuid(item->data(0, kSessionIdRole).toString()));
    });
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &Sidebar::showContextMenu);
    // The Delete key removes the selected chats while the tree has focus.
    auto *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), m_tree);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &Sidebar::requestDeleteSelected);
}

void Sidebar::setSessions(const QVector<ChatSession *> &sessions) {
    const QUuid current = currentSessionId();
    const bool firstBuild = m_tree->topLevelItemCount() == 0;
    // Remember which group items were expanded so a rebuild keeps the state.
    QStringList expandedKeys;
    const std::function<void(QTreeWidgetItem *)> collectExpanded = [&](QTreeWidgetItem *item) {
        if (item->data(0, kIsGroupRole).toBool() && item->isExpanded())
            expandedKeys.push_back(item->data(0, kSessionIdRole).toString());
        for (int i = 0; i < item->childCount(); ++i)
            collectExpanded(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        collectExpanded(m_tree->topLevelItem(i));

    // Partition: deleted chats only appear under "Deleted", pinned chats
    // only under "Pinned" (never in the date-based groups).
    QVector<ChatSession *> pinned;
    QVector<ChatSession *> deleted;
    QVector<ChatSession *> dated;
    for (auto *session : sessions) {
        if (session->deletedAt().isValid())
            deleted.push_back(session);
        else if (session->pinned())
            pinned.push_back(session);
        else
            dated.push_back(session);
    }
    std::ranges::sort(pinned, [](const ChatSession *a, const ChatSession *b) {
        return a->lastActivityAt() > b->lastActivityAt();
    });
    std::ranges::sort(deleted, [](const ChatSession *a, const ChatSession *b) {
        return a->deletedAt() > b->deletedAt();
    });

    m_tree->clear();

    const auto makeSection = [&](const QString &key, const QString &label,
                                 const QString &iconPath) {
        auto *section = new QTreeWidgetItem(m_tree);
        section->setText(0, label);
        section->setData(0, kSessionIdRole, key);
        section->setData(0, kIsGroupRole, true);
        section->setData(0, kSectionRole, key);
        section->setIcon(0, themedIcon(iconPath));
        QFont sectionFont = section->font(0);
        sectionFont.setBold(true);
        section->setFont(0, sectionFont);
        // On the first build the sections are expanded; afterwards the
        // user's expand state is preserved exactly.
        section->setExpanded(firstBuild || expandedKeys.contains(key));
        return section;
    };

    auto *pinnedSection = makeSection(QStringLiteral("pinned"), tr("Pinned"),
                                      QStringLiteral(":/resources/icons/pin-symbolic.svg"));
    for (auto *session : pinned)
        makeChatItem(pinnedSection, session);

    auto *dateSection = makeSection(QStringLiteral("date"), tr("Date"),
                                    QStringLiteral(":/resources/icons/date-symbolic.svg"));
    for (const auto &key : orderedGroupKeys(dated)) {
        auto *group = new QTreeWidgetItem(dateSection);
        group->setText(0, SessionGrouper::groupLabel(key));
        group->setData(0, kSessionIdRole, key);
        group->setData(0, kIsGroupRole, true);
        group->setData(0, kSectionRole, QStringLiteral("time"));
        // On the first build only "Today" is expanded; afterwards the
        // user's expand state is preserved exactly.
        group->setExpanded(firstBuild ? key == QLatin1String("today") : expandedKeys.contains(key));
        QFont groupFont = group->font(0);
        groupFont.setBold(true);
        group->setFont(0, groupFont);
        for (auto *session : dated) {
            if (SessionGrouper::groupKey(session->lastActivityAt()) != key)
                continue;
            makeChatItem(group, session);
        }
    }

    auto *deletedSection =
        makeSection(QStringLiteral("deleted"), tr("Deleted"),
                    QStringLiteral(":/resources/icons/delete-symbolic-sidebar.svg"));
    for (auto *session : deleted)
        makeChatItem(deletedSection, session);

    if (!current.isNull())
        selectSession(current);
    applyIndicators();
}

void Sidebar::showContextMenu(const QPoint &pos) {
    auto *item = m_tree->itemAt(pos);
    if (!item)
        return;
    // Right-clicking an item outside the selection selects only that
    // item; right-clicking a selected item keeps the multi-selection.
    // (setCurrentItem alone does not clear the selection in
    // ExtendedSelection mode.)
    if (!item->isSelected()) {
        m_tree->clearSelection();
        m_tree->setCurrentItem(item);
    }
    QMenu menu(this);
    const bool isChat = !item->data(0, kIsGroupRole).toBool();
    const QString section = sectionOf(item);
    if (section == QLatin1String("deleted")) {
        // Chats in the "Deleted" section can be restored or removed
        // permanently.
        const QList<QUuid> targets = selectedDeletedSessionIds(item);
        auto *restoreAction = menu.addAction(tr("Restore"));
        auto *permanentAction = menu.addAction(tr("Permanently delete"));
        const QAction *picked = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (picked == restoreAction)
            emit sessionRestoreRequested(targets);
        else if (picked == permanentAction)
            emit sessionPermanentDeleteRequested(targets);
        return;
    }
    QAction *pinAction = nullptr;
    QUuid pinId;
    bool pinState = false;
    if (isChat) {
        pinId = QUuid(item->data(0, kSessionIdRole).toString());
        pinState = item->data(0, kPinnedRole).toBool();
        pinAction = menu.addAction(pinState ? tr("Unpin chat") : tr("Pin chat"));
    }
    auto *deleteAction = menu.addAction(tr("Delete chat"));
    const QList<QUuid> deletable = deletableSelectedSessionIds();
    // Running, unread and already deleted chats can never be deleted.
    deleteAction->setEnabled(!deletable.isEmpty());
    if (const QAction *picked = menu.exec(m_tree->viewport()->mapToGlobal(pos))) {
        if (picked == pinAction)
            emit sessionPinToggled(pinId, !pinState);
        else if (picked == deleteAction)
            emit sessionDeleteRequested(deletable);
    }
}

QTreeWidgetItem *Sidebar::makeChatItem(QTreeWidgetItem *parent, const ChatSession *session) {
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, session->title());
    item->setData(0, kSessionIdRole, session->id().toString());
    item->setData(0, kIsGroupRole, false);
    item->setData(0, kPinnedRole, session->pinned());
    item->setToolTip(0, session->title());
    return item;
}

QString Sidebar::sectionOf(const QTreeWidgetItem *item) {
    for (const QTreeWidgetItem *p = item; p; p = p->parent()) {
        const QString section = p->data(0, kSectionRole).toString();
        // The "time" sub-groups belong to the "Date" section.
        if (!section.isEmpty() && section != QLatin1String("time"))
            return section;
    }
    return {};
}

void Sidebar::collectChatIds(const QTreeWidgetItem *item, QSet<QUuid> &ids) const {
    if (!item->data(0, kIsGroupRole).toBool()) {
        ids.insert(QUuid(item->data(0, kSessionIdRole).toString()));
        return;
    }
    for (int i = 0; i < item->childCount(); ++i)
        collectChatIds(item->child(i), ids);
}

void Sidebar::selectSession(const QUuid &id) {
    const std::function<QTreeWidgetItem *(QTreeWidgetItem *)> find = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            auto *child = parent->child(i);
            if (!child->data(0, kIsGroupRole).toBool() &&
                QUuid(child->data(0, kSessionIdRole).toString()) == id)
                return child;
            if (auto *nested = find(child))
                return nested;
        }
        return static_cast<QTreeWidgetItem *>(nullptr);
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (auto *item = find(m_tree->topLevelItem(i))) {
            // With ExtendedSelection setCurrentItem() does not clear the
            // previous selection (unlike SingleSelection), so clear it
            // explicitly: switching chats must show exactly one selected.
            m_tree->clearSelection();
            m_tree->setCurrentItem(item);
            return;
        }
    }
}

QUuid Sidebar::currentSessionId() const {
    auto *item = m_tree->currentItem();
    if (!item || item->data(0, kIsGroupRole).toBool())
        return {};
    return QUuid(item->data(0, kSessionIdRole).toString());
}

QList<QUuid> Sidebar::selectedSessionIds() const {
    QSet<QUuid> ids;
    const auto selected = m_tree->selectedItems();
    for (const auto *item : selected)
        collectChatIds(item, ids);
    return {ids.begin(), ids.end()};
}

QList<QUuid> Sidebar::selectedDeletedSessionIds(const QTreeWidgetItem *clicked) const {
    QSet<QUuid> ids;
    const auto selected = m_tree->selectedItems();
    for (const auto *item : selected) {
        if (!item->data(0, kIsGroupRole).toBool() && sectionOf(item) == QLatin1String("deleted"))
            ids.insert(QUuid(item->data(0, kSessionIdRole).toString()));
    }
    if (ids.isEmpty() && clicked && !clicked->data(0, kIsGroupRole).toBool() &&
        sectionOf(clicked) == QLatin1String("deleted"))
        ids.insert(QUuid(clicked->data(0, kSessionIdRole).toString()));
    return {ids.begin(), ids.end()};
}

QList<QUuid> Sidebar::deletableSelectedSessionIds() const {
    // Finds the tree item of a chat by its session-id role (findItems would
    // match the display text instead).
    const std::function<QTreeWidgetItem *(QTreeWidgetItem *, const QString &)> findItem =
        [&](QTreeWidgetItem *parent, const QString &idString) {
            for (int i = 0; i < parent->childCount(); ++i) {
                auto *child = parent->child(i);
                if (child->data(0, kSessionIdRole).toString() == idString)
                    return child;
                if (auto *nested = findItem(child, idString))
                    return nested;
            }
            return static_cast<QTreeWidgetItem *>(nullptr);
        };
    QList<QUuid> ids = selectedSessionIds();
    const auto removed = std::ranges::remove_if(ids, [&](const QUuid &id) {
        if (m_running.contains(id) || m_unread.contains(id))
            return true;
        // Already deleted chats are not deleted again; they are restored or
        // permanently deleted via their own context-menu entries.
        QTreeWidgetItem *found = nullptr;
        for (int i = 0; i < m_tree->topLevelItemCount() && !found; ++i)
            found = findItem(m_tree->topLevelItem(i), id.toString());
        return found && sectionOf(found) == QLatin1String("deleted");
    });
    ids.erase(removed.begin(), removed.end());
    return ids;
}

void Sidebar::requestDeleteSelected() {
    const QList<QUuid> deletable = deletableSelectedSessionIds();
    if (!deletable.isEmpty())
        emit sessionDeleteRequested(deletable);
}

void Sidebar::setIndicators(const QSet<QUuid> &running, const QSet<QUuid> &unread,
                            const QSet<QUuid> &drafts) {
    m_running = running;
    m_unread = unread;
    m_drafts = drafts;
    if (!m_running.isEmpty())
        m_spinnerTimer->start();
    else
        m_spinnerTimer->stop();
    applyIndicators();
}

void Sidebar::applyIndicators() {
    const std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
        if (item->data(0, kIsGroupRole).toBool()) {
            for (int i = 0; i < item->childCount(); ++i)
                walk(item->child(i));
            return;
        }
        const QUuid id(item->data(0, kSessionIdRole).toString());
        QIcon icon;
        QStringList state;
        if (m_running.contains(id)) {
            icon = spinnerIcon();
            state << tr("generating");
        }
        if (m_unread.contains(id)) {
            if (icon.isNull())
                icon = unreadDotIcon();
            state << tr("unread messages");
        }
        if (m_drafts.contains(id)) {
            if (icon.isNull())
                icon = draftIcon();
            state << tr("unsent draft");
        }
        item->setIcon(0, icon);
        item->setToolTip(0, state.isEmpty() ? item->text(0)
                                            : item->text(0) + QLatin1String(" \u2014 ") +
                                                  state.join(QLatin1String(", ")));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        walk(m_tree->topLevelItem(i));
}

void Sidebar::advanceSpinner() {
    m_spinnerAngle = (m_spinnerAngle + 45) % 360;
    applyIndicators();
}

QIcon Sidebar::spinnerIcon() const {
    // Breeze-style loading spinner: a thick arc sweeping most of the circle,
    // rotating with the timer.
    const QPen pen(palette().color(QPalette::Text), 2.4, Qt::SolidLine, Qt::RoundCap);
    const int angle = m_spinnerAngle;
    return QIcon(new VectorIconEngine([pen, angle](QPainter &painter, const QRectF &rect) {
        // The pen width scales with the icon so the spinner keeps its look
        // at every size; the arc leaves a gap that rotates with the timer.
        QPen scaled = pen;
        scaled.setWidthF(pen.widthF() * (rect.width() / 16.0));
        painter.setPen(scaled);
        const QRectF circle = rect.marginsRemoved(QMarginsF(
            rect.width() * 0.14, rect.height() * 0.14, rect.width() * 0.14, rect.height() * 0.14));
        painter.drawArc(circle, -angle * 16, 300 * 16);
    }));
}

QIcon Sidebar::draftIcon() {
    // Unsaved composer draft: the Breeze "edit-entry-symbolic" icon,
    // recolored to the palette text color so it stays visible in dark mode.
    return themedIcon(QStringLiteral(":/resources/icons/edit-entry-symbolic.svg"));
}

QIcon Sidebar::unreadDotIcon() const {
    // Unread badge: a filled circle in the theme's highlight color.
    const QBrush brush(palette().color(QPalette::Highlight));
    return QIcon(new VectorIconEngine([brush](QPainter &painter, const QRectF &rect) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
        painter.drawEllipse(rect.center(), rect.width() * 0.25, rect.height() * 0.25);
    }));
}

} // namespace okapi
