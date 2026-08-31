#pragma once

#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_toolbar_sdk.h"

#include <QHash>
#include <QPointer>

class QAction;
class PluginHost;
class QToolBar;

class PluginToolbarService final
    : public QObject
    , public viewer::plugin::IPluginToolbarService
{
    Q_OBJECT
    Q_INTERFACES(viewer::plugin::IPluginToolbarService)

public:
    PluginToolbarService(PluginHost* host, QToolBar* toolBar,
                         QObject* parent = nullptr);

    viewer::plugin::PluginToolbarButtonHandle addMenuItemButton(
        const QString& ownerPluginId,
        viewer::plugin::PluginMenuHandle menu,
        const QString& itemId,
        const viewer::plugin::PluginToolbarButtonSpec& spec) override;
    bool updateButton(
        viewer::plugin::PluginToolbarButtonHandle button,
        const viewer::plugin::PluginToolbarButtonSpec& spec) override;
    void removeButton(
        viewer::plugin::PluginToolbarButtonHandle button) override;

    void removeOwnedButtons(const QString& ownerPluginId);

private:
    struct ButtonRecord
    {
        QString ownerPluginId;
        viewer::plugin::PluginMenuHandle menu = 0;
        QString itemId;
        QPointer<QAction> action;
        viewer::plugin::PluginToolbarButtonSpec spec;
        QIcon originalIcon;
        QString originalToolTip;
    };

    QIcon resolvedIcon(
        const viewer::plugin::PluginToolbarButtonSpec& spec) const;
    QIcon placeholderIcon(const QString& text) const;
    void applyRecord(ButtonRecord& record);
    void rebuildOrder();
    void removeSeparatorIfUnused();

    QPointer<PluginHost> m_host;
    QPointer<QToolBar> m_toolBar;
    QPointer<QAction> m_separator;
    QHash<viewer::plugin::PluginToolbarButtonHandle, ButtonRecord> m_buttons;
    viewer::plugin::PluginToolbarButtonHandle m_nextHandle = 1;
};
