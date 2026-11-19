// Qt Designer plugin that registers okapi's promoted custom widgets, so Qt
// Designer shows them as real widgets in the widget box (instead of generic
// placeholders) and preserves them when a .ui file is saved.
//
// Build (needs the qttools module, i.e. Qt6::Designer):
//   cmake --build build --target okapiwidgets
// Install for the current user:
//   cmake --install build --component designer  (or copy the .so manually to
//   ~/.local/share/qt6/plugins/designer/), then restart Designer.

#pragma once

#include <QDesignerCustomWidgetCollectionInterface>
#include <QDesignerCustomWidgetInterface>
#include <QIcon>
#include <QList>
#include <QString>
#include <QWidget>

namespace okapi_designer {

class OkapiCustomWidget : public QObject, public QDesignerCustomWidgetInterface {
    Q_OBJECT
    Q_INTERFACES(QDesignerCustomWidgetInterface)

public:
    OkapiCustomWidget(QString name, QString includeFile, QWidget *(*factory)(QWidget *));

    [[nodiscard]] QString name() const override { return m_name; }
    [[nodiscard]] QString group() const override { return QStringLiteral("Okapi Widgets"); }
    [[nodiscard]] QString toolTip() const override { return m_name; }
    [[nodiscard]] QString whatsThis() const override { return m_name; }
    [[nodiscard]] QString includeFile() const override { return m_includeFile; }
    [[nodiscard]] QIcon icon() const override { return {}; }
    [[nodiscard]] bool isContainer() const override { return false; }
    QWidget *createWidget(QWidget *parent) override { return m_factory(parent); }

private:
    QString m_name;
    QString m_includeFile;
    QWidget *(*m_factory)(QWidget *);
};

class OkapiWidgetCollection : public QObject, public QDesignerCustomWidgetCollectionInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QDesignerCustomWidgetCollectionInterface")
    Q_INTERFACES(QDesignerCustomWidgetCollectionInterface)

public:
    [[nodiscard]] QList<QDesignerCustomWidgetInterface *> customWidgets() const override;
};

} // namespace okapi_designer
