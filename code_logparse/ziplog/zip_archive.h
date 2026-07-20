#pragma once

#include "code_logparse/ziplog/zip_entry_info.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace viewer::logparse
{
class BinaryInput;
}

namespace viewer::logparse::ziplog
{

struct ZipReadLimits
{
    uint64_t maximumEntryCount = 200000;
    uint64_t maximumEntrySize = 2ull * 1024ull * 1024ull * 1024ull;
    uint64_t maximumTotalSize = 8ull * 1024ull * 1024ull * 1024ull;
};

// Read-only catalog of a ZIP central directory. Opening the catalog never
// extracts an entry and never creates a temporary file.
class ZipArchive
{
public:
    ZipArchive();
    ~ZipArchive();
    ZipArchive(ZipArchive&&) noexcept;
    ZipArchive& operator=(ZipArchive&&) noexcept;
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    bool open(const std::filesystem::path& archivePath,
              const ZipReadLimits& limits = {});
    void close() noexcept;
    bool isOpen() const noexcept;

    const std::filesystem::path& path() const noexcept { return m_path; }
    const std::vector<ZipEntryInfo>& entries() const noexcept { return m_entries; }
    const std::string& lastError() const noexcept { return m_lastError; }

    std::unique_ptr<BinaryInput> createInput(uint64_t entryIndex) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::filesystem::path m_path;
    std::vector<ZipEntryInfo> m_entries;
    std::string m_lastError;
};

} // namespace viewer::logparse::ziplog
