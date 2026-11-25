#include "core/backendregistry.h"
#include "core/mcpmanager.h"
#include "core/mcpregistry.h"
#include "core/prompttemplateregistry.h"
#include "core/shortcutregistry.h"
#include "core/sttregistry.h"
#include "core/systemprompt.h"
#include "core/ttsregistry.h"
#include "core/uisettings.h"
#include "ui/mcppage.h"
#include "ui/settingsdialog.h"
#include "ui/systempromptpage.h"
#include "ui/theme.h"
#include "ui/userinterfacepage.h"

#include <QApplication>
#include <QComboBox>
#include <QEventLoop>
#include <QFile>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtTest>

#include <memory>

using namespace okapi;
namespace {

QListWidget *categoriesList(const SettingsDialog &dialog) {
    return dialog.findChild<QListWidget *>();
}

QStackedWidget *stackOf(const SettingsDialog &dialog) {
    return dialog.findChild<QStackedWidget *>();
}

QTabWidget *tabsOf(const SettingsDialog &dialog, int categoryRow) {
    auto *stack = stackOf(dialog);
    return stack ? qobject_cast<QTabWidget *>(stack->widget(categoryRow)) : nullptr;
}

// Evaluates expr in the web page and returns the result as a string.
QString evalJs(QWebEnginePage *page, const QString &expr) {
    QString result;
    QEventLoop loop;
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    page->runJavaScript(expr, [&result, &loop](const QVariant &v) {
        result = v.toString();
        loop.quit();
    });
    loop.exec();
    return result;
}

// Polls expr until its result contains needle (the preview re-renders
// asynchronously after each keystroke).
bool waitForJs(QWebEnginePage *page, const QString &expr, const QString &needle) {
    for (int i = 0; i < 100; ++i) {
        if (evalJs(page, expr).contains(needle))
            return true;
        QTest::qWait(100);
    }
    return false;
}

} // namespace

class TestSettingsDialog : public QObject {
    Q_OBJECT

private slots:
    void categoriesAreTopLevelItems();
    void pagesAreTabsOfTheirCategory();
    void shortcutsPageListsAllActions();
    void selectPageSelectsCategoryAndTab();
    void mcpHttpAuthModeShowsOnlyRelevantField();
    void mcpTransportSwitchHidesOtherGroupIncludingLabels();
    void themeComboAppliesImmediately();
    void themedIconFollowsPaletteInDarkMode();
    void sidebarKeepsMaxWidthWhenDialogGrows();
    void systemPromptPagePreviewsMarkdownLive();
    void systemPromptPageFillsTabArea();

private:
    SettingsDialog *makeDialog();

    // The dialog builds its pages lazily (on first tab selection), so the
    // registries it points to must outlive the dialog — like in the app,
    // where MainWindow owns them. Members, not makeDialog() locals.
    BackendRegistry m_backends;
    McpRegistry m_mcpRegistry;
    McpManager m_mcpManager{&m_mcpRegistry};
    SttRegistry m_sttRegistry;
    TtsRegistry m_ttsRegistry;
    SystemPrompt m_systemPrompt;
    ShortcutRegistry m_shortcutRegistry;
    UiSettings m_uiSettings;
    PromptTemplateRegistry m_promptTemplates;
};

SettingsDialog *TestSettingsDialog::makeDialog() {
    return new SettingsDialog(&m_backends, &m_mcpRegistry, &m_mcpManager, &m_sttRegistry,
                              &m_ttsRegistry, &m_systemPrompt, &m_shortcutRegistry, &m_uiSettings,
                              &m_promptTemplates);
}

void TestSettingsDialog::categoriesAreTopLevelItems() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *list = categoriesList(*dialog);
    QVERIFY(list);
    QCOMPARE(list->count(), 4);
    QCOMPARE(list->item(0)->text(), QStringLiteral("General"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("Models"));
    QCOMPARE(list->item(2)->text(), QStringLiteral("Tools"));
    QCOMPARE(list->item(3)->text(), QStringLiteral("Shortcuts"));

    // Each category carries its themed icon with the bundled SVG fallback,
    // so it renders even without an icon theme providing the name.
    for (int i = 0; i < list->count(); ++i)
        QVERIFY(!list->item(i)->icon().isNull());
}

void TestSettingsDialog::pagesAreTabsOfTheirCategory() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *generalTabs = tabsOf(*dialog, 0);
    auto *modelsTabs = tabsOf(*dialog, 1);
    auto *toolsTabs = tabsOf(*dialog, 2);
    auto *shortcutsTabs = tabsOf(*dialog, 3);
    QVERIFY(generalTabs);
    QVERIFY(modelsTabs);
    QVERIFY(toolsTabs);
    QVERIFY(shortcutsTabs);

    QCOMPARE(generalTabs->count(), 3);
    QCOMPARE(generalTabs->tabText(0), QStringLiteral("System Prompt"));
    QCOMPARE(generalTabs->tabText(1), QStringLiteral("Prompt Templates"));
    QCOMPARE(generalTabs->tabText(2), QStringLiteral("User Interface"));

    QCOMPARE(modelsTabs->count(), 3);
    QCOMPARE(modelsTabs->tabText(0), QStringLiteral("LLM"));
    QCOMPARE(modelsTabs->tabText(1), QStringLiteral("STT"));
    QCOMPARE(modelsTabs->tabText(2), QStringLiteral("TTS"));

    QCOMPARE(toolsTabs->count(), 1);
    QCOMPARE(toolsTabs->tabText(0), QStringLiteral("MCP"));

    QCOMPARE(shortcutsTabs->count(), 1);
    QCOMPARE(shortcutsTabs->tabText(0), QStringLiteral("Shortcuts"));
}

void TestSettingsDialog::shortcutsPageListsAllActions() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    // Scope the lookup to the shortcuts tab: other pages (e.g. MCP) have
    // tables of their own.
    auto *shortcutsTabs = tabsOf(*dialog, 3);
    QVERIFY(shortcutsTabs);
    auto *table = shortcutsTabs->widget(0)->findChild<QTableWidget *>();
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Action"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Shortcut"));
    QCOMPARE(table->rowCount(), static_cast<int>(ShortcutRegistry::actions().size()));
    // The first row shows the first action with its default shortcut.
    const auto first = ShortcutRegistry::actions().constFirst();
    QCOMPARE(table->item(0, 0)->text(), first.description);
    QCOMPARE(table->item(0, 0)->data(Qt::UserRole).toString(), first.id);
    QCOMPARE(table->item(0, 1)->text(), first.defaultSequence.toString(QKeySequence::NativeText));
}

void TestSettingsDialog::selectPageSelectsCategoryAndTab() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    auto *list = categoriesList(*dialog);
    auto *generalTabs = tabsOf(*dialog, 0);
    auto *modelsTabs = tabsOf(*dialog, 1);
    auto *toolsTabs = tabsOf(*dialog, 2);
    auto *shortcutsTabs = tabsOf(*dialog, 3);

    dialog->selectPage(QStringLiteral("tools/mcp"));
    QCOMPARE(list->currentRow(), 2);
    QCOMPARE(toolsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("shortcuts/shortcuts"));
    QCOMPARE(list->currentRow(), 3);
    QCOMPARE(shortcutsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("general/systemprompt"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(generalTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("general/prompttemplates"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(generalTabs->currentIndex(), 1);

    dialog->selectPage(QStringLiteral("general/userinterface"));
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(generalTabs->currentIndex(), 2);

    dialog->selectPage(QStringLiteral("models/llm"));
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(modelsTabs->currentIndex(), 0);

    dialog->selectPage(QStringLiteral("models/tts"));
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(modelsTabs->currentIndex(), 2);
}

// The settings dialog is a QWidget, so the test needs a QApplication; force
// the offscreen platform because the CI test runner has no display server.
// The HTTP authorization mode radio buttons show only the relevant input
// field; switching the mode keeps both entered values.
void TestSettingsDialog::mcpHttpAuthModeShowsOnlyRelevantField() {
    McpRegistry registry;
    McpManager manager(&registry);
    // Add the server before constructing the page so its list is populated
    // and the first row is selected.
    registry.add(QStringLiteral("srv"), McpTransport::StreamableHttp);
    McpPage page(&registry, &manager);
    page.show();
    auto *list = page.findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    list->setCurrentRow(0);

    auto *apiKeyRadio = page.findChild<QRadioButton *>(QStringLiteral("apiKeyRadio"));
    auto *headersRadio = page.findChild<QRadioButton *>(QStringLiteral("headersRadio"));
    auto *apiKeyEdit = page.findChild<QLineEdit *>(QStringLiteral("apiKeyEdit"));
    auto *headersEdit = page.findChild<QLineEdit *>(QStringLiteral("headersEdit"));
    QVERIFY(apiKeyRadio && headersRadio && apiKeyEdit && headersEdit);
    QCOMPARE(apiKeyRadio->text(), QStringLiteral("API token"));
    QCOMPARE(apiKeyEdit->placeholderText(), QStringLiteral("API token"));

    // Default: API token mode, headers field hidden.
    QVERIFY(apiKeyRadio->isChecked());
    QVERIFY(apiKeyEdit->isVisibleTo(&page));
    QVERIFY(!headersEdit->isVisibleTo(&page));

    QTest::keyClicks(apiKeyEdit, QStringLiteral("secret"));
    QCOMPARE(registry.servers().first().apiKey, QStringLiteral("secret"));

    // Switch to HTTP headers: only the headers field is visible, the API
    // token value is kept.
    headersRadio->setChecked(true);
    QVERIFY(!apiKeyEdit->isVisibleTo(&page));
    QVERIFY(headersEdit->isVisibleTo(&page));
    QCOMPARE(apiKeyEdit->text(), QStringLiteral("secret"));
    QCOMPARE(registry.servers().first().apiKey, QStringLiteral("secret"));

    QTest::keyClicks(headersEdit, QStringLiteral("X-A: 1"));
    QCOMPARE(registry.servers().first().headers, QStringList({QStringLiteral("X-A: 1")}));

    // Switch back: API token field visible again, headers value kept.
    apiKeyRadio->setChecked(true);
    QVERIFY(apiKeyEdit->isVisibleTo(&page));
    QVERIFY(!headersEdit->isVisibleTo(&page));
    QCOMPARE(headersEdit->text(), QStringLiteral("X-A: 1"));
    QCOMPARE(registry.servers().first().headers, QStringList({QStringLiteral("X-A: 1")}));
}

// Regression test: the settings sidebar (and the MCP server list) must not
// absorb extra width when the dialog grows — QSplitter's first-show sizing
// used to leave a large empty gap next to the list until the splitter handle
// was moved once. Stretch factor 0 keeps the list at its maximum width.
void TestSettingsDialog::sidebarKeepsMaxWidthWhenDialogGrows() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    dialog->resize(880, 580);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
    QTest::qWait(200);
    auto *splitter = dialog->findChild<QSplitter *>();
    auto *cats = dialog->findChild<QListWidget *>();
    if (!splitter || !cats)
        QFAIL("splitter or category list not found");
    dialog->resize(1400, 700);
    QTest::qWait(200);
    QVERIFY(splitter->sizes().first() <= cats->maximumWidth());

    auto *page = dialog->findChild<McpPage *>();
    if (!page)
        QFAIL("MCP page not found");
    dialog->selectPage(QStringLiteral("tools/mcp"));
    QTest::qWait(200);
    auto *inner = page->findChild<QSplitter *>();
    auto *srvList = page->findChild<QListWidget *>();
    if (!inner || !srvList)
        QFAIL("inner splitter or server list not found");
    QVERIFY(inner->sizes().first() <= srvList->maximumWidth() + 20);
}

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // WebEngine needs these flags in containers without a user namespace
    // (e.g. the sandbox); harmless on a normal desktop runner.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--no-sandbox --disable-gpu --disable-dev-shm-usage");
    QApplication app(argc, argv);
    TestSettingsDialog test;
    return QTest::qExec(&test, argc, argv);
}

// The User Interface page offers Auto/Light/Dark (Auto default); changing it
// persists the setting and applyTheme() switches the application palette
// immediately.
void TestSettingsDialog::themeComboAppliesImmediately() {
    UiSettings settings;
    UserInterfacePage page(&settings);
    page.show();
    auto *combo = page.findChild<QComboBox *>(QStringLiteral("themeCombo"));
    if (!combo)
        QFAIL("theme combo not found");
    QCOMPARE(combo->count(), 3);
    QCOMPARE(combo->currentIndex(), 0); // Auto is the default
    QCOMPARE(settings.theme(), UiSettings::Theme::Auto);

    const QPalette original = qApp->palette();
    combo->setCurrentIndex(2); // Dark
    QCOMPARE(settings.theme(), UiSettings::Theme::Dark);
    QVERIFY(applyTheme(settings.theme()));
    QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 128);

    combo->setCurrentIndex(1); // Light
    QCOMPARE(settings.theme(), UiSettings::Theme::Light);
    QVERIFY(!applyTheme(settings.theme()));
    QVERIFY(qApp->palette().color(QPalette::Window).lightness() >= 128);

    qApp->setPalette(original);
}

// Themed icons replace the fixed dark stroke color with the palette text
// color, so they stay visible on a dark background.
void TestSettingsDialog::themedIconFollowsPaletteInDarkMode() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString svgPath = dir.filePath(QStringLiteral("icon.svg"));
    {
        QFile f(svgPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"24\" "
                "height=\"24\"><path d=\"M4 4 h16 v16 h-16 z\" fill=\"#232629\"/></svg>");
    }

    const QPalette original = qApp->palette();
    QVERIFY(applyTheme(UiSettings::Theme::Dark));
    const QImage darkImage = themedIcon(svgPath).pixmap(24, 24).toImage();
    // The icon must contain light (text-colored) pixels, not the dark #232629.
    bool hasLightPixel = false;
    for (int y = 0; y < darkImage.height() && !hasLightPixel; ++y)
        for (int x = 0; x < darkImage.width(); ++x) {
            const QColor c = darkImage.pixelColor(x, y);
            if (c.alpha() > 200 && c.lightness() > 128) {
                hasLightPixel = true;
                break;
            }
        }
    QVERIFY(hasLightPixel);

    QVERIFY(!applyTheme(UiSettings::Theme::Light));
    const QImage lightImage = themedIcon(svgPath).pixmap(24, 24).toImage();
    bool hasDarkPixel = false;
    for (int y = 0; y < lightImage.height() && !hasDarkPixel; ++y)
        for (int x = 0; x < lightImage.width(); ++x) {
            const QColor c = lightImage.pixelColor(x, y);
            if (c.alpha() > 200 && c.lightness() < 128) {
                hasDarkPixel = true;
                break;
            }
        }
    QVERIFY(hasDarkPixel);
    qApp->setPalette(original);
}

// Switching the transport must hide the whole other group, including the
// row labels (URL/Authorization vs Command/Arguments/Environment).
void TestSettingsDialog::mcpTransportSwitchHidesOtherGroupIncludingLabels() {
    McpRegistry registry;
    McpManager manager(&registry);
    registry.add(QStringLiteral("srv"), McpTransport::StreamableHttp);
    McpPage page(&registry, &manager);
    page.show();
    auto *list = page.findChild<QListWidget *>();
    if (!list || list->count() == 0)
        QFAIL("server list not populated");
    list->setCurrentRow(0);
    auto *transportCombo = page.findChild<QComboBox *>();
    if (!transportCombo)
        QFAIL("transport combo not found");
    auto *urlEdit = page.findChild<QLineEdit *>(QStringLiteral("urlEdit"));
    if (!urlEdit)
        urlEdit = [&]() {
            for (auto *e : page.findChildren<QLineEdit *>())
                if (e->placeholderText().contains(QStringLiteral("http://")))
                    return e;
            return static_cast<QLineEdit *>(nullptr);
        }();
    auto *cmdEdit = [&]() {
        for (auto *e : page.findChildren<QLineEdit *>())
            if (e->placeholderText() == QStringLiteral("npx"))
                return e;
        return static_cast<QLineEdit *>(nullptr);
    }();
    if (!urlEdit || !cmdEdit)
        QFAIL("url/command edits not found");
    auto labelWithText = [&page](const QString &text) {
        for (auto *l : page.findChildren<QLabel *>())
            if (l->text() == text)
                return l;
        return static_cast<QLabel *>(nullptr);
    };

    const int stdioIndex = transportCombo->findText(QStringLiteral("stdio"));
    QVERIFY(stdioIndex >= 0);
    transportCombo->setCurrentIndex(stdioIndex);
    QVERIFY(!urlEdit->isVisibleTo(&page));
    QVERIFY(!labelWithText(QStringLiteral("URL:"))->isVisibleTo(&page));
    QVERIFY(!labelWithText(QStringLiteral("Authorization:"))->isVisibleTo(&page));
    QVERIFY(cmdEdit->isVisibleTo(&page));
    QVERIFY(labelWithText(QStringLiteral("Command:"))->isVisibleTo(&page));

    transportCombo->setCurrentIndex(0); // back to Streamable HTTP
    QVERIFY(urlEdit->isVisibleTo(&page));
    QVERIFY(labelWithText(QStringLiteral("URL:"))->isVisibleTo(&page));
    QVERIFY(!cmdEdit->isVisibleTo(&page));
    QVERIFY(!labelWithText(QStringLiteral("Command:"))->isVisibleTo(&page));
}

// The system prompt page has no preview button: editor and preview sit side
// by side in a horizontal splitter, and the preview is a web view that
// renders the substituted prompt as Markdown, updated live on every edit.
void TestSettingsDialog::systemPromptPagePreviewsMarkdownLive() {
    SystemPrompt prompt;
    prompt.setText(QStringLiteral("# Title\n\nSome **bold** text with {{date}}"));
    UiSettings uiSettings;
    SystemPromptPage page(&prompt, &uiSettings);
    auto *preview = page.findChild<QWebEngineView *>(QStringLiteral("preview"));
    QVERIFY(preview);
    QSignalSpy loadSpy(preview, &QWebEngineView::loadFinished);
    page.resize(800, 500);
    page.show();

    QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("previewButton")));

    auto *splitter = page.findChild<QSplitter *>(QStringLiteral("splitter"));
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Horizontal);
    auto *editor = page.findChild<QPlainTextEdit *>(QStringLiteral("editor"));
    QVERIFY(editor);
    QCOMPARE(splitter->widget(0), editor->parentWidget());
    QCOMPARE(splitter->widget(1), preview->parentWidget());
    QCOMPARE(preview->url(), QUrl(QStringLiteral("qrc:/resources/web/preview.html")));

    // The splitter fills the page vertically: only the layout spacing (not a
    // large dead area) separates it from the button row below.
    auto *buttonRow = page.findChild<QHBoxLayout *>(QStringLiteral("buttonRow"));
    QVERIFY(buttonRow);
    QTest::qWait(200);
    const int gap = buttonRow->geometry().top() - splitter->geometry().bottom();
    QVERIFY2(gap <= 12, qPrintable(QStringLiteral("gap=%1").arg(gap)));

    // The preview page loads and renders the initial prompt as Markdown:
    // heading and bold markup, with the {{date}} field substituted.
    QTRY_VERIFY_WITH_TIMEOUT(loadSpy.count() >= 1, 30000);
    QVERIFY(loadSpy.first().at(0).toBool());
    const QString contentExpr = QStringLiteral("document.getElementById('content').innerHTML");
    QVERIFY(waitForJs(preview->page(), contentExpr, QStringLiteral("<h1")));
    const QString initial = evalJs(preview->page(), contentExpr);
    QVERIFY(initial.contains(QStringLiteral("<strong>bold</strong>")));
    QVERIFY(!initial.contains(QStringLiteral("{{")));

    // Live update: editing re-renders the preview without pressing a button.
    editor->setPlainText(QStringLiteral("Plain line\n\n- bullet **x**"));
    QVERIFY(waitForJs(preview->page(), contentExpr, QStringLiteral("<li>")));
    const QString updated = evalJs(preview->page(), contentExpr);
    QVERIFY(updated.contains(QStringLiteral("<strong>x</strong>")));
}

// Inside the real settings dialog the system prompt page must fill the tab
// area: no dead space between the splitter and the button row.
void TestSettingsDialog::systemPromptPageFillsTabArea() {
    std::unique_ptr<SettingsDialog> dialog(makeDialog());
    dialog->resize(1400, 800);
    dialog->show();
    QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
    dialog->selectPage(QStringLiteral("general/systemprompt"));
    QTest::qWait(300);

    auto *page = dialog->findChild<SystemPromptPage *>();
    QVERIFY(page);
    auto *splitter = page->findChild<QSplitter *>(QStringLiteral("splitter"));
    auto *buttonRow = page->findChild<QHBoxLayout *>(QStringLiteral("buttonRow"));
    QVERIFY(splitter && buttonRow);
    QVERIFY(page->height() > dialog->height() / 2);
    // The splitter fills the page vertically: it takes most of the height
    // and only the layout spacing separates it from the button row below.
    const int gap = buttonRow->geometry().top() - splitter->geometry().bottom();
    QVERIFY2(gap <= 12, qPrintable(QStringLiteral("gap=%1 page=%2x%3 dialog=%4x%5")
                                       .arg(gap)
                                       .arg(page->width())
                                       .arg(page->height())
                                       .arg(dialog->width())
                                       .arg(dialog->height())));
    QVERIFY2(
        splitter->height() > page->height() * 7 / 10,
        qPrintable(
            QStringLiteral("splitter=%1 page=%2").arg(splitter->height()).arg(page->height())));

    // Same at the dialog's default size (880x580): the splitter still fills.
    dialog->resize(880, 580);
    QTest::qWait(300);
    const int gapSmall = buttonRow->geometry().top() - splitter->geometry().bottom();
    QVERIFY2(gapSmall <= 12, qPrintable(QStringLiteral("gap=%1").arg(gapSmall)));
    QVERIFY(splitter->height() > page->height() * 7 / 10);
}

#include "tst_settingsdialog.moc"
