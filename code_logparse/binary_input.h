#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace viewer::logparse
{

// Seekable, read-only byte source used by the binary log parser. Implementations
// may represent a normal file or a file stored inside another container.
class BinaryInput
{
public:
    virtual ~BinaryInput() = default;

    virtual bool open() = 0;
    virtual void close() noexcept = 0;
    virtual bool isOpen() const noexcept = 0;
    virtual bool read(void* destination, size_t byteCount) = 0;
    virtual bool seek(uint64_t absoluteOffset) = 0;
    virtual uint64_t tell() const noexcept = 0;
    virtual uint64_t size() const noexcept = 0;
    virtual std::filesystem::path displayPath() const = 0;
    virtual const std::string& lastError() const noexcept = 0;
};

class LocalFileBinaryInput final : public BinaryInput
{
public:
    explicit LocalFileBinaryInput(std::filesystem::path filePath);
    ~LocalFileBinaryInput() override;

    bool open() override;
    void close() noexcept override;
    bool isOpen() const noexcept override;
    bool read(void* destination, size_t byteCount) override;
    bool seek(uint64_t absoluteOffset) override;
    uint64_t tell() const noexcept override { return m_offset; }
    uint64_t size() const noexcept override { return m_size; }
    std::filesystem::path displayPath() const override { return m_filePath; }
    const std::string& lastError() const noexcept override { return m_lastError; }

private:
    std::filesystem::path m_filePath;
    std::ifstream m_stream;
    uint64_t m_offset = 0;
    uint64_t m_size = 0;
    std::string m_lastError;
};

} // namespace viewer::logparse
