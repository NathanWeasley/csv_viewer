#pragma once

#include "viewer_plugin/viewer_plugin_sdk.h"

#include <QObject>
#include <QString>

class LogExpandPlugin final
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
    void handleDataLoaded(const viewer::plugin::LoadSessionInfo& session);
    void handleDataAboutToUnload(quint64 sessionId);
    void handleJsonDocumentsChanged(
        quint64 sessionId, const QString& providerPluginId);
    void showInputStatus();
    void refreshMenuState();
    bool hasReadyDatDecryptInput() const;
    void log(viewer::plugin::LogLevel level, const QString& message) const;

    viewer::plugin::IViewerHost* m_host = nullptr;
    viewer::plugin::PluginMenuHandle m_menu = 0;
    viewer::plugin::SubscriptionId m_dataLoadedSubscription = 0;
    viewer::plugin::SubscriptionId m_dataUnloadSubscription = 0;
    viewer::plugin::JsonSubscriptionId m_jsonChangedSubscription = 0;
    quint64 m_currentSessionId = 0;
    bool m_shuttingDown = false;
};
