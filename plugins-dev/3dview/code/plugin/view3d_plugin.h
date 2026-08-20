#pragma once

#include "viewer_plugin/viewer_plugin_sdk.h"

#include <QObject>
#include <QPointer>

class View3DWidget;

class View3DPlugin final
    : public QObject
    , public viewer::plugin::IViewerPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID VIEWER_PLUGIN_INTERFACE_IID FILE "../../plugin.json")
    Q_INTERFACES(viewer::plugin::IViewerPlugin)

public:
    QString id() const override;
    QString name() const override;
    QString version() const override;
    bool initialize(viewer::plugin::IViewerHost* host) override;
    void shutdown() override;

private:
    void showView();

    viewer::plugin::IViewerHost* m_host = nullptr;
    viewer::plugin::PluginMenuHandle m_menu = 0;
    viewer::plugin::PluginDockHandle m_viewDock = 0;
    QPointer<View3DWidget> m_view;
    bool m_shuttingDown = false;
};
