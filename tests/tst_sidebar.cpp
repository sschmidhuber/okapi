#include "core/chatsession.h"
#include "core/sessiongrouper.h"
#include "ui/sidebar.h"

#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>
#include <QtTest>

#include <memory>

using namespace okapi;

namespace {

std::unique_ptr<ChatSession> makeSession(const QString &title, const QDateTime &activity) {
    auto session = std::make_unique<ChatSession>(title, QStringLiteral("backend"));
    session->setLastActivityAt(activity);
    return session;
}

QTreeWidgetItem *groupItem(QTreeWidget *tree, const QDateTime &activity) {
    const QString label = SessionGrouper::groupLabel(
        SessionGrouper::groupKey(activity, QDateTime::currentDateTime()));
    // Time groups are nested below the "Date" section, so search recursively.
    const auto items = tree->findItems(label, Qt::MatchExactly | Qt::MatchRecursive, 0);
    return items.isEmpty() ? nullptr : items.first();
}

// Top-level section ("Pinned"/"Date"/"Deleted") by its object key stored in
// the session-id role.
QTreeWidgetItem *sectionItem(QTreeWidget *tree, const QString &key) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *item = tree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == key)
            return item;
    }
    return nullptr;
}

int countChatItems(QTreeWidget *tree) {
    int count = 0;
    const auto items =
        tree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive, 0);
    for (const auto *item : items) {
        if (!item->data(0, Qt::UserRole + 1).toBool())
            ++count;
    }
    return count;
}

} // namespace

class TestSidebar : public QObject {
    Q_OBJECT

private slots:
    void firstBuildExpandsOnlyToday();
    void rebuildPreservesExpandState();
    void collapsingAllGroupsStaysCollapsed();
    void currentSessionIdIsNullForGroupItems();
    void emptySessionsClearTree();
    void sidebarHasNoToolbarButtons();
    void indicatorsBadgeRunningAndUnreadChats();
    void indicatorsBadgeDraftChats();
    void pinnedAndDeletedSections();
    void deletableSelectionExcludesDeletedChats();
    void multiSelectionViaCtrlAndShiftClick();
    void selectedGroupExpandsToItsChats();
    void deletableSelectionExcludesRunningAndUnread();
    void deleteKeyEmitsDeletableSelection();
    void selectSessionClearsMultiSelection();
};

void TestSidebar::firstBuildExpandsOnlyToday() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    auto yesterday = makeSession(QStringLiteral("yesterday chat"), now.addDays(-1));
    auto lastWeek = makeSession(QStringLiteral("old chat"), now.addDays(-8));
    const QVector<ChatSession *> sessions{today.get(), yesterday.get(), lastWeek.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    // The three top-level sections are always present, in this order.
    QCOMPARE(tree->topLevelItemCount(), 3);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Pinned"));
    QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Date"));
    QCOMPARE(tree->topLevelItem(2)->text(0), QStringLiteral("Deleted"));
    auto *todayGroup = groupItem(tree, now);
    auto *yesterdayGroup = groupItem(tree, now.addDays(-1));
    auto *oldGroup = groupItem(tree, now.addDays(-8));
    QVERIFY(todayGroup);
    QVERIFY(yesterdayGroup);
    QVERIFY(oldGroup);
    QVERIFY(todayGroup->isExpanded());
    QVERIFY(!yesterdayGroup->isExpanded());
    QVERIFY(!oldGroup->isExpanded());
    QCOMPARE(todayGroup->childCount(), 1);
    QCOMPARE(yesterdayGroup->childCount(), 1);
    QCOMPARE(oldGroup->childCount(), 1);
    QCOMPARE(todayGroup->child(0)->text(0), QStringLiteral("today chat"));
}

void TestSidebar::rebuildPreservesExpandState() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    auto yesterday = makeSession(QStringLiteral("yesterday chat"), now.addDays(-1));
    const QVector<ChatSession *> sessions{today.get(), yesterday.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *todayGroup = groupItem(tree, now);
    auto *yesterdayGroup = groupItem(tree, now.addDays(-1));
    QVERIFY(todayGroup && yesterdayGroup);
    QVERIFY(todayGroup->isExpanded());
    QVERIFY(!yesterdayGroup->isExpanded());

    // The user expands Yesterday; a rebuild must keep that state.
    yesterdayGroup->setExpanded(true);
    sidebar.setSessions(sessions);

    todayGroup = groupItem(tree, now);
    yesterdayGroup = groupItem(tree, now.addDays(-1));
    QVERIFY(todayGroup && yesterdayGroup);
    QVERIFY(todayGroup->isExpanded());
    QVERIFY(yesterdayGroup->isExpanded());
}

void TestSidebar::collapsingAllGroupsStaysCollapsed() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    const QVector<ChatSession *> sessions{today.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *todayGroup = groupItem(tree, now);
    QVERIFY(todayGroup);
    QVERIFY(todayGroup->isExpanded());

    // Collapsing the only group must not re-expand it on the next rebuild.
    todayGroup->setExpanded(false);
    sidebar.setSessions(sessions);

    todayGroup = groupItem(tree, now);
    QVERIFY(todayGroup);
    QVERIFY(!todayGroup->isExpanded());
}

void TestSidebar::currentSessionIdIsNullForGroupItems() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);
    const QVector<ChatSession *> sessions{today.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *group = groupItem(tree, now);
    QVERIFY(group);
    tree->setCurrentItem(group);
    QVERIFY(sidebar.currentSessionId().isNull());

    tree->setCurrentItem(group->child(0));
    QCOMPARE(sidebar.currentSessionId(), today->id());
}

void TestSidebar::sidebarHasNoToolbarButtons() {
    // New chat / settings live in the main window toolbar now; the sidebar
    // only contains the history tree.
    Sidebar sidebar;
    QVERIFY(sidebar.findChildren<QPushButton *>().isEmpty());
    QVERIFY(sidebar.findChild<QTreeWidget *>() != nullptr);
}

void TestSidebar::emptySessionsClearTree() {
    const QDateTime now = QDateTime::currentDateTime();
    auto today = makeSession(QStringLiteral("today chat"), now);

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions({today.get()});
    // The sections stay, the chat is listed under "Date".
    QCOMPARE(tree->topLevelItemCount(), 3);
    QCOMPARE(countChatItems(tree), 1);

    sidebar.setSessions({});
    QCOMPARE(tree->topLevelItemCount(), 3);
    QCOMPARE(countChatItems(tree), 0);
}

// The running/unread badges: a running chat shows a spinner icon, an
// unread chat a dot; a chat that is neither has no icon. Rebuilding the
// tree (setSessions) must keep the badges.
void TestSidebar::indicatorsBadgeRunningAndUnreadChats() {
    const QDateTime now = QDateTime::currentDateTime();
    auto running = makeSession(QStringLiteral("running chat"), now);
    auto unread = makeSession(QStringLiteral("unread chat"), now);
    auto plain = makeSession(QStringLiteral("plain chat"), now);
    const QVector<ChatSession *> sessions{running.get(), unread.get(), plain.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    sidebar.setIndicators({running->id()}, {unread->id()}, {});

    auto *group = groupItem(tree, now);
    QVERIFY(group);
    QCOMPARE(group->childCount(), 3);
    bool sawRunning = false;
    bool sawUnread = false;
    bool sawPlain = false;
    for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        const QUuid id(item->data(0, Qt::UserRole).toString());
        if (id == running->id()) {
            QVERIFY(!item->icon(0).isNull());
            QVERIFY(item->toolTip(0).contains(QStringLiteral("generating")));
            sawRunning = true;
        } else if (id == unread->id()) {
            QVERIFY(!item->icon(0).isNull());
            QVERIFY(item->toolTip(0).contains(QStringLiteral("unread")));
            sawUnread = true;
        } else {
            QVERIFY(item->icon(0).isNull());
            QCOMPARE(item->toolTip(0), item->text(0));
            sawPlain = true;
        }
    }
    QVERIFY(sawRunning && sawUnread && sawPlain);

    // A rebuild must keep the badges.
    sidebar.setSessions(sessions);
    group = groupItem(tree, now);
    QVERIFY(group);
    bool runningKept = false;
    bool unreadKept = false;
    for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        const QUuid id(item->data(0, Qt::UserRole).toString());
        if (id == running->id() && !item->icon(0).isNull())
            runningKept = true;
        if (id == unread->id() && !item->icon(0).isNull())
            unreadKept = true;
    }
    QVERIFY(runningKept);
    QVERIFY(unreadKept);

    // Clearing the indicators removes the badges again.
    sidebar.setIndicators({}, {}, {});
    group = groupItem(tree, now);
    QVERIFY(group);
    for (int i = 0; i < group->childCount(); ++i)
        QVERIFY(group->child(i)->icon(0).isNull());
}

// The draft badge: a chat with an unsent composer draft shows the
// edit-entry icon and an "unsent draft" tooltip; running/unread win over
// the draft icon when several states apply at once.
void TestSidebar::indicatorsBadgeDraftChats() {
    const QDateTime now = QDateTime::currentDateTime();
    auto draft = makeSession(QStringLiteral("draft chat"), now);
    auto runningDraft = makeSession(QStringLiteral("running draft chat"), now);
    auto plain = makeSession(QStringLiteral("plain chat"), now);
    const QVector<ChatSession *> sessions{draft.get(), runningDraft.get(), plain.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    sidebar.setIndicators({runningDraft->id()}, {}, {draft->id(), runningDraft->id()});

    auto *group = groupItem(tree, now);
    QVERIFY(group);
    QCOMPARE(group->childCount(), 3);
    bool sawDraft = false;
    bool sawRunningDraft = false;
    bool sawPlain = false;
    for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        const QUuid id(item->data(0, Qt::UserRole).toString());
        if (id == draft->id()) {
            QVERIFY(!item->icon(0).isNull());
            QVERIFY(item->toolTip(0).contains(QStringLiteral("unsent draft")));
            sawDraft = true;
        } else if (id == runningDraft->id()) {
            // Running has priority over the draft badge.
            QVERIFY(!item->icon(0).isNull());
            QVERIFY(item->toolTip(0).contains(QStringLiteral("generating")));
            QVERIFY(item->toolTip(0).contains(QStringLiteral("unsent draft")));
            sawRunningDraft = true;
        } else {
            QVERIFY(item->icon(0).isNull());
            QCOMPARE(item->toolTip(0), item->text(0));
            sawPlain = true;
        }
    }
    QVERIFY(sawDraft && sawRunningDraft && sawPlain);

    // Clearing the draft set removes the badge again.
    sidebar.setIndicators({}, {}, {});
    group = groupItem(tree, now);
    QVERIFY(group);
    for (int i = 0; i < group->childCount(); ++i)
        QVERIFY(group->child(i)->icon(0).isNull());
}

// The three top-level sections: pinned chats appear under "Pinned" (sorted
// by last activity, newest first) and NOT in the date groups; deleted chats
// appear under "Deleted" (sorted by deletion time, newest first) and not in
// the date groups either. Section headers carry their icons.
void TestSidebar::pinnedAndDeletedSections() {
    const QDateTime now = QDateTime::currentDateTime();
    auto pinnedOld = makeSession(QStringLiteral("pinned old"), now.addSecs(-7200));
    auto pinnedNew = makeSession(QStringLiteral("pinned new"), now);
    pinnedOld->setPinned(true);
    pinnedNew->setPinned(true);
    auto deletedOld = makeSession(QStringLiteral("deleted old"), now.addDays(-5));
    auto deletedNew = makeSession(QStringLiteral("deleted new"), now.addDays(-1));
    deletedOld->setDeletedAt(now.addDays(-10));
    deletedNew->setDeletedAt(now.addDays(-2));
    auto normal = makeSession(QStringLiteral("normal chat"), now);
    const QVector<ChatSession *> sessions{pinnedOld.get(), pinnedNew.get(), deletedOld.get(),
                                          deletedNew.get(), normal.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *pinnedSection = sectionItem(tree, QStringLiteral("pinned"));
    auto *dateSection = sectionItem(tree, QStringLiteral("date"));
    auto *deletedSection = sectionItem(tree, QStringLiteral("deleted"));
    QVERIFY(pinnedSection && dateSection && deletedSection);
    QVERIFY(!pinnedSection->icon(0).isNull());
    QVERIFY(!dateSection->icon(0).isNull());
    QVERIFY(!deletedSection->icon(0).isNull());

    // Pinned: newest activity first, and only pinned chats.
    QCOMPARE(pinnedSection->childCount(), 2);
    QCOMPARE(pinnedSection->child(0)->text(0), QStringLiteral("pinned new"));
    QCOMPARE(pinnedSection->child(1)->text(0), QStringLiteral("pinned old"));

    // Deleted: newest deletion first.
    QCOMPARE(deletedSection->childCount(), 2);
    QCOMPARE(deletedSection->child(0)->text(0), QStringLiteral("deleted new"));
    QCOMPARE(deletedSection->child(1)->text(0), QStringLiteral("deleted old"));

    // Only the normal chat is in the date-based groups.
    auto *todayGroup = groupItem(tree, now);
    QVERIFY(todayGroup);
    QCOMPARE(todayGroup->childCount(), 1);
    QCOMPARE(todayGroup->child(0)->text(0), QStringLiteral("normal chat"));
    QCOMPARE(countChatItems(tree), 5);
}

// Chats in the "Deleted" section are not offered for another (soft) delete.
void TestSidebar::deletableSelectionExcludesDeletedChats() {
    const QDateTime now = QDateTime::currentDateTime();
    auto deleted = makeSession(QStringLiteral("deleted chat"), now);
    deleted->setDeletedAt(now.addDays(-1));
    auto plain = makeSession(QStringLiteral("plain chat"), now);
    const QVector<ChatSession *> sessions{deleted.get(), plain.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    tree->selectAll();
    const QList<QUuid> deletable = sidebar.deletableSelectedSessionIds();
    QCOMPARE(deletable.size(), 1);
    QCOMPARE(deletable.first(), plain->id());
}

// Ctrl+click adds to the selection, Shift+click selects a range, and a
// group header can be added with Ctrl+click as well.
void TestSidebar::multiSelectionViaCtrlAndShiftClick() {
    const QDateTime now = QDateTime::currentDateTime();
    auto s0 = makeSession(QStringLiteral("chat 0"), now);
    auto s1 = makeSession(QStringLiteral("chat 1"), now);
    auto s2 = makeSession(QStringLiteral("chat 2"), now);
    const QVector<ChatSession *> sessions{s0.get(), s1.get(), s2.get()};

    Sidebar sidebar;
    sidebar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sidebar));
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    QCOMPARE(tree->selectionMode(), QAbstractItemView::ExtendedSelection);
    sidebar.setSessions(sessions);

    auto *group = groupItem(tree, now);
    QVERIFY(group);
    QCOMPARE(group->childCount(), 3);
    auto *i0 = group->child(0);
    auto *i1 = group->child(1);
    auto *i2 = group->child(2);

    // Plain click selects only the clicked chat.
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualItemRect(i0).center());
    QCOMPARE(tree->selectedItems().size(), 1);

    // Shift+click selects the whole range from the anchor to the clicked item.
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                      tree->visualItemRect(i2).center());
    QCOMPARE(tree->selectedItems().size(), 3);

    // Ctrl+click toggles a single item off again.
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      tree->visualItemRect(i1).center());
    QCOMPARE(tree->selectedItems().size(), 2);

    // Ctrl+click on the group header adds all of its chats to the selection.
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      tree->visualItemRect(group).center());
    QVERIFY(group->isSelected());
    QCOMPARE(sidebar.selectedSessionIds().size(), 3);
}

// Selecting a top-level group item yields all chats of that group.
void TestSidebar::selectedGroupExpandsToItsChats() {
    const QDateTime now = QDateTime::currentDateTime();
    auto t0 = makeSession(QStringLiteral("today 0"), now);
    auto t1 = makeSession(QStringLiteral("today 1"), now);
    auto y0 = makeSession(QStringLiteral("yesterday 0"), now.addDays(-1));
    const QVector<ChatSession *> sessions{t0.get(), t1.get(), y0.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    auto *todayGroup = groupItem(tree, now);
    QVERIFY(todayGroup);
    todayGroup->setSelected(true);

    const QList<QUuid> ids = sidebar.selectedSessionIds();
    QCOMPARE(ids.size(), 2);
    QVERIFY(ids.contains(t0->id()));
    QVERIFY(ids.contains(t1->id()));
    QVERIFY(!ids.contains(y0->id()));
}

// Only chats that are neither running nor unread may be deleted.
void TestSidebar::deletableSelectionExcludesRunningAndUnread() {
    const QDateTime now = QDateTime::currentDateTime();
    auto running = makeSession(QStringLiteral("running chat"), now);
    auto unread = makeSession(QStringLiteral("unread chat"), now);
    auto plain = makeSession(QStringLiteral("plain chat"), now);
    const QVector<ChatSession *> sessions{running.get(), unread.get(), plain.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);
    sidebar.setIndicators({running->id()}, {unread->id()}, {});

    tree->selectAll();
    QCOMPARE(sidebar.selectedSessionIds().size(), 3);
    const QList<QUuid> deletable = sidebar.deletableSelectedSessionIds();
    QCOMPARE(deletable.size(), 1);
    QCOMPARE(deletable.first(), plain->id());

    // Selecting only the running chat leaves nothing deletable.
    tree->clearSelection();
    auto *group = groupItem(tree, now);
    QVERIFY(group);
    for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        if (QUuid(item->data(0, Qt::UserRole).toString()) == running->id())
            item->setSelected(true);
    }
    QVERIFY(sidebar.deletableSelectedSessionIds().isEmpty());
}

// The Delete key emits the deletable selected chats (running/unread are
// filtered out).
void TestSidebar::deleteKeyEmitsDeletableSelection() {
    const QDateTime now = QDateTime::currentDateTime();
    auto running = makeSession(QStringLiteral("running chat"), now);
    auto plain0 = makeSession(QStringLiteral("plain 0"), now);
    auto plain1 = makeSession(QStringLiteral("plain 1"), now);
    const QVector<ChatSession *> sessions{running.get(), plain0.get(), plain1.get()};

    Sidebar sidebar;
    sidebar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sidebar));
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);
    sidebar.setIndicators({running->id()}, {}, {});

    tree->selectAll();
    tree->setFocus();
    QVERIFY(QApplication::focusWidget() == tree);

    QSignalSpy spy(&sidebar, &Sidebar::sessionDeleteRequested);
    QTest::keyClick(tree, Qt::Key_Delete);
    QCOMPARE(spy.count(), 1);
    const auto ids = spy.takeFirst().at(0).value<QList<QUuid>>();
    QCOMPARE(ids.size(), 2);
    QVERIFY(!ids.contains(running->id()));
    QVERIFY(ids.contains(plain0->id()));
    QVERIFY(ids.contains(plain1->id()));
}

// Regression: switching the chat programmatically (e.g. via the Ctrl+Tab
// chat-cycling shortcuts) must leave exactly one selected item, even after
// a multi-selection — with ExtendedSelection setCurrentItem() alone does
// not clear the previous selection.
void TestSidebar::selectSessionClearsMultiSelection() {
    const QDateTime now = QDateTime::currentDateTime();
    auto s0 = makeSession(QStringLiteral("chat 0"), now);
    auto s1 = makeSession(QStringLiteral("chat 1"), now);
    auto s2 = makeSession(QStringLiteral("chat 2"), now);
    const QVector<ChatSession *> sessions{s0.get(), s1.get(), s2.get()};

    Sidebar sidebar;
    auto *tree = sidebar.findChild<QTreeWidget *>();
    QVERIFY(tree);
    sidebar.setSessions(sessions);

    tree->selectAll();
    // 3 chats plus the section headers (Pinned, Date, Deleted) and the
    // time group below "Date".
    QCOMPARE(tree->selectedItems().size(), 7);

    sidebar.selectSession(s1->id());
    QCOMPARE(tree->selectedItems().size(), 1);
    QCOMPARE(sidebar.currentSessionId(), s1->id());
}

// Sidebar is a QWidget, so the test needs a QApplication; force the
// offscreen platform because the CI test runner has no display server.
int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestSidebar test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_sidebar.moc"
