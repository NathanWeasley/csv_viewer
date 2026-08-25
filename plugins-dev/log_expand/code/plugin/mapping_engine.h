#pragma once

#include "log_expand_types.h"
#include "viewer_plugin/viewer_json_sdk.h"

#include <QHash>
#include <QList>
#include <QString>

class MappingEngine
{
public:
    static bool loadDefinitions(
        const QString& filePath,
        QList<MappingDefinition>& definitions,
        QList<PluginDiagnostic>& diagnostics);

    static QList<MappedVariable> resolve(
        const QList<MappingDefinition>& definitions,
        const QHash<QString, viewer::plugin::JsonDocumentPtr>& documents,
        QList<PluginDiagnostic>& diagnostics);
};
