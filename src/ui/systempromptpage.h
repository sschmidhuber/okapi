#pragma once

#include <QWidget>
#include <memory>

namespace Ui {
class SystemPromptPage;
}

class QPlainTextEdit;
class QPushButton;
class QTextEdit;

namespace okapi {

class SystemPrompt;

// Settings page for the system prompt: edit the template (with the dynamic
// fields {{date}}, {{time}}, {{language}}, {{os}}) in the top pane; the
// bottom pane shows the substituted prompt rendered as Markdown, updated
// live on every keystroke.
class SystemPromptPage : public QWidget {
    Q_OBJECT

public:
    ~SystemPromptPage() override;

    explicit SystemPromptPage(SystemPrompt *systemPrompt, QWidget *parent = nullptr);

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSave();
    void onResetDefault();

private:
    void updatePreview();

    std::unique_ptr<Ui::SystemPromptPage> m_ui;
    SystemPrompt *m_systemPrompt;
    QPlainTextEdit *m_editor = nullptr;
    QTextEdit *m_preview = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_resetButton = nullptr;
};

} // namespace okapi
