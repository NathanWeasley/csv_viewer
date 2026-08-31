#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/plotmgr/highlight/highlight_manager.h"

#include <QJsonObject>
#include <QString>
#include <string>
#include <vector>

namespace viewer
{

// Shared JSON codec for both global highlight rules and the per-plot rules
// embedded in bookmarks. Keeping the schema here prevents the two stores from
// drifting apart.
class VIEWER_API HighlightRuleJson final
{
public:
    static QJsonObject toJson(const HighlightRule& rule);
    static bool fromJson(const QJsonObject& object, HighlightRule* rule);

    static bool loadFile(const std::string& path,
                         std::vector<HighlightRule>* rules,
                         QString* error = nullptr);
    static bool saveFile(const std::string& path,
                         const std::vector<HighlightRule>& rules,
                         QString* error = nullptr);
};

} // namespace viewer
