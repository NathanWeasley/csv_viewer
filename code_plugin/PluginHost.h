#pragma once

#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_plugin_sdk.h"

#include <QHash>
#include <QPointer>

#include <functional>
#include <memory>

class QAction;
class QMainWindow;
class QMenu;

namespace ads
{
class CDockManager;
class CDockWidget;
}

namespace viewer
{
class Viewer;
}

class PluginHost final
    : public QObject
    , public viewer::plugin::IViewerHost
    , public viewer::plugin::IDataService
    , public viewer::plugin::IArchiveService
    , public viewer::plugin::IEventService
    , public viewer::plugin::IPluginRegistry
    , public viewer::plugin::IUiService
    , public viewer::plugin::ILogService
{
public:
    PluginHost(viewer::Viewer& viewer,
               QMainWindow* mainWindow,
               ads::CDockManager* dockManager,
               QMenu* pluginMenu,
               std::function<void()> rebuildDataTree,
               QObject* parent = nullptr);
    ~PluginHost() override;

    // IViewerHost
    int sdkVersion() const noexcept override;
    viewer::plugin::IDataService* data() noexcept override { return this; }
    viewer::plugin::IArchiveService* archive() noexcept override { return this; }
    viewer::plugin::IEventService* events() noexcept override { return this; }
    viewer::plugin::IPluginRegistry* plugins() noexcept override { return this; }
    viewer::plugin::IUiService* ui() noexcept override { return this; }
    viewer::plugin::ILogService* log() noexcept override { return this; }

    // IDataService
    viewer::plugin::LoadSessionInfo currentSession() const override;
    viewer::plugin::DataSnapshotPtr acquireSnapshot() const override;
    viewer::plugin::DerivedColumnCreateResult createDerivedColumn(
        quint64 sessionId,
        const QString& name,
        qsizetype rowCount) override;

    // IArchiveService
    QList<viewer::plugin::ArchiveEntryInfo> listCurrentZipEntries(
        quint64 sessionId,
        QString* error = nullptr) const override;
    viewer::plugin::ArchiveReadResult readCurrentZipEntry(
        quint64 sessionId,
        const QString& entryPath) const override;

    // IEventService
    viewer::plugin::SubscriptionId subscribeDataLoaded(
        const QString& ownerPluginId,
        DataLoadedCallback callback) override;
    viewer::plugin::SubscriptionId subscribeDataAboutToUnload(
        const QString& ownerPluginId,
        DataAboutToUnloadCallback callback) override;
    viewer::plugin::SubscriptionId subscribeColumnAdded(
        const QString& ownerPluginId,
        ColumnAddedCallback callback) override;
    void unsubscribe(viewer::plugin::SubscriptionId subscription) override;

    // IPluginRegistry
    bool isPluginLoaded(const QString& pluginId) const override;
    viewer::plugin::PluginState pluginState(const QString& pluginId) const override;
    bool registerService(const QString& providerPluginId,
                         const QString& serviceId,
                         int version,
                         QObject* service) override;
    void unregisterService(const QString& providerPluginId,
                           const QString& serviceId) override;
    QObject* queryService(const QString& providerPluginId,
                          const QString& serviceId,
                          int minimumVersion,
                          int* actualVersion = nullptr) const override;

    // IUiService
    viewer::plugin::PluginActionHandle addPluginAction(
        const QString& ownerPluginId,
        const QString& text,
        std::function<void()> callback) override;
    viewer::plugin::PluginDockHandle createDock(
        const QString& ownerPluginId,
        const QString& dockId,
        const QString& title,
        QWidget* content,
        viewer::plugin::DockArea preferredArea) override;
    bool showDock(viewer::plugin::PluginDockHandle dock) override;
    bool closeDock(viewer::plugin::PluginDockHandle dock) override;
    void showError(const QString& title, const QString& message) override;
    void showInformation(const QString& title, const QString& message) override;

    // ILogService
    void write(const QString& ownerPluginId,
               viewer::plugin::LogLevel level,
               const QString& message) override;

    // PluginManager-only lifecycle hooks.
    void setPluginState(const QString& pluginId, viewer::plugin::PluginState state);
    void removeOwnedResources(const QString& pluginId);
    void beginShutdown();

private:
    friend class DerivedColumnWriterImpl;

    viewer::plugin::DataCommitResult commitDerivedColumn(
        quint64 sessionId,
        QString name,
        std::vector<double>&& values);
    QString serviceKey(const QString& providerPluginId, const QString& serviceId) const;
    quint64 nextHandle();

    struct ServiceRecord
    {
        QString providerPluginId;
        QString serviceId;
        int version = 0;
        QPointer<QObject> service;
    };
    struct ActionRecord
    {
        QString ownerPluginId;
        QPointer<QAction> action;
    };
    struct DockRecord
    {
        QString ownerPluginId;
        QPointer<ads::CDockWidget> dock;
    };
    template<typename Callback>
    struct SubscriptionRecord
    {
        QString ownerPluginId;
        Callback callback;
    };

    viewer::Viewer& m_viewer;
    QPointer<QMainWindow> m_mainWindow;
    ads::CDockManager* m_dockManager = nullptr;
    QPointer<QMenu> m_pluginMenu;
    std::function<void()> m_rebuildDataTree;
    bool m_shuttingDown = false;
    quint64 m_nextHandle = 1;

    QHash<QString, viewer::plugin::PluginState> m_pluginStates;
    QHash<QString, ServiceRecord> m_services;
    QHash<quint64, ActionRecord> m_actions;
    QHash<quint64, DockRecord> m_docks;
    QHash<quint64, SubscriptionRecord<DataLoadedCallback>> m_dataLoadedSubscriptions;
    QHash<quint64, SubscriptionRecord<DataAboutToUnloadCallback>> m_dataUnloadSubscriptions;
    QHash<quint64, SubscriptionRecord<ColumnAddedCallback>> m_columnAddedSubscriptions;
};
