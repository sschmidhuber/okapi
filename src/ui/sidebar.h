#pragma once

#include <QList>
#include <QSet>
#include <QUuid>
#include <QWidget>

class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace okapi {

class ChatSession;

// The chat history panel: a tree with three top-level sections — "Pinned"
// (chats the user pinned, newest activity first), "Date" (all other chats,
// grouped by their last activity time: Today, Yesterday, weekday, date,
// month, year) and "Deleted" (soft-deleted chats, newest deletion first).
// The toolbar actions (new chat, settings) live in the main window's
// toolbar. Chats can be multi-selected (Ctrl+click adds to the selection,
// Shift+click selects a range, selecting a group header includes all its
// chats); the selected chats can be deleted via the context menu's
// "Delete chat" or the Delete key. Chats that are running or unread are
// never deleted. The context menu also offers "Pin chat"/"Unpin chat" for
// normal chats and "Restore"/"Permanently delete" for deleted ones.
class Sidebar : public QWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    // Rebuilds the tree from the given sessions (pinned/deleted state is
    // read from the ChatSession objects).
    void setSessions(const QVector<ChatSession *> &sessions);
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

signals:
    void sessionSelected(const QUuid &id);
    // Emitted with the deletable selected session ids (context menu or Delete
    // key); the main window asks for confirmation when there are several and
    // soft-deletes them (they land in the "Deleted" section).
    void sessionDeleteRequested(const QList<QUuid> &ids);
    // Pin/unpin of one chat (context menu).
    void sessionPinToggled(const QUuid &id, bool pin);
    // Restore / permanent deletion of chats in the "Deleted" section.
    void sessionRestoreRequested(const QList<QUuid> &ids);
    void sessionPermanentDeleteRequested(const QList<QUuid> &ids);

private:
    void applyIndicators();
    void advanceSpinner();
    void requestDeleteSelected();
    [[nodiscard]] QIcon spinnerIcon() const;
    [[nodiscard]] QIcon unreadDotIcon() const;
    [[nodiscard]] static QIcon draftIcon();
    // The section ("pinned"/"date"/"deleted") the item belongs to, found by
    // walking up the parents; empty for the "time" sub-groups of "Date".
    [[nodiscard]] static QString sectionOf(const QTreeWidgetItem *item);
    [[nodiscard]] static QTreeWidgetItem *makeChatItem(QTreeWidgetItem *parent,
                                                       const ChatSession *session);
    void collectChatIds(const QTreeWidgetItem *item, QSet<QUuid> &ids) const;
    [[nodiscard]] QList<QUuid> selectedDeletedSessionIds(const QTreeWidgetItem *clicked) const;
    // Context menu at the given viewport position (pin/unpin, delete, or
    // restore/permanently delete for the "Deleted" section).
    void showContextMenu(const QPoint &pos);

    QTreeWidget *m_tree;
    QTimer *m_spinnerTimer = nullptr;
    int m_spinnerAngle = 0;
    QSet<QUuid> m_running;
    QSet<QUuid> m_unread;
    QSet<QUuid> m_drafts;
};

} // namespace okapi
