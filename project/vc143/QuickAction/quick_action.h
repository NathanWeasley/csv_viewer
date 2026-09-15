#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace quickaction
{

struct ItemResult
{
    std::string type;
    std::string sourcePath;
    std::filesystem::path outputPath;
    bool success = false;
    std::string detail;
};

struct RunResult
{
    std::filesystem::path outputDirectory;
    std::vector<ItemResult> items;
    std::string archiveError;
    std::size_t hiklogCount = 0;
    std::size_t hiklogSuccess = 0;
    std::size_t rbtCount = 0;
    std::size_t rbtSuccess = 0;
    std::size_t datCount = 0;
    std::size_t datSuccess = 0;

    std::size_t failureCount() const noexcept;
    bool success() const noexcept;
};

RunResult convertArchive(const std::filesystem::path& archivePath);

} // namespace quickaction
