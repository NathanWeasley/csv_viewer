#pragma once

#include "code_logparse/binary_log_types.h"

#include <filesystem>
#include <vector>

namespace viewer::logparse
{

// Standalone parser core. It has no Viewer, DataManager, or UI dependency.
class BinaryLogParser
{
public:
    ParseResult parseFiles(const std::vector<std::filesystem::path>& filePaths) const;
};

} // namespace viewer::logparse
