#pragma once

#include "viewer_plugin_sdk.h"

#include <QIcon>

namespace viewer::plugin
{

inline constexpr const char* kPluginToolbarProviderId = "viewer.core";
inline constexpr const char* kPluginToolbarServiceId = "plugin-toolbar";
inline constexpr int kPluginToolbarServiceVersion = 1;

using PluginToolbarButtonHandle = quint64;

enum class PluginToolbarButtonStyle
{
    IconOnly,
    TextOnly,
    TextBesideIcon,
    TextUnderIcon
};

// Describes the toolbar presentation of an existing plugin menu command.
// The command callback and its enabled/visible/checked state remain owned by
// the menu QAction. A null icon may be used during development together with
// placeholderText; production plugins should supply their own icon assets.
struct PluginToolbarButtonSpec
{
    QIcon icon;
    QIcon darkIcon;
    QString placeholderText;
    QString buttonText;
    QString toolTip;
    QString styleSheet;
    PluginToolbarButtonStyle style = PluginToolbarButtonStyle::IconOnly;
    int order = 0;
};

class IPluginToolbarService
{
public:
    virtual ~IPluginToolbarService() = default;

    virtual PluginToolbarButtonHandle addMenuItemButton(
        const QString& ownerPluginId,
        PluginMenuHandle menu,
        const QString& itemId,
        const PluginToolbarButtonSpec& spec) = 0;
    virtual bool updateButton(
        PluginToolbarButtonHandle button,
        const PluginToolbarButtonSpec& spec) = 0;
    virtual void removeButton(PluginToolbarButtonHandle button) = 0;
};

} // namespace viewer::plugin

#define VIEWER_PLUGIN_TOOLBAR_SERVICE_IID \
    "org.nathan.viewer.IPluginToolbarService/1.0"
Q_DECLARE_INTERFACE(viewer::plugin::IPluginToolbarService,
                    VIEWER_PLUGIN_TOOLBAR_SERVICE_IID)
