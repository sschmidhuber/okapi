#pragma once

#include <QWidget>
#include <memory>

namespace Ui {
class SystemPromptPage;
}

class QPlainTextEdit;
class QPushButton;
class QWebEngineView;

namespace okapi {

class SystemPrompt;
class UiSettings;

// Settings page for the system prompt: edit the template (with the dynamic
// fields {{date}}, {{time}}, {{language}}, {{os}}) in the left pane; the
// right pane shows the substituted prompt rendered as Markdown in a web
// view, updated live on every keystroke.
class SystemPromptPage : public QWidget {
    Q_OBJECT

public:
    ~SystemPromptPage() override;

    explicit SystemPromptPage(SystemPrompt *systemPrompt, UiSettings *uiSettings,
                              QWidget *parent = nullptr);

signals:
    void changed();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onSave();
    void onResetDefault();

private:
    void updatePreview();
    void applyDarkMode();
    void callJs(const QString &expr);

    std::unique_ptr<Ui::SystemPromptPage> m_ui;
    SystemPrompt *m_systemPrompt;
    UiSettings *m_uiSettings;
    QPlainTextEdit *m_editor = nullptr;
    QWebEngineView *m_preview = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    bool m_previewReady = false;
    bool m_urlLoaded = false;
    bool m_darkMode = false;
};

} // namespace okapi
