#include "code_plugin/PluginManager.h"

#include "code_plugin/PluginHost.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPluginLoader>

#include <algorithm>
#include <functional>

using namespace viewer::plugin;

PluginManager::PluginManager(PluginHost& host)
    : m_host(host)
{
}

PluginManager::~PluginManager()
{
    shutdownAll();
}

void PluginManager::loadFromDirectories(const QStringList& directories)
{
    if (!m_loaded.empty() || m_shutDown)
        return;

    auto manifests = discover(directories);
    const auto order = resolveLoadOrder(manifests);
    for (int index : order)
    {
        if (index < 0 || index >= static_cast<int>(manifests.size()))
            continue;
        auto& manifest = manifests[static_cast<size_t>(index)];
        if (!manifest.enabled)
            continue;

        bool requiredDependenciesStarted = true;
        for (const auto& dependency : manifest.dependencies)
        {
            if (dependency.mode == QStringLiteral("required")
                && m_host.pluginState(dependency.id) != PluginState::Started)
            {
                requiredDependenciesStarted = false;
                m_host.setPluginState(manifest.id, PluginState::Failed);
                log(manifest.id, LogLevel::Error,
                    QStringLiteral("Required plugin dependency did not start: %1")
                        .arg(dependency.id));
                break;
            }
        }
        if (requiredDependenciesStarted)
            loadOne(manifest);
    }
    log(QStringLiteral("host"), LogLevel::Info,
        QStringLiteral("Plugin scan completed: discovered=%1 started=%2")
            .arg(manifests.size()).arg(m_loaded.size()));
}

void PluginManager::shutdownAll()
{
    if (m_shutDown)
        return;
    m_shutDown = true;

    for (auto it = m_loaded.rbegin(); it != m_loaded.rend(); ++it)
    {
        if (it->initialized && it->plugin)
        {
            try
            {
                it->plugin->shutdown();
            }
            catch (...)
            {
                log(it->manifest.id, LogLevel::Error,
                    QStringLiteral("Unhandled exception while shutting down plugin."));
            }
        }

        // Destroy callbacks, docks and services while the plugin code is still loaded.
        m_host.removeOwnedResources(it->manifest.id);
        m_host.setPluginState(it->manifest.id, PluginState::Discovered);
        it->plugin = nullptr;
        it->instance = nullptr;
        if (it->loader && !it->loader->unload())
        {
            log(it->manifest.id, LogLevel::Warning,
                QStringLiteral("Plugin DLL could not be unloaded: %1")
                    .arg(it->loader->errorString()));
        }
    }
    m_loaded.clear();
    m_host.beginShutdown();
}

std::vector<PluginManager::Manifest> PluginManager::discover(
    const QStringList& directories)
{
    std::vector<Manifest> manifests;
    QHash<QString, QString> manifestById;

    for (const QString& directory : directories)
    {
        QDirIterator iterator(directory,
                              QStringList{QStringLiteral("plugin.json")},
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            const QString manifestPath = iterator.next();
            Manifest manifest;
            QString error;
            if (!parseManifest(manifestPath, manifest, error))
            {
                log(QStringLiteral("discovery"), LogLevel::Error,
                    QStringLiteral("Invalid plugin manifest %1: %2")
                        .arg(manifestPath, error));
                continue;
            }
            if (manifestById.contains(manifest.id))
            {
                log(manifest.id, LogLevel::Error,
                    QStringLiteral("Duplicate plugin id in %1 and %2; the latter is ignored.")
                        .arg(manifestById.value(manifest.id), manifestPath));
                continue;
            }
            manifestById.insert(manifest.id, manifestPath);
            m_host.setPluginState(manifest.id, PluginState::Discovered);
            manifests.push_back(std::move(manifest));
        }
    }
    return manifests;
}

bool PluginManager::parseManifest(const QString& path,
                                  Manifest& manifest,
                                  QString& error) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error = parseError.errorString();
        return false;
    }

    const QJsonObject object = document.object();
    manifest.manifestPath = QFileInfo(path).absoluteFilePath();
    manifest.rootDirectory = QFileInfo(path).absolutePath();
    manifest.id = object.value(QStringLiteral("id")).toString().trimmed();
    manifest.name = object.value(QStringLiteral("name")).toString().trimmed();
    manifest.version = object.value(QStringLiteral("version")).toString().trimmed();
    manifest.entry = object.value(QStringLiteral("entry")).toString().trimmed();
    manifest.debugEntry = object.value(QStringLiteral("debugEntry")).toString().trimmed();
    manifest.sdkVersion = object.value(QStringLiteral("sdkVersion")).toInt(0);
    if (manifest.id.isEmpty() || manifest.entry.isEmpty())
    {
        error = QStringLiteral("Fields 'id' and 'entry' are required.");
        return false;
    }
    if (manifest.sdkVersion != kViewerPluginSdkVersion)
    {
        error = QStringLiteral("Unsupported SDK version %1 (Viewer supports %2).")
                    .arg(manifest.sdkVersion).arg(kViewerPluginSdkVersion);
        return false;
    }

    const QJsonArray dependencies = object.value(QStringLiteral("dependencies")).toArray();
    for (const auto& value : dependencies)
    {
        const QJsonObject dependencyObject = value.toObject();
        Dependency dependency;
        dependency.id = dependencyObject.value(QStringLiteral("id")).toString().trimmed();
        dependency.mode = dependencyObject.value(QStringLiteral("mode"))
                              .toString(QStringLiteral("runtime")).trimmed().toLower();
        if (!dependency.id.isEmpty())
            manifest.dependencies.push_back(std::move(dependency));
    }
    return true;
}

std::vector<int> PluginManager::resolveLoadOrder(std::vector<Manifest>& manifests)
{
    QHash<QString, int> byId;
    for (int i = 0; i < static_cast<int>(manifests.size()); ++i)
        byId.insert(manifests[static_cast<size_t>(i)].id, i);

    for (auto& manifest : manifests)
    {
        for (const auto& dependency : manifest.dependencies)
        {
            if (!byId.contains(dependency.id)
                && dependency.mode.compare(QStringLiteral("required"), Qt::CaseInsensitive) == 0)
            {
                manifest.enabled = false;
                m_host.setPluginState(manifest.id, PluginState::Failed);
                log(manifest.id, LogLevel::Error,
                    QStringLiteral("Required plugin dependency is missing: %1")
                        .arg(dependency.id));
            }
            else if (!byId.contains(dependency.id))
            {
                log(manifest.id, LogLevel::Warning,
                    QStringLiteral("Runtime plugin dependency is not installed: %1")
                        .arg(dependency.id));
            }
        }
    }

    std::vector<int> state(manifests.size(), 0);
    std::vector<int> order;
    std::function<bool(int)> visit = [&](int index) -> bool
    {
        if (state[static_cast<size_t>(index)] == 2)
            return true;
        if (state[static_cast<size_t>(index)] == 1)
        {
            manifests[static_cast<size_t>(index)].enabled = false;
            m_host.setPluginState(manifests[static_cast<size_t>(index)].id, PluginState::Failed);
            log(manifests[static_cast<size_t>(index)].id, LogLevel::Error,
                QStringLiteral("Cyclic plugin dependency detected."));
            return false;
        }
        state[static_cast<size_t>(index)] = 1;
        bool valid = true;
        for (const auto& dependency : manifests[static_cast<size_t>(index)].dependencies)
        {
            const auto depIt = byId.constFind(dependency.id);
            if (depIt != byId.constEnd())
                valid = visit(depIt.value()) && valid;
        }
        state[static_cast<size_t>(index)] = 2;
        if (!valid)
            manifests[static_cast<size_t>(index)].enabled = false;
        order.push_back(index);
        return valid;
    };

    for (int i = 0; i < static_cast<int>(manifests.size()); ++i)
        visit(i);
    return order;
}

bool PluginManager::loadOne(const Manifest& manifest)
{
#ifdef _DEBUG
    const QString selectedEntry = manifest.debugEntry.isEmpty()
        ? manifest.entry : manifest.debugEntry;
#else
    const QString selectedEntry = manifest.entry;
#endif
    const QString entryPath = QDir(manifest.rootDirectory).absoluteFilePath(selectedEntry);
    const QFileInfo entryInfo(entryPath);
    if (!entryInfo.exists() || !entryInfo.isFile())
    {
        m_host.setPluginState(manifest.id, PluginState::Failed);
        log(manifest.id, LogLevel::Error,
            QStringLiteral("Plugin entry DLL does not exist: %1").arg(entryPath));
        return false;
    }

    LoadedPlugin loaded;
    loaded.manifest = manifest;
    loaded.loader = std::make_unique<QPluginLoader>(entryInfo.absoluteFilePath());
    QObject* instance = loaded.loader->instance();
    if (!instance)
    {
        m_host.setPluginState(manifest.id, PluginState::Failed);
        log(manifest.id, LogLevel::Error,
            QStringLiteral("Plugin DLL failed to load: %1").arg(loaded.loader->errorString()));
        return false;
    }

    auto* plugin = qobject_cast<IViewerPlugin*>(instance);
    if (!plugin)
    {
        m_host.setPluginState(manifest.id, PluginState::Failed);
        log(manifest.id, LogLevel::Error,
            QStringLiteral("DLL does not implement the Viewer plugin interface."));
        loaded.loader->unload();
        return false;
    }
    if (plugin->id() != manifest.id)
    {
        m_host.setPluginState(manifest.id, PluginState::Failed);
        log(manifest.id, LogLevel::Error,
            QStringLiteral("Manifest id and plugin interface id do not match (%1).")
                .arg(plugin->id()));
        loaded.loader->unload();
        return false;
    }

    loaded.instance = instance;
    loaded.plugin = plugin;
    m_host.setPluginState(manifest.id, PluginState::Loaded);
    bool initialized = false;
    try
    {
        initialized = plugin->initialize(&m_host);
    }
    catch (...)
    {
        log(manifest.id, LogLevel::Error,
            QStringLiteral("Unhandled exception while initializing plugin."));
    }
    if (!initialized)
    {
        m_host.removeOwnedResources(manifest.id);
        m_host.setPluginState(manifest.id, PluginState::Failed);
        loaded.loader->unload();
        return false;
    }

    loaded.initialized = true;
    m_host.setPluginState(manifest.id, PluginState::Started);
    log(manifest.id, LogLevel::Info,
        QStringLiteral("Plugin started: %1 %2")
            .arg(plugin->name(), plugin->version()));
    m_loaded.push_back(std::move(loaded));
    return true;
}

void PluginManager::log(const QString& pluginId,
                        LogLevel level,
                        const QString& message) const
{
    m_host.write(pluginId, level, message);
}
