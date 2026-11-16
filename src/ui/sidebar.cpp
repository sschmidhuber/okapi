#include "ui/sidebar.h"
#include "core/chatsession.h"
#include "core/sessiongrouper.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDropEvent>
#include <QHash>
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
constexpr auto kSectionRole = Qt::UserRole + 2; // "pinned" / "date" / "topics" / "deleted" / "time"
constexpr auto kPinnedRole = Qt::UserRole + 3;
constexpr auto kTopicIdRole = Qt::UserRole + 4; // topic items and chats carry their topic id

// The section ("pinned"/"date"/"topics"/"deleted") the item belongs to,
// found by walking up the parents; empty for the "time" sub-groups of
// "Date".
QString sectionOf(const QTreeWidgetItem *item) {
    for (const QTreeWidgetItem *p = item; p; p = p->parent()) {
        const QString section = p->data(0, kSectionRole).toString();
        // The "time" sub-groups belong to the "Date" section.
        if (!section.isEmpty() && section != QLatin1String("time"))
            return section;
    }
    return {};
}

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
    // Chats can be dragged onto the sections and topics to move them there.
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    // Drops are handled in eventFilter(): instead of moving items internally
    // (which would corrupt the section model), the drop is translated into
    // the target section/topic and emitted as sessionsDropped().
    m_tree->viewport()->installEventFilter(this);

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

bool Sidebar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_tree->viewport() && event->type() == QEvent::Drop) {
        auto *drop = dynamic_cast<QDropEvent *>(event);
        if (!drop) {
            event->ignore();
            return true;
        }
        auto *target = m_tree->itemAt(drop->position().toPoint());
        // Dropping onto a chat drops into its container group.
        if (target && !target->data(0, kIsGroupRole).toBool())
            target = target->parent();
        QList<QUuid> ids;
        QSet<QUuid> seen;
        const auto selected = m_tree->selectedItems();
        for (auto *item : selected) {
            if (item->data(0, kIsGroupRole).toBool())
                continue;
            const QUuid id(item->data(0, kSessionIdRole).toString());
            if (!id.isNull() && !seen.contains(id)) {
                seen.insert(id);
                ids.append(id);
            }
        }
        const QString section = target ? sectionOf(target) : QString();
        if (!target || ids.isEmpty() || section.isEmpty()) {
            drop->ignore();
            return true;
        }
        drop->accept();
        emit sessionsDropped(ids, section, target->data(0, kTopicIdRole).toString());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void Sidebar::setSessions(const QVector<ChatSession *> &sessions) {
    m_sessions = sessions;
    rebuildTree();
}

void Sidebar::setTopics(const QVector<StoredTopic> &topics) {
    m_topics = topics;
    rebuildTree();
}

QStringList Sidebar::collectExpandedKeys() const {
    // Remembers which group items were expanded so a rebuild keeps the state.
    QStringList expandedKeys;
    const std::function<void(const QTreeWidgetItem *)> collectExpanded =
        [&](const QTreeWidgetItem *item) {
            if (item->data(0, kIsGroupRole).toBool() && item->isExpanded())
                expandedKeys.push_back(item->data(0, kSessionIdRole).toString());
            for (int i = 0; i < item->childCount(); ++i)
                collectExpanded(item->child(i));
        };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        collectExpanded(m_tree->topLevelItem(i));
    return expandedKeys;
}

void Sidebar::partitionSessions(QVector<ChatSession *> &pinned, QVector<ChatSession *> &deleted,
                                QVector<ChatSession *> &dated,
                                QHash<QString, QVector<ChatSession *>> &byTopic) const {
    // Deleted chats only appear under "Deleted", chats in a topic only under
    // that topic, pinned chats only under "Pinned" (never in the date-based
    // groups).
    QSet<QString> topicIds;
    for (const auto &topic : m_topics)
        topicIds.insert(topic.id);
    for (auto *session : m_sessions) {
        if (session->deletedAt().isValid()) {
            deleted.push_back(session);
        } else if (!session->topicId().isEmpty() && topicIds.contains(session->topicId())) {
            auto it = byTopic.find(session->topicId());
            if (it == byTopic.end())
                it = byTopic.insert(session->topicId(), {});
            it->push_back(session);
        } else if (session->pinned()) {
            pinned.push_back(session);
        } else {
            dated.push_back(session);
        }
    }
    const auto byActivityDesc = [](QVector<ChatSession *> &chats) {
        std::ranges::sort(chats, [](const ChatSession *a, const ChatSession *b) {
            return a->lastActivityAt() > b->lastActivityAt();
        });
    };
    byActivityDesc(pinned);
    std::ranges::sort(deleted, [](const ChatSession *a, const ChatSession *b) {
        return a->deletedAt() > b->deletedAt();
    });
    for (auto &chats : byTopic)
        byActivityDesc(chats);
}

QTreeWidgetItem *Sidebar::makeSection(const QString &key, const QString &label,
                                      const QString &iconPath, bool expanded) {
    auto *section = new QTreeWidgetItem(m_tree);
    section->setText(0, label);
    section->setData(0, kSessionIdRole, key);
    section->setData(0, kIsGroupRole, true);
    section->setData(0, kSectionRole, key);
    section->setIcon(0, themedIcon(iconPath));
    QFont sectionFont = section->font(0);
    sectionFont.setBold(true);
    section->setFont(0, sectionFont);
    section->setExpanded(expanded);
    return section;
}

void Sidebar::buildDateSection(QTreeWidgetItem *dateSection, const QVector<ChatSession *> &dated,
                               bool firstBuild, const QStringList &expandedKeys) {
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
}

void Sidebar::buildTopicsSection(QTreeWidgetItem *topicsSection,
                                 const QHash<QString, QVector<ChatSession *>> &byTopic,
                                 bool firstBuild, const QStringList &expandedKeys) {
    for (const auto &topic : m_topics) {
        auto *topicItem = new QTreeWidgetItem(topicsSection);
        topicItem->setText(0, topic.name);
        topicItem->setData(0, kSessionIdRole, topic.id);
        topicItem->setData(0, kIsGroupRole, true);
        topicItem->setData(0, kSectionRole, QStringLiteral("topics"));
        topicItem->setData(0, kTopicIdRole, topic.id);
        topicItem->setIcon(
            0, themedIcon(QStringLiteral(":/resources/icons/document-open-folder-symbolic.svg")));
        topicItem->setToolTip(0, topic.name);
        QFont topicFont = topicItem->font(0);
        topicFont.setBold(true);
        topicItem->setFont(0, topicFont);
        // On the first build topics are expanded so their chats are
        // visible; afterwards the user's expand state is preserved.
        topicItem->setExpanded(firstBuild || expandedKeys.contains(topic.id));
        for (auto *session : byTopic.value(topic.id))
            makeChatItem(topicItem, session);
    }
}

void Sidebar::rebuildTree() {
    const QUuid current = currentSessionId();
    const bool firstBuild = m_tree->topLevelItemCount() == 0;
    const QStringList expandedKeys = collectExpandedKeys();

    QVector<ChatSession *> pinned;
    QVector<ChatSession *> deleted;
    QVector<ChatSession *> dated;
    QHash<QString, QVector<ChatSession *>> byTopic;
    partitionSessions(pinned, deleted, dated, byTopic);

    m_tree->clear();

    // On the first build the sections are expanded, except "Deleted" (a
    // deleted chat must never make it expand automatically); afterwards the
    // user's expand state is preserved exactly.
    const auto sectionExpanded = [&](const QString &key) {
        return firstBuild ? key != QLatin1String("deleted") : expandedKeys.contains(key);
    };

    auto *pinnedSection = makeSection(QStringLiteral("pinned"), tr("Pinned"),
                                      QStringLiteral(":/resources/icons/pin-symbolic.svg"),
                                      sectionExpanded(QStringLiteral("pinned")));
    for (auto *session : pinned)
        makeChatItem(pinnedSection, session);

    auto *dateSection = makeSection(QStringLiteral("date"), tr("Date"),
                                    QStringLiteral(":/resources/icons/date-symbolic.svg"),
                                    sectionExpanded(QStringLiteral("date")));
    buildDateSection(dateSection, dated, firstBuild, expandedKeys);

    auto *topicsSection =
        makeSection(QStringLiteral("topics"), tr("Topic"),
                    QStringLiteral(":/resources/icons/document-open-folder-symbolic.svg"),
                    sectionExpanded(QStringLiteral("topics")));
    buildTopicsSection(topicsSection, byTopic, firstBuild, expandedKeys);

    auto *deletedSection =
        makeSection(QStringLiteral("deleted"), tr("Deleted"),
                    QStringLiteral(":/resources/icons/delete-symbolic-sidebar.svg"),
                    sectionExpanded(QStringLiteral("deleted")));
    for (auto *session : deleted)
        makeChatItem(deletedSection, session);

    if (!current.isNull())
        selectSession(current);
    applyIndicators();
}

void Sidebar::addMoveToTopicSubmenu(QMenu *menu, const QUuid &chatId,
                                    const QString &currentTopicId) {
    // Second level of the context menu: one entry per available topic.
    QMenu *submenu = menu->addMenu(tr("Move to topic"));
    for (const auto &topic : m_topics) {
        QAction *action = submenu->addAction(topic.name);
        if (topic.id == currentTopicId)
            action->setEnabled(false); // already in this topic
        connect(action, &QAction::triggered, this,
                [this, chatId, id = topic.id]() { emit sessionMoveToTopicRequested(chatId, id); });
    }
    if (m_topics.isEmpty())
        submenu->setEnabled(false);
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
    const bool isChat = !item->data(0, kIsGroupRole).toBool();
    const QString section = sectionOf(item);
    const QString itemTopicId = item->data(0, kTopicIdRole).toString();

    QMenu menu(this);
    if (isChat)
        populateChatMenu(menu, item, section);
    else if (!itemTopicId.isEmpty())
        populateTopicMenu(menu, itemTopicId);
    else if (section == QLatin1String("topics"))
        populateTopicsHeaderMenu(menu);
    else if (section == QLatin1String("deleted"))
        populateDeletedHeaderMenu(menu, item);
    else
        populateSectionMenu(menu);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// Chat entries: Pin/Unpin, Rename, Move to topic (submenu) and Delete —
// chats inside a topic additionally get "Remove from topic", deleted chats
// get Restore / Permanently delete (plus Rename and Move to topic).
void Sidebar::populateChatMenu(QMenu &menu, const QTreeWidgetItem *item, const QString &section) {
    const QUuid chatId(item->data(0, kSessionIdRole).toString());
    const bool pinState = item->data(0, kPinnedRole).toBool();
    const QString chatTopicId = item->data(0, kTopicIdRole).toString();
    if (section == QLatin1String("deleted")) {
        QAction *renameAction = menu.addAction(tr("Rename"));
        connect(renameAction, &QAction::triggered, this,
                [this, chatId]() { emit sessionRenameRequested(chatId); });
        addMoveToTopicSubmenu(&menu, chatId, chatTopicId);
        menu.addSeparator();
        const QList<QUuid> targets = selectedDeletedSessionIds(item);
        QAction *restoreAction = menu.addAction(tr("Restore"));
        connect(restoreAction, &QAction::triggered, this,
                [this, targets]() { emit sessionRestoreRequested(targets); });
        QAction *permanentAction = menu.addAction(tr("Permanently delete"));
        connect(permanentAction, &QAction::triggered, this,
                [this, targets]() { emit sessionPermanentDeleteRequested(targets); });
        return;
    }
    QAction *pinAction = menu.addAction(pinState ? tr("Unpin chat") : tr("Pin chat"));
    connect(pinAction, &QAction::triggered, this,
            [this, chatId, pinState]() { emit sessionPinToggled(chatId, !pinState); });
    QAction *renameAction = menu.addAction(tr("Rename"));
    connect(renameAction, &QAction::triggered, this,
            [this, chatId]() { emit sessionRenameRequested(chatId); });
    addMoveToTopicSubmenu(&menu, chatId, chatTopicId);
    if (section == QLatin1String("topics")) {
        QAction *removeAction = menu.addAction(tr("Remove from topic"));
        connect(removeAction, &QAction::triggered, this,
                [this, chatId]() { emit sessionRemoveFromTopicRequested(chatId); });
    }
    QAction *deleteAction = menu.addAction(tr("Delete chat"));
    // Running, unread and already deleted chats can never be deleted.
    deleteAction->setEnabled(!deletableSelectedSessionIds().isEmpty());
    connect(deleteAction, &QAction::triggered, this,
            [this]() { emit sessionDeleteRequested(deletableSelectedSessionIds()); });
}

// A topic item: create a new topic, or rename/delete this one.
void Sidebar::populateTopicMenu(QMenu &menu, const QString &topicId) {
    QAction *createAction = menu.addAction(tr("New topic"));
    connect(createAction, &QAction::triggered, this, [this]() { emit topicCreateRequested(); });
    QAction *renameAction = menu.addAction(tr("Rename topic"));
    connect(renameAction, &QAction::triggered, this,
            [this, topicId]() { emit topicRenameRequested(topicId); });
    QAction *deleteAction = menu.addAction(tr("Delete topic"));
    connect(deleteAction, &QAction::triggered, this,
            [this, topicId]() { emit topicDeleteRequested(topicId); });
}

// The "Topic" section header: create a new topic.
void Sidebar::populateTopicsHeaderMenu(QMenu &menu) {
    QAction *createAction = menu.addAction(tr("New topic"));
    connect(createAction, &QAction::triggered, this, [this]() { emit topicCreateRequested(); });
}

// The "Deleted" section header: restore or permanently delete the selected
// deleted chats.
void Sidebar::populateDeletedHeaderMenu(QMenu &menu, const QTreeWidgetItem *item) {
    const QList<QUuid> targets = selectedDeletedSessionIds(item);
    QAction *restoreAction = menu.addAction(tr("Restore"));
    connect(restoreAction, &QAction::triggered, this,
            [this, targets]() { emit sessionRestoreRequested(targets); });
    QAction *permanentAction = menu.addAction(tr("Permanently delete"));
    connect(permanentAction, &QAction::triggered, this,
            [this, targets]() { emit sessionPermanentDeleteRequested(targets); });
}

// Section headers ("Pinned"/"Date") and time groups: delete the selected
// chats.
void Sidebar::populateSectionMenu(QMenu &menu) {
    QAction *deleteAction = menu.addAction(tr("Delete chat"));
    deleteAction->setEnabled(!deletableSelectedSessionIds().isEmpty());
    connect(deleteAction, &QAction::triggered, this,
            [this]() { emit sessionDeleteRequested(deletableSelectedSessionIds()); });
}

QTreeWidgetItem *Sidebar::makeChatItem(QTreeWidgetItem *parent, const ChatSession *session) {
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, session->title());
    item->setData(0, kSessionIdRole, session->id().toString());
    item->setData(0, kIsGroupRole, false);
    item->setData(0, kPinnedRole, session->pinned());
    item->setData(0, kTopicIdRole, session->topicId());
    item->setToolTip(0, session->title());
    return item;
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
            // Auto-scroll is disabled while selecting: scrollTo() would
            // expand collapsed ancestors of the item (e.g. a chat that just
            // moved into "Deleted" must not expand that section).
            const bool autoScroll = m_tree->hasAutoScroll();
            m_tree->setAutoScroll(false);
            m_tree->setCurrentItem(item);
            m_tree->setAutoScroll(autoScroll);
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
