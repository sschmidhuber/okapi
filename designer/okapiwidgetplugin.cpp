// Qt Designer plugin that registers okapi's promoted custom widgets, so Qt
// Designer shows them as real widgets in the widget box (instead of generic
// placeholders) and preserves them when a .ui file is saved.
//
// Build (needs the qttools module, i.e. Qt6::Designer):
//   cmake --build build --target okapiwidgets
// Install for the current user:
//   cmake --install build --component designer  (or copy the .so manually to
//   ~/.local/share/qt6/plugins/designer/), then restart Designer.

#include "ui/composer.h"
#include "ui/mainwindow.h"
#include "ui/sidebar.h"
#include "ui/webchatview.h"

#include <QDesignerCustomWidgetCollectionInterface>
#include <QDesignerCustomWidgetInterface>
#include <QIcon>
#include <QWidget>

namespace {

class OkapiCustomWidget : public QObject, public QDesignerCustomWidgetInterface {
    Q_OBJECT
    Q_INTERFACES(QDesignerCustomWidgetInterface)

public:
    OkapiCustomWidget(QString name, QString header, QWidget *(*factory)(QWidget *))
        : m_name(std::move(name)), m_header(std::move(header)), m_factory(factory) {}

    QString name() const override { return m_name; }
    QString group() const override { return QStringLiteral("Okapi Widgets"); }
    QString toolTip() const override { return m_name; }
    QString whatsThis() const override { return m_name; }
    QString includeFile() const override { return m_header; }
    QIcon icon() const override { return QIcon(); }
    bool isContainer() const override { return false; }
    QWidget *createWidget(QWidget *parent) override { return m_factory(parent); }

private:
    QString m_name;
    QString m_header;
    QWidget *(*m_factory)(QWidget *);
};

class OkapiWidgetCollection : public QObject, public QDesignerCustomWidgetCollectionInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QDesignerCustomWidgetCollectionInterface")
    Q_INTERFACES(QDesignerCustomWidgetCollectionInterface)

public:
    QList<QDesignerCustomWidgetInterface *> customWidgets() const override {
        return {
            new OkapiCustomWidget(QStringLiteral("Sidebar"), QStringLiteral("ui/sidebar.h"),
                                  [](QWidget *p) -> QWidget * { return new okapi::Sidebar(p); }),
            new OkapiCustomWidget(
                QStringLiteral("WebChatView"), QStringLiteral("ui/webchatview.h"),
                [](QWidget *p) -> QWidget * { return new okapi::WebChatView(p); }),
            new OkapiCustomWidget(QStringLiteral("Composer"), QStringLiteral("ui/composer.h"),
                                  [](QWidget *p) -> QWidget * { return new okapi::Composer(p); }),
            new OkapiCustomWidget(
                QStringLiteral("ModelPickerButton"), QStringLiteral("ui/mainwindow.h"),
                [](QWidget *p) -> QWidget * { return new okapi::ModelPickerButton(p); }),
        };
    }
};

} // namespace

#include "okapiwidgetplugin.moc"
