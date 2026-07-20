#pragma once

#include "code_logparse/binary_log_types.h"
#include "code_logparse/binary_input.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace viewer::logparse
{

struct ParseOptions
{
    // Called from the parsing thread. processedBytes and totalBytes refer to
    // the current input; inputIndex is zero based.
    std::function<void(const std::filesystem::path& source,
                       uint64_t processedBytes,
                       uint64_t totalBytes,
                       size_t inputIndex,
                       size_t inputCount)> progress;
    std::function<bool()> isCancelled;
};

// Standalone parser core. Files are parsed sequentially in caller-provided
// order into one continuous result; the first file defines the master schema.
// It has no Viewer, DataManager, or UI dependency.
class BinaryLogParser
{
public:
    ParseResult parseFiles(const std::vector<std::filesystem::path>& filePaths) const;
    ParseResult parseFiles(const std::vector<std::filesystem::path>& filePaths,
                           const ParseOptions& options) const;
    ParseResult parseInputs(std::vector<std::unique_ptr<BinaryInput>> inputs,
                            const ParseOptions& options = {}) const;
};

} // namespace viewer::logparse
