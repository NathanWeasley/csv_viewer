#include "viewer_plugin/viewer_plugin_sdk.h"

#include <QLabel>

class ExamplePlugin final
    : public QObject
    , public viewer::plugin::IViewerPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID VIEWER_PLUGIN_INTERFACE_IID FILE "plugin.json")
    Q_INTERFACES(viewer::plugin::IViewerPlugin)

public:
    QString id() const override { return QStringLiteral("com.weekendbuild.example"); }
    QString name() const override { return QStringLiteral("Viewer SDK Example"); }
    QString version() const override { return QStringLiteral("1.0.0"); }

    bool initialize(viewer::plugin::IViewerHost* host) override
    {
        m_host = host;
        m_action = host->ui()->addPluginAction(id(), QStringLiteral("Open SDK Example"),
            [this]()
            {
                if (!m_dock)
                {
                    auto* label = new QLabel(QStringLiteral("Viewer plugin SDK is ready."));
                    label->setAlignment(Qt::AlignCenter);
                    m_dock = m_host->ui()->createDock(
                        id(), QStringLiteral("main"), name(), label,
                        viewer::plugin::DockArea::Right);
                }
                m_host->ui()->showDock(m_dock);
            });
        return m_action != 0;
    }

    void shutdown() override
    {
        m_host = nullptr;
        m_action = 0;
        m_dock = 0;
    }

private:
    viewer::plugin::IViewerHost* m_host = nullptr;
    viewer::plugin::PluginActionHandle m_action = 0;
    viewer::plugin::PluginDockHandle m_dock = 0;
};

#include "ExamplePlugin.moc"

