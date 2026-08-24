#include "log_expand_plugin.h"

using namespace viewer::plugin;

namespace
{

const QString kDatDecryptPluginId = QStringLiteral("dat_decrypt");

} // namespace

QString LogExpandPlugin::id() const
{
    return QStringLiteral("log_expand");
}

QString LogExpandPlugin::name() const
{
    return QStringLiteral("log_expand");
}

QString LogExpandPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

bool LogExpandPlugin::initialize(IViewerHost* host)
{
    if (!host || host->sdkVersion() != kViewerPluginSdkVersion
        || !host->data() || !host->events() || !host->jsonDocuments()
        || !host->plugins() || !host->ui() || !host->log())
    {
        return false;
    }
    if (host->plugins()->pluginState(kDatDecryptPluginId)
        != PluginState::Started)
    {
        host->log()->write(id(), LogLevel::Error,
            QStringLiteral("Required plugin 'dat_decrypt' is not started."));
        return false;
    }

    m_host = host;
    m_shuttingDown = false;

    PluginMenuItemSpec status;
    status.id = QStringLiteral("input_status");
    status.type = PluginMenuItemType::Action;
    status.text = QString::fromUtf8(u8"查看扩充输入状态");
    status.order = 10;
    status.enabled = false;
    m_menu = m_host->ui()->addPluginMenu(
        id(), name(), {status},
        [this](const QString& itemId, bool)
        {
            if (itemId == QStringLiteral("input_status"))
                showInputStatus();
        });

    m_dataLoadedSubscription = m_host->events()->subscribeDataLoaded(
        id(), [this](const LoadSessionInfo& session) { handleDataLoaded(session); });
    m_dataUnloadSubscription = m_host->events()->subscribeDataAboutToUnload(
        id(), [this](quint64 sessionId) { handleDataAboutToUnload(sessionId); });
    m_jsonChangedSubscription = m_host->jsonDocuments()->subscribeDocumentsChanged(
        id(), [this](quint64 sessionId, const QString& providerPluginId)
        {
            handleJsonDocumentsChanged(sessionId, providerPluginId);
        });

    if (!m_menu || !m_dataLoadedSubscription || !m_dataUnloadSubscription
        || !m_jsonChangedSubscription)
    {
        return false;
    }

    const LoadSessionInfo session = m_host->data()->currentSession();
    m_currentSessionId = session.isValid() ? session.sessionId : 0;
    refreshMenuState();
    log(LogLevel::Info, QStringLiteral("log_expand initialized."));
    return true;
}

void LogExpandPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (m_host)
    {
        if (m_dataLoadedSubscription)
            m_host->events()->unsubscribe(m_dataLoadedSubscription);
        if (m_dataUnloadSubscription)
            m_host->events()->unsubscribe(m_dataUnloadSubscription);
        if (m_jsonChangedSubscription)
            m_host->jsonDocuments()->unsubscribeDocumentsChanged(
                m_jsonChangedSubscription);
    }
    m_dataLoadedSubscription = 0;
    m_dataUnloadSubscription = 0;
    m_jsonChangedSubscription = 0;
    m_currentSessionId = 0;
    m_menu = 0;
    m_host = nullptr;
}

void LogExpandPlugin::handleDataLoaded(const LoadSessionInfo& session)
{
    m_currentSessionId = session.isValid() ? session.sessionId : 0;
    refreshMenuState();
}

void LogExpandPlugin::handleDataAboutToUnload(quint64 sessionId)
{
    if (sessionId != m_currentSessionId)
        return;
    m_currentSessionId = 0;
    refreshMenuState();
}

void LogExpandPlugin::handleJsonDocumentsChanged(
    quint64 sessionId, const QString& providerPluginId)
{
    if (sessionId != m_currentSessionId)
        return;
    if (providerPluginId.isEmpty() || providerPluginId == kDatDecryptPluginId)
        refreshMenuState();
}

void LogExpandPlugin::showInputStatus()
{
    if (!m_host || !m_currentSessionId)
        return;
    const QList<JsonDocumentInfo> documents =
        m_host->jsonDocuments()->listDocuments(
            m_currentSessionId, kDatDecryptPluginId);
    int readyCount = 0;
    for (const JsonDocumentInfo& document : documents)
        readyCount += document.isReady() ? 1 : 0;
    m_host->ui()->showInformation(
        name(),
        QString::fromUtf8(u8"dat_decrypt 已提供 %1 个可用 JSON 输入。\n"
                          u8"扩充计算规则将在后续功能规格中定义。")
            .arg(readyCount));
}

void LogExpandPlugin::refreshMenuState()
{
    if (!m_host || !m_menu)
        return;
    m_host->ui()->setPluginMenuItemEnabled(
        m_menu, QStringLiteral("input_status"), hasReadyDatDecryptInput());
}

bool LogExpandPlugin::hasReadyDatDecryptInput() const
{
    if (!m_host || !m_currentSessionId)
        return false;
    const QList<JsonDocumentInfo> documents =
        m_host->jsonDocuments()->listDocuments(
            m_currentSessionId, kDatDecryptPluginId);
    for (const JsonDocumentInfo& document : documents)
    {
        if (document.isReady())
            return true;
    }
    return false;
}

void LogExpandPlugin::log(LogLevel level, const QString& message) const
{
    if (m_host)
        m_host->log()->write(id(), level, message);
}

#include "moc_log_expand_plugin.cpp"
