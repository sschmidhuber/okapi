#include "ui/systempromptpage.h"
#include "ui_systempromptpage.h"

#include "core/systemprompt.h"
#include "core/uisettings.h"
#include "ui/theme.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QUrl>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace okapi {

namespace {

// The prompt text is passed to the preview page as a JS string literal.
QString jsString(const QString &s) {
    QJsonArray arr;
    arr.append(s);
    QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return json.mid(1, json.size() - 2); // strip the array brackets -> bare JSON string
}

} // namespace

SystemPromptPage::~SystemPromptPage() = default;

SystemPromptPage::SystemPromptPage(SystemPrompt *systemPrompt, UiSettings *uiSettings,
                                   QWidget *parent)
    : QWidget(parent), m_systemPrompt(systemPrompt), m_uiSettings(uiSettings),
      m_ui(std::make_unique<Ui::SystemPromptPage>()) {
    m_ui->setupUi(this);
    m_editor = m_ui->editor;
    m_preview = m_ui->preview;
    m_saveButton = m_ui->saveButton;
    m_resetButton = m_ui->resetButton;

    // A horizontal splitter is only Expanding along its orientation, so
    // without this the page layout would not give it the free vertical space
    // (the dead area between splitter and button row).
    m_ui->splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_ui->layout->setStretchFactor(m_ui->splitter, 1);
    m_ui->splitter->setStretchFactor(0, 1);
    m_ui->splitter->setStretchFactor(1, 1);

    m_preview->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, false);
    connect(m_preview, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok)
            return;
        m_previewReady = true;
        callJs(QStringLiteral("setTheme(%1)")
                   .arg(m_darkMode ? QStringLiteral("true") : QStringLiteral("false")));
        updatePreview();
    });

    m_editor->setPlainText(m_systemPrompt->text());

    connect(m_saveButton, &QPushButton::clicked, this, &SystemPromptPage::onSave);
    connect(m_resetButton, &QPushButton::clicked, this, &SystemPromptPage::onResetDefault);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &SystemPromptPage::updatePreview);
    if (m_uiSettings) {
        connect(m_uiSettings, &UiSettings::changed, this, &SystemPromptPage::applyDarkMode);
        applyDarkMode();
    }

    m_preview->setUrl(QUrl(QStringLiteral("qrc:/resources/web/preview.html")));
}

void SystemPromptPage::onSave() {
    m_systemPrompt->setText(m_editor->toPlainText());
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QSettings settings(configDir + QStringLiteral("/okapi.conf"), QSettings::IniFormat);
    m_systemPrompt->save(settings);
    emit changed();
}

void SystemPromptPage::onResetDefault() {
    // textChanged fires, so the preview follows automatically.
    m_editor->setPlainText(SystemPrompt::defaultTemplate());
}

void SystemPromptPage::applyDarkMode() {
    m_darkMode = m_uiSettings->theme() == UiSettings::Theme::Dark ||
                 (m_uiSettings->theme() == UiSettings::Theme::Auto && systemPrefersDark());
    callJs(QStringLiteral("setTheme(%1)")
               .arg(m_darkMode ? QStringLiteral("true") : QStringLiteral("false")));
}

void SystemPromptPage::updatePreview() {
    callJs(QStringLiteral("renderPreview(%1)")
               .arg(jsString(SystemPrompt::substitute(m_editor->toPlainText()))));
}

void SystemPromptPage::callJs(const QString &expr) {
    if (m_previewReady)
        m_preview->page()->runJavaScript(expr);
}

} // namespace okapi
