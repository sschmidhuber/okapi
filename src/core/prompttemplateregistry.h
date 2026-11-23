#pragma once

#include "core/imagemessage.h"
#include "core/message.h"

#include <QObject>
#include <QSettings>
#include <QUuid>
#include <QVector>

namespace okapi {

// One prompt template: a name shown in the composer's autocompletion, a
// mandatory prompt text and optional file attachments. Inserted into the
// composer (text at the cursor + attachments) when selected.
struct PromptTemplate {
    QUuid id = QUuid::createUuid();
    QString name;
    QString text;
    QVector<ImageAttachment> images;
    QVector<FileAttachment> files;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static PromptTemplate fromJson(const QJsonObject &object);
};

// Registry of the user's prompt templates (Settings > General > Prompt
// Templates), persisted as JSON in the application settings under
// "promptTemplates". Mirrors the TtsRegistry pattern: load() remembers the
// file so changes can save() immediately; every change emits changed().
class PromptTemplateRegistry : public QObject {
    Q_OBJECT

public:
    // Maximum number of most-recently-used templates kept (and offered in
    // the main window's "Prompt Template" toolbar dropdown).
    static constexpr int kMaxRecentTemplates = 20;

    explicit PromptTemplateRegistry(QObject *parent = nullptr);

    [[nodiscard]] const QVector<PromptTemplate> &templates() const { return m_templates; }
    [[nodiscard]] const PromptTemplate *find(const QUuid &id) const;
    // Adds a new template (id must be fresh); returns the stored template.
    PromptTemplate &add(PromptTemplate tpl);
    // Replaces the template with the same id.
    void update(const PromptTemplate &tpl);
    void remove(const QUuid &id);

    // Ids of the most recently used templates, most recent first (at most
    // kMaxRecentTemplates); used for the toolbar dropdown's entry order.
    [[nodiscard]] const QVector<QUuid> &recentIds() const { return m_recentIds; }
    // Marks a template as used now: moves it to the front of the recent
    // list (capped at kMaxRecentTemplates) and persists the list.
    void recordUsed(const QUuid &id);

    void load(QSettings &settings);
    void save(QSettings &settings) const;
    void save() const;

signals:
    void changed();

private:
    QVector<PromptTemplate> m_templates;
    QVector<QUuid> m_recentIds;
    QString m_persistPath;
    QSettings::Format m_persistFormat = QSettings::IniFormat;
};

} // namespace okapi
