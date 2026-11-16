#pragma once

#include "storage/chatdatabase.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <QWidget>

class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QMenu;

namespace okapi {

class ChatSession;

// The chat history panel: a tree with four top-level sections — "Pinned"
// (chats the user pinned, newest activity first), "Topic" (user-created
// topics, each listing its chats), "Date" (all other chats, grouped by
// their last activity time: Today, Yesterday, weekday, date, month, year)
// and "Deleted" (soft-deleted chats, newest deletion first; never expanded
// automatically). The toolbar actions (new chat, settings) live in the
// main window's toolbar. Chats can be multi-selected (Ctrl+click adds to
// the selection, Shift+click selects a range, selecting a group header
// includes all its chats); the selected chats can be deleted via the
// context menu's "Delete chat" or the Delete key. Chats that are running
// or unread are never deleted.
//
// Context menus: chats offer "Pin chat"/"Unpin chat", "Rename", "Move to
// topic" (submenu listing the available topics), "Delete chat" — chats
// inside a topic additionally offer "Remove from topic", deleted chats
// offer "Restore"/"Permanently delete" (plus "Rename"/"Move to topic").
// The "Topic" section header offers "New topic"; a topic offers "New
// topic"/"Rename topic"/"Delete topic". Chats can also be dragged between
// the top-level sections and topics (pinned/date/deleted/topic targets).
class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    // Rebuilds the tree from the given sessions (pinned/deleted/topic state
    // is read from the ChatSession objects).
    void setSessions(const QVector<ChatSession *> &sessions);
    // The topics listed under the "Topic" section; rebuilds the tree.
    void setTopics(const QVector<StoredTopic> &topics);
    void selectSession(const QUuid &id);
    [[nodiscard]] QUuid currentSessionId() const;

    // Session ids of the current selection; a selected group header
    // contributes all of its descendant chats.
    [[nodiscard]] QList<QUuid> selectedSessionIds() const;
    // The selected session ids that may be deleted (neither running nor
    // unread nor already deleted).
    [[nodiscard]] QList<QUuid> deletableSelectedSessionIds() const;

    // Per-session state badges: an animated spinner for chats whose model
    // is still working, a dot for chats with messages the user has not
    // seen yet, an edit-entry icon for chats with an unsent composer
    // draft. Priority when several apply: running > unread > draft.
    // Applied on the next (or the current) tree build.
    void setIndicators(const QSet<QUuid> &running, const QSet<QUuid> &unread,
                       const QSet<QUuid> &drafts);

    // Handles drops on the tree viewport: translates them into
    // sessionsDropped(ids, section, topicId). Public (it overrides the
    // public QObject::eventFilter) so tests can feed it synthetic drops.
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void sessionSelected(const QUuid &id);
    // Emitted with the deletable selected session ids (context menu or Delete
    // key); the main window soft-deletes them (they land in the "Deleted"
    // section) without asking for confirmation.
    void sessionDeleteRequested(const QList<QUuid> &ids);
    // Pin/unpin of one chat (context menu).
    void sessionPinToggled(const QUuid &id, bool pin);
    // Restore / permanent deletion of chats in the "Deleted" section.
    void sessionRestoreRequested(const QList<QUuid> &ids);
    void sessionPermanentDeleteRequested(const QList<QUuid> &ids);
    // Rename of one chat (context menu "Rename"); the main window asks for
    // the new title.
    void sessionRenameRequested(const QUuid &id);
    // Move one chat into a topic ("Move to topic" submenu entry).
    void sessionMoveToTopicRequested(const QUuid &id, const QString &topicId);
    // Remove one chat from its topic ("Remove from topic").
    void sessionRemoveFromTopicRequested(const QUuid &id);
    // Drag&drop of chats onto a section or topic: section is "pinned",
    // "date", "deleted" or "topics"; topicId is set when dropped onto a
    // specific topic.
    void sessionsDropped(const QList<QUuid> &ids, const QString &section, const QString &topicId);
    // Topic management (context menu on "Topic" / a topic).
    void topicCreateRequested();
    void topicRenameRequested(const QString &topicId);
    void topicDeleteRequested(const QString &topicId);

private:
    void rebuildTree();
    // Tree-build helpers (kept out of rebuildTree() to stay readable).
    [[nodiscard]] QStringList collectExpandedKeys() const;
    void partitionSessions(QVector<ChatSession *> &pinned, QVector<ChatSession *> &deleted,
                           QVector<ChatSession *> &dated,
                           QHash<QString, QVector<ChatSession *>> &byTopic) const;
    [[nodiscard]] QTreeWidgetItem *makeSection(const QString &key, const QString &label,
                                               const QString &iconPath, bool expanded);
    static void buildDateSection(QTreeWidgetItem *section, const QVector<ChatSession *> &dated,
                                 bool firstBuild, const QStringList &expandedKeys);
    void buildTopicsSection(QTreeWidgetItem *section,
                            const QHash<QString, QVector<ChatSession *>> &byTopic, bool firstBuild,
                            const QStringList &expandedKeys);
    void applyIndicators();
    void advanceSpinner();
    void requestDeleteSelected();
    [[nodiscard]] QIcon spinnerIcon() const;
    [[nodiscard]] QIcon unreadDotIcon() const;
    [[nodiscard]] static QIcon draftIcon();
    [[nodiscard]] static QTreeWidgetItem *makeChatItem(QTreeWidgetItem *parent,
                                                       const ChatSession *session);
    void collectChatIds(const QTreeWidgetItem *item, QSet<QUuid> &ids) const;
    [[nodiscard]] QList<QUuid> selectedDeletedSessionIds(const QTreeWidgetItem *clicked) const;
    // Context menu at the given viewport position; the populate* helpers fill
    // the menu and connect each action to its signal directly.
    void showContextMenu(const QPoint &pos);
    void populateChatMenu(QMenu &menu, const QTreeWidgetItem *item, const QString &section);
    void populateTopicMenu(QMenu &menu, const QString &topicId);
    void populateTopicsHeaderMenu(QMenu &menu);
    void populateDeletedHeaderMenu(QMenu &menu, const QTreeWidgetItem *item);
    void populateSectionMenu(QMenu &menu);
    // Adds the "Move to topic" submenu (one entry per available topic).
    void addMoveToTopicSubmenu(QMenu *menu, const QUuid &chatId, const QString &currentTopicId);

    QTreeWidget *m_tree;
    QTimer *m_spinnerTimer = nullptr;
    int m_spinnerAngle = 0;
    QVector<ChatSession *> m_sessions;
    QVector<StoredTopic> m_topics;
    QSet<QUuid> m_running;
    QSet<QUuid> m_unread;
    QSet<QUuid> m_drafts;
};

} // namespace okapi
