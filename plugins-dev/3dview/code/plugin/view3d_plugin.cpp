#include "view3d_plugin.h"

#include "view3d_widget.h"

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
    if (!host || host->sdkVersion() != kViewerPluginSdkVersion || !host->ui())
        return false;

    m_host = host;
    PluginMenuItemSpec show;
    show.id = QStringLiteral("show");
    show.type = PluginMenuItemType::Action;
    show.text = QString::fromUtf8(u8"显示 3D 视图");
    m_menu = m_host->ui()->addPluginMenu(
        id(), name(), {show},
        [this](const QString& itemId, bool)
        {
            if (itemId == QStringLiteral("show"))
                showView();
        });
    return m_menu != 0;
}

void View3DPlugin::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    m_host = nullptr;
    m_menu = 0;
    m_viewDock = 0;
    m_view = nullptr;
}

void View3DPlugin::showView()
{
    if (!m_host || m_shuttingDown)
        return;

    if (!m_view)
    {
        auto* view = new View3DWidget;
        m_viewDock = m_host->ui()->createDock(
            id(), QStringLiteral("main"), QString::fromUtf8(u8"3D 视图"),
            view, DockArea::Right);
        if (!m_viewDock)
        {
            delete view;
            m_host->ui()->showError(
                name(), QString::fromUtf8(u8"无法创建 3D 视图停靠窗口。"));
            return;
        }
        m_view = view;
    }

    m_host->ui()->showDock(m_viewDock);
}

#include "moc_view3d_plugin.cpp"
