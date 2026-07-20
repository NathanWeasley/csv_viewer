#pragma once

#include "code_logparse/binary_input.h"
#include "code_logparse/ziplog/zip_entry_info.h"

#include <filesystem>
#include <memory>

namespace viewer::logparse::ziplog
{

class ZipEntryBinaryInput final : public BinaryInput
{
public:
    ZipEntryBinaryInput(std::filesystem::path archivePath, ZipEntryInfo entry);
    ~ZipEntryBinaryInput() override;

    bool open() override;
    void close() noexcept override;
    bool isOpen() const noexcept override;
    bool read(void* destination, size_t byteCount) override;
    bool seek(uint64_t absoluteOffset) override;
    uint64_t tell() const noexcept override { return m_offset; }
    uint64_t size() const noexcept override { return m_entry.uncompressedSize; }
    std::filesystem::path displayPath() const override;
    const std::string& lastError() const noexcept override { return m_lastError; }

private:
    bool reopenEntry();
    bool skipForward(uint64_t byteCount);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::filesystem::path m_archivePath;
    ZipEntryInfo m_entry;
    uint64_t m_offset = 0;
    bool m_seekable = false;
    std::string m_lastError;
};

} // namespace viewer::logparse::ziplog
