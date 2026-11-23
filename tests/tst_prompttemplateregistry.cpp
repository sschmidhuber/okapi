#include "core/prompttemplateregistry.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace okapi;

class TestPromptTemplateRegistry : public QObject {
    Q_OBJECT

private slots:
    void addUpdateRemoveFind();
    void changedSignalEmitted();
    void saveLoadRoundtripWithAttachments();
    void recentUsageIsTrackedCappedAndPersisted();
    void recordUsedIgnoresUnknownIds();
    void removeDropsRecentEntryAndLoadDropsStaleIds();
};

void TestPromptTemplateRegistry::addUpdateRemoveFind() {
    PromptTemplateRegistry registry;
    QVERIFY(registry.templates().isEmpty());
    QVERIFY(!registry.find(QUuid::createUuid()));

    PromptTemplate t;
    t.name = QStringLiteral("Summarize");
    t.text = QStringLiteral("Summarize the following text:");
    auto &stored = registry.add(t);
    QCOMPARE(registry.templates().size(), 1);
    QVERIFY(!stored.id.isNull());
    QCOMPARE(registry.find(stored.id)->name, QStringLiteral("Summarize"));

    PromptTemplate updated = *registry.find(stored.id);
    updated.text = QStringLiteral("Summarize briefly:");
    registry.update(updated);
    QCOMPARE(registry.templates().size(), 1);
    QCOMPARE(registry.find(stored.id)->text, QStringLiteral("Summarize briefly:"));

    registry.remove(stored.id);
    QVERIFY(registry.templates().isEmpty());
    QVERIFY(!registry.find(stored.id));
}

void TestPromptTemplateRegistry::changedSignalEmitted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PromptTemplateRegistry registry;
    {
        QSettings file(dir.filePath(QStringLiteral("okapi.conf")), QSettings::IniFormat);
        registry.load(file);
    }
    QSignalSpy spy(&registry, &PromptTemplateRegistry::changed);
    PromptTemplate t;
    t.name = QStringLiteral("t");
    t.text = QStringLiteral("x");
    auto &stored = registry.add(t);
    QCOMPARE(spy.count(), 1);
    registry.update(*registry.find(stored.id));
    QCOMPARE(spy.count(), 2);
    registry.remove(stored.id);
    QCOMPARE(spy.count(), 3);
}

void TestPromptTemplateRegistry::saveLoadRoundtripWithAttachments() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("okapi.conf"));

    PromptTemplateRegistry registry;
    {
        QSettings file(path, QSettings::IniFormat);
        registry.load(file);
    }
    PromptTemplate t;
    t.name = QStringLiteral("With files");
    t.text = QStringLiteral("Analyze:");
    ImageAttachment image;
    image.fileName = QStringLiteral("chart.png");
    image.mimeType = QStringLiteral("image/png");
    image.sourcePath = QStringLiteral("/tmp/chart.png");
    image.width = 800;
    image.height = 600;
    image.data = QByteArray("fakepng");
    t.images.append(image);
    FileAttachment file;
    file.fileName = QStringLiteral("notes.txt");
    file.sourcePath = QStringLiteral("/tmp/notes.txt");
    file.thumbnail = QByteArray("thumb");
    file.extractedText = QStringLiteral("some text");
    t.files.append(file);
    const QUuid id = registry.add(t).id;

    PromptTemplateRegistry loaded;
    {
        QSettings file(path, QSettings::IniFormat);
        loaded.load(file);
    }
    QCOMPARE(loaded.templates().size(), 1);
    const PromptTemplate *restored = loaded.find(id);
    QVERIFY(restored);
    QCOMPARE(restored->name, QStringLiteral("With files"));
    QCOMPARE(restored->text, QStringLiteral("Analyze:"));
    QCOMPARE(restored->images.size(), 1);
    QCOMPARE(restored->images.first().fileName, QStringLiteral("chart.png"));
    QCOMPARE(restored->images.first().data, QByteArray("fakepng"));
    QCOMPARE(restored->images.first().width, 800);
    QCOMPARE(restored->files.size(), 1);
    QCOMPARE(restored->files.first().fileName, QStringLiteral("notes.txt"));
    QCOMPARE(restored->files.first().extractedText, QStringLiteral("some text"));
    QCOMPARE(restored->files.first().thumbnail, QByteArray("thumb"));
}

// recordUsed() keeps a most-recent-first list capped at
// kMaxRecentTemplates, re-using an id moves it to the front, and the list
// survives a save/load roundtrip.
void TestPromptTemplateRegistry::recentUsageIsTrackedCappedAndPersisted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("okapi.conf"));

    PromptTemplateRegistry registry;
    {
        QSettings file(path, QSettings::IniFormat);
        registry.load(file);
    }
    QVector<QUuid> ids;
    for (int i = 0; i < 25; ++i) {
        PromptTemplate t;
        t.name = QStringLiteral("T%1").arg(i);
        t.text = QStringLiteral("text %1").arg(i);
        ids.append(registry.add(t).id);
    }
    // Recorded in order 0..24: most recent first, so id 24 leads; the cap
    // drops the oldest entries (ids 0..4 are gone, id 5 is the oldest kept).
    for (const QUuid &id : ids)
        registry.recordUsed(id);
    QCOMPARE(registry.recentIds().size(), PromptTemplateRegistry::kMaxRecentTemplates);
    QCOMPARE(registry.recentIds().first(), ids.constLast());
    QCOMPARE(registry.recentIds().constLast(), ids.at(5));
    QVERIFY(!registry.recentIds().contains(ids.at(4)));

    // Re-using an id moves it to the front without duplicating it.
    registry.recordUsed(ids.at(10));
    QCOMPARE(registry.recentIds().size(), PromptTemplateRegistry::kMaxRecentTemplates);
    QCOMPARE(registry.recentIds().first(), ids.at(10));
    int occurrences = 0;
    for (const QUuid &id : registry.recentIds()) {
        if (id == ids.at(10))
            ++occurrences;
    }
    QCOMPARE(occurrences, 1);

    // The list is persisted: a fresh registry loading the same file sees
    // the same ids in the same order.
    PromptTemplateRegistry loaded;
    {
        QSettings file(path, QSettings::IniFormat);
        loaded.load(file);
    }
    QCOMPARE(loaded.recentIds().size(), registry.recentIds().size());
    QVERIFY(loaded.recentIds() == registry.recentIds());
}

void TestPromptTemplateRegistry::recordUsedIgnoresUnknownIds() {
    PromptTemplateRegistry registry;
    QSignalSpy spy(&registry, &PromptTemplateRegistry::changed);
    registry.recordUsed(QUuid::createUuid()); // unknown id: no-op
    registry.recordUsed(QUuid());             // null id: no-op
    QVERIFY(registry.recentIds().isEmpty());
    QCOMPARE(spy.count(), 0);
}

// Removing a template also drops it from the recent list (and persists
// that); on load, recent ids whose template no longer exists are dropped.
void TestPromptTemplateRegistry::removeDropsRecentEntryAndLoadDropsStaleIds() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("okapi.conf"));

    PromptTemplateRegistry registry;
    {
        QSettings file(path, QSettings::IniFormat);
        registry.load(file);
    }
    PromptTemplate a;
    a.name = QStringLiteral("Alpha");
    a.text = QStringLiteral("a");
    const QUuid idA = registry.add(a).id;
    PromptTemplate b;
    b.name = QStringLiteral("Beta");
    b.text = QStringLiteral("b");
    const QUuid idB = registry.add(b).id;
    registry.recordUsed(idA);
    registry.recordUsed(idB);
    QCOMPARE(registry.recentIds().first(), idB);

    registry.remove(idB);
    QCOMPARE(registry.recentIds().size(), 1);
    QCOMPARE(registry.recentIds().first(), idA);

    // Persisted removal: reload sees only idA.
    PromptTemplateRegistry loaded;
    {
        QSettings file(path, QSettings::IniFormat);
        loaded.load(file);
    }
    QCOMPARE(loaded.recentIds().size(), 1);
    QCOMPARE(loaded.recentIds().first(), idA);

    // A stale recent id (template gone) in the settings file is dropped.
    {
        QSettings file(path, QSettings::IniFormat);
        const QString stale = QUuid::createUuid().toString(QUuid::WithoutBraces);
        file.setValue(
            QStringLiteral("promptTemplatesRecent"),
            QStringLiteral("[\"%1\",\"%2\"]").arg(stale, idA.toString(QUuid::WithoutBraces)));
    }
    PromptTemplateRegistry loaded2;
    {
        QSettings file(path, QSettings::IniFormat);
        loaded2.load(file);
    }
    QCOMPARE(loaded2.recentIds().size(), 1);
    QCOMPARE(loaded2.recentIds().first(), idA);
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    TestPromptTemplateRegistry test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_prompttemplateregistry.moc"
