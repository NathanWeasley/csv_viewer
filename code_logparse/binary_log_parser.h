#pragma once

#include "code_logparse/binary_log_types.h"

#include <filesystem>
#include <vector>

namespace viewer::logparse
{

// Standalone parser core. Files are parsed sequentially in caller-provided
// order into one continuous result; the first file defines the master schema.
// It has no Viewer, DataManager, or UI dependency.
class BinaryLogParser
{
public:
    ParseResult parseFiles(const std::vector<std::filesystem::path>& filePaths) const;
};

} // namespace viewer::logparse
