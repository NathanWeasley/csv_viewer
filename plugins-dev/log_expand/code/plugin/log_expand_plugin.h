#pragma once

#include "log_expand_types.h"
#include "viewer_plugin/viewer_expression_data_sdk.h"
#include "viewer_plugin/viewer_plugin_sdk.h"
#include "viewer_plugin/viewer_toolbar_sdk.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QThreadPool>

#include <atomic>

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
    void createMenu();
    void createToolbarButtons();
    void handleMenuCommand(const QString& itemId);
    void handleDataLoaded(const viewer::plugin::LoadSessionInfo& session);
    void handleDataAboutToUnload(quint64 sessionId);
    void handleJsonDocumentsChanged(
        quint64 sessionId, const QString& providerPluginId);

    bool loadExpansionDefinitions();
    bool saveExpansionDefinitions(
        const QList<ExpansionDefinition>& definitions);
    bool refreshMappings(bool scheduleCalculation);
    void scheduleRecompute(const QString& reason);
    void finishRecompute(
        quint64 generation,
        viewer::plugin::PluginProgressHandle progress,
        const viewer::plugin::DerivedColumnBatchWriterPtr& writer,
        QList<ExpansionResult> results);

    void showMappedVariables();
    void editExpansionDefinitions();
    void showDiagnostics();
    void refreshMenuState();
    void log(viewer::plugin::LogLevel level, const QString& message) const;

    QList<PluginDiagnostic> allDiagnostics() const;
    QHash<QString, QString> lastStatusMap() const;

    viewer::plugin::IViewerHost* m_host = nullptr;
    viewer::plugin::IExpressionDataService* m_expressionData = nullptr;
    viewer::plugin::IPluginToolbarService* m_toolbarService = nullptr;
    viewer::plugin::PluginMenuHandle m_menu = 0;
    viewer::plugin::PluginToolbarButtonHandle m_mappedVariablesToolbarButton = 0;
    viewer::plugin::PluginToolbarButtonHandle m_editExpansionsToolbarButton = 0;
    viewer::plugin::SubscriptionId m_dataLoadedSubscription = 0;
    viewer::plugin::SubscriptionId m_dataUnloadSubscription = 0;
    viewer::plugin::JsonSubscriptionId m_jsonChangedSubscription = 0;
    viewer::plugin::PluginProgressHandle m_progress = 0;

    QString m_mappingPath;
    QString m_expressionsPath;
    QList<ExpansionDefinition> m_expansionDefinitions;
    QList<MappedVariable> m_mappedVariables;
    QList<PluginDiagnostic> m_mappingDiagnostics;
    QList<PluginDiagnostic> m_configDiagnostics;
    QList<ExpansionResult> m_expansionResults;
    QStringList m_publishedNames;

    QThreadPool m_workerPool;
    std::atomic<quint64> m_generation{0};
    quint64 m_currentSessionId = 0;
    bool m_datBatchAvailable = false;
    bool m_mappingConfigValid = false;
    bool m_expressionConfigValid = false;
    bool m_shuttingDown = false;
};
