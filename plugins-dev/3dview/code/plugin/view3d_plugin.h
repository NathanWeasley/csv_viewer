#pragma once

#include "viewer_plugin/viewer_plugin_sdk.h"
#include "viewer_plugin/viewer_toolbar_sdk.h"

#include <QObject>
#include <QPointer>
#include <QString>

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
    void reloadConfiguration();
    void refreshData();
    void scheduleRefreshData();

    viewer::plugin::IViewerHost* m_host = nullptr;
    viewer::plugin::IPluginToolbarService* m_toolbarService = nullptr;
    viewer::plugin::PluginMenuHandle m_menu = 0;
    viewer::plugin::PluginToolbarButtonHandle m_openToolbarButton = 0;
    viewer::plugin::SubscriptionId m_dataLoadedSubscription = 0;
    viewer::plugin::SubscriptionId m_dataUnloadingSubscription = 0;
    viewer::plugin::SubscriptionId m_columnAddedSubscription = 0;
    QPointer<View3DWidget> m_view;
    QString m_configPath;
    QString m_statePath;
    bool m_shuttingDown = false;
    bool m_refreshDataPending = false;
};
