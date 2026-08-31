#pragma once

#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_plugin_sdk.h"
#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_expression_data_sdk.h"
#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_toolbar_sdk.h"

#include <QHash>
#include <QPointer>
#include <QSet>

#include <functional>
#include <memory>

#include "code_plugin/export.h"

class QAction;
class QMainWindow;
class QMenu;
class CoreExpressionDataService;
class DerivedColumnBatchWriterImpl;
class PluginToolbarService;
class QToolBar;

namespace ads
{
class CDockManager;
class CDockWidget;
}

namespace viewer
{
class Viewer;
}

class PM_API PluginHost final
    : public QObject
    , public viewer::plugin::IViewerHost
    , public viewer::plugin::IDataService
    , public viewer::plugin::IArchiveService
    , public viewer::plugin::IJsonDocumentService
    , public viewer::plugin::IEventService
    , public viewer::plugin::IPluginRegistry
    , public viewer::plugin::IUiService
    , public viewer::plugin::ILogService
{
public:
    using PluginProgressCallback = std::function<void(
        viewer::plugin::PluginProgressHandle,
        const QString&,
        const QString&,
        float,
        const QString&,
        bool)>;

    PluginHost(viewer::Viewer& viewer,
               QMainWindow* mainWindow,
               ads::CDockManager* dockManager,
               QMenu* pluginMenu,
               QToolBar* pluginToolBar,
               std::function<void()> rebuildDataTree,
               std::function<void(const QStringList&,
                                  const QStringList&,
                                  const QStringList&)> refreshDataColumns,
               PluginProgressCallback pluginProgress,
               QObject* parent = nullptr);
    ~PluginHost() override;

    // IViewerHost
    int sdkVersion() const noexcept override;
    viewer::plugin::IDataService* data() noexcept override { return this; }
    viewer::plugin::IArchiveService* archive() noexcept override { return this; }
    viewer::plugin::IJsonDocumentService* jsonDocuments() noexcept override { return this; }
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

    // IJsonDocumentService
    viewer::plugin::JsonPublishResult publishBatch(
        const QString& providerPluginId,
        quint64 sessionId,
        const QList<viewer::plugin::JsonDocumentPublishItem>& documents) override;
    QList<viewer::plugin::JsonDocumentInfo> listDocuments(
        quint64 sessionId,
        const QString& providerPluginId = {}) const override;
    viewer::plugin::JsonDocumentPtr acquireDocument(
        quint64 sessionId,
        const QString& providerPluginId,
        const QString& documentId,
        viewer::plugin::JsonDocumentInfo* info = nullptr) const override;
    viewer::plugin::JsonSubscriptionId subscribeDocumentsChanged(
        const QString& ownerPluginId,
        viewer::plugin::JsonDocumentsChangedCallback callback) override;
    void unsubscribeDocumentsChanged(
        viewer::plugin::JsonSubscriptionId subscription) override;

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
    viewer::plugin::PluginMenuHandle addPluginMenu(
        const QString& ownerPluginId,
        const QString& rootTitle,
        const QList<viewer::plugin::PluginMenuItemSpec>& items,
        viewer::plugin::PluginMenuCallback callback) override;
    bool setPluginMenuItemEnabled(
        viewer::plugin::PluginMenuHandle menu,
        const QString& itemId,
        bool enabled) override;
    bool setPluginMenuItemChecked(
        viewer::plugin::PluginMenuHandle menu,
        const QString& itemId,
        bool checked) override;
    bool setPluginMenuItemVisible(
        viewer::plugin::PluginMenuHandle menu,
        const QString& itemId,
        bool visible) override;
    viewer::plugin::PluginDockHandle createDock(
        const QString& ownerPluginId,
        const QString& dockId,
        const QString& title,
        QWidget* content,
        viewer::plugin::DockArea preferredArea) override;
    bool showDock(viewer::plugin::PluginDockHandle dock) override;
    bool closeDock(viewer::plugin::PluginDockHandle dock) override;
    viewer::plugin::PluginProgressHandle beginLoadProgress(
        const QString& ownerPluginId,
        quint64 sessionId,
        const QString& title) override;
    bool reportLoadProgress(
        viewer::plugin::PluginProgressHandle progress,
        float value,
        const QString& detail = {}) override;
    void finishLoadProgress(
        viewer::plugin::PluginProgressHandle progress) override;
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
    friend class DerivedColumnBatchWriterImpl;
    friend class CoreExpressionDataService;
    friend class PluginToolbarService;

    viewer::plugin::DerivedColumnBatchCreateResult createDerivedColumnBatch(
        const QString& providerPluginId,
        quint64 sessionId,
        const QStringList& columnNames,
        qsizetype rowCount);
    viewer::plugin::DerivedColumnBatchCommitResult commitDerivedColumnBatch(
        const QString& providerPluginId,
        quint64 sessionId,
        QStringList names,
        std::vector<std::vector<double>> values);

    viewer::plugin::DataCommitResult commitDerivedColumn(
        quint64 sessionId,
        QString name,
        std::vector<double>&& values);
    QString serviceKey(const QString& providerPluginId, const QString& serviceId) const;
    QAction* pluginMenuCommandAction(
        const QString& ownerPluginId,
        viewer::plugin::PluginMenuHandle menu,
        const QString& itemId) const;
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
    struct MenuRecord
    {
        QString ownerPluginId;
        QPointer<QMenu> root;
        QHash<QString, QPointer<QAction>> actions;
        QSet<QString> commandIds;
    };
    struct DockRecord
    {
        QString ownerPluginId;
        QPointer<ads::CDockWidget> dock;
    };
    struct ProgressRecord
    {
        QString ownerPluginId;
        quint64 sessionId = 0;
        QString title;
        QString detail;
        float value = 0.0f;
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
    std::function<void(const QStringList&,
                       const QStringList&,
                       const QStringList&)> m_refreshDataColumns;
    // 批量发布已经通过 DataColumnsChanged 刷新过界面，随后逐列信号只用于插件订阅。
    QSet<QString> m_columnsRefreshedByBatch;
    PluginProgressCallback m_pluginProgress;
    bool m_shuttingDown = false;
    quint64 m_nextHandle = 1;

    QHash<QString, viewer::plugin::PluginState> m_pluginStates;
    QHash<QString, ServiceRecord> m_services;
    QPointer<CoreExpressionDataService> m_coreExpressionDataService;
    QPointer<PluginToolbarService> m_pluginToolbarService;
    QHash<quint64, ActionRecord> m_actions;
    QHash<quint64, MenuRecord> m_menus;
    QHash<quint64, DockRecord> m_docks;
    QHash<quint64, ProgressRecord> m_progressRecords;
    QHash<quint64, SubscriptionRecord<DataLoadedCallback>> m_dataLoadedSubscriptions;
    QHash<quint64, SubscriptionRecord<DataAboutToUnloadCallback>> m_dataUnloadSubscriptions;
    QHash<quint64, SubscriptionRecord<ColumnAddedCallback>> m_columnAddedSubscriptions;
    QHash<quint64, SubscriptionRecord<viewer::plugin::JsonDocumentsChangedCallback>>
        m_jsonChangedSubscriptions;
};
