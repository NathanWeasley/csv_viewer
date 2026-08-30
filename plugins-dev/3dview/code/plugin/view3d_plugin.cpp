#include "view3d_plugin.h"

#include "view3d_widget.h"

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>

using namespace viewer::plugin;

QString View3DPlugin::id() const
{
    return QStringLiteral("3dview");
}

QString View3DPlugin::name() const
{
    return QStringLiteral("3dview");
}

QString View3DPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

bool View3DPlugin::initialize(IViewerHost* host)
{
    if (!host || host->sdkVersion() != kViewerPluginSdkVersion
        || !host->data() || !host->events() || !host->ui())
        return false;

    m_host = host;
    m_shuttingDown = false;
    m_refreshDataPending = false;
    QString rootDirectory = property(kPluginRootDirectoryProperty).toString();
    if (rootDirectory.isEmpty())
        rootDirectory = QCoreApplication::applicationDirPath();
    m_configPath = QDir(rootDirectory).absoluteFilePath(
        QStringLiteral("data/3dview.json"));
    m_statePath = QDir(rootDirectory).absoluteFilePath(
        QStringLiteral("3dview.ini"));

    PluginMenuItemSpec show;
    show.id = QStringLiteral("show");
    show.type = PluginMenuItemType::Action;
    show.text = QString::fromUtf8(u8"显示 3D 视图");
    show.order = 10;
    PluginMenuItemSpec separator;
    separator.id = QStringLiteral("separator.config");
    separator.type = PluginMenuItemType::Separator;
    separator.order = 20;
    PluginMenuItemSpec reload;
    reload.id = QStringLiteral("reload_config");
    reload.type = PluginMenuItemType::Action;
    reload.text = QString::fromUtf8(u8"重新加载配置");
    reload.order = 30;
    m_menu = m_host->ui()->addPluginMenu(
        id(), name(), {show, separator, reload},
        [this](const QString& itemId, bool)
        {
            if (itemId == QStringLiteral("show"))
                showView();
            else if (itemId == QStringLiteral("reload_config"))
                reloadConfiguration();
        });

    m_dataLoadedSubscription = m_host->events()->subscribeDataLoaded(
        id(), [this](const LoadSessionInfo&) { refreshData(); });
    m_dataUnloadingSubscription = m_host->events()->subscribeDataAboutToUnload(
        id(), [this](quint64)
        {
            if (m_view)
                m_view->clearData();
        });
    m_columnAddedSubscription = m_host->events()->subscribeColumnAdded(
        id(), [this](quint64, const QString&) { scheduleRefreshData(); });
    return m_menu != 0
        && m_dataLoadedSubscription != 0
        && m_dataUnloadingSubscription != 0
        && m_columnAddedSubscription != 0;
}

void View3DPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    if (m_host && m_host->events())
    {
        if (m_dataLoadedSubscription)
            m_host->events()->unsubscribe(m_dataLoadedSubscription);
        if (m_dataUnloadingSubscription)
            m_host->events()->unsubscribe(m_dataUnloadingSubscription);
        if (m_columnAddedSubscription)
            m_host->events()->unsubscribe(m_columnAddedSubscription);
    }
    m_dataLoadedSubscription = 0;
    m_dataUnloadingSubscription = 0;
    m_columnAddedSubscription = 0;
    m_refreshDataPending = false;
    if (m_view)
    {
        View3DWidget* view = m_view.data();
        m_view = nullptr;
        delete view;
    }
    m_host = nullptr;
    m_menu = 0;
}

void View3DPlugin::showView()
{
    if (!m_host || m_shuttingDown)
        return;

    if (!m_view)
    {
        auto* view = new View3DWidget(
            m_configPath, m_statePath);
        view->setWindowTitle(QString::fromUtf8(u8"3D 视图"));
        m_view = view;
        refreshData();
    }

    if (m_view->isMinimized())
        m_view->showNormal();
    else
        m_view->show();
    m_view->raise();
    m_view->activateWindow();
}

void View3DPlugin::reloadConfiguration()
{
    showView();
    if (m_view && !m_view->reloadConfiguration() && m_host)
        m_host->ui()->showError(
            QString::fromUtf8(u8"3D 视图配置错误"), m_view->lastError());
}

void View3DPlugin::refreshData()
{
    if (!m_host || !m_view || !m_host->data())
        return;
    m_view->setDataSnapshot(m_host->data()->acquireSnapshot());
}

void View3DPlugin::scheduleRefreshData()
{
    if (m_refreshDataPending || m_shuttingDown || !m_view)
        return;

    // 批量发布仍会逐列发送兼容事件；合并为一次快照刷新，避免重复构建 3D 数据视图。
    m_refreshDataPending = true;
    QMetaObject::invokeMethod(this, [this]()
    {
        m_refreshDataPending = false;
        if (!m_shuttingDown)
            refreshData();
    }, Qt::QueuedConnection);
}

#include "moc_view3d_plugin.cpp"
