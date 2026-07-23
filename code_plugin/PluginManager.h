#pragma once

#include "sdk/viewer_plugin_sdk/include/viewer_plugin/viewer_plugin_sdk.h"

#include <QJsonObject>
#include <QStringList>

#include <memory>
#include <vector>

#include "code_plugin/export.h"

class PluginHost;
class QPluginLoader;

class PM_API PluginManager final
{
public:
    explicit PluginManager(PluginHost& host);
    ~PluginManager();

    // PluginManager uniquely owns loaded DLL handles and is bound to one host.
    // Explicitly deleting these operations also prevents MSVC from trying to
    // export an implicit copy constructor for vector<LoadedPlugin>, whose
    // elements intentionally contain a non-copyable unique_ptr<QPluginLoader>.
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    PluginManager(PluginManager&&) = delete;
    PluginManager& operator=(PluginManager&&) = delete;

    void loadFromDirectories(const QStringList& directories);
    void shutdownAll();

private:
    struct Dependency
    {
        QString id;
        QString mode;
    };
    struct Manifest
    {
        QString manifestPath;
        QString rootDirectory;
        QString id;
        QString name;
        QString version;
        QString entry;
        int sdkVersion = 0;
        std::vector<Dependency> dependencies;
        bool enabled = true;
    };
    struct LoadedPlugin
    {
        Manifest manifest;
        std::unique_ptr<QPluginLoader> loader;
        QObject* instance = nullptr;
        viewer::plugin::IViewerPlugin* plugin = nullptr;
        bool initialized = false;
    };

    std::vector<Manifest> discover(const QStringList& directories);
    bool parseManifest(const QString& path, Manifest& manifest, QString& error) const;
    std::vector<int> resolveLoadOrder(std::vector<Manifest>& manifests);
    bool loadOne(const Manifest& manifest);
    void log(const QString& pluginId,
             viewer::plugin::LogLevel level,
             const QString& message) const;

    PluginHost& m_host;
    std::vector<LoadedPlugin> m_loaded;
    bool m_shutDown = false;
};
