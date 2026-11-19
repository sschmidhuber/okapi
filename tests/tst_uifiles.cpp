#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSet>
#include <QXmlStreamReader>
#include <QtTest>

// Guards the Qt Designer round-trip: Qt Designer's form engine cannot
// resolve namespaced class names (e.g. "okapi::Sidebar") and drops the
// promoted widgets plus the <customwidgets> block when saving, after which
// the generated ui_*.h no longer has the widget members and the app does
// not compile. Every non-Qt widget class used in a .ui file must therefore
// be a plain class name declared in <customwidgets>.
class TestUiFiles : public QObject {
    Q_OBJECT

private slots:
    void uiFilesUseDesignerCompatibleClassNames();
};

void TestUiFiles::uiFilesUseDesignerCompatibleClassNames() {
    const QDir dir(QStringLiteral(OKAPI_SOURCE_DIR) + QStringLiteral("/src/ui"));
    const QStringList files = dir.entryList({QStringLiteral("*.ui")}, QDir::Files);
    QVERIFY2(!files.isEmpty(), "no .ui files found under src/ui");

    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(name));

        QSet<QString> declaredCustomWidgets;
        QList<QString> widgetClasses;
        QXmlStreamReader xml(&file);
        bool inCustomWidgets = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == QLatin1String("customwidgets"))
                    inCustomWidgets = true;
                else if (xml.name() == QLatin1String("widget")) {
                    const QString cls = xml.attributes().value(QLatin1String("class")).toString();
                    if (!cls.isEmpty())
                        widgetClasses.append(cls);
                } else if (xml.name() == QLatin1String("class") && inCustomWidgets)
                    declaredCustomWidgets.insert(xml.readElementText());
            } else if (xml.isEndElement()) {
                if (xml.name() == QLatin1String("customwidgets"))
                    inCustomWidgets = false;
            }
            QVERIFY2(!xml.hasError(), qPrintable(name + ": " + xml.errorString()));
        }

        for (const QString &cls : widgetClasses)
            QVERIFY2(!cls.contains(QLatin1String("::")),
                     qPrintable(name + ": namespaced widget class " + cls));
        for (const QString &cls : declaredCustomWidgets)
            QVERIFY2(!cls.contains(QLatin1String("::")),
                     qPrintable(name + ": namespaced customwidget class " + cls));
        for (const QString &cls : widgetClasses) {
            if (cls.startsWith(QLatin1Char('Q')))
                continue;
            QVERIFY2(declaredCustomWidgets.contains(cls),
                     qPrintable(name + ": widget class " + cls +
                                " has no <customwidget> entry and would be dropped by Designer"));
        }
    }
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestUiFiles test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_uifiles.moc"
