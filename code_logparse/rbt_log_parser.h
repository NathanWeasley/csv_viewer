#pragma once

#include "code_logparse/binary_input.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace viewer::logparse
{

struct RbtLogFileResult
{
    uint64_t entryIndex = 0;
    std::filesystem::path sourcePath;
    std::filesystem::path outputPath;
    uint64_t inputBytes = 0;
    uint64_t outputBytes = 0;
    size_t blockCount = 0;
    std::string error;

    bool success() const noexcept { return error.empty() && blockCount > 0; }
};

struct RbtLogBatchResult
{
    std::vector<RbtLogFileResult> files;
    bool cancelled = false;
    std::string archiveError;

    size_t successCount() const noexcept;
};

struct RbtLogParseOptions
{
    std::function<bool()> isCancelled;
    std::function<void(const std::filesystem::path& source,
                       uint64_t processedBytes,
                       uint64_t totalBytes,
                       size_t inputIndex,
                       size_t inputCount)> progress;
    uint64_t maximumOutputBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
};

class RbtLogParser final
{
public:
    RbtLogFileResult parse(
        BinaryInput& input,
        const std::filesystem::path& outputPath,
        const RbtLogParseOptions& options = {}) const;

    RbtLogBatchResult parseZipEntries(
        const std::filesystem::path& archivePath,
        const std::vector<uint64_t>& entryIndices,
        const std::filesystem::path& outputDirectory,
        const RbtLogParseOptions& options = {}) const;
};

} // namespace viewer::logparse
