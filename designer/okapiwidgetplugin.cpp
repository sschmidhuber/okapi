#include "okapiwidgetplugin.h"

#include "ui/composer.h"
#include "ui/mainwindow.h"
#include "ui/sidebar.h"
#include "ui/webchatview.h"

namespace okapi_designer {

OkapiCustomWidget::OkapiCustomWidget(QString name, QString includeFile,
                                     QWidget *(*factory)(QWidget *))
    : m_name(std::move(name)), m_includeFile(std::move(includeFile)), m_factory(factory) {}

QList<QDesignerCustomWidgetInterface *> OkapiWidgetCollection::customWidgets() const {
    return {
        new OkapiCustomWidget(QStringLiteral("Sidebar"), QStringLiteral("ui/sidebar.h"),
                              [](QWidget *p) -> QWidget * { return new okapi::Sidebar(p); }),
        new OkapiCustomWidget(QStringLiteral("WebChatView"), QStringLiteral("ui/webchatview.h"),
                              [](QWidget *p) -> QWidget * { return new okapi::WebChatView(p); }),
        new OkapiCustomWidget(QStringLiteral("Composer"), QStringLiteral("ui/composer.h"),
                              [](QWidget *p) -> QWidget * { return new okapi::Composer(p); }),
        new OkapiCustomWidget(
            QStringLiteral("ModelPickerButton"), QStringLiteral("ui/mainwindow.h"),
            [](QWidget *p) -> QWidget * { return new okapi::ModelPickerButton(p); }),
        new OkapiCustomWidget(
            QStringLiteral("MessageTextEdit"), QStringLiteral("ui/composer.h"),
            [](QWidget *p) -> QWidget * { return new okapi::MessageTextEdit(p); }),
    };
}

} // namespace okapi_designer
