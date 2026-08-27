#pragma once

#include "log_expand_types.h"

#include <QList>
#include <QStringList>

struct ExpansionDependencyInfo
{
    QStringList dependencies;
    QStringList invalidDependencies;
};

QList<ExpansionDependencyInfo> analyzeExpansionDependencies(
    const QList<ExpansionDefinition>& definitions);
