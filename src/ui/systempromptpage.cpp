#include "ui/systempromptpage.h"
#include "ui_systempromptpage.h"

#include "core/markdown.h"
#include "core/systemprompt.h"

#include <QDir>
#include <QEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTextEdit>

namespace okapi {

SystemPromptPage::~SystemPromptPage() = default;

SystemPromptPage::SystemPromptPage(SystemPrompt *systemPrompt, QWidget *parent)
    : QWidget(parent), m_systemPrompt(systemPrompt),
      m_ui(std::make_unique<Ui::SystemPromptPage>()) {
    m_ui->setupUi(this);
    m_editor = m_ui->editor;
    m_preview = m_ui->preview;
    m_saveButton = m_ui->saveButton;
    m_resetButton = m_ui->resetButton;

    m_ui->splitter->setStretchFactor(0, 1);
    m_ui->splitter->setStretchFactor(1, 1);

    m_editor->setPlainText(m_systemPrompt->text());

    connect(m_saveButton, &QPushButton::clicked, this, &SystemPromptPage::onSave);
    connect(m_resetButton, &QPushButton::clicked, this, &SystemPromptPage::onResetDefault);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &SystemPromptPage::updatePreview);
    // The preview document is laid out for a fixed text width, so re-render
    // when the preview pane changes width (dialog resize or splitter moved).
    m_preview->viewport()->installEventFilter(this);

    updatePreview();
}

bool SystemPromptPage::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_preview->viewport() && event->type() == QEvent::Resize)
        updatePreview();
    return QWidget::eventFilter(watched, event);
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

void SystemPromptPage::updatePreview() {
    const QString text = SystemPrompt::substitute(m_editor->toPlainText());
    const int width = qMax(60, m_preview->viewport()->width());
    m_preview->setDocument(MarkdownRenderer::render(text, width, m_preview->font()).release());
}

} // namespace okapi
