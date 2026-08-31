#pragma once

#include "viewer_plugin/viewer_plugin_sdk.h"
#include "viewer_plugin/viewer_toolbar_sdk.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QThreadPool>

#include <atomic>

class DatJsonViewer;

class DatDecryptPlugin final
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
    struct InputItem
    {
        QString archiveName;
        QString expectedTable;
        QString documentId;
        QString displayName;
        QString sourceEntryPath;
        bool present = false;
        viewer::plugin::JsonDocumentState unavailableState =
            viewer::plugin::JsonDocumentState::Missing;
        QString inputError;
        QSharedPointer<const QByteArray> bytes;
    };

    struct ConversionBatch
    {
        QList<viewer::plugin::JsonDocumentPublishItem> documents;
        int readyCount = 0;
        int failedCount = 0;
    };

    void createMenu();
    void createToolbarButtons();
    void handleMenuCommand(const QString& itemId, bool checked);
    void handleDataLoaded(const viewer::plugin::LoadSessionInfo& session);
    void handleDataAboutToUnload(quint64 sessionId);
    void startConversion(bool userInitiated);
    QList<InputItem> collectInputs(quint64 sessionId) const;
    ConversionBatch convertInputs(
        const QList<InputItem>& inputs,
        bool strictSchema,
        bool preserveUnknownFields,
        quint64 generation) const;
    void finishConversion(
        quint64 sessionId,
        quint64 generation,
        bool userInitiated,
        ConversionBatch batch);

    void showJsonViewer();
    void refreshJsonViewer();
    void exportJson();
    void validateJson();
    void refreshMenuState();
    void loadSettings();
    void saveSettings() const;
    void log(viewer::plugin::LogLevel level, const QString& message) const;

    viewer::plugin::IViewerHost* m_host = nullptr;
    viewer::plugin::IPluginToolbarService* m_toolbarService = nullptr;
    viewer::plugin::PluginMenuHandle m_menu = 0;
    viewer::plugin::PluginToolbarButtonHandle m_viewToolbarButton = 0;
    viewer::plugin::PluginToolbarButtonHandle m_exportToolbarButton = 0;
    viewer::plugin::PluginDockHandle m_jsonDock = 0;
    viewer::plugin::SubscriptionId m_dataLoadedSubscription = 0;
    viewer::plugin::SubscriptionId m_dataUnloadSubscription = 0;
    viewer::plugin::JsonSubscriptionId m_jsonChangedSubscription = 0;
    QPointer<DatJsonViewer> m_jsonViewer;
    QThreadPool m_workers;
    std::atomic<quint64> m_generation{0};
    quint64 m_currentSessionId = 0;
    bool m_autoConvert = true;
    bool m_strictSchema = false;
    bool m_preserveUnknownFields = true;
    bool m_conversionRunning = false;
    bool m_shuttingDown = false;
};
